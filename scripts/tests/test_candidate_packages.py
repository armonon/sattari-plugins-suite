import importlib.util
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("candidate_package", ROOT / "scripts/package_release_candidates.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class CandidatePackageTests(unittest.TestCase):
    def test_only_windows_x64_binaries_are_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture.exe"
            data = bytearray(128)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3c, 64)
            data[64:70] = b"PE\0\0\x64\x86"
            path.write_bytes(data)
            package.verify_pe(path)
            data[68:70] = b"\x64\xaa"
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                package.verify_pe(path)
            path.write_bytes(b"not a Windows binary")
            with self.assertRaises(ValueError):
                package.verify_pe(path)

    def test_contracts_must_be_complete_passed_and_not_skipped(self):
        import json
        products = json.loads(package.PRODUCT_FILE.read_text())
        with tempfile.TemporaryDirectory() as temp:
            build = Path(temp)
            evidence = build / "validation"
            evidence.mkdir()
            suite = ET.Element("testsuite")
            for name in package.PROCESSOR_TESTS | {p["target"] + "UIContract" for p in products}:
                ET.SubElement(suite, "testcase", name=name)
            for product in products:
                (evidence / (product["slug"] + "-pluginval.log")).write_text("SUCCESS\n")
            xml = evidence / "contracts.xml"
            ET.ElementTree(suite).write(xml)
            package.require_contracts(build, products)
            skipped = ET.SubElement(suite[0], "skipped")
            ET.ElementTree(suite).write(xml)
            with self.assertRaises(ValueError):
                package.require_contracts(build, products)
            suite[0].remove(skipped)
            suite.remove(suite[-1])
            ET.ElementTree(suite).write(xml)
            with self.assertRaises(ValueError):
                package.require_contracts(build, products)

    def test_source_archive_excludes_git_metadata(self):
        self.assertIsNone(package.source_filter(tarfile.TarInfo("third_party/JUCE/.git")))
        self.assertIsNone(package.source_filter(tarfile.TarInfo("core/__pycache__/private.pyc")))
        self.assertIsNotNone(package.source_filter(tarfile.TarInfo("core/include/Engine.h")))

    def test_checksum_is_stable(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture"
            path.write_bytes(b"abc")
            self.assertEqual(package.sha256(path), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")


if __name__ == "__main__":
    unittest.main()
