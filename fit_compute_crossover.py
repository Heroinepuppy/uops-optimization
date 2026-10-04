import os
"""Fit exponential curves to midpoint estimates of measured crossover brackets."""
import argparse, csv, json, math, subprocess
from pathlib import Path
from run_compute_benchmark import ROOT, load, quote, discover_sizes
from hyperbola_steps import fit_steps


def fit(points, offset):
    x0=min(x for x,y in points)
    ys=[y for x,y in points]
    def solve(log_b):
        b=math.exp(log_b)
        zs=[math.exp(-b*(x-x0)) for x,y in points]
        if offset:
            zm=sum(zs)/len(zs); ym=sum(ys)/len(ys)
            denom=sum((z-zm)**2 for z in zs)
            if denom<1e-25: return float('inf'),0,0,b
            a=sum((z-zm)*(y-ym) for z,y in zip(zs,ys))/denom
            c=ym-a*zm
        else:
            a=sum(z*y for z,y in zip(zs,ys))/sum(z*z for z in zs);c=0
        loss=sum((a*z+c-y)**2 for z,y in zip(zs,ys))
        return loss,a,c,b
    # Profile least squares: for each b, solve a and c exactly. Search all
    # local minima on a broad logarithmic grid, then refine each bracket.
    unique_x = sorted({x for x,y in points})
    minimum_spacing = min(b-a for a,b in zip(unique_x,unique_x[1:]))
    b_max = max(10.0, 30.0/minimum_spacing)
    grid=[math.log(1e-6)+i*(math.log(b_max)-math.log(1e-6))/2000 for i in range(2001)]
    costs=[solve(t)[0] for t in grid]
    candidates=[(costs[0],grid[0]),(costs[-1],grid[-1])]
    for i in range(1,len(grid)-1):
        if costs[i]>costs[i-1] or costs[i]>costs[i+1]: continue
        left,right=grid[i-1],grid[i+1]
        for _ in range(90):
            l=right-(right-left)/1.61803398875
            r=left+(right-left)/1.61803398875
            if solve(l)[0]<solve(r)[0]:right=r
            else:left=l
        t=(left+right)/2;candidates.append((solve(t)[0],t))
    _,best=min(candidates)
    sse,a,c,b=solve(best)
    mean=sum(ys)/len(ys);sst=sum((y-mean)**2 for y in ys)
    return dict(model='a * exp(-b * (x-x0)) + c',a_us=a,b_per_transform=b,
                c_us=c,x0=x0,sse_us2=sse,rmse_us=math.sqrt(sse/len(ys)),r_squared=1-sse/sst,
                parameters=3 if offset else 2,b_search_min=1e-6,b_search_max=b_max)


