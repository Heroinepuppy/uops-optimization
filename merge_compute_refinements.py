import os
"""Combine disjoint refined cloud sizes without changing the original measurements."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys
from refine_compute_crossover import summarize
from run_compute_benchmark import ROOT, load, plot, result_file


def main():
    os.chdir(Path(__file__).resolve().parent)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources',nargs='+',type=Path)
    parser.add_argument('--output-dir',type=Path,default=ROOT/'results'/'data')
    args=parser.parse_args();target=Path(os.path.relpath(args.output_dir))
    cases={};origins={}
    for source in args.sources:
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
    summarize(target,plan);plot(target,sorted(cases))
    subprocess.run([sys.executable,str(ROOT/'fit_compute_crossover.py'),str(target)],check=True)

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.CalledProcessError) as exc:raise SystemExit(str(exc))
