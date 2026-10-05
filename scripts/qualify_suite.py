#!/usr/bin/env python3
"""Freeze a catalog-driven, natively tested internal suite candidate, never a public release."""
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zipfile

import gen_hub_manifest as catalog_generator

ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATHS = ("CMakeLists.txt", "cmake", "core", "plugins", "assets", "config", "scripts",
                "packaging", ".github/workflows", "third_party/JUCE", "third_party/rubberband",
                "third_party/ARA_SDK", "apps/sattari-hub", "docs/launch-readiness.md", "validation/golden",
                "validation/schemas", "LICENSE", "LICENSING.md")
DEPENDENCY_NOTICES = (
    ("third_party/JUCE/LICENSE.md", "JUCE-LICENSE.md"),
    ("third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt", "VST3-SDK-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt", "AudioUnitSDK-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_audio_formats/codecs/flac/Flac Licence.txt", "FLAC-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt", "Ogg-Vorbis-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_audio_formats/codecs/oggvorbis/libvorbis-1.3.7/COPYING", "Vorbis-COPYING.txt"),
    ("third_party/JUCE/modules/juce_graphics/image_formats/jpglib/README", "JPEG-README.txt"),
    ("third_party/JUCE/modules/juce_graphics/image_formats/pnglib/LICENSE", "PNG-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_core/zip/zlib/LICENSE", "zlib-LICENSE.txt"),
    ("third_party/JUCE/modules/juce_graphics/fonts/harfbuzz/COPYING", "HarfBuzz-COPYING.txt"),
    ("third_party/JUCE/modules/juce_graphics/unicode/sheenbidi/LICENSE", "SheenBidi-LICENSE.txt"),
    ("third_party/rubberband/COPYING", "RubberBand-COPYING.txt"),
    ("assets/icons/LICENSE.lucide", "Lucide-LICENSE.txt"),
    ("plugins/auto-pitch/models/CREPE-LICENSE.txt", "CREPE-LICENSE.txt"),
    ("plugins/auto-pitch/models/model-release.json", "CREPE-model-release.json"),
    ("third_party/ARA_SDK/ARA_API/LICENSE.txt", "ARA_API-LICENSE.txt"),
    ("third_party/ARA_SDK/ARA_Library/LICENSE.txt", "ARA_Library-LICENSE.txt"),
    ("third_party/ARA_SDK/NOTICE.txt", "ARA_SDK-NOTICE.txt"))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_inventory(extra_paths=()):
    files = {}
    for relative in (*SOURCE_PATHS, *extra_paths):
        source = ROOT / relative
        if not source.exists():
            raise ValueError(f"Missing corresponding-source input: {relative}")
        for path in sorted(source.rglob("*") if source.is_dir() else [source]):
            if path.is_file() and not any(p in {".git", ".build", "__pycache__", ".DS_Store"} for p in path.parts):
                files[path.relative_to(ROOT).as_posix()] = sha256(path)
    return files


def products_for_launch():
    catalog = json.loads((ROOT / "config/products.json").read_text())
    plan = json.loads((ROOT / "config/launch.json").read_text())
    if plan.get("schemaVersion") != 1 or plan.get("publicReleaseApproved") is not False:
        raise ValueError("Qualification creates internal candidates only")
    products = catalog_generator.validate(catalog)
    selected = [p for p in products if p["portfolioStatus"] in plan["catalogStatuses"]]
    if set(plan["requiredContracts"]) != {p["id"] for p in selected}:
        raise ValueError("Launch contract coverage differs from the current catalog")
    result = []
    for product in selected:
        directory = product.get("sourceDirectory", product["id"])
        cmake = (ROOT / "plugins" / directory / "CMakeLists.txt").read_text()
        version = re.search(r"project\([^)]*\bVERSION\s+(\d+\.\d+\.\d+)\s*\)", cmake)
        if not version:
            raise ValueError(f"Cannot identify native version: {product['id']}")
        target, name, bundles = catalog_generator.deployment(product)
        result.append({"id": product["id"], "name": name, "directory": directory,
                       "target": target, "version": version.group(1),
                       "formats": [b["kind"] for b in bundles],
                       "standalone": any(b["kind"] == "Standalone" for b in bundles),
                       "requiredContracts": plan["requiredContracts"][product["id"]]})
    return result, plan


