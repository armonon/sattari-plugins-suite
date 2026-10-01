from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile
import unittest


class PackageSourceGuardTests(unittest.TestCase):
    def test_required_dependency_notice_copy_commands(self):
        root = Path(__file__).resolve().parents[2]
        script = (root / "scripts/package_suite_installer.sh").read_text()
        required = {"RUBBERBAND-COPYING.txt", "ONNXRUNTIME-LICENSE.txt", "ONNXRUNTIME-THIRD-PARTY-NOTICES.txt"}
        copied = set()
        with tempfile.TemporaryDirectory(prefix="sattari-notice-contract-") as tmp:
            for line in script.splitlines():
                if not line.startswith('cp "$repo_root/') or '"$legal/' not in line:
                    continue
                command = shlex.split(line.replace("$repo_root", str(root)).replace("$legal", tmp))
                if Path(command[-1]).name in required:
                    subprocess.run(command, check=True)
                    self.assertEqual(Path(command[1]).read_bytes(), Path(command[2]).read_bytes())
                    copied.add(Path(command[-1]).name)
        self.assertEqual(copied, required)

    def test_untracked_compiled_source_blocks_otherwise_clean_package(self):
        original = Path(__file__).resolve().parents[1] / "package_suite_installer.sh"
        for path in ["core/new.h", "plugins/fixture/new.cpp", "apps/sattari-hub/Sources/new.swift"]:
            with self.subTest(path=path), tempfile.TemporaryDirectory(prefix="sattari-source-guard-") as tmp:
                root = Path(tmp)
                (root / "scripts").mkdir()
                script = root / "scripts/package_suite_installer.sh"
                shutil.copyfile(original, script)
                subprocess.run(["git", "init", "-q", tmp], check=True)
                subprocess.run(["git", "-C", tmp, "add", "scripts"], check=True)
                subprocess.run(["git", "-C", tmp, "-c", "user.name=Contract", "-c", "user.email=contract@invalid.example",
                                "commit", "-qm", "Fixture"], check=True)
                extra = root / path
                extra.parent.mkdir(parents=True, exist_ok=True)
                extra.write_text("// Untracked compilation input\n")
                result = subprocess.run(["bash", str(script)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn("untracked binary-bearing source", result.stderr)
                self.assertFalse((root / "build").exists(), "guard must run before packaging/building")


if __name__ == "__main__":
    unittest.main()
