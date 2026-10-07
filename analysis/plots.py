"""Render prepared benchmark values with Matplotlib, without reading CSV files."""

import math

from calculations import CPU, GPU, exponential, grid, histogram, stepped

METHODS = {CPU: '#D55E00', 'GPU kernel only': '#56B4E9',
           'GPU resident (host sync)': '#009E73', 'Upload + GPU': '#8C564B',
           GPU: '#777777'}


def pyplot():
    # Lazy loading keeps --help and --no-plot usable without Matplotlib.
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    return plt


def save_figures(pictures, figures):
    """Save and close each figure immediately to bound batch memory usage."""
    plt = pyplot()
    pictures.mkdir(parents=True, exist_ok=True)
    for name, figure in figures:
        try:
            path = pictures / (name + '.png')
            figure.savefig(path, dpi=150)
            print(f'Plot: {path}')
        finally:
            plt.close(figure)


def panels(count, title):
    if not count:
        raise ValueError('Keine Messwerte vorhanden')
    columns = min(3, count)
    rows = math.ceil(count / columns)
    figure, axes = pyplot().subplots(rows, columns, figsize=(6 * columns, 4 * rows),
                                    squeeze=False, layout='constrained')
    figure.suptitle(title)
    axes = list(axes.flat)
    for axis in axes[count:]:
        axis.set_visible(False)
    return figure, axes[:count]


def comparison_plot(values, counts_by_size):
    sizes = sorted(counts_by_size)
    for speedup in (False, True):
        title = 'GPU-Vorteil inklusive Upload und Download' if speedup else 'CPU / GPU - Transformationsketten'
        figure, axes = panels(len(sizes), title)
        for axis, size in zip(axes, sizes):
            counts = counts_by_size[size]
            if speedup:
                axis.plot(counts, [values[size, k, CPU] / values[size, k, GPU] for k in counts],
                          'o-', color='#009E73', label='CPU / GPU-Roundtrip')
                axis.axhline(1, color='gray', linestyle='--', label='Gleich schnell')
            else:
                for method in (CPU, GPU):
                    axis.plot(counts, [values[size, k, method] for k in counts],
                              'o-', color=METHODS[method], label=method)
            axis.set(title=f'{size:g} Punkte', xlabel='Transformationen pro Punkt',
                     ylabel='CPU-Zeit / GPU-Zeit' if speedup else 'Laufzeit pro Cloud [us]')
            axis.set_xscale('log', base=2)
            axis.set_ylim(bottom=0)
            axis.legend(fontsize=8)
        yield ('compute_speedup' if speedup else 'compute_comparison'), figure

    figure, axis = pyplot().subplots(figsize=(14, 8), layout='constrained')
    palette = pyplot().get_cmap('turbo')
    for index, size in enumerate(sizes):
        counts = counts_by_size[size]
        color = palette(index / max(1, len(sizes) - 1))
        for method, style, label in ((CPU, '-', 'CPU'), (GPU, '--', 'GPU gesamt')):
            axis.plot(counts, [values[size, k, method] for k in counts], style,
                      color=color, label=f'{size / 1e6:g} Mio. - {label}')
    axis.set(xlabel='Transformationen pro Punkt', ylabel='Laufzeit pro Cloud [us]',
             title='CPU / GPU - alle Punktwolken-Groessen')
    axis.set_xscale('log', base=2)
    axis.set_ylim(bottom=0)
    axis.legend(bbox_to_anchor=(1, 1), loc='upper left', fontsize=8)
    yield 'compute_combined', figure
    if 25600000 in sizes:
        # A separate figure is needed because the batch saver closes each yield.
        figure, axis = pyplot().subplots(figsize=(14, 8), layout='constrained')
        first_above, last_below = [], []
        for index, size in enumerate(sizes):
            counts = counts_by_size[size]
            for method, style in ((CPU, '-'), (GPU, '--')):
                axis.plot(counts, [values[size, k, method] for k in counts], style,
                          color=palette(index / max(1, len(sizes) - 1)),
                          label=f'{size / 1e6:g} Mio. - {method}')
            above = next((k for k in counts if values[size, k, CPU] > values[size, k, GPU]), None)
            if above is not None:
                first_above.append((above, values[size, above, CPU]))
                below = [k for k in counts if k < above and values[size, k, CPU] < values[size, k, GPU]]
                if below:
                    last_below.append((below[-1], values[size, below[-1], CPU]))
        for points, style, label in ((first_above, 'o-', 'CPU erstmals langsamer'),
                                     (last_below, 's--', 'CPU zuletzt schneller davor')):
            if points:
                axis.plot(*zip(*points), style, color='red', label=label)
        if len(first_above) >= 2 and len(last_below) >= 2:
            axis.fill(*zip(*(first_above + last_below[::-1])), color='red', alpha=.2)
        axis.set_xscale('log', base=2)
        axis.set_ylim(0, 1.1 * max(values[25600000, k, GPU] for k in counts_by_size[25600000]))
        axis.set(title='CPU / GPU - GPU-Zoom (25.6 Mio. Punkte)',
                 xlabel='Transformationen pro Punkt', ylabel='Laufzeit pro Cloud [us]')
        axis.legend(bbox_to_anchor=(1, 1), loc='upper left', fontsize=8)
        yield 'compute_combined_gpu_zoom', figure


