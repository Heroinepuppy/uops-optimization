"""Resume checks with synthetic CSV blocks; no benchmark results are accessed."""
import csv
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import json

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

    def test_resume_with_new_and_mixed_method_names(self):
        for mixed in (False, True):
            rows = [row for command in self.commands for row in self.block(command, 2)]
            for row in rows:
                if row['method'] == 'CPU 4T / 4 CCX' and (not mixed or row['sample'] == 1):
                    row['method'] = 'avx2x2 / four-ccx'
            encoded = self.encode(rows)
            with self.subTest(mixed=mixed):
                self.assertEqual(runner.resume_position(io.BytesIO(encoded), self.commands),
                                 (len(self.commands), len(encoded)))

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


class NativeMatrixTests(unittest.TestCase):
    def setUp(self):
        self.profile = dict(cpu_exe='build/bin/ARMv8/lidar_compute_cpu', gpu_exe=None,
                            methods=['aos', 'soa', 'auto', 'neon', 'neonx2', 'neonx4', 'neonx8'],
                            thread_modes=['single', 'two-core', 'four-core'], cpu=0)
        self.commands = list(runner.matrix_commands([1], [1], 300, Path('build/test-results'),
                                                   profile=self.profile))

    def test_arm_matrix_uses_only_supported_devices_and_modes(self):
        self.assertEqual(len(self.commands), 3)
        self.assertEqual([c[c.index('--thread-mode') + 1] for c in self.commands],
                         ['single', 'two-core', 'four-core'])
        command = self.commands[0]
        self.assertEqual(command[0], self.profile['cpu_exe'])
        self.assertEqual(command[command.index('--thread-mode') + 1], 'single')
        self.assertEqual(command[command.index('--cpu') + 1], '0')

    def test_arm_resume_and_foreign_methods(self):
        rows = [dict(device='cpu', thread_mode=mode, points=1, transformations=1,
                     method=f'{method} / {mode}', sample=0, us=1, median_us=1,
                     peak_us=1, samples=1, us_per_transform=1, invalid_samples=0)
                for mode in self.profile['thread_modes'] for method in self.profile['methods']]
        encoded = self.encode(rows)
        self.assertEqual(runner.resume_position(io.BytesIO(encoded), self.commands, self.profile),
                         (3, len(encoded)))
        self.assertEqual(runner.resume_position(io.BytesIO(self.encode(rows[:-1])),
                                               self.commands, self.profile)[0], 2)
        rows[-1]['method'] = 'avx2x8 / single'
        with self.assertRaises(ValueError):
            runner.resume_position(io.BytesIO(self.encode(rows)), self.commands, self.profile)

    def test_arm_discovery_queries_native_build(self):
        info = SimpleNamespace(architecture='aarch64')
        capabilities = json.dumps({key: self.profile[key] for key in ('methods', 'thread_modes', 'cpu')})
        with patch.object(Path, 'is_file', lambda path: str(path) == self.profile['cpu_exe']), \
                patch.object(runner.subprocess, 'run', return_value=SimpleNamespace(stdout=capabilities)) as query:
            selected = runner.select_profile(info)
        self.assertEqual(selected, self.profile)
        self.assertEqual(query.call_args.args[0], [self.profile['cpu_exe'], '--capabilities'])

    def test_vulkan_discovery_and_gpu_resume(self):
        vulkan = 'build/bin/ARMv8/lidar_compute_vulkan'
        gpu_methods = ['GPU resident (host sync)', 'Upload + GPU', 'Upload + GPU + Download']
        cpu_json = json.dumps({key: self.profile[key] for key in ('methods', 'thread_modes', 'cpu')})
        responses = [SimpleNamespace(stdout=cpu_json),
                     SimpleNamespace(returncode=0, stdout=json.dumps({'methods': gpu_methods}))]
        with patch.object(Path, 'is_file', lambda path: str(path) in (self.profile['cpu_exe'], vulkan)), \
                patch.object(runner.subprocess, 'run', side_effect=responses):
            profile = runner.select_profile(SimpleNamespace(architecture='aarch64'))
        self.assertEqual(profile['gpu_exe'], vulkan)
        commands = list(runner.matrix_commands([1], [1], 2, Path('build/test-results'), profile=profile))
        self.assertEqual(len(commands), 4)
        rows = [dict(device='cpu', thread_mode=mode, points=1, transformations=1,
                     method=f'{method} / {mode}', sample=0, us=1, median_us=1,
                     peak_us=1, samples=1, us_per_transform=1, invalid_samples=0)
                for mode in profile['thread_modes'] for method in profile['methods']]
        rows.extend(dict(device='gpu', thread_mode='', points=1, transformations=1,
                         method=method, sample=0, us=1, median_us=1, peak_us=1,
                         samples=1, us_per_transform=1, invalid_samples=0) for method in gpu_methods)
        encoded = self.encode(rows)
        self.assertEqual(runner.resume_position(io.BytesIO(encoded), commands, profile), (4, len(encoded)))
        self.assertEqual(runner.resume_position(io.BytesIO(self.encode(rows[:-1])), commands, profile)[0], 3)

    def test_software_renderer_is_not_included(self):
        cpu_json = json.dumps({key: self.profile[key] for key in ('methods', 'thread_modes', 'cpu')})
        with patch.object(Path, 'is_file', return_value=True), \
                patch.object(runner.subprocess, 'run', side_effect=[SimpleNamespace(stdout=cpu_json),
                    SimpleNamespace(returncode=1, stderr='No hardware Vulkan compute GPU')]):
            profile = runner.select_profile(SimpleNamespace(architecture='aarch64'))
        self.assertIsNone(profile['gpu_exe'])

    encode = ResumeTests.encode


if __name__ == '__main__':
    unittest.main()
