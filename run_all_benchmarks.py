"""Rebuild and reproduce all LiDAR benchmarks without existing result files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from run_compute_benchmark import ROOT, SIZES


def run_logged(command, log, environment):
    """Show child progress live while retaining the complete step log."""
    # Windows does not use the child's PATH to locate the initial executable.
    # Resolve external tools explicitly; project executable paths stay relative.
    executable = command[0]
    if not os.path.dirname(executable):
        executable = shutil.which(executable, path=environment.get('PATH', ''))
        if not executable:
            raise FileNotFoundError(f'Programm nicht im konfigurierten PATH gefunden: {command[0]}')
    with subprocess.Popen(command, executable=executable, cwd=ROOT, env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace', bufsize=1) as process:
        for line in process.stdout:
            log.write(line)
            log.flush()
            if line.strip():
                print(line.rstrip(), flush=True)
        code = process.wait()
        if code:
            raise subprocess.CalledProcessError(code, command)


def main():
    os.chdir(Path(__file__).resolve().parent)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true', help='Show commands without building or measuring')
    args = parser.parse_args()
    cmake = shutil.which('cmake') or str(Path(os.environ.get('ProgramFiles', 'C:/Program Files')) /
        'Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    # Locate installed tools internally, but invoke them by name through PATH.
    tool_path = os.pathsep.join([str(Path(cmake).parent), str(Path(sys.executable).parent),
                                os.environ.get('PATH', '')])
    cmake = Path(cmake).name
    py = Path(sys.executable).name
    commands = [
        [cmake, '-S', '.', '-B', 'build/vs2026', '-G', 'Visual Studio 18 2026', '-A', 'x64'],
        [cmake, '--build', 'build/vs2026', '--config', 'Release'],
        ['cmd', '/c', 'build_compute_gpu.cmd'],
        ['cmd', '/c', 'build_gpu_benchmark.cmd'],
        *[['build/vs2026/Release/lidar_uop_benchmark.exe', '600', '50', '4', str(points)]
          for points in SIZES],
        ['build/vs2026/Release/lidar_thread_scaling.exe'],
        ['build/gpu/lidar_gpu_benchmark.exe', '200', '256'],
        [py, 'plot_cpu_gpu.py'],
        [py, 'run_compute_benchmark.py', '--rounds', '30'],
        [py, 'refine_compute_crossover.py', '--steps', '50', '--rounds', '30'],
        [py, 'fit_compute_crossover.py'],
    ]
    if args.dry_run:
        for command in commands: print(subprocess.list2cmdline(command))
        return
    if not shutil.which('gnuplot'):
        raise SystemExit('Gnuplot fehlt im PATH.')
    data = ROOT / 'results' / 'data'
    data.mkdir(parents=True, exist_ok=True)
    (ROOT / 'results' / 'pics').mkdir(parents=True, exist_ok=True)
    status = data / 'pipeline_status.json'
    environment = dict(os.environ, LIDAR_BATCH='1', PYTHONUNBUFFERED='1', PATH=tool_path)
    for index, command in enumerate(commands, 1):
        started = time.monotonic()
        print(f'\n[{index}/{len(commands)}] {subprocess.list2cmdline(command)}', flush=True)
        status.write_text(json.dumps(dict(step=index, command=command, status='running'), indent=2))
        try:
            with (data / f'pipeline_{index:02d}.log').open('w', encoding='utf-8') as log:
                run_logged(command, log, environment)
        except (subprocess.CalledProcessError, OSError) as exc:
            status.write_text(json.dumps(dict(step=index, command=command, status='failed'), indent=2))
            raise SystemExit(f'Schritt {index} fehlgeschlagen: {exc}\nProtokoll: {data / f"pipeline_{index:02d}.log"}')
        elapsed = time.monotonic() - started
        print(f'[{index}/{len(commands)}] Abgeschlossen nach {elapsed:.1f} Sekunden.', flush=True)
        # Preserve the full sweep in the same folder before refinement overwrites working files.
        if command[1:2] == ['run_compute_benchmark.py']:
            for path in data.glob('compute_*.csv'):
                shutil.copy2(path, data / ('coarse_' + path.name))
    status.write_text(json.dumps(dict(status='complete', steps=len(commands)), indent=2))
    print('Fertig: results/data und results/pics', flush=True)


if __name__ == '__main__':
    main()