def cpu_gpu_plot(cpu, gpu):
    sizes = sorted({n for n, _ in cpu} | {n for n, _ in gpu})
    figure, axes = panels(len(sizes), 'CPU / GPU - Histogramm-Peaks (60 Bins)')
    stack = ('GPU kernel only', 'PCIe upload + GPU kernel',
             'PCIe upload + GPU kernel + PCIe download')
    for axis, size in zip(axes, sizes):
        items = [('CPU: ' + method, value) for (n, method), value in cpu.items() if n == size]
        items += [(method, value) for (n, method), value in gpu.items() if n == size]
        components = None
        if all((size, method) in gpu for method in stack):
            kernel, upload, total = [gpu[size, method] for method in stack]
            if not kernel <= upload <= total:
                pyplot().close(figure)
                raise ValueError(f'{size} Punkte: GPU-Peaks erlauben keine positive Aufteilung')
            components = [('Upload', upload - kernel, '#8C564B'),
                          ('GPU', kernel, '#56B4E9'), ('Download', total - upload, '#777777')]
        used = set()
        for index, (method, value) in enumerate(items):
            if components and method in stack[1:]:
                bottom = 0
                for label, height, color in components[:2 if method == stack[1] else 3]:
                    axis.bar(index, height, bottom=bottom, color=color,
                             label=label if label not in used else None)
                    bottom += height
                    used.add(label)
            else:
                axis.bar(index, value, color=METHODS.get(method, '#D55E00'))
        axis.set_xticks(range(len(items)), [label for label, _ in items], rotation=30, ha='right', fontsize=7)
        axis.set(title=f'{size:g} Punkte', ylabel='Laufzeit pro Cloud [us]')
        if components:
            axis.legend(title='Anteile aus Peak-Differenzen', fontsize=7)
    yield 'cpu_gpu_comparison', figure


def histogram_plot(groups, stem):
    for index, ((points, transforms, method), values) in enumerate(sorted(groups.items())):
        width, bins = histogram(values)
        figure, axis = pyplot().subplots(figsize=(10, 6), layout='constrained')
        centers, counts = zip(*bins)
        axis.bar(centers, counts, width=width, color='#56B4E9')
        axis.set(title=f'{points} Punkte, {transforms} Transformationen: {method}',
                 xlabel='Laufzeit pro Cloud [us]', ylabel='Haeufigkeit')
        yield f'{stem}_{transforms}_{index}', figure








