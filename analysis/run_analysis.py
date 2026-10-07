"""Select plots from saved benchmark data (paths relative to the project root)."""

import argparse
import os
from pathlib import Path
import calculations
import data
import plots

PLOT_TYPES = ("comparison", "cpu-gpu", "histograms", "fits")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plots", nargs="+", choices=(*PLOT_TYPES, "all"),
                        help="Gewuenschte Plots; ohne Auswahl wird nur die Hilfe angezeigt")
    parser.add_argument("--result-dir", type=Path, default=Path("results/data"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/pics"))
    parser.add_argument("--device", choices=("cpu", "gpu"), default="gpu")
    parser.add_argument("--transforms", type=int, default=1)
    parser.add_argument("--no-plot", action="store_true",
                        help="Daten auswerten und Berichte schreiben, ohne Diagramme zu erzeugen")
    args = parser.parse_args(argv)
    if not args.plots:
        parser.print_help()
        return
    if args.transforms <= 0:
        parser.error("--transforms muss positiv sein")
    os.chdir(Path(__file__).resolve().parent.parent)
    directory = data.relative_path(args.result_dir)
    output = data.relative_path(args.output_dir)
    selected = PLOT_TYPES if "all" in args.plots else tuple(dict.fromkeys(args.plots))
    if not args.no_plot:
        plots.pyplot()
    values = counts = None
    for kind in selected:
        if kind in ("comparison", "fits") and values is None:
            values, counts = data.load(directory, None)
        if kind == "comparison":
            rows = calculations.crossover_summary(values, counts, sorted(counts))
            output.mkdir(parents=True, exist_ok=True)
            data.write_csv(output / "compute_crossover.csv", rows)
            if not args.no_plot:
                plots.save_figures(output, plots.comparison_plot(values, counts))
        elif kind == "cpu-gpu":
            cpu = data.read_cpu_results(directory)
            gpu = data.read_gpu_results(directory)
            if not args.no_plot:
                plots.save_figures(output, plots.cpu_gpu_plot(cpu, gpu))
        elif kind == "histograms":
            for path in data.sample_files(directory, args.device):
                groups = data.sample_groups(path, args.transforms, args.device)
                if groups:
                    stem = f"{path.stem}_{args.device}_{args.transforms}"
                    if not args.no_plot:
                        plots.save_figures(output, plots.histogram_plot(groups, stem))
        elif kind == "fits":
            report = calculations.crossover_fit_report(
                values, counts, data.read_boundaries(directory), data.has_refinements(directory))
            reports = calculations.fit_reports(report["points"])
            output.mkdir(parents=True, exist_ok=True)
            data.write_json(output / "crossover_exponential_fit.json", report)
            data.write_csv(output / "crossover_fit_points.csv", report["points"])
            for name, value in reports.items():
                data.write_json(output / f"{name}.json", value)
            if not args.no_plot:
                plots.save_figures(output, plots.crossover_plot(report, reports))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, ImportError) as exc:
        raise SystemExit(str(exc)) from exc
