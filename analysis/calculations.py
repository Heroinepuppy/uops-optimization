"""Pure numerical calculations for benchmark analysis; no file or plot I/O."""

import math
import random

CPU = "CPU 4T / 4 CCX"
GPU = "Upload + GPU + Download"


def histogram(values):
    low = min(values)
    width = max(1e-9, (max(values) - low) / 60)
    bins = [0] * 60
    for value in values:
        bins[min(59, int((value - low) / width))] += 1
    return width, [(low + (i + 0.5) * width, count) for i, count in enumerate(bins)]

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
                c_us=c,x0=x0,sse_us2=sse,rmse_us=math.sqrt(sse/len(ys)),r_squared=1-sse/sst if sst else None,
                parameters=3 if offset else 2,b_search_min=1e-6,b_search_max=b_max)

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

def fit_steps(points, terms):
    if len(points) <= 2+3*terms:
        return dict(m=terms, status='insufficient_data', parameters=2+3*terms)
    ns, ys = zip(*points)
    lo, hi = math.log(min(ns)), math.log(max(ns))
    bounds = [(lo, hi), (math.log(.03), math.log(3))]*terms

    def solve(q):
        columns = [[1.0]*len(ns), [min(ns)/n for n in ns]]
        for t,v in zip(q[::2],q[1::2]):
            columns.append([1/(1+math.exp(max(-700,min(700,-(math.log(n)-t)/math.exp(v))))) for n in ns])
        # Reorthogonalized QR avoids normal-equation conditioning problems.
        orthogonal=[]; r=[[0.0]*len(columns) for _ in columns]
        for j,column in enumerate(columns):
            z=column[:]
            for _ in range(2):
                for i,u in enumerate(orthogonal):
                    coefficient=sum(a*b for a,b in zip(u,z)); r[i][j]+=coefficient
                    z=[a-coefficient*b for a,b in zip(z,u)]
            norm=math.sqrt(sum(a*a for a in z))
            if norm<1e-9: return float('inf'), []
            r[j][j]=norm;orthogonal.append([a/norm for a in z])
        beta=[sum(a*b for a,b in zip(u,ys)) for u in orthogonal]
        for j in reversed(range(len(beta))):
            beta[j]=(beta[j]-sum(r[j][k]*beta[k] for k in range(j+1,len(beta))))/r[j][j]
        loss=sum((y-sum(b*col[i] for b,col in zip(beta,columns)))**2 for i,y in enumerate(ys))
        return loss,beta

    rng=random.Random(741+terms)
    starts=[[lo+(hi-lo)*(j+1)/(terms+1) if i%2==0 else math.log(.3)
             for j in range(terms) for i in (0,1)]]
    starts += [[rng.uniform(a,b) for a,b in bounds] for _ in range(19)]
    best=(float('inf'), [], [])
    for q in starts:
        loss,beta=solve(q)
        steps=[(b-a)/8 for a,b in bounds]
        for _ in range(300):
            improved=False
            for i,(a,b) in enumerate(bounds):
                for direction in (-1,1):
                    trial=q[:];trial[i]=max(a,min(b,q[i]+direction*steps[i]))
                    cost,coeff=solve(trial)
                    if cost<loss:
                        loss,beta,q=cost,coeff,trial;improved=True
            if not improved:steps=[s*.5 for s in steps]
            if max(steps,default=0)<1e-6:break
        if loss<best[0]:best=loss,q,beta
    loss,q,beta=best
    if not math.isfinite(loss):return dict(m=terms,status='singular')
    transitions=sorted([dict(N=math.exp(t),w=math.exp(v),d=d)
                        for t,v,d in zip(q[::2],q[1::2],beta[2:])],key=lambda s:s['N'])
    mean=sum(ys)/len(ys);sst=sum((y-mean)**2 for y in ys)
    return dict(m=terms,status='ok',parameters=2+3*terms,a=beta[1]*min(ns),c=beta[0],
                transitions=transitions,asymptote=beta[0]+sum(beta[2:]),sse=loss,
                rmse=math.sqrt(loss/len(ns)),r_squared=1-loss/sst if sst else None,
                at_search_boundary=any(min(abs(x-a),abs(x-b))<1e-5 for x,(a,b) in zip(q,bounds)),
                bounds=dict(N=[min(ns),max(ns)],w=[.03,3]),
                method='profile QR least squares; 20 deterministic starts, bounded coordinate search')

