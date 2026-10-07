"""Read and write benchmark data. Relative CLI paths use the project root."""

import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import shutil

METHODS = ("CPU 4T / 4 CCX", "GPU kernel only", "GPU resident (host sync)", "Upload + GPU", "Upload + GPU + Download")


def relative_path(path):
    """Prefer project-relative paths; another Windows drive requires an absolute path."""
    try:
        return Path(os.path.relpath(path))
    except ValueError:
        return Path(path).resolve()


def result_file(directory, size, device, kind='results'):
    flat = directory / f'compute_{device}_{size}_{kind}.csv'
    legacy = directory / str(size) / f'compute_{device}_{kind}.csv'
    return flat if flat.exists() or not legacy.exists() else legacy

def discover_sizes(directory):
    if (directory / 'benchmark_matrix.csv').is_file():
        return sorted({key[2] for key in read_matrix(directory)})
    sizes = {int(p.name) for p in directory.iterdir() if p.is_dir() and p.name.isdigit()}
    for path in directory.glob('compute_cpu_*_results.csv'):
        token = path.name.removeprefix('compute_cpu_').removesuffix('_results.csv')
        if token.isdigit(): sizes.add(int(token))
    return sorted(n for n in sizes if all(result_file(directory,n,d).exists() for d in ('cpu','gpu')))

def load(directory, sizes):
    if (directory / 'benchmark_matrix.csv').is_file():
        rows = read_matrix(directory)
        sizes = sorted({key[2] for key in rows}) if sizes is None else sizes
        values = {}
        counts = {}
        for (device, mode, size, transforms, method), row in rows.items():
            if size not in sizes or method not in METHODS:
                continue
            if device == 'cpu' and mode != 'four-ccx':
                continue
            values[size, transforms, method] = float(row['median_us'])
            counts.setdefault(size, set()).add(transforms)
        for size in sizes:
            if size not in counts:
                raise ValueError(f'Keine Vergleichsmessungen fuer {size}')
            for transforms in counts[size]:
                for method in METHODS:
                    if (size, transforms, method) not in values:
                        raise ValueError(f'Fehlende Messung: {size}, {transforms}, {method}')
        return values, {size: sorted(items) for size, items in counts.items()}
    sizes = discover_sizes(directory) if sizes is None else sizes
    values = {}
    counts_by_size = {}
    for size in sizes:
        for device in ('cpu', 'gpu'):
            paths = [result_file(directory,size,device)]
            refined = directory / f'refined_compute_{device}_{size}_results.csv'
            if refined.exists(): paths.append(refined)
            for path in paths:
                seen = set()
                with path.open(encoding='utf-8') as source:
                    for row in csv.DictReader(source, delimiter=';'):
                        if int(row['points']) != size:
                            raise ValueError(f'Falsche Punktzahl in {path}')
                        key = (size, int(row['transformations']), row['method'])
                        value = float(row['median_us'])
                        if key in seen or not math.isfinite(value) or value <= 0 or int(row['samples']) <= 0:
                            raise ValueError(f'Ungueltige Messung in {path}: {key}')
                        seen.add(key)
                        # At repeated counts, prefer the dedicated refinement measurement.
                        values[key] = value
        counts = sorted({k for n, k, _ in values if n == size})
        if not counts:
            raise ValueError(f'Keine Messungen fuer {size}')
        for k in counts:
            for method in METHODS:
                if (size, k, method) not in values:
                    raise ValueError(f'Fehlende Messung: {size}, {k}, {method}')
        counts_by_size[size] = counts
    return values, counts_by_size

def read_results(path):
    """Read sectioned semicolon files; latest row per size/method wins."""
    results = {}
    if not path.exists():
        raise ValueError(f"Ergebnisdatei fehlt: {path}")
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        fields = next(csv.reader([line], delimiter=";"))
        if not fields or not fields[0].strip().isdigit():
            continue
        try:
            points = int(fields[0])
            method = fields[1].strip()
            peak = float(fields[2])
            samples = int(fields[4])
            if points <= 0 or not method or not math.isfinite(peak) or peak <= 0 or samples <= 0:
                raise ValueError("ungueltige Messung")
        except (ValueError, IndexError) as exc:
            raise ValueError(f"{path}:{number}: ungueltige Ergebniszeile") from exc
        results[points, method] = peak
    return results