def require_configuration(build, mac):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    required = {"SATTARI_COPY_PLUGINS_AFTER_BUILD": "OFF", "SATTARI_INCLUDE_STEMDECK": "OFF",
                "SATTARI_USE_ONNX": "ON", "SATTARI_REQUIRE_ONNX": "ON", "BUILD_TESTING": "ON",
                "SATTARI_AUTOPITCH_ENABLE_ARA": "ON", "SATTARI_SONGKEY_ENABLE_ARA": "ON",
                "SATTARI_USE_RUBBERBAND": "ON", "SATTARI_AUTOPITCH_PRINT_ENGINE": "RUBBERBAND",
                "SATTARI_USE_ELASTIQUE": "OFF", "SATTARI_USE_SIGNALSMITH": "OFF",
                "SATTARI_AUTOPITCH_ENABLE_AAX": "OFF"}
    if any(cache.get(k) != v for k, v in required.items()):
        raise ValueError("Unsafe/incomplete launch configuration: " + ", ".join(
            k for k, v in required.items() if cache.get(k) != v))
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != ROOT:
        raise ValueError("Use a root-suite build, not a single-plugin build tree")
    if cache.get("CMAKE_CONFIGURATION_TYPES"):
        if "Release" not in cache["CMAKE_CONFIGURATION_TYPES"].split(";"):
            raise ValueError("Release configuration is unavailable")
    elif cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("Launch binaries must use Release")
    if mac:
        if set(cache.get("CMAKE_OSX_ARCHITECTURES", "").split(";")) != {"arm64", "x86_64"}:
            raise ValueError("macOS launch requires both arm64 and x86_64")
        if cache.get("CMAKE_OSX_DEPLOYMENT_TARGET") != "13.3":
            raise ValueError("Pinned neural runtime requires an honest macOS 13.3 floor")
    return cache


def run(command, log, timeout=5400, env=None, cwd=ROOT):
    print("Running: " + " ".join(map(str, command)), flush=True)
    environment = dict(os.environ if env is None else env)
    environment["SATTARI_SESSION_ID"] = "suite-qualification-" + uuid.uuid4().hex
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(list(map(str, command)), cwd=cwd, stdout=output,
                                stderr=subprocess.STDOUT, timeout=timeout, env=environment)
    if result.returncode:
        raise RuntimeError(f"Failed ({result.returncode}): {log}")
    return log.read_text(encoding="utf-8", errors="replace")


def require_contract_report(path, inventory, products):
    report = json.loads(path.read_text())
    current = {t["name"]: t for t in inventory if "stemdeck" not in
               (t["name"] + " ".join(t.get("command", []))).lower()}
    expected = set(current)
    results = report.get("results", [])
    if not expected or len(results) != len(expected) or {r["name"] for r in results} != expected:
        raise ValueError("Contract report is incomplete or has duplicate tests")
    required = {test for p in products for test in p["requiredContracts"]}
    if not required.issubset(expected):
        raise ValueError(f"Launch contracts missing from CTest: {sorted(required - expected)}")
    if not report.get("passed") or not report.get("buildFirst") or report.get("buildExitCode") != 0:
        raise ValueError("Contracts were not freshly built and passed")
    for result in results:
        command = result.get("command", [])
        if result.get("exitCode") != 0 or not command or not result.get("executableSha256"):
            raise ValueError(f"Failed, missing or unhashed contract: {result['name']}")
        if command != current[result["name"]].get("command"):
            raise ValueError(f"Contract command differs from current CTest: {result['name']}")
        if sha256(Path(command[0])) != result["executableSha256"]:
            raise ValueError(f"Contract binary changed after testing: {result['name']}")
    return report


def verify_mac_runtime(binary, evidence, label):
    output = run(["otool", "-L", binary], evidence / f"{label}-dependencies.log")
    for line in output.splitlines():
        if not line.startswith("\t"):
            continue
        library = line.strip().split(" (", 1)[0]
        if not library.startswith(("/System/Library/", "/usr/lib/", "@loader_path/", "@rpath/")):
            raise ValueError(f"Non-portable library dependency: {binary}: {library}")


def verify_pe(path):
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError(f"Not a Windows executable: {path}")
        stream.seek(0x3c)
        offset = stream.read(4)
        if len(offset) != 4:
            raise ValueError(f"Truncated PE header: {path}")
        stream.seek(struct.unpack("<I", offset)[0])
        if stream.read(6) != b"PE\0\0\x64\x86":
            raise ValueError(f"Not a Windows x64 binary: {path}")


