#!/usr/bin/env python3
"""Build, validate, and freeze a Modular candidate. A failure produces no archive."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import plistlib
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[3]
VERSION = "0.2.0"
PRODUCTS = ("FX", "Instrument")
CONTRACTS = ("ModularFX", "ModularFXHosting", "ModularInstrument", "ModularInstrumentHosting")
SOURCE_PATHS = ("plugins/modular", "core", "cmake", "assets", "third_party/JUCE", "LICENSE", "LICENSING.md", ".github/workflows/modular-platforms.yml")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_inventory():
    result = {}
    for relative in SOURCE_PATHS:
        source = ROOT / relative
        for path in sorted(source.rglob("*") if source.is_dir() else [source]):
            if path.is_file() and not any(part in {".git", "__pycache__", ".DS_Store"} for part in path.parts):
                result[path.relative_to(ROOT).as_posix()] = sha256(path)
    return result


def run(command, log, timeout=600):
    print("Running: " + " ".join(map(str, command)), flush=True)
    with log.open("w", encoding="utf-8") as output:
        completed = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=output,
                                   stderr=subprocess.STDOUT, timeout=timeout, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"Failed ({completed.returncode}): {log}")
    return log.read_text(encoding="utf-8", errors="replace")


def require_contracts(path):
    cases = ET.parse(path).getroot().findall(".//testcase")
    if sorted(case.get("name") for case in cases) != sorted(CONTRACTS):
        raise ValueError("Expected all four Modular native and hosting contracts, exactly once")
    if any(case.find(tag) is not None for case in cases for tag in ("failure", "error", "skipped")):
        raise ValueError("A required native contract failed or was skipped")


def verify_pe(path):
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError(f"Not a Windows binary: {path}")
        stream.seek(0x3C)
        stream.seek(struct.unpack("<I", stream.read(4))[0])
        if stream.read(6) != b"PE\0\0\x64\x86":
            raise ValueError(f"Not a Windows x64 executable: {path}")


def main(args):
    if sys.platform not in ("darwin", "win32"):
        raise ValueError("Qualify natively on macOS or Windows")
    mac = sys.platform == "darwin"
    if args.sign_identity and not mac:
        raise ValueError("--sign-identity is a macOS Developer ID identity")
    build = args.build_dir.resolve()
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    name = f"Sattari-Modular-{VERSION}-{'macos-universal' if mac else 'windows-x64'}-candidate"
    archive = destination / (name + ".zip")
    if archive.exists():
        raise ValueError(f"Refusing to overwrite {archive}")
    evidence = build / "qualification"
    evidence.mkdir(exist_ok=True)
    sources = source_inventory()
    (evidence / "source-inputs.json").write_text(json.dumps(sources, indent=2) + "\n")
    run(["cmake", "--build", build, "--config", "Release", "--target", "SattariModularBuild", "--parallel", args.jobs], evidence / "build.log", 5400)
    run([sys.executable, "-m", "unittest", "discover", "-s", "plugins/modular/tests", "-p", "test_*.py", "-v"], evidence / "packaging-tests.log")
    run(["ctest", "--test-dir", build, "-C", "Release", "--output-on-failure", "--timeout", "300",
         "--output-junit", evidence / "contracts.xml"], evidence / "contracts.log", 660)
    require_contracts(evidence / "contracts.xml")
    shutil.copy2(build / "Testing/Temporary/LastTest.log", evidence / "native-checks.log")
    architectures = ["arm64", "x86_64"] if mac and platform.machine() == "arm64" else ["x86_64"]
    with tempfile.TemporaryDirectory(prefix="modular-", dir=destination) as temporary:
        stage = Path(temporary) / name
        stage.mkdir()
        for kind in PRODUCTS:
            product = f"Sattari Modular {kind}"
            formats = [("VST3", ".vst3"), ("Standalone", ".app" if mac else ".exe")]
            if mac:
                formats.append(("AU", ".component"))
            for format_name, extension in formats:
                source = build / f"SattariModular{kind}_artefacts/Release" / format_name / (product + extension)
                target = stage / format_name / source.name
                target.parent.mkdir(exist_ok=True)
                if source.is_dir():
                    shutil.copytree(source, target, symlinks=True)
                else:
                    shutil.copy2(source, target)
                if mac:
                    with (target / "Contents/Info.plist").open("rb") as stream:
                        metadata = plistlib.load(stream)
                        if metadata["CFBundleShortVersionString"] != VERSION:
                            raise ValueError(f"Wrong version: {target}")
                        if kind == "FX" and format_name == "Standalone" and not metadata.get("NSMicrophoneUsageDescription"):
                            raise ValueError("FX standalone lacks its audio-input permission description")
                    run(["lipo", target / "Contents/MacOS" / product, "-verify_arch", "arm64", "x86_64"], evidence / f"{kind}-{format_name}-architectures.log")
                    if args.sign_identity:
                        entitlements = ["--entitlements", ROOT / "plugins/modular/packaging/fx.entitlements"] if kind == "FX" and format_name == "Standalone" else []
                        run(["codesign", "--force", "--options", "runtime", "--timestamp", "--sign", args.sign_identity, *entitlements, target], evidence / f"{kind}-{format_name}-sign.log")
                    run(["codesign", "--verify", "--deep", "--strict", target], evidence / f"{kind}-{format_name}-signature.log")
                    run(["codesign", "--display", "--verbose=4", "--entitlements", "-", target], evidence / f"{kind}-{format_name}-identity.log")
                else:
                    verify_pe(target if extension == ".exe" else target / "Contents/x86_64-win" / target.name)
            vst3 = stage / "VST3" / (product + ".vst3")
            for arch in architectures:
                prefix = ["arch", "-" + arch] if mac else []
                log = evidence / f"{kind}-pluginval-{arch}.log"
                result = run(prefix + [args.pluginval, "--validate-in-process", "--strictness-level", "10", "--timeout-ms", "180000", vst3], log, 1800)
                if "SUCCESS" not in result or f"v{VERSION}" not in result:
                    raise ValueError(f"Validator did not confirm the expected plugin: {log}")
            if mac:
                if not args.validate_au:
                    raise ValueError("Mac qualification needs --validate-au; it temporarily installs only the two Modular AUs")
                component = stage / "AU" / (product + ".component")
                installed = Path.home() / "Library/Audio/Plug-Ins/Components" / component.name
                installed.parent.mkdir(parents=True, exist_ok=True)
                backup = Path(temporary) / (kind + "-previous.component")
                existed = installed.exists()
                if existed:
                    shutil.copytree(installed, backup, symlinks=True)
                try:
                    if existed:
                        shutil.rmtree(installed)
                    shutil.copytree(component, installed, symlinks=True)
                    for arch in architectures:
                        result = run(["arch", "-" + arch, "auval", "-v", "aumf" if kind == "FX" else "aumu", "MoF1" if kind == "FX" else "MoI1", "Satr"], evidence / f"{kind}-auval-{arch}.log", 600)
                        if "AU VALIDATION SUCCEEDED" not in result:
                            raise ValueError("AU validation did not report success")
                finally:
                    if installed.exists():
                        shutil.rmtree(installed)
                    if existed:
                        shutil.copytree(backup, installed, symlinks=True)

        if source_inventory() != sources:
            raise ValueError("Source changed during qualification; rebuild before packaging")
        for source, target in [("plugins/modular/README.md", "README.md"),
                               ("plugins/modular/RELEASE.md", "RELEASE.md"),
                               ("LICENSE", "LICENSE"), ("LICENSING.md", "LICENSING-repository.md"),
                               ("third_party/JUCE/LICENSE.md", "JUCE-LICENSE.md"),
                               ("assets/icons/LICENSE.lucide", "LICENSE.lucide")]:
            shutil.copy2(ROOT / source, stage / target)
        with tarfile.open(stage / "source-snapshot.tar.gz", "w:gz") as snapshot:
            for relative in sources:
                snapshot.add(ROOT / relative, arcname=relative, recursive=False)
        if source_inventory() != sources:
            raise ValueError("Source changed while freezing the source snapshot")
        shutil.copytree(evidence, stage / "Validation")
        shutil.copytree(build / "modular-verification", stage / "Preview")
        shutil.copy2(build / "CMakeCache.txt", stage / "Validation/CMakeCache.txt")
        manifest = {
            "product": "Sattari Modular", "version": VERSION,
            "createdUtc": datetime.now(timezone.utc).isoformat(),
            "status": "candidate-not-public-release-approved",
            "platform": "macos-universal" if mac else "windows-x64",
            "testedArchitectures": architectures, "intelMacTesting": "Rosetta" if mac and platform.machine() == "arm64" else "native",
            "pluginvalStrictness": 10, "nativeContracts": "passed",
            "signature": "Developer ID" if args.sign_identity else "ad-hoc" if mac else "unsigned",
            "notarized": False, "publicReleaseBlockers": ["licensing review", "supported DAW acceptance", "listening approval", "clean-machine installation", "platform matrix completion", "notarization / Windows signing"],
            "sourceRevision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "sourceWorkingTreeDirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--", *SOURCE_PATHS], cwd=ROOT, text=True).strip()),
            "files": {path.relative_to(stage).as_posix(): sha256(path) for path in sorted(stage.rglob("*")) if path.is_file()},
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        pending = destination / (name + ".partial.zip")
        try:
            if mac:
                subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", stage, pending], check=True)
            else:
                with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED) as package:
                    for path in sorted(stage.rglob("*")):
                        if path.is_file():
                            package.write(path, path.relative_to(Path(temporary)))
            with zipfile.ZipFile(pending) as package:
                if package.testzip() is not None:
                    raise ValueError("Archive integrity failed")
            pending.rename(archive)
        finally:
            pending.unlink(missing_ok=True)
    (destination / (archive.name + ".sha256")).write_text(sha256(archive) + "  " + archive.name + "\n")
    print(archive, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--pluginval", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--validate-au", action="store_true")
    parser.add_argument("--sign-identity")
    main(parser.parse_args())
