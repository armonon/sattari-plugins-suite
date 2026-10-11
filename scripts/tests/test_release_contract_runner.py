import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "runner", Path(__file__).resolve().parents[1] / "run_release_contracts.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ContractRunnerTests(unittest.TestCase):
    def test_build_failure_does_not_run_stale_tests(self):
        with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 1)) as mocked:
            self.assertEqual(runner.run(Path("build"), build_first=True), 1)
            self.assertEqual(mocked.call_count, 1)

    def test_build_uses_only_test_aggregate(self):
        with patch.object(runner.subprocess, "run", side_effect=[
            subprocess.CompletedProcess([], 0),
            subprocess.CompletedProcess([], 0, '{"tests":[{"name":"Pass"}]}'),
            subprocess.CompletedProcess([], 0)
        ]) as mocked:
            self.assertEqual(runner.run(Path("build"), build_first=True, jobs=2), 0)
            self.assertEqual(mocked.call_args_list[0].args[0], [
                "cmake", "--build", "build", "--parallel", "2", "--target", "SattariReleaseContracts"])

    def test_each_test_gets_private_session_and_exact_name(self):
        calls = []
        def fake(command, **kwargs):
            calls.append((command, kwargs))
            return subprocess.CompletedProcess(command, 0, json.dumps({"tests": [
                {"name": "A+Contract"}, {"name": "A+ContractSmoke"}]}))
        with patch.object(runner.subprocess, "run", side_effect=fake):
            self.assertEqual(runner.run(Path("build")), 0)
        self.assertEqual(calls[1][0][-1], "^A\\+Contract$")
        identities = [call[1]["env"]["SATTARI_SESSION_ID"] for call in calls[1:]]
        self.assertEqual(len(set(identities)), 2)
        self.assertTrue(all(len(value) <= 48 and value.startswith("gate-") for value in identities))

    def test_empty_inventory_fails(self):
        with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, '{"tests":[]}')):
            self.assertEqual(runner.run(Path("build")), 1)

    def test_stemdeck_is_explicitly_skipped(self):
        with patch.object(runner.subprocess, "run", side_effect=[
            subprocess.CompletedProcess([], 0, '{"tests":[{"name":"StemDeckSuite"},{"name":"Pass"}]}'),
            subprocess.CompletedProcess([], 0)
        ]) as mocked:
            self.assertEqual(runner.run(Path("build")), 0)
            self.assertEqual(mocked.call_count, 2)

    def test_failed_or_missing_test_fails_gate_and_continues(self):
        with patch.object(runner.subprocess, "run", side_effect=[
            subprocess.CompletedProcess([], 0, '{"tests":[{"name":"Missing"},{"name":"Pass"}]}'),
            subprocess.CompletedProcess([], 8), subprocess.CompletedProcess([], 0)
        ]) as mocked:
            self.assertEqual(runner.run(Path("build")), 1)
            self.assertEqual(mocked.call_count, 3)

    def test_release_configuration_and_executable_hash_are_recorded(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = Path(temp) / "contract"
            executable.write_bytes(b"actual tested binary")
            report = Path(temp) / "results.json"
            with patch.object(runner.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0),
                subprocess.CompletedProcess([], 0, json.dumps({"tests": [
                    {"name": "Pass", "command": [str(executable), "--fixture"]}]})),
                subprocess.CompletedProcess([], 0)
            ]) as mocked:
                self.assertEqual(runner.run(Path(temp), True, 2, "Release", report), 0)
            evidence = json.loads(report.read_text())
            self.assertTrue(evidence["passed"])
            self.assertTrue(evidence["buildFirst"])
            self.assertEqual(evidence["buildExitCode"], 0)
            self.assertEqual(evidence["results"][0]["executableSha256"], runner.fingerprint(executable))
            self.assertIn("--config", mocked.call_args_list[0].args[0])
            self.assertIn("-C", mocked.call_args_list[1].args[0])
            self.assertIn("-C", mocked.call_args_list[2].args[0])

    def test_failed_build_replaces_old_green_report(self):
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "results.json"
            report.write_text('{"passed":true}')
            with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 1)):
                self.assertEqual(runner.run(Path(temp), True, report=report), 1)
            self.assertFalse(json.loads(report.read_text())["passed"])

    def test_changed_executable_cannot_be_reported_as_passed(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = Path(temp) / "contract"
            executable.write_bytes(b"the tested binary")
            def changed_binary(*args, **kwargs):
                executable.write_bytes(b"a different binary")
                return subprocess.CompletedProcess([], 0)
            def fake_run(*args, **kwargs):
                return (
                    subprocess.CompletedProcess([], 0, json.dumps({"tests": [
                        {"name": "Rebuilt", "command": [str(executable)]}]}))
                    if "--show-only=json-v1" in args[0] else changed_binary(*args, **kwargs))
            with patch.object(runner.subprocess, "run", side_effect=fake_run):
                self.assertEqual(runner.run(Path(temp)), 1)


if __name__ == "__main__":
    unittest.main()