def read_gpu_results(directory):
    if (directory / 'benchmark_matrix.csv').is_file():
        aliases = {'Upload + GPU': 'PCIe upload + GPU kernel',
                   'Upload + GPU + Download': 'PCIe upload + GPU kernel + PCIe download'}
        return {(size, aliases.get(method, method)): float(row['peak_us'])
                for (device, mode, size, transforms, method), row in read_matrix(directory).items()
                if device == 'gpu' and transforms == 1}
    paths = sorted(directory.glob('compute_gpu_*_results.csv'))
    if not paths:
        return read_results(directory / 'gpu_results.txt')
    aliases = {'Upload + GPU': 'PCIe upload + GPU kernel',
               'Upload + GPU + Download': 'PCIe upload + GPU kernel + PCIe download'}
    results = {}
    paths += sorted(directory.glob('refined_compute_gpu_*_results.csv'))
    for path in paths:
        seen = set()
        with path.open(encoding='utf-8') as source:
            for row in csv.DictReader(source, delimiter=';'):
                if int(row['transformations']) != 1:
                    continue
                points, peak = int(row['points']), float(row['peak_us'])
                method = aliases.get(row['method'], row['method'])
                if points <= 0 or not math.isfinite(peak) or peak <= 0 or int(row['samples']) <= 0:
                    raise ValueError(f'Ungueltige Messung: {path}')
                if (points, method) in seen:
                    raise ValueError(f'Doppelte Messung: {path}')
                seen.add((points, method))
                results[points, method] = peak
    if not results:
        raise ValueError('Keine GPU-Messungen mit einer Transformation vorhanden')
    return results

def read_cpu_results(directory):
    if (directory / 'benchmark_matrix.csv').is_file():
        names = {'single': '1 Thread', 'smt': '2 Threads / 1 Core SMT',
                 'same-ccx': '2 Threads / 2 Cores same CCX',
                 'different-ccx': '2 Threads / 2 Cores different CCX',
                 'four-ccx': '4 Threads / 4 Cores / 4 CCX'}
        return {(size, names[mode]): float(row['peak_us'])
                for (device, mode, size, transforms, method), row in read_matrix(directory).items()
                if device == 'cpu' and transforms == 1
                and (method == 'CPU 4T / 4 CCX' or method == f'avx2x2 / {mode}')}
    paths = sorted(directory.glob('threading_compute_cpu*_results.csv'))
    if not paths:
        return read_results(directory / 'cpu_results.txt')
    names = {'single': '1 Thread', 'smt': '2 Threads / 1 Core SMT',
             'same-ccx': '2 Threads / 2 Cores same CCX',
             'different-ccx': '2 Threads / 2 Cores different CCX',
             'four-ccx': '4 Threads / 4 Cores / 4 CCX'}
    results = {}
    for path in paths:
        with path.open(encoding='utf-8') as source:
            for row in csv.DictReader(source, delimiter=';'):
                if int(row['transformations']) != 1:
                    continue
                mode = 'four-ccx' if row['method'] == 'CPU 4T / 4 CCX' else row['method'].split(' / ')[-1]
                key = int(row['points']), names[mode]
                peak = float(row['peak_us'])
                if key in results or not math.isfinite(peak) or peak <= 0 or int(row['samples']) <= 0:
                    raise ValueError(f'Ungueltige CPU-Messung: {path}')
                results[key] = peak
    return results


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def write_csv(path, rows):
    if not rows:
        raise ValueError(f"Keine Zeilen fuer {path}")
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter=";")
        writer.writeheader()
        writer.writerows(rows)


def read_boundaries(directory):
    if (directory / 'benchmark_matrix.csv').is_file():
        return []
    path = directory / "refined_crossover.csv"
    if not path.exists():
        return []
    with path.open(encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter=";"))


