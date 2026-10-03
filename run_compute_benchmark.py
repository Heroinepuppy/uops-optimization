import os
"""Sweep cloud size and chained transforms; compare CPU with GPU end-to-end latency."""
import argparse
import csv
import json
from datetime import datetime, timezone
import math
from pathlib import Path
import shutil
import subprocess

ROOT = Path(".")
SIZES = [10000, 25000, 50000, 100000] + [200000 * 2**i for i in range(8)]
METHODS = {
    "CPU 4T / 4 CCX": "#D55E00",
    "GPU kernel only": "#56B4E9",
    "GPU resident (host sync)": "#009E73",
    "Upload + GPU": "#8C564B",
    "Upload + GPU + Download": "#777777",
}

def quote(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'

def result_file(directory, size, device, kind='results'):
    flat = directory / f'compute_{device}_{size}_{kind}.csv'
    legacy = directory / str(size) / f'compute_{device}_{kind}.csv'
    return flat if flat.exists() or not legacy.exists() else legacy


def discover_sizes(directory):
    sizes = {int(p.name) for p in directory.iterdir() if p.is_dir() and p.name.isdigit()}
    for path in directory.glob('compute_cpu_*_results.csv'):
        token = path.name.removeprefix('compute_cpu_').removesuffix('_results.csv')
        if token.isdigit(): sizes.add(int(token))
    return sorted(n for n in sizes if all(result_file(directory,n,d).exists() for d in ('cpu','gpu')))


def load(directory, sizes):
    values = {}
    counts_by_size = {}
    for size in sizes:
        for device in ('cpu', 'gpu'):
            path = result_file(directory,size,device)
            with path.open(encoding='utf-8') as source:
                for row in csv.DictReader(source, delimiter=';'):
                    if int(row['points']) != size:
                        raise ValueError(f'Falsche Punktzahl in {path}')
                    key = (size, int(row['transformations']), row['method'])
                    value = float(row['median_us'])
                    if key in values or not math.isfinite(value) or value <= 0 or int(row['samples']) <= 0:
                        raise ValueError(f'Ungueltige Messung in {path}: {key}')
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

def plot(directory, sizes):
    pictures = ROOT / "results" / "pics"
    pictures.mkdir(parents=True, exist_ok=True)
    values, counts_by_size = load(directory, sizes)
    crossings = []
    for size in sizes:
        counts = counts_by_size[size]
        wins = [values[size,k,'Upload + GPU + Download'] < values[size,k,'CPU 4T / 4 CCX'] for k in counts]
        first = next((i for i, win in enumerate(wins) if win), None)
        sustained = next((i for i, win in enumerate(wins) if win and all(wins[i:])), None)
        crossings.append({
            'points': size,
            'last_tested_transformations': counts[-1],
            'first_gpu_win': '' if first is None else counts[first],
            'cpu_median_us_at_first_win': '' if first is None else values[size,counts[first],'CPU 4T / 4 CCX'],
            'gpu_roundtrip_median_us_at_first_win': '' if first is None else values[size,counts[first],'Upload + GPU + Download'],
            'previous_tested_transformations': '' if first is None or first == 0 else counts[first-1],
            'gpu_faster_at_all_later_tested_counts_from': '' if sustained is None else counts[sustained],
        })
        print(f'{size} Punkte: erster gemessener GPU-Roundtrip-Vorteil bei '
              + ('keinem getesteten Wert' if first is None else f'{counts[first]} Transformationen'), flush=True)
    with (directory / 'compute_crossover.csv').open('w', newline='', encoding='utf-8') as out:
        writer = csv.DictWriter(out, fieldnames=list(crossings[0]), delimiter=';')
        writer.writeheader(); writer.writerows(crossings)
    columns = min(3, len(sizes)); rows = math.ceil(len(sizes)/columns)
    commands = ['reset', f"set terminal pngcairo size {columns*900},{rows*600} enhanced font 'Segoe UI,10'",
                f'set output {quote(pictures / "compute_comparison.png")}',
                "set datafile separator ';'", 'set origin 0,0', 'set size 1,1',
                f"set multiplot layout {rows},{columns} rowsfirst title 'CPU / GPU - Transformationsketten (Median pro Cloud)' font ',16'",
                "set xlabel 'Transformationen pro Punkt (log2)'", "set ylabel 'Laufzeit pro Cloud [us]'",
                'set logscale x 2', 'unset grid', 'unset mxtics', 'unset mytics',
                "set key top left font ',8'", 'set lmargin 14', 'set rmargin 3', 'set bmargin 4']
    for panel, size in enumerate(sizes):
        counts = counts_by_size[size]
        commands += [f'$panel{panel} << EOD']
        commands += [';'.join(map(str, [k]+[values[size,k,method] for method in METHODS])) for k in counts]
        upper = max(values[size,k,method] for k in counts for method in METHODS)*1.1
        raw = upper/9; magnitude = 10**math.floor(math.log10(raw))
        step = next(magnitude*f for f in (1,2,2.5,5,10) if magnitude*f >= raw)
        upper = math.ceil(upper/step)*step
        xticks = counts[::max(1,math.ceil(len(counts)/10))]
        commands += ['EOD', f"set title '{size//1000}k Punkte'",
                     f'set xrange [{counts[0]/1.1}:{counts[-1]*1.1}]',
                     'set xtics ('+', '.join(f'"{k}" {k}' for k in xticks)+')',
                     f'set yrange [0:{upper}]', f'set ytics 0,{step},{upper}',
                     'plot '+', '.join(f'$panel{panel} using 1:{col} with linespoints lw 2 pt {col} '
                                      f'lc rgb {quote(color)} title {quote(method)}'
                                      for col,(method,color) in enumerate(METHODS.items(),2)
                                      if method in ('CPU 4T / 4 CCX', 'Upload + GPU + Download'))]
    commands += ['unset multiplot', 'unset output']
    commands += [f'set output {quote(pictures / "compute_speedup.png")}',
                 'set origin 0,0', 'set size 1,1',
                 f"set multiplot layout {rows},{columns} rowsfirst title 'GPU-Vorteil inklusive Upload und Download (CPU / GPU)' font ',16'",
                 "set ylabel 'CPU-Zeit / GPU-Roundtrip-Zeit'", "set key top left font ',9'"]
    for panel, size in enumerate(sizes):
        counts = counts_by_size[size]
        ratios = [values[size,k,'CPU 4T / 4 CCX']/values[size,k,'Upload + GPU + Download'] for k in counts]
        upper = max(1.1, max(ratios)*1.1)
        raw = upper/9; magnitude = 10**math.floor(math.log10(raw))
        step = next(magnitude*f for f in (1,2,2.5,5,10) if magnitude*f >= raw)
        upper = math.ceil(upper/step)*step
        xticks = counts[::max(1,math.ceil(len(counts)/10))]
        commands += [f"set title '{size//1000}k Punkte'",
                     f'set xrange [{counts[0]/1.1}:{counts[-1]*1.1}]',
                     'set xtics ('+', '.join(f'"{k}" {k}' for k in xticks)+')',
                     f'set yrange [0:{upper}]', f'set ytics 0,{step},{upper}',
                     f"plot $panel{panel} using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', "
                     "1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'"]
    commands += ['unset multiplot', 'unset output']
    colors = ['#0072B2', '#E69F00', '#009E73', '#CC79A7',
              '#D55E00', '#56B4E9', '#7B3294', '#333333',
              '#A6761D', '#E7298A', '#66A61E', '#1B9E77']
    all_counts = sorted({k for counts in counts_by_size.values() for k in counts})
    upper = max(values[size,k,method] for size in sizes for k in counts_by_size[size]
                for method in ('CPU 4T / 4 CCX', 'Upload + GPU + Download')) * 1.1
    raw = upper / 9
    magnitude = 10**math.floor(math.log10(raw))
    step = next(magnitude*f for f in (1,2,2.5,5,10) if magnitude*f >= raw)
    upper = math.ceil(upper/step)*step
    xticks = all_counts[::max(1,math.ceil(len(all_counts)/10))]
    commands += ["set terminal pngcairo size 2000,1200 enhanced font 'Segoe UI,12'",
                 f'set output {quote(pictures / "compute_combined.png")}',
                 'set origin 0,0', 'set size 1,1',
                 "set title 'CPU / GPU - alle Punktwolken-Groessen' font ',18'",
                 "set ylabel 'Laufzeit pro Cloud [us]'",
                 'set format y "%.0f"',
                 "set key outside right center font ',11'",
                 'set key title "CPU: durchgezogen / GPU: gestrichelt\\nGPU inklusive Upload und Download"',
                 'set rmargin', 'set tmargin 4', 'set bmargin 5',
                 f'set xrange [{all_counts[0]/1.1}:{all_counts[-1]*1.1}]',
                 'set xtics ('+', '.join(f'"{k}" {k}' for k in xticks)+')',
                 f'set yrange [0:{upper}]', f'set ytics 0,{step},{upper}',
                 'plot '+', '.join(
                     f'$panel{panel} using 1:{col} with linespoints lw 2.5 dt {dash} pt {point} ps 0.7 '
                     f'lc rgb {quote(colors[panel % len(colors)])} title "{size/1e6:g} Mio. - {label}"'
                     for panel,size in enumerate(sizes)
                     for col,dash,point,label in ((2,1,7,'CPU'),(6,2,6,'GPU gesamt'))),
                 'unset output']
    if 25600000 in sizes:
        gpu_upper = 1.1 * max(values[25600000,k,'Upload + GPU + Download']
                              for k in counts_by_size[25600000])
        raw = gpu_upper / 9
        magnitude = 10**math.floor(math.log10(raw))
        step = next(magnitude*f for f in (1,2,2.5,5,10) if magnitude*f >= raw)
        first_above, last_below = [], []
        for size in sizes:
            counts = counts_by_size[size]
            above = next((k for k in counts
                          if values[size,k,'CPU 4T / 4 CCX'] >
                          values[size,k,'Upload + GPU + Download']), None)
            if above is None:
                continue
            first_above.append((above, values[size,above,'CPU 4T / 4 CCX']))
            below = [k for k in counts if k < above
                     and values[size,k,'CPU 4T / 4 CCX'] <
                     values[size,k,'Upload + GPU + Download']]
            if below:
                last_below.append((below[-1], values[size,below[-1],'CPU 4T / 4 CCX']))
        boundary_plots = []
        for name, points, dash, marker, label in (
                ('first_above', first_above, 1, 7, 'CPU erstmals langsamer'),
                ('last_below', last_below, 2, 5, 'CPU zuletzt schneller davor')):
            if points:
                commands += [f'${name} << EOD',
                             *(f'{k};{time}' for k,time in points), 'EOD']
                boundary_plots.append(
                    f'${name} using 1:2 with linespoints lc rgb "#FF0000" '
                    f'lw 3.5 dt {dash} pt {marker} ps 1.1 title {quote(label)}')
        if len(first_above) >= 2 and len(last_below) >= 2:
            polygon = first_above + list(reversed(last_below)) + [first_above[0]]
            commands += ['set object 1 polygon from '
                         + ' to '.join(f'{k},{time}' for k,time in polygon)
                         + ' behind fillcolor rgb "#FF0000" fillstyle transparent solid 0.25 noborder']
        commands += [f'set output {quote(pictures / "compute_combined_gpu_zoom.png")}',
                     "set title 'CPU / GPU - Ausschnitt bis 1.1 x GPU-Maximum (25.6 Mio. Punkte)' font ',18'",
                     f'set yrange [0:{gpu_upper}]', f'set ytics 0,{step},{gpu_upper}',
                     'replot' + (' ' + ', '.join(boundary_plots) if boundary_plots else ''),
                     'unset output', 'unset object 1']
        print(f'GPU-Zoom: Y-Maximum {gpu_upper:.3f} us', flush=True)
    script = directory / 'compute_comparison.gp'
    script.write_text('\n'.join(commands)+'\n', encoding='utf-8')
    executable = shutil.which('gnuplot')
    if not executable: raise ValueError('Gnuplot fehlt im PATH')
    subprocess.run([executable, str(script)], check=True)
    print(f'Plot: {pictures / "compute_comparison.png"}', flush=True)

def main():
    os.chdir(Path(__file__).resolve().parent)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--points', nargs='+', type=int)
    parser.add_argument('--rounds', type=int, default=20)
    parser.add_argument('--max-transforms', type=int, default=1024)
    parser.add_argument('--output-dir', type=Path, default=ROOT/'results'/'data')
    parser.add_argument('--plot-only', action='store_true')
    args = parser.parse_args()
    directory=Path(os.path.relpath(args.output_dir))
    if args.points is None:
        args.points = discover_sizes(directory) if args.plot_only and directory.exists() else SIZES
    if not args.points or min(args.points)<=0 or args.rounds<=0 or not 1<=args.max_transforms<=65536:
        parser.error('Punktzahlen und Runden muessen positiv sein; Transformationen 1..65536')
    sizes = sorted(set(args.points)); directory=Path(os.path.relpath(args.output_dir))
    directory.mkdir(parents=True, exist_ok=True)
    if not args.plot_only:
        cpu=ROOT/'build'/'vs2026'/'Release'/'lidar_compute_cpu.exe'
        gpu=ROOT/'build'/'gpu'/'lidar_compute_gpu.exe'
        for executable in (cpu,gpu):
            if not executable.exists(): raise ValueError(f'Zuerst bauen: {executable}')
        (directory / 'run_config.json').write_text(json.dumps({
            'started_utc': datetime.now(timezone.utc).isoformat(),
            'points': sizes, 'rounds': args.rounds, 'max_transforms': args.max_transforms,
            'cpu_executable': str(cpu), 'gpu_executable': str(gpu),
            'mode': 'chained transforms, one load/store and one dispatch per cloud',
            'statistic': 'median', 'crossover': 'CPU vs GPU including upload and download',
        }, indent=2)+'\n', encoding='utf-8')
        for size in sizes:
            for executable in (cpu,gpu):
                print(f'\n{size} Punkte - {executable.name}', flush=True)
                subprocess.run([str(executable), '--points', str(size), '--rounds', str(args.rounds),
                                '--max-transforms', str(args.max_transforms),
                                '--output-dir', str(directory)], cwd=ROOT, check=True)
    plot(directory,sizes)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as exc: raise SystemExit(str(exc))
