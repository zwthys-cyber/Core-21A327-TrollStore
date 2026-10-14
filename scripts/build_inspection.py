#!/usr/bin/env python3
"""Build the independent installed-game source inventory app on GitHub Actions."""
import hashlib
import json
import plistlib
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path
from build import ROOT, run
from verify import signature


def main():
    work = ROOT / "build/model-inspection"
    if work.exists():
        shutil.rmtree(work)
    app = work / "Payload/CoreModelInspector.app"
    app.mkdir(parents=True)
    binary = app / "CoreModelInspector"
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
    run("xcrun", "--sdk", "iphoneos", "clang", "-arch", "arm64", "-miphoneos-version-min=17.0",
        "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fobjc-arc", "-fblocks",
        "-isysroot", sdk, "-framework", "Foundation", "-framework", "UIKit",
        ROOT / "inspection/main.m", "-o", binary)
    info = dict(CFBundleIdentifier="local.core.model-source-inspector", CFBundleExecutable=binary.name,
        CFBundleName="CoreModelInspector", CFBundleDisplayName="Core 模型检查", CFBundlePackageType="APPL",
        CFBundleShortVersionString="1.0", CFBundleVersion="1", MinimumOSVersion="17.0",
        LSRequiresIPhoneOS=True, UIDeviceFamily=[1, 2], UIFileSharingEnabled=True,
        LSSupportsOpeningDocumentsInPlace=True, UILaunchScreen={},
        UISupportedInterfaceOrientations=["UIInterfaceOrientationPortrait"])
    (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    entitlements = ROOT / "preparation/entitlements.plist"
    run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements", entitlements, app)
    run("codesign", "--verify", "--deep", "--strict", app)
    data = binary.read_bytes()
    assert struct.unpack_from("<III", data, 4) == (0x100000C, 0, 2)
    assert signature(data) == plistlib.loads(entitlements.read_bytes())
    dist = ROOT / "dist/model-inspector"
    dist.mkdir(parents=True, exist_ok=True)
    artifact = dist / "Core-Model-Source-Inspector-TrollStore-v1.ipa"
    with zipfile.ZipFile(artifact, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted((work / "Payload").rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(work))
    with zipfile.ZipFile(artifact) as archive:
        installed = plistlib.loads(archive.read("Payload/CoreModelInspector.app/Info.plist"))
        assert installed == info
        assert signature(archive.read("Payload/CoreModelInspector.app/CoreModelInspector")) == plistlib.loads(entitlements.read_bytes())
    report = dict(artifact=artifact.name, bytes=artifact.stat().st_size,
        sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
        source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        bundle_id=info["CFBundleIdentifier"], target_bundle_id="com.tencent.tmgp.pubgmhd",
        version="1.0", build="1", minimum_os="17.0", diagnostic_version=1,
        device_runtime_verified=False, live_mesh_verified=False, silhouette_implemented=False,
        game_process_memory_read=False, game_started=False, resource_contents_read=False,
        maximum_scan_entries_per_root=15000, maximum_resources_per_root=512, maximum_seconds_per_root=8,
        report_name="Core-model-source-report.json")
    (dist / "model-inspector-metadata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
