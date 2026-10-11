"""Analysis checks using synthetic data only; never access results/."""

from contextlib import redirect_stdout
import io
import math
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

# Load the standalone scripts as if started from the analysis directory.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'analysis'))
import calculations
import data
import plots
import run_analysis


def write_matrix(directory):
    rows = []
    for size, threshold in ((100000, 16), (200000, 8), (400000, 4), (800000, 2)):
        for transforms in (1, 2, 4, 8, 16, 32):
            scale = math.sqrt(size / 100000)
            for index, method in enumerate(data.METHODS):
                duration = transforms * scale if index == 0 else (threshold - .25) * scale * index / 4
                for sample in range(2):
                    rows.append(dict(device='cpu' if index == 0 else 'gpu',
                                     thread_mode='four-ccx' if index == 0 else '',
                                     points=size, transformations=transforms, method=method,
                                     sample=sample, us=duration, median_us=duration,
                                     peak_us=duration, samples=2))
    data.write_csv(directory / 'benchmark_matrix.csv', rows)
    return rows


class AnalysisTests(unittest.TestCase):
    def test_legacy_and_mixed_names_are_normalized(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            rows = write_matrix(directory)
            expected = data.load(directory, None)
            for mixed in (False, True):
                changed = [dict(row) for row in rows]
                for row in changed:
                    if row['device'] == 'cpu' and (not mixed or row['sample'] == 0):
                        row['method'] = 'CPU 4T / 4 CCX'
                data.write_csv(directory / 'benchmark_matrix.csv', changed)
                self.assertEqual(data.load(directory, None), expected)
                self.assertTrue(all(key[-1] != 'CPU 4T / 4 CCX'
                                    for key in data.sample_groups(directory / 'benchmark_matrix.csv', 1)))

    def test_help_does_not_read_data(self):
        with patch.object(data, 'load', side_effect=AssertionError('data access')):
            with redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as exit_status:
                    run_analysis.main(['--help'])
                self.assertEqual(exit_status.exception.code, 0)

    def test_numerical_models(self):
        width, bins = calculations.histogram([2, 2, 2])
        self.assertGreater(width, 0)
        self.assertEqual(sum(count for _, count in bins), 3)
        result = calculations.hyperbola_fit([(n, 3 + 5 / n) for n in (1, 2, 4, 8)])
        self.assertAlmostEqual(result['a'], 5)
        self.assertAlmostEqual(result['c'], 3)
        result = calculations.fit([(x, 2 + 5 * math.exp(-.4 * x)) for x in range(6)], True)
        self.assertLess(result['rmse_us'], 1e-6)

    def test_exponential_spline_joins(self):
        records = [dict(points=n, x=100*math.exp(-n/1e6),
                        lower=1, upper=200) for n in (8192, 16384, 32768, 131072, 262144, 524288)]
        model = calculations.exponential_spline_fit(records)
        self.assertEqual(model['unsupported_regions'], ['L1', 'RAM'])
        self.assertEqual([s['region'] for s in model['segments']], ['L2', 'L3'])
        for segment in model['segments']:
            for x in (segment['start'], segment['end']):
                self.assertAlmostEqual(calculations.exponential_spline(model, x),
                                       100*math.exp(-x), places=6)
                h = x * 1e-5
                left = (calculations.exponential_spline(model,x)-calculations.exponential_spline(model,x-h))/h
                right = (calculations.exponential_spline(model,x+h)-calculations.exponential_spline(model,x))/h
                self.assertAlmostEqual(left, right, delta=.01)

    def test_all_plots_from_synthetic_matrix(self):
        with tempfile.TemporaryDirectory(prefix='analysis-test-') as temporary:
            directory = Path(temporary)
            write_matrix(directory)
            before = (directory / 'benchmark_matrix.csv').read_bytes()
            values, counts = data.load(directory, None)
            self.assertEqual(len(counts), 4)
            summary = calculations.crossover_summary(values, counts, sorted(counts))
            self.assertEqual([row['first_gpu_win'] for row in summary], [16, 8, 4, 2])
            output = directory / 'plots'
            with patch.object(plots, 'pyplot', side_effect=AssertionError('rendering')):
                run_analysis.main(['--result-dir', str(directory),
                                   '--output-dir', str(output), '--no-plot'])
            self.assertEqual(list(output.glob('*.png')), [])
            self.assertTrue((output / 'crossover' / 'crossover_exponential_fit.json').is_file())
            self.assertEqual((directory / 'benchmark_matrix.csv').read_bytes(), before)

    def test_partial_or_duplicate_matrix_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix='analysis-test-') as temporary:
            directory = Path(temporary)
            rows = write_matrix(directory)
            data.write_csv(directory / 'benchmark_matrix.csv', rows[:-1])
            with self.assertRaisesRegex(ValueError, 'Unvollstaendige'):
                data.read_matrix(directory)
            data.write_csv(directory / 'benchmark_matrix.csv', rows + [rows[0]])
            with self.assertRaisesRegex(ValueError, 'Doppelte'):
                data.read_matrix(directory)

    def test_matplotlib_renders_all_groups(self):
        with tempfile.TemporaryDirectory(prefix='analysis-render-') as temporary:
            directory = Path(temporary)
            write_matrix(directory)
            original = (directory / 'benchmark_matrix.csv').read_bytes()
            output = directory / 'plots'
            with redirect_stdout(io.StringIO()):
                run_analysis.main(['--plots', 'all', '--result-dir', str(directory),
                                   '--output-dir', str(output)])
            images = list(output.rglob('*.png'))
            self.assertTrue(list((output / 'hist').glob('*.png')))
            self.assertTrue(list((output / 'hist' / 'matrix').glob('histograms_points_*.png')))
            self.assertTrue((output / 'crossover' / 'compute_comparison.png').is_file())
            self.assertTrue((output / 'crossover' / 'crossover_exponential_fit.png').is_file())
            self.assertEqual(len(images), 35)
            self.assertEqual({p.name for p in (output / 'comparision').glob('*.png')},
                             {f'cpu_gpu_comparison_transforms_{n}.png' for n in (1, 2, 4, 8, 16, 32)})
            for image in images:
                self.assertEqual(image.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
                self.assertGreater(image.stat().st_size, 1000)
            self.assertEqual(list(output.glob('*.gp')), [])
            self.assertEqual(plots.pyplot().get_fignums(), [])
            self.assertEqual((directory / 'benchmark_matrix.csv').read_bytes(), original)

    def test_zoom_and_optional_fit_panels(self):
        with tempfile.TemporaryDirectory(prefix='analysis-extra-') as temporary:
            directory = Path(temporary)
            write_matrix(directory)
            values, counts = data.load(directory, None)
            for (size, k, method), value in list(values.items()):
                if size == 800000:
                    values[25600000, k, method] = value * 32
            counts[25600000] = counts[800000]
            figures = plots.comparison_plot(values, counts)
            name, figure = next(figures)
            self.assertEqual(name, 'compute_comparison')
            self.assertEqual(list(figure.axes[0].lines[0].get_ydata()),
                             [values[100000, k, calculations.CPU] for k in counts[100000]])
            plots.pyplot().close(figure)
            name, figure = next(figures)
            self.assertEqual(name, 'compute_speedup')
            self.assertEqual(list(figure.axes[0].lines[0].get_ydata()),
                             [values[100000, k, calculations.CPU] / values[100000, k, calculations.GPU]
                              for k in counts[100000]])
            plots.pyplot().close(figure)
            with redirect_stdout(io.StringIO()):
                plots.save_figures(directory, figures)
            self.assertTrue((directory / 'compute_combined_gpu_zoom.png').exists())
            report = calculations.crossover_fit_report(values, counts, [])
            reports = calculations.fit_reports(report['points'])
            # Exercise rendering of a supplied transition model, independently of fitting.
            reports['crossover_hyperbola_transition_fit'] = dict(
                model='c + a/N + d/(1+exp(-ln(N/N1)/w))',
                a=1, c=2, d1=1, N1=.4, w1=.3)
            reports['crossover_hyperbola_models']['models'][1] = dict(
                m=1, status='ok', a=1, c=2, transitions=[dict(d=1, N=.4, w=.3)])
            with redirect_stdout(io.StringIO()):
                plots.save_figures(directory, plots.crossover_plot(report, reports))
            self.assertTrue((directory / 'crossover_by_point_count_from_200k.png').exists())
            self.assertTrue((directory / 'crossover_hyperbola_transition.png').exists())
            self.assertEqual(plots.pyplot().get_fignums(), [])

    def test_matrix_plots_and_histogram_peaks(self):
        self.assertEqual(calculations.histogram_peak([7]), 7)
        self.assertEqual(calculations.histogram_peak([7, 7]), 7)
        self.assertAlmostEqual(calculations.histogram_peak([1, 1, 61]), 1.5)
        self.assertAlmostEqual(calculations.histogram_peak([1, 61]), 1.5)
        with tempfile.TemporaryDirectory(prefix='analysis-matrix-') as temporary:
            directory = Path(temporary)
            rows = write_matrix(directory)
            groups = data.read_matrix(directory, raw_samples=True)
            self.assertEqual(sum(map(len, groups.values())), len(rows))
            groups = {key: value for key, value in groups.items()
                      if key[2] in (100000, 200000) and key[3] in (1, 2)
                      and key[4] in (calculations.CPU, calculations.GPU)}
            cpu_key = ('cpu', 'four-ccx', 100000, 1, calculations.CPU)
            groups[cpu_key] = [1, 1, 61]
            figures = plots.histogram_matrix_plot(groups)
            name, figure = next(figures)
            self.assertEqual(name, 'histograms_points_100000')
            self.assertEqual(len(figure.axes), 4)
            self.assertIn('four-ccx', figure.axes[0].get_title())
            self.assertIn('2 Transformationen', figure.axes[2].get_ylabel())
            self.assertEqual(figure.axes[0].lines[0].get_xdata()[0], 1.5)
            with redirect_stdout(io.StringIO()):
                plots.save_figures(directory, [(name, figure)])
                plots.save_figures(directory, figures)
            name, figure = next(plots.runtime_by_points_plot(groups))
            self.assertEqual(name, 'runtime_by_points_four-ccx')
            self.assertEqual(list(figure.axes[0].lines[0].get_xdata()), [100000, 200000])
            self.assertEqual(figure.axes[0].lines[0].get_ydata()[0], 1.5)
            self.assertEqual(figure.axes[0].get_yscale(), 'log')
            self.assertIsNotNone(figure.axes[0].get_legend())
            self.assertAlmostEqual(figure.axes[0].lines[-1].get_xdata()[0],
                                   64 * 2**20 / 24)
            with redirect_stdout(io.StringIO()):
                plots.save_figures(directory, [(name, figure)])
            images = list(directory.glob('*.png'))
            self.assertEqual(len(images), 3)
            for image in images:
                self.assertEqual(image.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
            self.assertEqual(plots.pyplot().get_fignums(), [])

    def test_legacy_files(self):
        with tempfile.TemporaryDirectory(prefix='analysis-test-') as temporary:
            directory = Path(temporary)
            for device, methods in [('cpu', data.METHODS[:1]), ('gpu', data.METHODS[1:])]:
                rows = [dict(points=100, transformations=1, method=method, median_us=2, samples=3)
                        for method in methods]
                data.write_csv(directory / f'compute_{device}_100_results.csv', rows)
            values, counts = data.load(directory, None)
            self.assertEqual(counts, {100: [1]})
            self.assertEqual(len(values), 5)


if __name__ == '__main__':
    unittest.main()
