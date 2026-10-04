import os
"""Measure additional integer transform counts inside each observed crossover bracket."""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
from run_compute_benchmark import ROOT, load, plot, discover_sizes

CPU = 'CPU 4T / 4 CCX'
GPU = 'Upload + GPU + Download'

def make_plan(values, counts_by_size, steps):
    plan = []
    for size, counts in sorted(counts_by_size.items()):
        first = next((k for k in counts if values[size,k,CPU] > values[size,k,GPU]), None)
        below = [k for k in counts if first is not None and k < first and values[size,k,CPU] < values[size,k,GPU]]
        if not below:
            print(f'{size}: kein beidseitig gemessenes Umschlagintervall; uebersprungen.')
            continue
        lower, upper = below[-1], first
        # Interior values, plus fresh measurements of both old endpoints.
        interior = sorted({round(lower + (upper-lower)*i/(steps+1)) for i in range(1,steps+1)} - {lower,upper})
        transforms = [lower, *interior, upper]
        plan.append({'points': size, 'old_lower': lower, 'old_upper': upper, 'transforms': transforms})
    return plan

def summarize(directory, plan):
    values, counts_by_size = load(directory, [item['points'] for item in plan])
    rows = []
    for item in plan:
        size = item['points']; counts = counts_by_size[size]
        if counts != item['transforms']:
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
        print(f'{size}: {item["old_lower"]}..{item["old_upper"]} -> {lower}..{upper} ({status}; Wechsel: {reversals})')
    with (directory/'refined_crossover.csv').open('w',newline='',encoding='utf-8') as out:
        writer=csv.DictWriter(out,fieldnames=list(rows[0]),delimiter=';')
        writer.writeheader();writer.writerows(rows)

def main():
    os.chdir(Path(__file__).resolve().parent)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=ROOT/'results'/'data')
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--steps',type=int,default=50)
    parser.add_argument('--rounds',type=int,default=30)
    parser.add_argument('--plan-only',action='store_true')
    parser.add_argument('--plot-only',action='store_true')
    args=parser.parse_args()
    if args.steps<1 or args.rounds<1: parser.error('Schritte und Messrunden muessen positiv sein')
    source=Path(os.path.relpath(args.source_dir))
    directory=Path(os.path.relpath(args.output_dir or ROOT/'results'/'data'))
    if args.plot_only:
        plan=json.loads((directory/'refinement_plan.json').read_text())['cases']
    else:
        sizes=discover_sizes(source)
        if not sizes: raise ValueError('Keine gepaarten CPU-/GPU-Ergebnisse gefunden')
        values,counts=load(source,sizes)
        plan=make_plan(values,counts,args.steps)
        if not plan: raise ValueError('Keine Umschlagintervalle gefunden')
        for item in plan: print(f'{item["points"]}: {item["transforms"]}',flush=True)
        if args.plan_only: return
        executables={'cpu':ROOT/'build'/'vs2026'/'Release'/'lidar_compute_cpu.exe',
                     'gpu':ROOT/'build'/'gpu'/'lidar_compute_gpu.exe'}
        for exe in executables.values():
            if not exe.exists(): raise ValueError(f'Zuerst bauen: {exe}')
        directory.mkdir(parents=True,exist_ok=True)
        (directory/'refinement_plan.json').write_text(json.dumps({
            'created_utc':datetime.now(timezone.utc).isoformat(),'source':str(source),
            'interior_steps':args.steps,'rounds':args.rounds,'cases':plan},indent=2)+'\n')
        for index,item in enumerate(plan):
            # Alternate CPU/GPU order between cloud sizes; never run concurrently.
            order=('cpu','gpu') if index%2==0 else ('gpu','cpu')
            for device in order:
                subprocess.run([str(executables[device]),'--points',str(item['points']),
                                '--transforms',','.join(map(str,item['transforms'])),
                                '--rounds',str(args.rounds),'--output-dir',str(directory)],
                               cwd=ROOT,check=True)
    summarize(directory,plan)
    plot(directory,[item['points'] for item in plan])
    print(f'Verfeinerte Ergebnisse: {directory}')

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as exc: raise SystemExit(str(exc))