def refinement_summary(values, counts_by_size, plan):
    rows = []
    for item in plan:
        size = item['points']; counts = counts_by_size[size]
        if not set(item['transforms']).issubset(counts):
            raise ValueError(f'{size}: Messwerte passen nicht zum gespeicherten Plan')
        wins = [values[size,k,GPU] < values[size,k,CPU] for k in counts]
        first = next((i for i,w in enumerate(wins) if w), None)
        below = [] if first is None else [k for k in counts[:first] if values[size,k,CPU] < values[size,k,GPU]]
        lower = below[-1] if below else None
        upper = counts[first] if first is not None else None
        sustained = next((counts[i] for i,w in enumerate(wins) if w and all(wins[i:])), None)
        status = ('eingegrenzt' if lower is not None else
                  'GPU bereits am unteren Rand schneller' if first is not None else
                  'kein GPU-Vorteil im erneut gemessenen Intervall')
        reversals = sum(a != b for a,b in zip(wins,wins[1:]))
        rows.append({'points':size, 'old_lower':item['old_lower'], 'old_upper':item['old_upper'],
                     'last_cpu_faster_before_first_gpu_win':lower, 'first_gpu_faster':upper,
                     'gpu_faster_at_all_later_tested_counts_from':sustained,
                     'winner_changes':reversals, 'status':status})
    return rows

def crossover_summary(values, counts_by_size, sizes):
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
    return crossings


def crossover_fit_report(values, counts, all_rows, has_refinements=False):
    stale = has_refinements or not all_rows or any(
        (int(r['points']), int(r[field]), 'CPU 4T / 4 CCX') not in values
        for r in all_rows for field in ('last_cpu_faster_before_first_gpu_win','first_gpu_faster')
        if r[field])
    if stale:
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
    return report


def fit_reports(records):
    ordered = sorted(records, key=lambda row: row['points'])
    points = [(row['points'] / 1e6, row['x']) for row in ordered]
    reports = {'crossover_hyperbola_fit': hyperbola_fit(points)}
    if len(records) >= 6:
        reports['crossover_hyperbola_transition_fit'] = transition_fit(points)
    reports['crossover_hyperbola_models'] = dict(
        formula='K(N)=c+a/N+sum(d_j/(1+exp(-ln(N/N_j)/w_j)))',
        N_unit='million points', K_unit='transformations per point', points=points,
        objective='unweighted squared residuals at interval midpoints',
        models=[fit_steps(points, terms) for terms in range(6)])
    for minimum in (0, 200000):
        selected = [row for row in ordered if row['points'] >= minimum]
        if len(selected) < 4:
            continue
        size_points = [(row['points'] / 1e6, row['x']) for row in selected]
        best = min([fit(size_points, False), fit(size_points, True)], key=lambda item: item['sse_us2'])
        stem = 'crossover_by_point_count' + ('_from_200k' if minimum else '')
        reports[stem + '_fit'] = dict(
            minimum_points=minimum, model='K(N) = a * exp(-b * (N-N0)) + c',
            N_unit='million points', K_unit='transformations per point',
            a=best['a_us'], b=best['b_per_transform'], c=best['c_us'], N0=best['x0'],
            rmse_transformations=best['rmse_us'], r_squared=best['r_squared'],
            objective='unweighted squared residuals in transformations; interval midpoints',
            points=size_points, fit=best)
    return reports


def grid(low, high, logarithmic=False):
    if logarithmic:
        return [math.exp(math.log(low) + i * math.log(high / low) / 499) for i in range(500)]
    return [low + i * (high - low) / 499 for i in range(500)]


def exponential(model, x):
    return model['a_us'] * math.exp(-model['b_per_transform'] * (x - model['x0'])) + model['c_us']


def stepped(model, x):
    return model['c'] + model['a'] / x + sum(
        step['d'] / (1 + math.exp(max(-700, min(700, -math.log(x / step['N']) / step['w']))))
        for step in model.get('transitions', []))