def point_count_plot(records, directory, pictures, minimum_points=0):
    records = [r for r in records if r['points'] >= minimum_points]
    if len(records) < 4:
        print(f'Fit ab {minimum_points} Punkten uebersprungen: weniger als vier Intervalle.')
        return []
    stem = 'crossover_by_point_count' + ('_from_200k' if minimum_points else '')
    title_suffix = ' (Fit ab 200.000 Punkten)' if minimum_points else ''
    ordered = sorted(records, key=lambda r: r['points'])
    size_points = [(r['points']/1e6, r['x']) for r in ordered]
    size_models = [fit(size_points, False), fit(size_points, True)]
    size_fit = min(size_models, key=lambda r: r['sse_us2'])
    # The generic fitter's historical *_us keys refer to y units. Here y is
    # transformations and x is millions of points; save explicit units.
    size_report = dict(
        minimum_points=minimum_points,
        model='K(N) = a * exp(-b * (N-N0)) + c',
        N_unit='million points', K_unit='transformations per point',
        a=size_fit['a_us'], b=size_fit['b_per_transform'],
        c=size_fit['c_us'], N0=size_fit['x0'],
        rmse_transformations=size_fit['rmse_us'], r_squared=size_fit['r_squared'],
        objective='unweighted squared residuals in transformations; interval midpoints',
        points=size_points)
    (directory/f'{stem}_fit.json').write_text(json.dumps(size_report,indent=2)+'\n')
    xmin = minimum_points/1e6 if minimum_points else 10**math.floor(math.log10(ordered[0]['points']/1e6))/1.2
    ymax = max(r['upper'] for r in ordered) * 1.15
    raw_step = ymax / 9
    magnitude = 10**math.floor(math.log10(raw_step))
    step = next(magnitude*f for f in (1,2,2.5,5,10) if magnitude*f >= raw_step)
    ymax = math.ceil(ymax/step)*step
    commands = ['reset',
                 "set terminal pngcairo size 1800,1100 enhanced font 'Segoe UI,12'",
                 f"set output {quote(pictures / (stem + '.png'))}",
                 f"set title 'CPU / GPU - Umschlag in Abhaengigkeit der Punktzahl{title_suffix}' font ',18'",
                 "set xlabel 'Punkte pro Cloud [Millionen, logarithmisch zur Basis 10]'",
                 "set ylabel 'Transformationen pro Punkt am ersten GPU-Vorteil'",
                 'set logscale x 10', 'unset grid', 'set mxtics 10', 'unset mytics',
                 f"set xrange [{xmin}:{ordered[-1]['points']/1e6*1.2}]",
                 'set xtics autofreq', 'set format x "%g"',
                 f'set yrange [0:{ymax}]', f'set ytics 0,{step},{ymax}',
                 'set key top right', 'set bmargin 5',
                 "set label 1 'GPU inklusive Upload und Download; Balken: gemessenes Umschlagintervall' at graph 0.40,0.22 left",
                 "set label 2 'Empirischer Fit an Intervallmitten; keine exakte Entscheidungsgrenze.' at graph 0.40,0.17 left",
                 "set label 3 'K(N) = a * exp(-b * (N-N0)) + c' at graph 0.40,0.76 left font ',15'",
                 f"set label 4 'K(N) = {size_fit['a_us']:.3f} * exp(-{size_fit['b_per_transform']:.6f} * (N-{size_fit['x0']:g})) + {size_fit['c_us']:.3f}' at graph 0.40,0.71 left",
                 f"set label 5 'a = {size_fit['a_us']:.3f} Transformationen/Punkt: Ueberhoehung ueber c bei N0' at graph 0.40,0.64 left",
                 f"set label 6 'b = {size_fit['b_per_transform']:.6f} / Mio. Punkte: Abklingrate' at graph 0.40,0.59 left",
                 f"set label 7 'c = {size_fit['c_us']:.3f} Transformationen/Punkt: asymptotischer Grenzwert (moeglicherweise durch das PCIe-Interface limitiert)' at graph 0.40,0.54 left textcolor rgb '#D97706' font ',10'",
                 f"set label 8 'N0 = {size_fit['x0']:g} Mio. Punkte: fester Bezugspunkt (nicht gefittet)' at graph 0.40,0.49 left",
                 "set label 9 'N: Millionen Punkte; K: Transformationen pro Punkt am Umschlag' at graph 0.40,0.42 left",
                 f"set label 10 'R^2 = {size_fit['r_squared']:.4f}; RMSE = {size_fit['rmse_us']:.3f} Transformationen/Punkt' at graph 0.50,0.37 left",
                 'set samples 1000',
                 f"g(x)={size_fit['a_us']}*exp(-{size_fit['b_per_transform']}*(x-{size_fit['x0']}))+{size_fit['c_us']}",
                 '$boundary << EOD',
                 *(f'{r["points"]/1e6} {r["x"]} {r["lower"]} {r["upper"]} {r["winner_changes"]}' for r in ordered),
                 'EOD',
                 "plot g(x) with lines lw 3 lc rgb '#FF0000' title 'Exponentialfit', "
                 "$boundary using 1:2:3:4 with yerrorbars lw 2 pt 7 ps 1.4 lc rgb '#FF0000' title 'Intervallmitte und Grenzen', "
                 "$boundary using 1:($5>1 ? $2 : 1/0) with points pt 6 ps 2.2 lw 2 lc rgb '#333333' title 'Mehrfache Gewinnerwechsel in der Messreihe'",
                 'unset output']
    return commands


