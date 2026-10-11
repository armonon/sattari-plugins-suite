import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("suite_qualification", ROOT / "scripts/qualify_suite.py")
suite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite)


class SuiteQualificationTests(unittest.TestCase):
    def test_required_framework_and_codec_notices_exist_and_are_unique(self):
        names = [name for _, name in suite.DEPENDENCY_NOTICES]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({"VST3-SDK-LICENSE.txt", "AudioUnitSDK-LICENSE.txt", "FLAC-LICENSE.txt",
                         "Ogg-Vorbis-LICENSE.txt", "JPEG-README.txt", "PNG-LICENSE.txt", "zlib-LICENSE.txt",
                         "HarfBuzz-COPYING.txt", "SheenBidi-LICENSE.txt"}.issubset(names))
        self.assertTrue(all((suite.ROOT / path).is_file() for path, _ in suite.DEPENDENCY_NOTICES))

    def test_validator_calls_use_private_session_ids_and_preserve_other_environment(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(suite.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            for _ in range(2):
                suite.run(["validator"], Path(temp) / "log", env={"PATH": "test-path", "SATTARI_SESSION_ID": "user-project"})
            environments = [call.kwargs["env"] for call in run.call_args_list]
            self.assertTrue(all(env["PATH"] == "test-path" for env in environments))
            self.assertTrue(all(env["SATTARI_SESSION_ID"].startswith("suite-qualification-") for env in environments))
            self.assertNotEqual(environments[0]["SATTARI_SESSION_ID"], environments[1]["SATTARI_SESSION_ID"])

    def test_launch_tracks_the_consolidated_catalog_not_old_wrappers(self):
        products, plan = suite.products_for_launch()
        self.assertEqual(len(products), 16)
        ids = {p["id"] for p in products}
        self.assertTrue({"modular", "modular-instrument", "royal-chain", "mix", "auto-pitch"}.issubset(ids))
        self.assertFalse({"vocal", "create", "stack", "bus", "voxkey", "stemdeck"} & ids)
        self.assertFalse(plan["publicReleaseApproved"])
        self.assertTrue(all(p["requiredContracts"] for p in products))
        self.assertTrue(all(p["target"] + "BooleanStateContract" in p["requiredContracts"] for p in products))
        self.assertFalse(next(p for p in products if p["id"] == "sattari-arp")["standalone"])
        self.assertEqual(next(p for p in products if p["id"] == "modular")["version"], "0.2.0")

    def test_configuration_requires_production_capabilities_and_no_install(self):
        configuration = {
            "SATTARI_COPY_PLUGINS_AFTER_BUILD": "OFF", "SATTARI_INCLUDE_STEMDECK": "OFF",
            "SATTARI_USE_ONNX": "ON", "SATTARI_REQUIRE_ONNX": "ON", "BUILD_TESTING": "ON",
            "SATTARI_AUTOPITCH_ENABLE_ARA": "ON", "SATTARI_SONGKEY_ENABLE_ARA": "ON",
            "SATTARI_USE_RUBBERBAND": "ON", "SATTARI_AUTOPITCH_PRINT_ENGINE": "RUBBERBAND",
            "SATTARI_USE_ELASTIQUE": "OFF", "SATTARI_USE_SIGNALSMITH": "OFF",
            "SATTARI_AUTOPITCH_ENABLE_AAX": "OFF",
            "CMAKE_HOME_DIRECTORY": str(ROOT), "CMAKE_BUILD_TYPE": "Release",
            "CMAKE_OSX_ARCHITECTURES": "arm64;x86_64", "CMAKE_OSX_DEPLOYMENT_TARGET": "13.3"}
        with tempfile.TemporaryDirectory() as temp:
            build = Path(temp)
            def write(values):
                (build / "CMakeCache.txt").write_text("\n".join(f"{k}:STRING={v}" for k, v in values.items()))
            write(configuration)
            suite.require_configuration(build, True)
            for key, bad in (("SATTARI_COPY_PLUGINS_AFTER_BUILD", "ON"),
                             ("SATTARI_USE_ONNX", "OFF"), ("SATTARI_AUTOPITCH_ENABLE_ARA", "OFF"),
                             ("SATTARI_USE_RUBBERBAND", "OFF"), ("SATTARI_AUTOPITCH_PRINT_ENGINE", "SIGNALSMITH"),
                             ("SATTARI_USE_ELASTIQUE", "ON"), ("SATTARI_USE_SIGNALSMITH", "ON"),
                             ("SATTARI_AUTOPITCH_ENABLE_AAX", "ON"),
                             ("CMAKE_OSX_ARCHITECTURES", "arm64"), ("CMAKE_OSX_DEPLOYMENT_TARGET", "11.0"),
                             ("CMAKE_BUILD_TYPE", "Debug"), ("CMAKE_HOME_DIRECTORY", str(ROOT / "plugins/modular"))):
                write({**configuration, key: bad})
                with self.subTest(key=key), self.assertRaises(ValueError):
                    suite.require_configuration(build, True)

    def test_candidate_hub_installs_all_current_products_and_no_legacy(self):
        products, _ = suite.products_for_launch()
        manifest = suite.candidate_hub_manifest(products, "rc-fixture")
        current = [p for p in manifest["products"] if p["shippingProfile"]]
        self.assertEqual({p["id"] for p in current}, {p["id"] for p in products})
        self.assertEqual(manifest["defaultUpdateChannel"], "internal-candidate")
        self.assertIsNone(manifest["updateFeedURL"])
        self.assertTrue(all(p["version"] == b["expectedVersion"] for p in current for b in p["bundles"]))
        legacy = [p for p in manifest["products"] if p.get("portfolioStatus") == "legacy"]
        self.assertTrue(legacy)
        self.assertTrue(all(p["shippingProfile"] is None for p in legacy))

    def test_reports_require_all_current_tests_and_exact_tested_binaries(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = Path(temp) / "test"
            executable.write_bytes(b"test binary")
            report = Path(temp) / "contracts.json"
            inventory = [{"name": name, "command": [str(executable)]} for name in ("A", "B")]
            results = [{"name": name, "command": [str(executable)], "exitCode": 0,
                        "executableSha256": suite.sha256(executable)} for name in ("A", "B")]
            passing = {"passed": True, "buildFirst": True, "buildExitCode": 0, "results": results}
            products = [{"requiredContracts": ["A"]}]
            def verify(data):
                report.write_text(json.dumps(data))
                suite.require_contract_report(report, inventory, products)
            verify(passing)
            for broken in ({**passing, "passed": False}, {**passing, "buildFirst": False},
                           {**passing, "buildExitCode": 1}, {**passing, "results": results[:1]},
                           {**passing, "results": [results[0], results[0]]},
                           {**passing, "results": [{**results[0], "exitCode": 8}, results[1]]},
                           {**passing, "results": [{**results[0], "command": [str(executable), "--wrong-test"]}, results[1]]},
                           {**passing, "results": [{**results[0], "executableSha256": None}, results[1]]}):
                with self.assertRaises(ValueError):
                    verify(broken)
            executable.write_bytes(b"stale different test binary")
            with self.assertRaises(ValueError):
                verify(passing)

    def test_windows_architecture_and_truncated_headers_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = Path(temp) / "plugin.exe"
            data = bytearray(128)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3c, 64)
            data[64:70] = b"PE\0\0\x64\x86"
            executable.write_bytes(data)
            suite.verify_pe(executable)
            for malformed in (b"MZ", b"not Windows", data[:68], data[:68] + b"\x64\xaa"):
                executable.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    suite.verify_pe(executable)

    def test_au_validation_failure_restores_the_previous_installation(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp) / "home"
            installed = home / "Library/Audio/Plug-Ins/Components/Test.component"
            installed.mkdir(parents=True)
            (installed / "old-file").write_text("keep exact user install")
            bundle = Path(temp) / "Test.component"
            (bundle / "Contents").mkdir(parents=True)
            with (bundle / "Contents/Info.plist").open("wb") as stream:
                plistlib.dump({"AudioComponents": [{"type": "aufx", "subtype": "Tst1", "manufacturer": "Satr"}]}, stream)
            with patch.object(suite.Path, "home", return_value=home), patch.object(suite, "run", side_effect=RuntimeError("validator failed")):
                with self.assertRaises(RuntimeError):
                    suite.validate_au(bundle, {"id": "test"}, ["arm64"], Path(temp))
            self.assertEqual((installed / "old-file").read_text(), "keep exact user install")
            self.assertFalse((installed / "Contents").exists())
            self.assertEqual(list(installed.parent.iterdir()), [installed])

    def test_mac_machine_local_libraries_block_packaging(self):
        for dependency in ("/Users/developer/lib.dylib", "/opt/homebrew/lib/rubberband.dylib"):
            with patch.object(suite, "run", return_value=f"plugin:\n\t{dependency} (compatibility version 1.0.0)\n"):
                with self.assertRaises(ValueError):
                    suite.verify_mac_runtime(Path("plugin"), Path("logs"), "plugin")

    def test_standalone_signing_grants_and_verifies_audio_input(self):
        entitlement_xml = plistlib.dumps({"com.apple.security.device.audio-input": True}).decode()
        with patch.object(suite, "run", return_value=entitlement_xml) as run:
            suite.sign_mac(Path("Sattari Mix.app"), "Developer ID Application: test", Path("logs"), "mix-Standalone")
            commands = [call.args[0] for call in run.call_args_list]
            self.assertTrue(any(suite.ROOT / "packaging/sattari-standalone.entitlements" in command for command in commands))
            self.assertTrue(any("--entitlements" in command and ":-" in command for command in commands))
        for output in ("no entitlement data", plistlib.dumps({}).decode()):
            with patch.object(suite, "run", return_value=output), self.assertRaises(ValueError):
                suite.sign_mac(Path("Sattari Mix.app"), "Developer ID Application: test", Path("logs"), "mix-Standalone")

    def test_hub_and_plugin_signing_do_not_request_audio_input(self):
        with patch.object(suite, "run", return_value="") as run:
            for bundle, label in (("Sattari Hub.app", "hub"), ("Sattari Mix.vst3", "mix-VST3")):
                suite.sign_mac(Path(bundle), "Developer ID Application: test", Path("logs"), label)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertFalse(any(suite.ROOT / "packaging/sattari-standalone.entitlements" in command for command in commands))

    def test_only_modular_standalone_gets_hosting_exceptions(self):
        xml = plistlib.dumps({"com.apple.security.device.audio-input": True}).decode()
        with patch.object(suite, "run", return_value=xml) as run:
            suite.sign_mac(Path("Sattari Modular.app"), "Developer ID Application: test", Path("logs"), "modular-Standalone")
            self.assertTrue(any(suite.ROOT / "packaging/sattari-modular-standalone.entitlements" in call.args[0]
                                for call in run.call_args_list))
        with (suite.ROOT / "packaging/sattari-standalone.entitlements").open("rb") as stream:
            self.assertEqual(plistlib.load(stream), {"com.apple.security.device.audio-input": True})


if __name__ == "__main__":
    unittest.main()
