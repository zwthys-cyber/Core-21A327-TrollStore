#!/usr/bin/env python3
"""Build the independent cache preparation UI, using the working diagnostic app model."""
import json
import hashlib
import plistlib
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path
from build import ROOT, run
from verify import signature


def main():
    work = ROOT / "build/preparation"
    if work.exists():
        shutil.rmtree(work)
    app = work / "Payload/CoreCachePrepare.app"
    app.mkdir(parents=True)
    binary = app / "CoreCachePrepare"
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
    run("xcrun", "--sdk", "iphoneos", "clang", "-arch", "arm64", "-miphoneos-version-min=17.0",
        "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fobjc-arc", "-fblocks",
        "-isysroot", sdk, "-framework", "Foundation", "-framework", "UIKit", "-framework", "IOKit",
        ROOT / "preparation/main.m", "-o", binary)
    info = dict(CFBundleIdentifier="local.core.cache-preparation", CFBundleExecutable=binary.name,
        CFBundleName="CoreCachePrepare", CFBundleDisplayName="Core 环境准备", CFBundlePackageType="APPL",
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
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    artifact = dist / "Core-Cache-Prepare-TrollStore.ipa"
    with zipfile.ZipFile(artifact, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted((work / "Payload").rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(work))
    report = dict(artifact=artifact.name, sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
        minimum_core_patch=7, device_runtime_verified=False)
    (dist / "preparation-metadata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