def hyperbola_fit(points):
    inverse = [1/n for n,k in points]
    ys = [k for n,k in points]
    xm, ym = sum(inverse)/len(points), sum(ys)/len(points)
    a = sum((x-xm)*(y-ym) for x,y in zip(inverse,ys)) / sum((x-xm)**2 for x in inverse)
    c = ym-a*xm
    residuals = [y-(c+a*x) for x,y in zip(inverse,ys)]
    sse = sum(r*r for r in residuals)
    sst = sum((y-ym)**2 for y in ys)
    return dict(model='K(N) = c + a/N', N_unit='million points',
                K_unit='transformations per point', a=a, c=c,
                rmse_transformations=math.sqrt(sse/len(points)),
                r_squared=1-sse/sst if sst else None,
                objective='unweighted squared residuals in transformations; interval midpoints',
                points=points, residuals=residuals)


def hyperbola_plot(records, directory, pictures):
    ordered = sorted(records, key=lambda r:r['points'])
    report = hyperbola_fit([(r['points']/1e6,r['x']) for r in ordered])
    (directory/'crossover_hyperbola_fit.json').write_text(json.dumps(report,indent=2)+'\n')
    a,c = report['a'],report['c']
    return ['reset', "set terminal pngcairo size 1800,1100 enhanced font 'Segoe UI,12'",
            f'set output {quote(pictures / "crossover_hyperbola.png")}',
            "set title 'CPU / GPU - Hyperbelfit der Umschlagpunkte'",
            "set xlabel 'Punkte pro Cloud [Millionen, logarithmisch zur Basis 10]'",
            "set ylabel 'Transformationen pro Punkt am ersten GPU-Vorteil'",
            'set logscale x 10', 'set mxtics 10', 'unset grid', 'set key top right',
            f"set xrange [{ordered[0]['points']/1e6}:{ordered[-1]['points']/1e6*1.2}]",
            'set yrange [0:*]', 'set samples 1000',
            f'h(x)={c}+{a}/x',
            f"set label 1 'K(N) = c + a/N = {c:.4f} + {a:.4f}/N' at graph 0.4,0.75",
            f"set label 2 'a = {a:.4f} (Transformationen/Punkt) * Mio. Punkte' at graph 0.4,0.69",
            f"set label 3 'c = {c:.4f} Transformationen/Punkt: asymptotischer Grenzwert' at graph 0.4,0.63 textcolor rgb '#D97706'",
            f"set label 4 'R^2 = {report['r_squared']}; RMSE = {report['rmse_transformations']:.3f}' at graph 0.4,0.57",
            "set label 5 'Empirischer Fit; keine eindeutige Zuordnung zu Hardwarelimits.' at graph 0.4,0.48",
            '$hyperbola_points << EOD',
            *(f"{r['points']/1e6} {r['x']} {r['lower']} {r['upper']}" for r in ordered), 'EOD',
            "plot h(x) with lines lw 3 lc rgb '#FF0000' title 'Hyperbelfit', "
            "$hyperbola_points using 1:2:3:4 with yerrorbars pt 7 lc rgb '#333333' title 'Intervallmitte und Grenzen'",
            'unset output']


