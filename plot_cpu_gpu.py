import os
"""Compare saved histogram peaks without running the benchmarks again."""

import argparse
import csv
import math
from pathlib import Path
import shutil
import subprocess


# Colors belong to method names, independent of series order or missing data.
METHOD_COLORS = {
    "CPU: 1 Thread": "#0072B2",
    "CPU: 2 Threads / 1 Core SMT": "#E69F00",
    "CPU: 2 Threads / 2 Cores same CCX": "#009E73",
    "CPU: 2 Threads / 2 Cores different CCX": "#CC79A7",
    "CPU: 4 Threads / 4 Cores / 4 CCX": "#D55E00",
    "GPU kernel only": "#56B4E9",
    "PCIe upload + GPU kernel": "#8C564B",
    "PCIe upload + GPU kernel + PCIe download": "#777777",
}


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


def main():
    os.chdir(Path(__file__).resolve().parent)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-dir", type=Path, default=Path("results") / "data")
    parser.add_argument("--no-plot", action="store_true", help="Messwerte nur einlesen und pruefen")
    args = parser.parse_args()
    directory = Path(os.path.relpath(args.result_dir))
    missing = [directory / name for name in ('cpu_results.txt', 'gpu_results.txt')
               if not (directory / name).is_file()]
    if missing:
        raise ValueError(
            'Benchmark-Ergebnisse fehlen:\n'
            + '\n'.join(str(path) for path in missing)
            + '\nDieses Skript zeichnet nur vorhandene Messwerte.\n'
            'Alle Benchmarks inklusive Histogrammen und Fits starten mit:\n'
            '  python "' + 'run_all_benchmarks.py' + '"')
    cpu = read_results(directory / "cpu_results.txt")
    gpu = read_results(directory / "gpu_results.txt")
    sizes = sorted({points for points, _ in cpu} | {points for points, _ in gpu})
    if not sizes:
        raise ValueError("Keine Messwerte vorhanden.")
    series = [("CPU: " + method, cpu, method) for method in dict.fromkeys(method for _, method in cpu)]
    series += [(method, gpu, method) for method in dict.fromkeys(method for _, method in gpu)]
    for label, _, _ in series:
        if label not in METHOD_COLORS:
            raise ValueError(f"Bitte eine eigene Plotfarbe fuer die neue Methode festlegen: {label}")
    # Gnuplot accepts forward slashes on Windows. Escape quoted paths and titles.
    def quote(value):
        return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'

    short_labels = {
        "CPU: 1 Thread": "CPU 1T",
        "CPU: 2 Threads / 1 Core SMT": "CPU 2T SMT",
        "CPU: 2 Threads / 2 Cores same CCX": "CPU 2T gleicher CCX",
        "CPU: 2 Threads / 2 Cores different CCX": "CPU 2T getrennte CCX",
        "CPU: 4 Threads / 4 Cores / 4 CCX": "CPU 4T / 4 CCX",
        "GPU kernel only": "GPU Kernel",
        "PCIe upload + GPU kernel": "Upload + GPU",
        "PCIe upload + GPU kernel + PCIe download": "Upload + GPU + Download",
    }
    columns = min(3, len(sizes))
    panel_rows = math.ceil(len(sizes) / columns)
    pictures = Path("results") / "pics"
    pictures.mkdir(parents=True, exist_ok=True)
    image = pictures / "cpu_gpu_comparison.png"
    commands = [
        "reset",
        f"set terminal pngcairo size {columns * 900},{panel_rows * 600} enhanced font 'Segoe UI,10'",
        f"set output {quote(image)}",
        "set datafile separator ';'",
        "set origin 0,0",
        "set size 1,1",
        f"set multiplot layout {panel_rows},{columns} rowsfirst title 'CPU / GPU - Histogramm-Peaks (60 Bins)' font ',16'",
        "set ylabel 'Laufzeit pro Cloud [us]'",
        "unset logscale y",
        "unset mytics",
        "unset grid",
        "unset key",
        "set style fill solid 0.8 border -1",
        "set boxwidth 0.7",
        "set xtics rotate by 30 right font ',9'",
        "set bmargin 7",
        "set tmargin 3",
        "set lmargin 10",
        "set rmargin 3",
    ]
    for panel, points in enumerate(sizes):
        available = [(label, values[points, method]) for label, values, method in series
                     if (points, method) in values]
        for kind, values in [("CPU", cpu), ("GPU", gpu)]:
            if not any(size == points for size, _ in values):
                print(f"Hinweis: {kind}-Messwerte fuer {points} Punkte fehlen.")
        commands += [f"$panel{panel} << EOD"]
        commands += [
            f"{index};{quote(short_labels[label])};{peak};{int(METHOD_COLORS[label][1:], 16)}"
            for index, (label, peak) in enumerate(available)
        ]
        commands += ["EOD", "unset key"]
        stack_methods = ["GPU kernel only", "PCIe upload + GPU kernel",
                         "PCIe upload + GPU kernel + PCIe download"]
        stack_plots = []
        stacked_indices = []
        if all((points, method) in gpu for method in stack_methods):
            kernel, upload_kernel, total = [gpu[points, method] for method in stack_methods]
            if not kernel <= upload_kernel <= total:
                raise ValueError(f"{points} Punkte: GPU-Peaks erlauben keine positive Aufteilung.")
            # Estimates from independent histogram peaks, not measured phase timings.
            segments = [("Upload", upload_kernel - kernel, METHOD_COLORS[stack_methods[1]]),
                        ("GPU", kernel, METHOD_COLORS[stack_methods[0]]),
                        ("Download", total - upload_kernel, METHOD_COLORS[stack_methods[2]])]
            stacked_indices = [index for index, (label, _) in enumerate(available)
                               if label in stack_methods[1:]]
            bottom = 0.0
            for part, (label, duration, color) in enumerate(segments):
                top = bottom + duration
                commands += [f"$stack{panel}_{part} << EOD"]
                for x in stacked_indices:
                    if part == 2 and available[x][0] == stack_methods[1]:
                        continue
                    commands += [f"{x};{(bottom + top) / 2};{x - 0.35};{x + 0.35};{bottom};{top}"]
                commands += ["EOD"]
                stack_plots.append(
                    f"$stack{panel}_{part} using 1:2:3:4:5:6 with boxxyerror "
                    f"lc rgb {quote(color)} title {quote(label)}")
                bottom = top
            commands += ["set key top left horizontal font ',8'",
                         "set key title 'Anteile aus Peak-Differenzen' font ',8'"]
        bar_value = ("(" + " || ".join(f"$1 == {index}" for index in stacked_indices)
                     + " ? 1/0 : $3)") if stacked_indices else "3"
        lower = 0
        target_upper = max(peak for _, peak in available) * 1.1
        # At most nine equal intervals (ten ticks including zero), rounded up
        # to a readable step size independently for each panel.
        raw_step = target_upper / 9
        magnitude = 10.0 ** math.floor(math.log10(raw_step))
        step = next(factor * magnitude for factor in (1, 2, 2.5, 5, 10)
                    if factor * magnitude >= raw_step)
        intervals = math.ceil(target_upper / step)
        upper = intervals * step
        ticks = [index * step for index in range(intervals + 1)]
        commands += [
            f"set title '{points // 1000}k Punkte'",
            f"set xrange [-0.7:{len(available) - 0.3}]",
            f"set yrange [{lower}:{upper}]",
            "set ytics (" + ", ".join(f'"{tick:g}" {tick:g}' for tick in ticks) + ")",
            "set xtics (" + ", ".join(
                f"{quote(short_labels[label])} {index}" for index, (label, _) in enumerate(available)
            ) + ")",
            f"plot $panel{panel} using 1:{bar_value}:4 with boxes lc rgb variable notitle"
            + (", " + ", ".join(stack_plots) if stack_plots else ""),
        ]
    commands += ["unset multiplot", "unset output"]
    print(f"Messwerte geprueft: {len(sizes)} Punktzahlen, {len(series)} Methoden")
    if not args.no_plot:
        executable = shutil.which("gnuplot")
        if not executable:
            raise ValueError("Gnuplot fehlt im PATH.")
        script = directory / "cpu_gpu_comparison.gp"
        script.write_text("\n".join(commands) + "\n", encoding="utf-8")
        subprocess.run([executable, str(script)], check=True)
        print(f"Vergleichsplot: {image}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
