#!/usr/bin/env python3
"""Package the five native candidates; reject missing, wrong-platform or untested binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PRODUCT_FILE = ROOT / "packaging/candidates/products.json"
PROCESSOR_TESTS = {"SattariArpTimingContract", "SubConjurerContract", "SattariMixProductContract",
                   "SattariMixAutoMixContract", "SattariVocalProductContract", "SattariCreateProductContract",
                   "SattariMixRackRegression"}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_pe(path):
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError(f"Not a Windows binary: {path}")
        stream.seek(0x3c)
        offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(offset)
        if stream.read(4) != b"PE\0\0" or stream.read(2) != b"\x64\x86":
            raise ValueError(f"Not a Windows x64 PE binary: {path}")


def require_contracts(build, products):
    root = ET.parse(build / "validation/contracts.xml").getroot()
    cases = root.findall(".//testcase")
    expected = PROCESSOR_TESTS | {p["target"] + "UIContract" for p in products}
    names = [case.get("name") for case in cases]
    if len(names) != len(expected) or set(names) != expected:
        raise ValueError("The native contract report does not contain all 12 required tests")
    if any(case.find(tag) is not None for case in cases for tag in ("failure", "error", "skipped")):
        raise ValueError("A required native contract failed or was skipped")
    for product in products:
        log = build / "validation" / (product["slug"] + "-pluginval.log")
        if not log.is_file() or "SUCCESS" not in log.read_text(encoding="utf-8-sig"):
            raise ValueError(f"Missing passing pluginval evidence: {product['slug']}")


def source_filter(info):
    if any(part in {".git", ".DS_Store", "__pycache__"} for part in Path(info.name).parts):
        return None
    return info


def package(args):
    platform = args.platform
    if platform == "windows-x64" and sys.platform != "win32":
        raise ValueError("Windows packages must be qualified and assembled on Windows")
    if platform == "macos-universal" and sys.platform != "darwin":
        raise ValueError("Mac packages must be qualified and assembled on macOS")
    products = json.loads(PRODUCT_FILE.read_text())
    build = args.build_dir.resolve()
    if args.require_tests:
        require_contracts(build, products)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = "Sattari-Candidates-" + platform
    archive = output / (name + ".zip")
    if archive.exists():
        raise ValueError(f"Refusing to overwrite {archive}")
    with tempfile.TemporaryDirectory(prefix="sattari-package-", dir=output) as temp:
        stage = Path(temp) / name
        stage.mkdir()
        for product in products:
            artifacts = build / product["slug"] / (product["target"] + "_artefacts") / "Release"
            formats = [("VST3", ".vst3")]
            if platform == "macos-universal":
                formats.append(("AU", ".component"))
            if product["standalone"]:
                formats.append(("Standalone", ".app" if platform == "macos-universal" else ".exe"))
            for format_name, suffix in formats:
                source = artifacts / format_name / (product["name"] + suffix)
                if platform == "windows-x64":
                    binary = source if suffix == ".exe" else source / "Contents/x86_64-win" / source.name
                    verify_pe(binary)
                else:
                    binary = source / "Contents/MacOS" / product["name"]
                    subprocess.run(["lipo", str(binary), "-verify_arch", "arm64", "x86_64"], check=True)
                    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(source)], check=True)
                    with (source / "Contents/Info.plist").open("rb") as stream:
                        version = plistlib.load(stream)["CFBundleShortVersionString"]
                    if version != product["version"]:
                        raise ValueError(f"Wrong bundle version: {source}: {version}")
                destination = stage / format_name / source.name
                destination.parent.mkdir(exist_ok=True)
                if source.is_dir():
                    shutil.copytree(source, destination, symlinks=True)
                else:
                    shutil.copy2(source, destination)
        for file in ("LICENSE", "LICENSING.md"):
            shutil.copy2(ROOT / file, stage / file)
        for source, destination in [("assets/icons/LICENSE.lucide", "LICENSE.lucide"),
                                    ("third_party/rubberband/COPYING", "RUBBERBAND-COPYING.txt"),
                                    ("third_party/JUCE/LICENSE.md", "JUCE-LICENSE.md"),
                                    ("packaging/candidates/README.md", "README.md")]:
            shutil.copy2(ROOT / source, stage / destination)
        installer = "Install-Windows.ps1" if platform == "windows-x64" else "Install-macOS.command"
        shutil.copy2(ROOT / "packaging/candidates" / installer, stage / installer)
        if platform == "macos-universal":
            (stage / installer).chmod(0o755)
        if (build / "validation").is_dir():
            shutil.copytree(build / "validation", stage / "validation")
        if args.evidence:
            shutil.copytree(args.evidence, stage / "validation", dirs_exist_ok=True)
        source_paths = ["cmake", "core", "assets", "packaging/candidates", "third_party/JUCE", "third_party/rubberband",
                        "scripts/package_release_candidates.py", "LICENSE", "LICENSING.md"]
        source_paths += ["plugins/" + product["slug"] for product in products]
        with tarfile.open(stage / "source-snapshot.tar.gz", "w:gz") as tar:
            for path in source_paths:
                tar.add(ROOT / path, arcname=path, filter=source_filter)
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--", *source_paths], cwd=ROOT, text=True).strip())
        files = [{"path": path.relative_to(stage).as_posix(), "sha256": sha256(path)}
                 for path in sorted(stage.rglob("*")) if path.is_file()]
        manifest = {"platform": platform, "products": products, "sourceRevision": revision,
                    "sourceWorkingTreeDirty": dirty,
                    "sourceSnapshot": "source-snapshot.tar.gz", "nativeContractsRequired": args.require_tests,
                    "status": "internal-candidate-not-public-release", "files": files}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        if platform == "macos-universal":
            subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(stage), str(archive)], check=True)
        else:
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zip_file:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        zip_file.write(path, path.relative_to(Path(temp)))
    print(archive)
    print("SHA256 " + sha256(archive))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=["macos-universal", "windows-x64"])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--require-tests", action="store_true")
    package(parser.parse_args())