def candidate_hub_manifest(products, candidate):
    manifest = json.loads(catalog_generator.MANIFEST.read_text())
    selected = {p["id"]: p for p in products}
    manifest.update(suiteVersion=candidate, defaultUpdateChannel="internal-candidate",
                    truthLabel="Internal candidate. Public distribution is not approved.",
                    updateFeedURL=None, updateFeedSHA256=None)
    for product in manifest["products"]:
        product.update(updateArchiveURL=None, updateArchiveSHA256=None, updateArchiveBytes=None,
                       shippingProfile="internal-candidate" if product["id"] in selected else None)
        if product["id"] in selected:
            product["version"] = selected[product["id"]]["version"]
            for bundle in product["bundles"]:
                bundle["expectedVersion"] = product["version"]
    return manifest


def assemble_hub(stage, products, args, build, evidence):
    app = stage / "Sattari Hub.app"
    macos = app / "Contents/MacOS"
    resources = app / "Contents/Resources"
    macos.mkdir(parents=True)
    resources.mkdir()
    binaries = []
    for architecture in ("arm64", "x86_64"):
        scratch = build / ("hub-" + architecture)
        command = ["swift", "build", "--package-path", ROOT / "apps/sattari-hub", "-c", "release",
                   "--scratch-path", scratch, "--triple", architecture + "-apple-macosx13.3", "--jobs", args.jobs]
        run(command, evidence / f"hub-build-{architecture}.log")
        directory = run([*command, "--show-bin-path"], evidence / f"hub-bin-path-{architecture}.log").strip()
        binaries.append(Path(directory) / "SattariHub")
    run(["lipo", "-create", *binaries, "-output", macos / "SattariHub"], evidence / "hub-lipo.log")
    run(["lipo", macos / "SattariHub", "-verify_arch", "arm64", "x86_64"], evidence / "hub-architectures.log")
    shutil.copy2(ROOT / "apps/sattari-hub/Packaging/Info.plist", app / "Contents/Info.plist")
    manifest = candidate_hub_manifest(products, args.candidate)
    (resources / "sattari-products.json").write_text(json.dumps(manifest, indent=2) + "\n")
    for product in manifest["products"]:
        if product["shippingProfile"] is None:
            continue
        for bundle in product["bundles"]:
            destination = resources / "Payload" / bundle["relativeSourcePath"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = stage / bundle["kind"] / Path(bundle["relativeSourcePath"]).name
            source.rename(destination)
    for folder in ("AU", "VST3", "Standalone"):
        (stage / folder).rmdir()
    legal = resources / "Legal"
    shutil.copytree(stage / "Notices", legal)
    for filename in ("LICENSE", "LICENSING.md", "RELEASE-READINESS.md"):
        shutil.copy2(stage / filename, legal / filename)
    (legal / "SOURCE-OFFER.txt").write_text(
        "The exact project build-source and SDK snapshot is included at the top level\n"
        "as source-snapshot.tar.gz, with input hashes in Validation/source-inputs.json.\n"
        "This is not certification that all corresponding-source obligations have been met.\n"
        "Internal candidate only; distribution remains pending licensing and release review.\n")
    sign_mac(app, args.sign_identity, evidence, "hub")
    environment = dict(os.environ, SATTARI_HUB_OFFLINE="1", SATTARI_REPO_ROOT="")
    with tempfile.TemporaryDirectory(prefix="sattari-hub-offline-check-") as isolated:
        architectures = ["arm64", "x86_64"] if platform.machine() == "arm64" else ["x86_64"]
        for architecture in architectures:
            result = run(["arch", "-" + architecture, macos / "SattariHub", "--contract-check", "--require-bundled-current"],
                         evidence / f"hub-contract-{architecture}.log", 600, environment, isolated)
            if f"currentProducts={len(products)}" not in result:
                raise ValueError("Packaged Hub did not confirm the complete current suite")
    return app


def sign_mac(bundle, identity, evidence, label):
    arguments = ["codesign", "--force", "--sign", identity or "-"]
    audio_app = bundle.suffix == ".app" and label != "hub"
    if identity:
        arguments += ["--options", "runtime", "--timestamp"]
    frameworks = bundle / "Contents/Frameworks"
    for index, path in enumerate(sorted(frameworks.rglob("*.dylib"))):
        run([*arguments, path], evidence / f"{label}-nested-{index}-sign.log")
    entitlements = ROOT / "packaging" / (
        "sattari-modular-standalone.entitlements" if audio_app and label.startswith("modular-") else
        "sattari-standalone.entitlements" if audio_app else "sattari-plugin.entitlements")
    run([*arguments, *(["--entitlements", entitlements] if identity else []), bundle],
        evidence / f"{label}-sign.log")
    run(["codesign", "--verify", "--deep", "--strict", bundle], evidence / f"{label}-signature.log")
    if identity and audio_app:
        output = run(["codesign", "-d", "--entitlements", ":-", bundle],
                     evidence / f"{label}-entitlements.log")
        start = output.find("<?xml")
        end = output.find("</plist>", start)
        if start < 0 or end < 0:
            raise ValueError(f"Signed app has no readable entitlements: {bundle}")
        permissions = plistlib.loads(output[start:end + len("</plist>")].encode())
        if permissions.get("com.apple.security.device.audio-input") is not True:
            raise ValueError(f"Signed app cannot request audio input: {bundle}")


def validate_au(bundle, product, architectures, evidence):
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        components = plistlib.load(stream).get("AudioComponents", [])
    if len(components) != 1:
        raise ValueError(f"Expected one AU identity: {bundle}")
    identity = components[0]
    installed = Path.home() / "Library/Audio/Plug-Ins/Components" / bundle.name
    installed.parent.mkdir(parents=True, exist_ok=True)
    if installed.is_symlink():
        raise ValueError(f"Refusing to replace a symlinked AU: {installed}")
    with tempfile.TemporaryDirectory(prefix="sattari-au-backup-", dir=installed.parent) as temp:
        backup = Path(temp) / bundle.name
        existed = installed.exists()
        if existed:
            installed.rename(backup)
        try:
            shutil.copytree(bundle, installed, symlinks=True)
            for architecture in architectures:
                output = run(["arch", "-" + architecture, "auval", "-v", identity["type"],
                              identity["subtype"], identity["manufacturer"]],
                             evidence / f"{product['id']}-auval-{architecture}.log", 600)
                if "AU VALIDATION SUCCEEDED" not in output:
                    raise ValueError(f"AU validation did not pass: {bundle}")
        finally:
            if installed.exists():
                shutil.rmtree(installed)
            if existed:
                backup.rename(installed)


def qualify(args):
    if sys.platform not in ("darwin", "win32"):
        raise ValueError("Qualify natively on macOS or Windows")
    mac = sys.platform == "darwin"
    if args.sign_identity and not mac:
        raise ValueError("Developer ID signing is a macOS operation")
    if mac and not args.validate_au and not args.prepare_only:
        raise ValueError("Specify --validate-au for temporary AU installation with restoration")
    build = args.build_dir.resolve()
    products, plan = products_for_launch()
    cache = require_configuration(build, mac)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    platform_name = "macos-universal" if mac else "windows-x64"
    name = "Sattari-Suite-" + platform_name + "-" + args.candidate
    archive = output / (name + ".zip")
    if archive.exists():
        raise ValueError(f"Refusing to overwrite {archive}")
    prepared = output / (name + ".prepared")
    if prepared.exists():
        raise ValueError(f"Refusing to overwrite {prepared}")
    evidence = build / ("launch-qualification-" + args.candidate)
    if evidence.exists():
        raise ValueError(f"Use a new candidate name; evidence already exists: {evidence}")
    evidence.mkdir()
    sdk = Path(cache["ONNXRUNTIME_INCLUDE_DIR"]).resolve().parent
    try:
        sdk_relative = sdk.relative_to(ROOT).as_posix()
    except ValueError as error:
        raise ValueError("Keep the pinned launch SDK under third_party for the source snapshot") from error
    extra_inputs = (sdk_relative,)
    inputs = source_inventory(extra_inputs)
    (evidence / "source-inputs.json").write_text(json.dumps(inputs, indent=2) + "\n")
    try:
        targets = [p["target"] + "_All" for p in products]
        run(["cmake", "--build", build, "--config", "Release", "--parallel", args.jobs,
             "--target", *targets], evidence / "products-build.log", 14400)
        run([sys.executable, ROOT / "scripts/run_release_contracts.py", build, "--build",
             "--jobs", args.jobs, "--config", "Release", "--report", evidence / "contracts.json"],
            evidence / "contracts.log", 14400)
        inventory = json.loads(subprocess.check_output(
            ["ctest", "--test-dir", build, "-C", "Release", "--show-only=json-v1"], text=True))["tests"]
        contracts = require_contract_report(evidence / "contracts.json", inventory, products)
        run([sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests", "-p", "test_*.py", "-v"],
            evidence / "release-tool-tests.log")
        run([sys.executable, ROOT / "scripts/check_brand_registry_sync.py"], evidence / "catalog-sync.log")
        run([sys.executable, ROOT / "scripts/verify_auto_pitch_models.py", "--model-dir",
             ROOT / "plugins/auto-pitch/models", "--record", ROOT / "plugins/auto-pitch/models/model-release.json",
             "--distribution", "--require", "crepe.onnx"], evidence / "model-provenance.log")
        architectures = ["arm64", "x86_64"] if mac and platform.machine() == "arm64" else ["x86_64"]
        with tempfile.TemporaryDirectory(prefix="sattari-suite-", dir=output) as temp:
            stage = Path(temp) / name
            stage.mkdir()
            for product in products:
                for kind in product["formats"]:
                    if kind == "AU" and not mac:
                        continue
                    extension = {"AU": ".component", "VST3": ".vst3", "Standalone": ".app" if mac else ".exe"}[kind]
                    source = build / "plugins" / product["directory"] / (product["target"] + "_artefacts") / "Release" / kind / (product["name"] + extension)
                    destination = stage / kind / source.name
                    destination.parent.mkdir(exist_ok=True)
                    if source.is_dir():
                        shutil.copytree(source, destination, symlinks=True)
                    else:
                        shutil.copy2(source, destination)
                    label = product["id"] + "-" + kind
                    if mac:
                        with (destination / "Contents/Info.plist").open("rb") as stream:
                            metadata = plistlib.load(stream)
                        if metadata["CFBundleShortVersionString"] != product["version"]:
                            raise ValueError(f"Stale binary version: {destination}")
                        if kind == "Standalone" and not metadata.get("NSMicrophoneUsageDescription", "").strip():
                            raise ValueError(f"Standalone microphone permission description missing: {destination}")
                        binary = destination / "Contents/MacOS" / metadata["CFBundleExecutable"]
                        run(["lipo", binary, "-verify_arch", "arm64", "x86_64"], evidence / f"{label}-architectures.log")
                        if product["id"] == "auto-pitch":
                            run(["bash", ROOT / "scripts/bundle_auto_pitch_runtime_deps.sh", destination], evidence / f"{label}-runtime-embed.log")
                        sign_mac(destination, args.sign_identity, evidence, label)
                        verify_mac_runtime(binary, evidence, label)
                        if product["id"] == "auto-pitch":
                            run(["bash", ROOT / "scripts/verify_auto_pitch_bundle.sh", destination], evidence / f"{label}-runtime-verify.log")
                    else:
                        binary = destination if kind == "Standalone" else destination / "Contents/x86_64-win" / destination.name
                        verify_pe(binary)
                        if product["id"] == "auto-pitch":
                            runtime = Path(cache["ONNXRUNTIME_LIB"]).parent / "onnxruntime.dll"
                            if not runtime.is_file():
                                raise ValueError("ONNX Runtime Windows DLL is missing")
                            for dll in sorted(runtime.parent.glob("*.dll")):
                                verify_pe(dll)
                                shutil.copy2(dll, binary.parent / dll.name)
                    if kind == "VST3":
                        for architecture in architectures:
                            prefix = ["arch", "-" + architecture] if mac else []
                            result = run([*prefix, args.pluginval, "--validate-in-process", "--strictness-level", "10",
                                          "--timeout-ms", "180000", destination], evidence / f"{label}-pluginval-{architecture}.log", 1800)
                            if "SUCCESS" not in result or f"v{product['version']}" not in result:
                                raise ValueError(f"Validator did not confirm expected plugin/version: {destination}")
                    elif kind == "AU" and args.validate_au:
                        validate_au(destination, product, architectures, evidence)
            if source_inventory(extra_inputs) != inputs:
                raise ValueError("Source changed during qualification; rebuild the exact candidate")
            with tarfile.open(stage / "source-snapshot.tar.gz", "w:gz") as snapshot:
                for relative in inputs:
                    snapshot.add(ROOT / relative, arcname=relative, recursive=False)
            if source_inventory(extra_inputs) != inputs:
                raise ValueError("Source changed while freezing source snapshot")
            for file in ("LICENSE", "LICENSING.md"):
                shutil.copy2(ROOT / file, stage / file)
            notices = stage / "Notices"
            notices.mkdir()
            for relative, name_in_package in DEPENDENCY_NOTICES:
                shutil.copy2(ROOT / relative, notices / name_in_package)
            (notices / "README.txt").write_text(
                "These notices accompany the exact pinned framework, codec, font,\n"
                "plug-in SDK and neural dependencies in this candidate. Platform-specific\n"
                "items are not necessarily compiled into every bundle. Presence of a\n"
                "notice does not authorize distribution under a commercial license.\n"
                "Owner licensing review remains required; see LICENSING.md.\n")
            for filename in ("LICENSE", "ThirdPartyNotices.txt"):
                shutil.copy2(sdk / filename, notices / ("ONNXRuntime-" + filename))
            shutil.copy2(ROOT / "docs/launch-readiness.md", stage / "RELEASE-READINESS.md")
            if mac:
                assemble_hub(stage, products, args, build, evidence)
            if source_inventory(extra_inputs) != inputs:
                raise ValueError("Source changed during Hub assembly; rebuild before packaging")
            shutil.copytree(evidence, stage / "Validation")
            shutil.copy2(build / "CMakeCache.txt", stage / "Validation/CMakeCache.txt")
            manifest = {"schemaVersion": 1, "candidate": args.candidate,
                        "createdUtc": datetime.now(timezone.utc).isoformat(), "platform": platform_name,
                        "status": "prepared-not-fully-qualified" if args.prepare_only else "internal-candidate-not-public-release",
                        "publicReleaseApproved": False, "auValidated": mac and args.validate_au,
                        "products": products, "testedArchitectures": architectures,
                        "intelExecution": "Rosetta" if mac and platform.machine() == "arm64" else "native",
                        "pluginvalStrictness": 10, "nativeContractCount": len(contracts["results"]),
                        "signature": "Developer ID" if args.sign_identity else "ad-hoc" if mac else "unsigned",
                        "notarized": False, "ownerGates": plan["ownerGates"],
                        "sourceRevision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                        "sourceWorkingTreeDirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
                        "sourceInventorySha256": sha256(evidence / "source-inputs.json"),
                        "files": {p.relative_to(stage).as_posix(): sha256(p) for p in sorted(stage.rglob("*")) if p.is_file()}}
            (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
            if args.prepare_only:
                stage.rename(prepared)
                print(f"Prepared only, no release archive: {prepared}", flush=True)
                return
            pending = archive.with_suffix(".partial.zip")
            try:
                if mac:
                    subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", stage, pending], check=True)
                else:
                    with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED) as package:
                        for path in sorted(stage.rglob("*")):
                            if path.is_file():
                                package.write(path, path.relative_to(Path(temp)))
                with zipfile.ZipFile(pending) as package:
                    if package.testzip() is not None:
                        raise ValueError("Candidate archive integrity failed")
                pending.rename(archive)
            finally:
                pending.unlink(missing_ok=True)
        (output / (archive.name + ".sha256")).write_text(sha256(archive) + "  " + archive.name + "\n")
        print(archive, flush=True)
    except Exception as error:
        (evidence / "failure.json").write_text(json.dumps({"publicReleaseApproved": False, "error": str(error)}, indent=2) + "\n")
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", action="store_true", help="Print the exact catalog-driven build plan")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--pluginval", type=Path)
    parser.add_argument("--candidate", default="rc1")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--validate-au", action="store_true")
    parser.add_argument("--prepare-only", action="store_true",
                        help="Retain a staged internal build without a qualified archive; permits deferring AU installation")
    parser.add_argument("--sign-identity")
    args = parser.parse_args()
    if args.plan:
        print(json.dumps(products_for_launch()[0], indent=2))
    else:
        if not all((args.build_dir, args.output_dir, args.pluginval)) or args.jobs < 1 or not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}", args.candidate):
            parser.error("Provide build-dir, output-dir, pluginval, positive jobs and a safe candidate name")
        try:
            qualify(args)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            print(f"Suite qualification failed: {error}", file=sys.stderr, flush=True)
            raise SystemExit(1)