def transition_fit(points):
    """Profile a,c,d by linear least squares; search log(N1),log(w)."""
    if len(points) < 6 or len({n for n,k in points}) < 6:
        raise ValueError('Mindestens sechs unterschiedliche Punktgroessen fuer den Uebergangsfit erforderlich')
    ns, ys = zip(*points)
    if min(ns) <= 0:
        raise ValueError('Punktgroessen muessen positiv sein')
    xs = [1/n for n in ns]
    xm, ym = sum(xs)/len(xs), sum(ys)/len(ys)
    xx = sum((x-xm)**2 for x in xs)
    bounds = [(math.log(min(ns)), math.log(max(ns))), (math.log(.03), math.log(3.0))]

    def solve(t, v):
        w = math.exp(v)
        zs = [1/(1+math.exp(max(-700, min(700, -(math.log(n)-t)/w)))) for n in ns]
        zm = sum(zs)/len(zs)
        zz = sum((z-zm)**2 for z in zs)
        xz = sum((x-xm)*(z-zm) for x,z in zip(xs,zs))
        xy = sum((x-xm)*(y-ym) for x,y in zip(xs,ys))
        zy = sum((z-zm)*(y-ym) for z,y in zip(zs,ys))
        det = xx*zz-xz*xz
        if det <= 1e-12*xx*zz:
            return (float('inf'), 0, 0, 0)
        a, d = (xy*zz-zy*xz)/det, (zy*xx-xy*xz)/det
        c = ym-a*xm-d*zm
        return sum((y-c-a*x-d*z)**2 for x,z,y in zip(xs,zs,ys)), a, c, d

    candidates = []
    for i in range(81):
        t = bounds[0][0]+i*(bounds[0][1]-bounds[0][0])/80
        for j in range(41):
            v = bounds[1][0]+j*(bounds[1][1]-bounds[1][0])/40
            candidates.append((solve(t,v)[0], t, v))
    refined = []
    for loss,t,v in sorted(candidates)[:12]:
        dt,dv = (bounds[0][1]-bounds[0][0])/80, (bounds[1][1]-bounds[1][0])/40
        for _ in range(160):
            choices = [(loss,t,v)]
            for it,iv in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)):
                nt = max(bounds[0][0], min(bounds[0][1], t+it*dt))
                nv = max(bounds[1][0], min(bounds[1][1], v+iv*dv))
                choices.append((solve(nt,nv)[0],nt,nv))
            best = min(choices)
            if best[0] >= loss:
                dt *= .5; dv *= .5
            else:
                loss,t,v = best
            if max(dt,dv) < 1e-8: break
        refined.append((loss,t,v))
    loss,t,v = min(refined)
    loss,a,c,d = solve(t,v)
    w,n1 = math.exp(v),math.exp(t)
    sst = sum((y-ym)**2 for y in ys)
    return dict(model='K(N) = c + a/N + d1/(1+exp(-ln(N/N1)/w1))',
                m=1, N_unit='million points', K_unit='transformations per point',
                a=a, c=c, d1=d, N1=n1, w1=w, asymptote=c+d,
                sse=loss, rmse_transformations=math.sqrt(loss/len(points)),
                r_squared=1-loss/sst if sst else None, points=points,
                search_bounds=dict(N1=[min(ns),max(ns)], w1=[.03,3.0]),
                at_search_boundary=any(min(abs(q-lo),abs(q-hi)) < 1e-5 for q,(lo,hi) in zip((t,v),bounds)),
                objective='unweighted squared residuals in transformations; interval midpoints',
                method='profile linear least squares; logarithmic grid and 12 local search starts',
                caveat='Empirical transition, not an identified cache boundary; a,c,d1 unconstrained')


