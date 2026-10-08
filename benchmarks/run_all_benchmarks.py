"""Run the CPU/GPU benchmark matrix using existing executables."""
import argparse
from contextlib import nullcontext
import csv
import os
import math
import re
from pathlib import Path
import shutil
import subprocess
import time

# All commands and output paths are relative to the project directory.
ROOT = Path('.')
CPU_EXE = 'build/vs2026/Release/lidar_compute_cpu.exe'
GPU_EXE = 'build/gpu/lidar_compute_gpu.exe'
SIZES = [2**i for i in range(26)]
TRANSFORMS = [2**i for i in range(11)]
ROUNDS = 300
CPU_METHODS = ['aos', 'soa', 'auto', 'fma', 'avx2x2', 'avx2x4', 'avx2x8']
CPU_THREAD_MODES = ['single', 'smt', 'same-ccx', 'different-ccx', 'four-ccx']
RESULT_FIELDS = ['device', 'thread_mode', 'points', 'transformations', 'method', 'sample', 'us',
                 'median_us', 'peak_us', 'samples', 'us_per_transform', 'invalid_samples']


def matrix_commands(sizes, counts, rounds, directory, max_seconds=600):
    """Yield CPU and GPU commands for each combination of matrix parameters.

    Args:
        sizes: Reusable iterable of positive integer point counts; the configured
            matrix uses powers of two from 1 through 2**25. May be empty.
        counts: Reusable iterable of positive integer transformation counts;
            the configured matrix uses powers of two from 1 through 2**10.
            May be empty; numeric limits are enforced by the executables.
        rounds: Positive maximum sample count per method; configured as 300.
        directory: pathlib.Path to any valid output directory containing the
            shared results CSV, normally relative to the project root.
        max_seconds: Integer estimated time budget per measurement block in
            seconds, in 1..100000000; defaults to 600. Not a hard timeout.
    """
    for points in sizes:
        for transforms in counts:
            common = ['--points', str(points), '--transforms', str(transforms),
                      '--rounds', str(rounds), '--max-seconds', str(max_seconds),
                      '--output-file', str(directory / 'benchmark_matrix.csv')]
            for mode in CPU_THREAD_MODES:
                # The CPU executable measures all seven methods in shuffled order.
                yield [CPU_EXE, *common, '--method', 'all', '--thread-mode', mode]
            yield [GPU_EXE, *common]



def format_output(line):
    """Expand CPU status lines into a readable method, mode, and core listing.

    Args:
        line: Any string, including an empty string, without a trailing newline;
            matching CPU status lines are expanded, other text is unchanged.
    """
    match = re.fullmatch(r'CPU: ([^,]+), ([^;]+); Punkte: [0-9]+; CPUs:(.*)', line)
    if match:
        method, mode, cores = match.groups()
        return f"CPU:\n    -{method}\n    -{mode}\nCPUs:\n    {','.join(cores.split())}\n"
    return line


