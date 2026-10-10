#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

def main():
    work = ROOT / "build/diagnostics"
    shutil.rmtree(work, ignore_errors=True)
    app = work / "Payload/CoreDiagnostics.app"
    app.mkdir(parents=True)
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
    binary = app / "CoreDiagnostics"
    run("xcrun", "--sdk", "iphoneos", "clang", "-arch", "arm64", "-miphoneos-version-min=15.0",
        "-isysroot", sdk, "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-Wno-deprecated-declarations", "-fobjc-arc", "-fblocks", "-framework", "Foundation",
        "-framework", "UIKit", "-framework", "IOKit", ROOT / "diagnostics/main.m", "-o", binary)
    info = dict(CFBundleIdentifier="local.core.diagnostics", CFBundleExecutable="CoreDiagnostics",
        CFBundleName="CoreDiagnostics", CFBundleDisplayName="Core 诊断", CFBundlePackageType="APPL",
        CFBundleShortVersionString="1.0", CFBundleVersion="1", MinimumOSVersion="15.0",
        LSRequiresIPhoneOS=True, UIFileSharingEnabled=True, LSSupportsOpeningDocumentsInPlace=True,
        UIDeviceFamily=[1,2], UILaunchScreen={}, UISupportedInterfaceOrientations=["UIInterfaceOrientationPortrait"])
    (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    binary.chmod(0o755)
    run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements",
        ROOT / "diagnostics/entitlements.plist", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    dist = ROOT / "dist/diagnostics"
    dist.mkdir(parents=True, exist_ok=True)
    ipa = dist / "Core-Diagnostics-TrollStore.ipa"
    with zipfile.ZipFile(ipa, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(app.rglob("*")):
            if path.is_file(): archive.write(path, path.relative_to(work))
    metadata = dict(artifact=ipa.name, sha256=hashlib.sha256(ipa.read_bytes()).hexdigest(),
        architecture="arm64", minimum_ios="15.0", device_runtime_verified=False,
        scope="Core registration, installed Mach-O metadata, Core logs, startup request result; no Core modification")
    (dist / "build-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))

if __name__ == "__main__": main()