def transition_plot(records, directory, pictures):
    if len(records) < 6:
        print('Uebergangsfit uebersprungen: weniger als sechs Intervalle.')
        return []
    ordered = sorted(records, key=lambda r:r['points'])
    report = transition_fit([(r['points']/1e6,r['x']) for r in ordered])
    (directory/'crossover_hyperbola_transition_fit.json').write_text(json.dumps(report,indent=2)+'\n')
    a,c,d,n,w = (report[k] for k in ('a','c','d1','N1','w1'))
    return ['reset', "set terminal pngcairo size 1800,1100 enhanced font 'Segoe UI,12'",
            f'set output {quote(pictures / "crossover_hyperbola_transition.png")}',
            "set title 'CPU / GPU - Hyperbel mit einem weichen Uebergang (m=1)'",
            "set xlabel 'Punkte pro Cloud [Millionen, logarithmisch zur Basis 10]'",
            "set ylabel 'Transformationen pro Punkt am ersten GPU-Vorteil'",
            'set logscale x 10', 'set mxtics 10', 'unset grid', 'set key top right',
            f"set xrange [{ordered[0]['points']/1e6}:{ordered[-1]['points']/1e6*1.2}]",
            'set yrange [0:*]', 'set samples 2000',
            f'h(x)={c}+{a}/x+({d})/(1+exp(-log(x/{n})/{w}))',
            "set label 1 'K(N) = c + a/N + d1 / (1 + exp(-ln(N/N1)/w1))' at graph 0.35,0.80",
            f"set label 2 'a = {a:.6g} (Transformationen/Punkt) * Mio. Punkte' at graph 0.35,0.74",
            f"set label 3 'c = {c:.6g} Transformationen/Punkt: Basisoffset' at graph 0.35,0.68",
            f"set label 4 'd1 = {d:.6g} Transformationen/Punkt: Stufenhoehe' at graph 0.35,0.62",
            f"set label 5 'N1 = {n:.6g} Mio. Punkte: Uebergangsmitte; w1 = {w:.6g}: Breite in ln(N)' at graph 0.35,0.56",
            f"set label 6 'Grenzwert fuer grosse N: c + d1 = {c+d:.6g} Transformationen/Punkt' at graph 0.35,0.50 textcolor rgb '#D97706'",
            f"set label 7 'R^2 = {report['r_squared']:.5f}; RMSE = {report['rmse_transformations']:.3f}' at graph 0.35,0.44",
            f"set label 8 'Empirischer Uebergang, keine identifizierte Cachegrenze. Suchrand erreicht: {report['at_search_boundary']}' at graph 0.35,0.36 font ',10'",
            '$transition_points << EOD',
            *(f"{r['points']/1e6} {r['x']} {r['lower']} {r['upper']}" for r in ordered), 'EOD',
            "plot h(x) with lines lw 3 lc rgb '#FF0000' title 'Hyperbel + weicher Uebergang', "
            "$transition_points using 1:2:3:4 with yerrorbars pt 7 lc rgb '#333333' title 'Intervallmitte und Grenzen'",
            'unset output']


def hyperbola_panels(records, directory, pictures):
    ordered = sorted(records, key=lambda r:r['points'])
    points = [(r['points']/1e6,r['x']) for r in ordered]
    models = [fit_steps(points,m) for m in range(6)]
    (directory/'crossover_hyperbola_models.json').write_text(json.dumps(dict(
        formula='K(N)=c+a/N+sum(d_j/(1+exp(-ln(N/N_j)/w_j)))',
        N_unit='million points', K_unit='transformations per point', points=points,
        objective='unweighted squared residuals at interval midpoints', models=models),indent=2)+'\n')
    commands=['reset', "set terminal pngcairo size 2400,2400 enhanced font 'Segoe UI,11'",
              f'set output {quote(pictures / "crossover_hyperbola_comparison.png")}',
              'set multiplot layout 3,2 rowsfirst', 'set logscale x 10', 'set mxtics 10',
              'unset grid', 'set key top right', 'set samples 2000',
              "set xlabel 'Punkte pro Cloud [Millionen, log10]'",
              "set ylabel 'Transformationen pro Punkt'",
              f'set xrange [{points[0][0]}:{points[-1][0]*1.2}]',
              f"set yrange [0:{max(r['upper'] for r in ordered)*1.15}]",
              '$comparison << EOD',
              *(f"{r['points']/1e6} {r['x']} {r['lower']} {r['upper']}" for r in ordered),'EOD']
    for model in models:
        m=model['m']
        commands += ['unset label', f"set title 'Hyperbel mit {m} Uebergangstermen ({2+3*m} Parameter)'",
                     f"set label 1 'K(N) = c + a/N + Summe(j=1..{m}) d_j / (1 + exp(-ln(N/N_j)/w_j))' at graph 0.30,0.80 font ',10' noenhanced"]
        if model['status']!='ok':
            commands += ["set label 2 'Zu wenige Messpunkte oder singulaerer Fit' at graph 0.3,0.65",
                         "plot $comparison using 1:2:3:4 with yerrorbars title 'Messintervalle'"]
            continue
        expression=f"{model['c']}+{model['a']}/x"
        commands += [f"set label 2 'a={model['a']:.5g}; c={model['c']:.5g}' at graph 0.30,0.73"]
        for j,s in enumerate(model['transitions'],1):
            expression+=f"+({s['d']})/(1+exp(-log(x/{s['N']})/{s['w']}))"
            commands += [f"set label {j+2} 'd{j}={s['d']:.5g}; N{j}={s['N']:.5g} Mio.; w{j}={s['w']:.5g}' at graph 0.30,{.73-j*.06}"]
        commands += [f"set label 10 'Grenzwert c+Summe(d): {model['asymptote']:.5g}' at graph 0.30,0.36 textcolor rgb '#D97706'",
                     f"set label 11 'RMSE={model['rmse']:.3f}; R^2={model['r_squared']:.5f}' at graph 0.30,0.30",
                     f"set label 12 'Suchrand: {model['at_search_boundary']}; empirisch, keine Cache-Zuordnung' at graph 0.30,0.24 font ',9'",
                     f'h(x)={expression}',
                     "plot h(x) with lines lw 3 lc rgb '#FF0000' title 'Fit', $comparison using 1:2:3:4 with yerrorbars pt 7 lc rgb '#333333' title 'Messintervalle'"]
    return commands+['unset multiplot','unset output']