def resume_position(source, commands):
    """Read sequential CSV blocks; return completed calls and safe byte boundary.

    Args:
        source: Readable binary stream with readline(), read(), and tell(),
            positioned at byte zero; accepts an empty file or a UTF-8 CSV
            with RESULT_FIELDS as its semicolon-separated header.
        commands: Sized, ordered sequence of argument lists from matrix_commands,
            possibly empty; each list describes one expected CSV block.
    """
    header = source.readline()
    if not header:
        return 0, 0
    if header.decode('utf-8').rstrip('\r\n').split(';') != RESULT_FIELDS:
        raise ValueError('CSV-Spalten passen nicht zur Matrix; Datei unveraendert.')
    boundary = source.tell()
    for index, command in enumerate(commands):
        def arg(name):
            """Return an option's value from the current benchmark command.

            Args:
                name: Option string present in the current command with a
                    following value. Used options: '--thread-mode' selects CPU
                    placement, '--rounds' gives the sample count, '--points'
                    gives the point count, and '--transforms' gives the
                    transformation count.
            """
            return command[command.index(name) + 1]
        cpu = command[0] == CPU_EXE
        device = 'cpu' if cpu else 'gpu'
        mode = arg('--thread-mode') if cpu else ''
        methods = ({'CPU 4T / 4 CCX' if m == 'avx2x2' and mode == 'four-ccx'
                    else f'{m} / {mode}' for m in CPU_METHODS} if cpu else
                   {'GPU kernel only', 'GPU resident (host sync)', 'Upload + GPU', 'Upload + GPU + Download'})
        max_rounds = int(arg('--rounds'))
        rounds = None
        seen = set()
        while rounds is None or len(seen) < len(methods) * rounds:
            line = source.readline()
            # Only a missing or unterminated trailing row permits resuming here.
            if not line or not line.endswith(b'\n'):
                return index, boundary
            try:
                fields = next(csv.reader([line.decode('utf-8')], delimiter=';'))
                if len(fields) != len(RESULT_FIELDS):
                    raise ValueError('Spaltenzahl')
                row = dict(zip(RESULT_FIELDS, fields))
                if rounds is None:
                    rounds = int(row['samples'])
                    if not 1 <= rounds <= max_rounds:
                        raise ValueError('Messrunden')
                key = (row['method'], int(row['sample']))
                # Reject mismatched parameters, duplicate samples, and invalid values.
                if (row['device'], row['thread_mode'], row['points'], row['transformations']) != (
                        device, mode, arg('--points'), arg('--transforms')):
                    raise ValueError('Matrixparameter')
                if key[0] not in methods or not 0 <= key[1] < rounds or key in seen or int(row['samples']) != rounds:
                    raise ValueError('Messrunden')
                if any(not math.isfinite(float(row[k])) or float(row[k]) < 0
                       for k in ('us', 'median_us', 'peak_us', 'us_per_transform')) or int(row['invalid_samples']) < 0:
                    raise ValueError('Messwerte')
                seen.add(key)
            except (ValueError, UnicodeError, csv.Error) as exc:
                raise ValueError(f'Ungueltige CSV bei Aufruf {index + 1}: {exc}. Datei unveraendert.') from exc
        # Advance the safe truncation boundary only after a complete call.
        boundary = source.tell()
    if source.read(1):
        raise ValueError('CSV enthaelt weitere Daten ausserhalb der Matrix; Datei unveraendert.')
    return len(commands), boundary


