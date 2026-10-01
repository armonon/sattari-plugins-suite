import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("qualify", Path(__file__).parents[1] / "scripts/qualify.py")
qualify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qualify)


class QualificationTests(unittest.TestCase):
    def report(self, xml):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contracts.xml"
            path.write_text(xml)
            qualify.require_contracts(path)

    def test_exact_passing_inventory(self):
        self.report('<testsuite><testcase name="ModularFX"/><testcase name="ModularInstrument"/></testsuite>')

    def test_missing_contract_fails(self):
        with self.assertRaises(ValueError):
            self.report('<testsuite><testcase name="ModularFX"/></testsuite>')

    def test_duplicate_contract_fails(self):
        with self.assertRaises(ValueError):
            self.report('<testsuite><testcase name="ModularFX"/><testcase name="ModularFX"/></testsuite>')

    def test_skips_and_failures_fail(self):
        for tag in ("failure", "error", "skipped"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                self.report(f'<testsuite><testcase name="ModularFX"><{tag}/></testcase><testcase name="ModularInstrument"/></testsuite>')

    def test_empty_inventory_fails(self):
        with self.assertRaises(ValueError):
            self.report('<testsuite/>')

    def test_pe_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plugin.exe"
            data = bytearray(256)
            data[:2] = b"MZ"
            data[0x3C:0x40] = (128).to_bytes(4, "little")
            data[128:134] = b"PE\0\0\x64\x86"
            path.write_bytes(data)
            qualify.verify_pe(path)
            data[132:134] = b"\x4c\x01"
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                qualify.verify_pe(path)

    def test_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file"
            path.write_bytes(b"abc")
            self.assertEqual(qualify.sha256(path), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")


if __name__ == "__main__":
    unittest.main()