def cache_markers(commands):
    # Four workers in separate CCX; input and output XYZ float arrays: 24 B/point.
    boundaries = [('L1', 4*32*1024/24/1e6),
                  ('L2', 4*512*1024/24/1e6),
                  ('L3', 4*16*1024*1024/24/1e6)]
    result = []
    logarithmic = False
    low, high = 0, float('inf')
    for command in commands:
        if command == 'reset':
            logarithmic = False
            low, high = 0, float('inf')
        if command == 'set logscale x 10': logarithmic = True
        if command.startswith('set xrange ['):
            low, high = map(float, command.split('[')[1].rstrip(']').split(':'))
        if command.startswith('plot ') and logarithmic:
            for tag,(name,n) in enumerate(boundaries,100):
                if low <= n <= high:
                    result += [f"set arrow {tag} from first {n}, graph 0 to first {n}, graph 1 nohead dt 2 lw 2 lc rgb '#FF0000' back",
                               f"set label {tag} '{name}' at first {n}, graph 0.04 rotate by 90 offset -0.6,0 textcolor rgb '#FF0000' font ',9' front"]
        result.append(command)
    return result


def main():
    os.chdir(Path(__file__).resolve().parent)
    pictures = ROOT / "results" / "pics"
    pictures.mkdir(parents=True, exist_ok=True)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path,nargs='?',default=ROOT/'results'/'data')
    args=parser.parse_args();directory=Path(os.path.relpath(args.directory))
    values, counts = load(directory, discover_sizes(directory))
    boundary_file = directory/'refined_crossover.csv'
    all_rows = []
    if boundary_file.exists():
        with boundary_file.open() as f: all_rows=list(csv.DictReader(f,delimiter=';'))
    stale = not all_rows or any(
        (int(r['points']), int(r[field]), 'CPU 4T / 4 CCX') not in values
        for r in all_rows for field in ('last_cpu_faster_before_first_gpu_win','first_gpu_faster')
        if r[field])
    if stale:
        print('Hinweis: Gespeicherte Umschlaggrenzen fehlen oder passen nicht zu den aktuellen Messungen. '
              'Alle Intervalle werden aus den aktuellen CSV-Dateien bestimmt; sie koennen grob sein.', flush=True)
        all_rows = []
        for n, ks in sorted(counts.items()):
            wins = [values[n,k,'CPU 4T / 4 CCX'] > values[n,k,'Upload + GPU + Download'] for k in ks]
            first = next((i for i,win in enumerate(wins) if win), None)
            below = [] if first is None else [k for k in ks[:first]
                if values[n,k,'CPU 4T / 4 CCX'] < values[n,k,'Upload + GPU + Download']]
            all_rows.append(dict(points=n,
                last_cpu_faster_before_first_gpu_win=below[-1] if below else '',
                first_gpu_faster=ks[first] if first is not None else '',
                winner_changes=sum(a!=b for a,b in zip(wins,wins[1:]))))
    rows=[r for r in all_rows if r['last_cpu_faster_before_first_gpu_win'] and r['first_gpu_faster']]
    excluded=[int(r['points']) for r in all_rows if r not in rows]
    points=[];records=[]
    for r in rows:
        n=int(r['points']);lo=int(r['last_cpu_faster_before_first_gpu_win']);hi=int(r['first_gpu_faster'])
        x=(lo+hi)/2
        y=(values[n,lo,'CPU 4T / 4 CCX']+values[n,hi,'CPU 4T / 4 CCX'])/2
        points.append((x,y));records.append(dict(points=n,x=x,y_us=y,lower=lo,upper=hi,winner_changes=int(r['winner_changes'])))
    if len(points)<4:raise ValueError('Mindestens vier Umschlagintervalle erforderlich')
    results=[fit(points,False),fit(points,True)]
    best=min(results,key=lambda r:r['sse_us2'])
    report=dict(definition='x: midpoint of transform bracket; y: CPU latency linearly interpolated at midpoint',
                objective='unweighted sum of squared residuals in microseconds; not logarithmic residuals',
                caveat='midpoint estimates, not exact intersections; intervals are not confidence intervals',
                models=results,best_by_sse=best,points=records,excluded_unbracketed_sizes=excluded,
                intervals_rebuilt_from_current_measurements=stale)
    (directory/'crossover_exponential_fit.json').write_text(json.dumps(report,indent=2)+'\n')
    with (directory/'crossover_fit_points.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(records[0]),delimiter=';');writer.writeheader();writer.writerows(records)
    commands=['reset',"set terminal pngcairo size 1800,1100 enhanced font 'Segoe UI,12'",
              f'set output {quote(pictures / "crossover_exponential_fit.png")}',
              "set title 'Exponentialfit an die verfeinerten Umschlagintervalle'",
              "set xlabel 'Transformationen pro Punkt'", "set ylabel 'CPU-Laufzeit am Intervallmittelpunkt [us]'",
              "unset grid", "set key top right", f'set xrange [{min(x for x,y in points)-1}:{max(x for x,y in points)+3}]', 'set samples 1000', 'set yrange [0:*]',
              '$points << EOD',*[f'{r["x"]} {r["y_us"]} {r["lower"]} {r["upper"]}' for r in records],'EOD',
              f'f(x)={best["a_us"]}*exp(-{best["b_per_transform"]}*(x-{best["x0"]}))+{best["c_us"]}',
              "set label 1 'y = a * exp(-b * (x-x0)) + c' at graph 0.40,0.80 left font ',16'",
              f"set label 2 'y = {best['a_us']:.3f} * exp(-{best['b_per_transform']:.6f} * (x-{best['x0']:g})) + {best['c_us']:.3f}' at graph 0.40,0.74 left font ',14'",
              f"set label 3 'a = {best['a_us']:.3f} us' at graph 0.40,0.67 left",
              f"set label 4 'b = {best['b_per_transform']:.6f} pro Transformation' at graph 0.40,0.62 left",
              f"set label 5 'c = {best['c_us']:.3f} us' at graph 0.40,0.57 left",
              f"set label 6 'x0 = {best['x0']:g} Transformationen (fest)' at graph 0.40,0.52 left",
              "set label 7 'x: Transformationen pro Punkt; y: Laufzeit in us' at graph 0.40,0.45 left",
              f"set label 8 'R^2 = {best['r_squared']:.6f}; RMSE = {best['rmse_us']:.3f} us' at graph 0.40,0.39 left",
              "plot $points using 1:2:3:4 with xerrorbars pt 7 ps 1.3 lc rgb '#333333' title 'Intervallmitten und gemessene Grenzen', f(x) with lines lw 3.5 lc rgb '#FF0000' title 'Exponentialfit (kleinste Fehlerquadratsumme)'",
              'unset output']
    commands += point_count_plot(records, directory, pictures)
    commands += point_count_plot(records, directory, pictures, minimum_points=200000)
    commands += hyperbola_plot(records, directory, pictures)
    commands += transition_plot(records, directory, pictures)
    commands += hyperbola_panels(records, directory, pictures)
    commands = cache_markers(commands)
    script=directory/'crossover_exponential_fit.gp';script.write_text('\n'.join(commands)+'\n')
    subprocess.run(['gnuplot',str(script)],check=True)
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()