def run_logged(command, log, environment):
    """Run a command, stream its output, and raise on a nonzero exit status.

    Args:
        command: Nonempty sequence of strings; first is an executable path or
            a name found through PATH, followed by any arguments it accepts.
        log: Writable text stream supporting write() and flush() to retain raw
            output, or None to disable logging.
        environment: Mapping of valid environment variable names to string
            values; replaces the child environment. PATH is used to resolve
            executable names without a directory component.
    """
    # Windows does not use the child's PATH to locate the initial executable.
    # Resolve external tools explicitly; project executable paths stay relative.
    executable = command[0]
    if not os.path.dirname(executable):
        executable = shutil.which(executable, path=environment.get('PATH', ''))
        if not executable:
            raise FileNotFoundError(f'Programm nicht im konfigurierten PATH gefunden: {command[0]}')
    # Start the executable here and merge stderr into the live stdout stream.
    with subprocess.Popen(command, executable=executable, cwd=ROOT, env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace', bufsize=1) as process:
        for line in process.stdout:
            if log is not None:
                log.write(line)
                log.flush()
            if line.strip():
                print(format_output(line.rstrip()), flush=True)
        code = process.wait()
        if code:
            raise subprocess.CalledProcessError(code, command)


class Runner:
    """Track sequential benchmark progress and manage optional per-call logs."""

    def __init__(self, directory, dry_run, pipeline_logs=False, total=0):
        """Initialize progress tracking and the child process environment.

        Args:
            directory: pathlib.Path to any valid log directory, normally
                relative to the project root; created when logging is enabled.
            dry_run: Boolean; True only prints commands, False executes them.
            pipeline_logs: Boolean; True writes per-command logs, False
                (default) disables them. Ignored when dry_run is True.
            total: Nonnegative integer giving the full matrix's call count;
                0 (default) is displayed when no total was supplied.
        """
        self.directory, self.dry_run, self.index = directory, dry_run, 0
        self.total = total
        self.pipeline_logs = pipeline_logs
        self.environment = dict(os.environ, LIDAR_BATCH='1', PYTHONUNBUFFERED='1')

    def run(self, command):
        """Display and execute one benchmark call, then report elapsed time.

        Args:
            command: Nonempty sequence of strings containing an executable path
                followed by option/value pairs, normally from matrix_commands;
                accepted options and values depend on that executable.
        """
        self.index += 1
        print('-'*len(f'[{self.index}/{self.total}] {command[0]}'))
        print('-'*len(f'[{self.index}/{self.total}] {command[0]}'))
        print(f'[{self.index}/{self.total}] {command[0]}', flush=True)
        for i in range(1, len(command), 2):
            print('    ' + subprocess.list2cmdline(command[i:i+2]), flush=True)
        print(flush=True)
        if self.dry_run: return
        started = time.monotonic()
        log_path = self.directory / f'pipeline_{self.index:05d}.log'
        try:
            if self.pipeline_logs:
                self.directory.mkdir(parents=True, exist_ok=True)
            with (log_path.open('w', encoding='utf-8') if self.pipeline_logs else nullcontext()) as log:
                # This call starts the executable and waits for it to finish.
                run_logged(command, log, self.environment)
        except (OSError, subprocess.CalledProcessError) as exc:
            detail = f'\nProtokoll: {log_path}' if self.pipeline_logs and log_path.exists() else ''
            raise RuntimeError(f'Schritt {self.index} fehlgeschlagen: {exc}{detail}') from exc
        print(f'Abgeschlossen nach {time.monotonic()-started:.1f} Sekunden.', flush=True)
        print('\n')


def main(argv=None):
    """Parse options and run or resume the benchmark matrix in CSV order.

    Args:
        argv: Sequence of argument strings without the program name; [] uses
            defaults and None reads the process arguments. Supported options:
            '--dry-run' prints pending commands without execution or writes;
            '--pipeline-logs' saves raw output per call (disabled by default);
            '--restart' starts over instead of resuming existing results;
            '--max-seconds N' sets the estimated time budget per measurement
            block in whole seconds, in 1..100000000 (default: 600);
            '--output-dir PATH' selects any valid output directory (default:
            'results/data'); '-h' or '--help' prints help and exits.
    """
    # Resolve relative executable and output paths from the project directory.
    os.chdir(Path(__file__).resolve().parent.parent)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true', help='Show commands without writes or execution')
    parser.add_argument('--pipeline-logs', action='store_true', help='Write per-step pipeline log files (disabled by default)')
    parser.add_argument('--restart', action='store_true', help='Discard existing results and start from the beginning')
    parser.add_argument('--output-dir', type=Path, default=Path('results/data'))
    parser.add_argument('--max-seconds', type=int, default=300,
                        help='Estimated seconds per measurement block (default: 600); not a hard timeout')
    args = parser.parse_args(argv)
    if not 1 <= args.max_seconds <= 100000000:
        parser.error('--max-seconds must be in 1..100000000')
    directory = Path(os.path.relpath(args.output_dir))
    total = len(SIZES) * len(TRANSFORMS) * (len(CPU_THREAD_MODES) + 1)
    print(f'Matrix: {len(SIZES)} Punktgroessen x {len(TRANSFORMS)} Transformationszahlen '
          f'x ({len(CPU_THREAD_MODES)} CPU-Thread-Modi mit allen {len(CPU_METHODS)} Methoden + GPU) '
          f'= {total} Aufrufe, jeweils maximal {ROUNDS} Messrunden pro Methode '
          f'({args.max_seconds} s Hochrechnung nach der ersten Runde).', flush=True)
    commands = list(matrix_commands(SIZES, TRANSFORMS, ROUNDS, directory, args.max_seconds))
    runner = Runner(directory, args.dry_run, args.pipeline_logs, total)
    result_path = directory / 'benchmark_matrix.csv'
    try:
        completed, boundary, original_size = 0, 0, 0
        if not args.restart and result_path.exists():
            # Validate existing results before making any changes to the CSV.
            with result_path.open('rb') as source:
                original_size = os.fstat(source.fileno()).st_size
                completed, boundary = resume_position(source, commands)
            print(f'Resume: {completed}/{total} Aufrufe vollstaendig gespeichert.', flush=True)
            if boundary < original_size:
                print(f'Unvollstaendiger Aufruf {completed + 1} wird erneut gemessen; '
                      'nur dessen CSV-Rest wird beim Start entfernt.', flush=True)
        runner.index = completed
        if args.dry_run:
            for command in commands[completed:]:
                runner.run(command)
            return
        if completed == total:
            print('Alle Benchmarks bereits vollstaendig gespeichert.', flush=True)
            return
        for executable in (CPU_EXE, GPU_EXE):
            if not Path(executable).is_file():
                raise FileNotFoundError(f'{executable} fehlt. Zuerst den CMake-Build ausfuehren.')
        directory.mkdir(parents=True, exist_ok=True)
        if args.restart or boundary == 0:
            with result_path.open('w', newline='', encoding='utf-8') as result:
                csv.DictWriter(result, fieldnames=RESULT_FIELDS, delimiter=';').writeheader()
        elif boundary < original_size:
            # Keep completed calls and discard only the incomplete trailing call.
            with result_path.open('r+b') as result:
                result.truncate(boundary)
        for command in commands[completed:]:
            runner.run(command)
    except (OSError, ValueError, KeyError, csv.Error, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == '__main__':
    main()
