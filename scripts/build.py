#!/usr/bin/env python3
"""Build a device arm64e fallback dylib and repackage the supplied TrollStore IPA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
INPUT_SHA256 = "04d5f9763f5c4dcd19d40fcd52d152cebaa7c18ec68dedb0e90c972fb1c2153c"
CORE_UUID = bytes.fromhex("4ed1fd29a46a36b38dfc36f67ea929d1")
LOAD_PATH = b"@executable_path/CoreBootCache.dylib\0"


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def commands(data):
    if struct.unpack_from("<I", data)[0] != 0xFEEDFACF:
        raise ValueError("Expected a thin 64-bit Mach-O")
    count, size = struct.unpack_from("<II", data, 16)
    position = 32
    for _ in range(count):
        command, length = struct.unpack_from("<II", data, position)
        if length < 8 or length % 8 or position + length > 32 + size:
            raise ValueError("Invalid load command")
        yield position, command, length
        position += length
    if position != 32 + size:
        raise ValueError("Invalid load command table length")


def inject(data):
    data = bytearray(data)
    first_section = len(data)
    uuid = None
    for pos, command, size in commands(data):
        if command == 0x1B:
            uuid = bytes(data[pos + 8:pos + 24])
        elif command == 0x19:
            for index in range(struct.unpack_from("<I", data, pos + 64)[0]):
                section = pos + 72 + index * 80
                offset = struct.unpack_from("<I", data, section + 48)[0]
                if offset:
                    first_section = min(first_section, offset)
        elif command == 0xC:
            offset = struct.unpack_from("<I", data, pos + 8)[0]
            if bytes(data[pos + offset:pos + size]).split(b"\0")[0] == LOAD_PATH[:-1]:
                raise ValueError("Patch already injected")
    if uuid != CORE_UUID:
        raise ValueError("Unsupported Core executable UUID")
    count, table_size = struct.unpack_from("<II", data, 16)
    start = 32 + table_size
    length = (24 + len(LOAD_PATH) + 7) & ~7
    if start + length > first_section or any(data[start:start + length]):
        raise ValueError("Insufficient zero-filled load-command padding")
    command = struct.pack("<6I", 0xC, length, 24, 0, 0x10000, 0x10000)
    data[start:start + length] = (command + LOAD_PATH).ljust(length, b"\0")
    struct.pack_into("<II", data, 16, count + 1, table_size + length)
    return bytes(data)


def extract(ipa, destination):
    with zipfile.ZipFile(ipa) as archive:
        for entry in archive.infolist():
            target = destination / entry.filename
            if not target.resolve().is_relative_to(destination.resolve()):
                raise ValueError("Unsafe archive entry")
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            mode = entry.external_attr >> 16
            if (mode & 0o170000) == 0o120000:
                raise ValueError("Unexpected symlink in this IPA")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(entry))
            target.chmod(mode & 0o777 or 0o644)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=ROOT / "input/Core.ipa")
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--clang", default="clang-21")
    parser.add_argument("--linker", default="ld64.lld-21")
    parser.add_argument("--ldid", type=Path)
    args = parser.parse_args()
    original_hash = hashlib.sha256(args.input.read_bytes()).hexdigest()
    if original_hash != INPUT_SHA256:
        raise ValueError("Input IPA hash does not match analyzed version")
    work = ROOT / "build"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    extract(args.input, work)
    app = work / "Payload/Core.app"
    binary = app / "Core"
    binary.write_bytes(inject(binary.read_bytes()))
    dylib = app / "CoreBootCache.dylib"
    source = ROOT / "patch/CoreBootCache.m"
    common = ["-arch", "arm64e", "-miphoneos-version-min=17.0", "-O2", "-Wall", "-Wextra",
              "-Werror", "-Wno-unused-parameter", "-fobjc-arc", "-fblocks"]
    if sys.platform == "darwin":
        sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
        run("xcrun", "--sdk", "iphoneos", "clang", *common, "-isysroot", sdk, "-dynamiclib",
            "-Wl,-install_name,@executable_path/CoreBootCache.dylib", "-framework", "Foundation",
            "-framework", "UIKit", "-framework", "IOKit", source, "-o", dylib)
    else:
        if not args.sdk or not args.ldid:
            parser.error("Linux builds require --sdk and --ldid")
        obj = work / "CoreBootCache.o"
        run(args.clang, "-target", "arm64e-apple-ios17.0", *common[2:], "-isysroot", args.sdk,
            "-c", source, "-o", obj)
        run(args.linker, "-dylib", "-arch", "arm64e", "-platform_version", "ios", "17.0", "16.5",
            "-syslibroot", args.sdk, "-lSystem", "-lobjc", "-framework", "Foundation", "-framework",
            "UIKit", "-framework", "IOKit", "-install_name", "@executable_path/CoreBootCache.dylib",
            "-adhoc_codesign", obj, "-o", dylib)
    info_path = app / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update(CFBundleVersion="3", UIFileSharingEnabled=True, LSSupportsOpeningDocumentsInPlace=True,
                CoreBootCachePatchVersion="2", CoreBootCacheSupportedBuild="21A327")
    info_path.write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    shutil.rmtree(app / "_CodeSignature", ignore_errors=True)
    (app / "embedded.mobileprovision").unlink(missing_ok=True)
    entitlements = ROOT / "patch/TrollStore.entitlements.plist"
    binary.chmod(0o755)
    for library in app.glob("*.dylib"):
        library.chmod(0o755)
        if sys.platform == "darwin":
            run("codesign", "--force", "--sign", "-", "--timestamp=none", library)
        else:
            run(args.ldid, "-S", library)
    if sys.platform == "darwin":
        run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements", entitlements, app)
        run("codesign", "--verify", "--deep", "--strict", app)
    else:
        run(args.ldid, "-S" + str(entitlements), binary)
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    artifact = dist / "Core-17.0-21A327-TrollStore-v2.ipa"
    with zipfile.ZipFile(artifact, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted((work / "Payload").rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(work))
    report = dict(input_sha256=original_hash, artifact=artifact.name,
                  sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                  core_uuid=CORE_UUID.hex(), target_build="21A327", patch_version=2,
                  build_host=sys.platform, device_runtime_verified=False)
    (dist / "build-metadata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