def sample_groups(path, transforms, device=None):
    groups = {}
    with path.open(encoding="utf-8-sig", newline="") as source:
        for row in csv.DictReader(source, delimiter=";"):
            if device and row.get("device", device) != device:
                continue
            if int(row["transformations"]) != transforms:
                continue
            key = (int(row["points"]), transforms, row["method"])
            value = float(row["us"])
            if not math.isfinite(value) or value <= 0:
                raise ValueError(f"Ungueltige Messung: {path}")
            groups.setdefault(key, []).append(value)
    return groups


def sample_files(directory, device):
    matrix = directory / "benchmark_matrix.csv"
    if matrix.exists():
        return [matrix]
    paths = sorted(directory.glob(f"compute_{device}_*_samples.csv"))
    paths += sorted(directory.glob(f"threading_compute_{device}_*_samples.csv"))
    paths += sorted(directory.glob(f"refined_compute_{device}_*_samples.csv"))
    if not paths:
        raise ValueError(f"Keine Rohdaten: {directory}")
    return paths

def merge_refinements(sources, target):
    cases={};origins={}
    for source in sources:
        source=Path(os.path.relpath(source))
        plan=json.loads((source/'refinement_plan.json').read_text())
        for case in plan['cases']:
            n=case['points']
            if n in cases: raise ValueError(f'Doppelte Punktzahl: {n}')
            _,counts=load(source,[n])
            if counts[n]!=case['transforms']:raise ValueError(f'Messplan passt nicht: {source}, {n}')
            for device in ('cpu','gpu'):
                for kind in ('results','samples'):
                    if not result_file(source,n,device,kind).is_file():
                        raise ValueError(f'Fehlende {device}-{kind} fuer {n}')
            cases[n]=case;origins[n]=source
    if not cases:raise ValueError('Keine Messreihen gefunden')
    target.mkdir(parents=True,exist_ok=True)
    for n,source in origins.items():
        for device in ('cpu','gpu'):
            for kind in ('results','samples'):
                source_file=result_file(source,n,device,kind)
                destination=target/f'compute_{device}_{n}_{kind}.csv'
                if source_file.resolve()!=destination.resolve(): shutil.copy2(source_file,destination)
    plan=[cases[n] for n in sorted(cases)]
    (target/'refinement_plan.json').write_text(json.dumps({
        'created_utc':datetime.now(timezone.utc).isoformat(),
        'note':'Combined measurements from different sessions, not one fresh sweep',
        'source_by_point_count':{n:str(source) for n,source in origins.items()},'cases':plan},indent=2)+'\n')
    return plan


def has_refinements(directory):
    return any(directory.glob('refined_compute_*_results.csv'))


def read_matrix(directory):
    """Read completed sample series, preserving device, mode and method identity.

    Summary columns repeat once per sample in the runner's CSV. Reject missing
    samples and inconsistent summaries instead of treating a partial series as
    a finished measurement. No file is opened until this function is called.
    """
    path = directory / 'benchmark_matrix.csv'
    records = {}
    samples = {}
    with path.open(encoding='utf-8-sig', newline='') as stream:
        for number, row in enumerate(csv.DictReader(stream, delimiter=';'), 2):
            try:
                key = (row['device'], row['thread_mode'], int(row['points']),
                       int(row['transformations']), row['method'])
                count, sample = int(row['samples']), int(row['sample'])
                if key[0] not in ('cpu', 'gpu') or min(key[2:4]) <= 0:
                    raise ValueError('Ungueltige Matrixparameter')
                if count <= 0 or not 0 <= sample < count:
                    raise ValueError('Ungueltige Messrunde')
                if any(not math.isfinite(float(row[field])) or float(row[field]) <= 0
                       for field in ('us', 'median_us', 'peak_us')):
                    raise ValueError('Ungueltige Laufzeit')
                seen = samples.setdefault(key, set())
                if sample in seen:
                    raise ValueError('Doppelte Messrunde')
                if key in records and any(row[field] != records[key][field]
                                          for field in ('samples', 'median_us', 'peak_us')):
                    raise ValueError('Widerspruechliche Zusammenfassung')
                seen.add(sample)
                records.setdefault(key, row)
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f'{path}:{number}: {exc}') from exc
    for key, row in records.items():
        if len(samples[key]) != int(row['samples']):
            raise ValueError(f'Unvollstaendige Messreihe: {key}')
    if not records:
        raise ValueError(f'Keine Messungen: {path}')
    return records