def boundary_axis(axis, records):
    x = [r['points'] / 1e6 for r in records]
    y = [r['x'] for r in records]
    axis.errorbar(x, y, yerr=[[r['x'] - r['lower'] for r in records],
                            [r['upper'] - r['x'] for r in records]],
                  fmt='o', color='#333333', label='Intervallmitte und Grenzen')
    reversals = [r for r in records if r['winner_changes'] > 1]
    if reversals:
        axis.scatter([r['points'] / 1e6 for r in reversals], [r['x'] for r in reversals],
                     facecolors='none', edgecolors='red', s=100, label='Mehrfache Gewinnerwechsel')
    axis.set_xscale('log', base=10)
    axis.set(xlabel='Punkte pro Cloud [Millionen]', ylabel='Transformationen pro Punkt')
    axis.set_ylim(bottom=0)
    # Historical hardware reference: four workers, XYZ input/output = 24 B/point.
    for name, capacity in [('L1', 4*32*1024), ('L2', 4*512*1024), ('L3', 4*16*1024*1024)]:
        location = capacity / 24 / 1e6
        if min(x) <= location <= max(x):
            axis.axvline(location, linestyle=':', color='red', alpha=.5)
            axis.text(location, .02, name, transform=axis.get_xaxis_transform(), rotation=90)


def describe(model):
    names = ('a_us', 'b_per_transform', 'c_us', 'x0', 'a', 'c', 'd1', 'N1', 'w1',
             'asymptote', 'rmse_us', 'rmse_transformations', 'rmse', 'r_squared')
    text = ', '.join(f'{key}={model[key]:.5g}' for key in names
                     if isinstance(model.get(key), (int, float)))
    return text + '\nEmpirischer Fit an Intervallmitten; keine exakte Entscheidungsgrenze.'


def crossover_plot(report, reports):
    records = sorted(report['points'], key=lambda r: r['points'])
    figure, axis = pyplot().subplots(figsize=(12, 8), layout='constrained')
    xs = [r['x'] for r in records]
    curve = grid(min(xs), max(xs))
    best = report['best_by_sse']
    axis.errorbar(xs, [r['y_us'] for r in records],
                  xerr=[[r['x'] - r['lower'] for r in records], [r['upper'] - r['x'] for r in records]],
                  fmt='o', label='Intervallmitten und Grenzen')
    axis.plot(curve, [exponential(best, x) for x in curve], color='red', label='Exponentialfit')
    axis.set(title='Exponentialfit: a * exp(-b * (x-x0)) + c',
             xlabel='Transformationen pro Punkt', ylabel='CPU-Laufzeit am Intervallmittelpunkt [us]')
    axis.set_ylim(bottom=0)
    axis.legend()
    figure.supxlabel(describe(best), fontsize=8)
    yield 'crossover_exponential_fit', figure
    for key, model in reports.items():
        if key == 'crossover_hyperbola_models':
            continue
        selected = [r for r in records if r['points'] >= model.get('minimum_points', 0)]
        curve = grid(selected[0]['points'] / 1e6, selected[-1]['points'] / 1e6, True)
        figure, axis = pyplot().subplots(figsize=(12, 8), layout='constrained')
        boundary_axis(axis, selected)
        if 'fit' in model:
            ys = [exponential(model['fit'], x) for x in curve]
        elif 'd1' in model:
            stepped_model = dict(model, transitions=[dict(d=model['d1'], N=model['N1'], w=model['w1'])])
            ys = [stepped(stepped_model, x) for x in curve]
        else:
            ys = [stepped(model, x) for x in curve]
        axis.plot(curve, ys, color='red', label='Fit')
        axis.set_title(model['model'])
        axis.legend()
        figure.supxlabel(describe(model), fontsize=8)
        yield key.removesuffix('_fit'), figure
    models = reports['crossover_hyperbola_models']['models']
    figure, axes = pyplot().subplots(3, 2, figsize=(16, 15), layout='constrained')
    curve = grid(records[0]['points'] / 1e6, records[-1]['points'] / 1e6, True)
    for axis, model in zip(axes.flat, models):
        boundary_axis(axis, records)
        axis.set_title(f"Hyperbel mit {model['m']} Uebergaengen")
        if model['status'] == 'ok':
            axis.plot(curve, [stepped(model, x) for x in curve], color='red', label='Fit')
            details = describe(model)
            for step in model['transitions']:
                details += f"\nN={step['N']:.4g}, w={step['w']:.4g}, d={step['d']:.4g}"
            axis.text(.03, .97, details, va='top', transform=axis.transAxes, fontsize=7, wrap=True)
        else:
            axis.text(.05, .9, 'Zu wenige Messpunkte oder singulaerer Fit', transform=axis.transAxes)
    yield 'crossover_hyperbola_comparison', figure
