"""Resume checks with synthetic CSV blocks; no benchmark results are accessed."""
import csv
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'benchmarks'))
import run_all_benchmarks as runner


class ResumeTests(unittest.TestCase):
    def setUp(self):
        self.commands = list(runner.matrix_commands([1], [1], 300, Path('results/data')))

    def block(self, command, rounds):
        cpu = command[0] == runner.CPU_EXE
        mode = command[command.index('--thread-mode') + 1] if cpu else ''
        methods = [('CPU 4T / 4 CCX' if m == 'avx2x2' and mode == 'four-ccx'
                    else f'{m} / {mode}') for m in runner.CPU_METHODS] if cpu else [
            'GPU kernel only', 'GPU resident (host sync)', 'Upload + GPU',
            'Upload + GPU + Download']
        return [dict(device='cpu' if cpu else 'gpu', thread_mode=mode, points=1,
                     transformations=1, method=method, sample=sample, us=1,
                     median_us=1, peak_us=1, samples=rounds, us_per_transform=1,
                     invalid_samples=0)
                for method in methods for sample in range(rounds)]

    def encode(self, rows):
        output = io.StringIO(newline='')
        writer = csv.DictWriter(output, fieldnames=runner.RESULT_FIELDS, delimiter=';')
        writer.writeheader()
        writer.writerows(rows)
        return output.getvalue().encode()

    def test_variable_and_legacy_round_counts(self):
        rows = []
        for command, rounds in zip(self.commands, [1, 2, 300, 4, 1, 3]):
            rows.extend(self.block(command, rounds))
        encoded = self.encode(rows)
        self.assertEqual(runner.resume_position(io.BytesIO(encoded), self.commands),
                         (len(self.commands), len(encoded)))

    def test_incomplete_block_keeps_previous_boundary(self):
        first = self.block(self.commands[0], 2)
        following = self.block(self.commands[1], 3)
        for tail in (self.encode(first + following[:-1]),
                     self.encode(first + following)[:-2]):
            with self.subTest(tail_length=len(tail)):
                self.assertEqual(runner.resume_position(io.BytesIO(tail), self.commands),
                                 (1, len(self.encode(first))))

    def test_invalid_counts_duplicates_and_missing_method_rejected(self):
        for change in ('zero', 'too_many', 'inconsistent', 'duplicate', 'method'):
            rows = self.block(self.commands[0], 2)
            if change == 'zero': rows[0]['samples'] = 0
            if change == 'too_many': rows[0]['samples'] = 301
            if change == 'inconsistent': rows[-1]['samples'] = 1
            if change == 'duplicate': rows[1] = dict(rows[0])
            if change == 'method': rows[-1]['method'] = 'unknown'
            with self.subTest(change=change), self.assertRaises(ValueError):
                runner.resume_position(io.BytesIO(self.encode(rows)), self.commands)


if __name__ == '__main__':
    unittest.main()
