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
DYLIB_VERSION = 0x10000
PATCH_VERSION = 19
BUNDLE_BUILD = "20"
ARTIFACT_NAME = "Core-17.0-21A327-TrollStore-v19-600m.ipa"
LICENSE_SHA256 = "765793d5c5202ce8e31dd97d1270fcb69b6244c4347ef9460d594fbcad40a529"
LICENSE_UUID = bytes.fromhex("4fd6c6bcafb83fb09594261e2f842bd4")
LICENSE_STARTUP_ENTRIES = {
    "+[CCCoreLicenseGuard load]": 0x4360,
    "_CCCoreLicenseConstructor": 0xD5C4,
}
ARM64_RET = bytes.fromhex("c0035fd6")


def disable_license_startup(data):
    """Disable the two automatic entry points of the analyzed local gate library.

    Returning before PACIBSP leaves the incoming LR and SP unchanged. Keep the
    library and its dependency ordinal; no remote authorization is fabricated.
    """
    if hashlib.sha256(data).hexdigest() != LICENSE_SHA256:
        raise ValueError("Unsupported license library hash")
    if struct.unpack_from("<II", data, 4) != (0x100000C, 0x80000002):
        raise ValueError("Unsupported license library ABI")
    uuid, text, initializers, symbol_table = None, None, None, None
    for pos, command, size in commands(data):
        if command == 0x1B:
            uuid = data[pos + 8:pos + 24]
        elif command == 0x2:
            symbol_table = struct.unpack_from("<4I", data, pos + 8)
        elif command == 0x19:
            for index in range(struct.unpack_from("<I", data, pos + 64)[0]):
                section = pos + 72 + index * 80
                name = data[section:section + 16].split(b"\0")[0]
                segment = data[section + 16:section + 32].split(b"\0")[0]
                address, length = struct.unpack_from("<QQ", data, section + 32)
                offset = struct.unpack_from("<I", data, section + 48)[0]
                if segment == b"__TEXT" and name == b"__text":
                    text = (address, length, offset)
                if segment == b"__TEXT" and name == b"__init_offsets":
                    initializers = data[offset:offset + length]
    if uuid != LICENSE_UUID or text is None or symbol_table is None:
        raise ValueError("Unsupported license library layout")
    if initializers != struct.pack("<I", LICENSE_STARTUP_ENTRIES["_CCCoreLicenseConstructor"]):
        raise ValueError("Unexpected license initializer table")
    symoff, count, stroff, strsize = symbol_table
    symbols = {}
    for index in range(count):
        string, kind, _, _, value = struct.unpack_from("<IBBHQ", data, symoff + index * 16)
        end = data.find(b"\0", stroff + string, stroff + strsize)
        name = data[stroff + string:end].decode("utf-8")
        if name in LICENSE_STARTUP_ENTRIES and kind == 0xE:
            if name in symbols:
                raise ValueError("Duplicate license startup symbol")
            symbols[name] = value
    if symbols != LICENSE_STARTUP_ENTRIES:
        raise ValueError("License startup symbols differ from analyzed addresses")
    result = bytearray(data)
    address, length, offset = text
    expected_prologue = bytes.fromhex("7f2303d5ffc300d1fd7b02a9fd830091")
    for entry in symbols.values():
        if not address <= entry <= address + length - len(expected_prologue):
            raise ValueError("License startup entry lies outside executable text")
        start = offset + entry - address
        if data[start:start + len(expected_prologue)] != expected_prologue:
            raise ValueError("Unexpected license startup instructions")
        result[start:start + 4] = ARM64_RET
    return bytes(result)


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
    command = struct.pack("<6I", 0xC, length, 24, 0, DYLIB_VERSION, DYLIB_VERSION)
    data[start:start + length] = (command + LOAD_PATH).ljust(length, b"\0")
    struct.pack_into("<II", data, 16, count + 1, table_size + length)
    return bytes(data)


def validate_patch_dependency(main, library):
    """Keep the injected requirement consistent with the actual linked library."""
    def records(data, kind):
        result = []
        for pos, cmd, size in commands(data):
            if cmd != kind:
                continue
            name_offset, _, current, compatibility = struct.unpack_from("<4I", data, pos + 8)
            name = data[pos + name_offset:pos + size].split(b"\0")[0]
            if name == LOAD_PATH[:-1]:
                result.append((current, compatibility))
        return result
    required = records(main, 0xC)
    provided = records(library, 0xD)
    if len(required) != 1 or len(provided) != 1:
        raise ValueError("Expected one matching patch dependency and install name")
    if provided[0][0] < required[0][1] or provided[0][1] > provided[0][0]:
        raise ValueError("Patch dylib version is lower than the injected requirement")
    if provided[0] != (DYLIB_VERSION, DYLIB_VERSION):
        raise ValueError("Patch dylib must explicitly declare version 1.0.0")


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
    parser.add_argument("--model-capture", action="store_true", help="Build the optional bounded runtime-data capture variant")
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
    from remote_imports import redirect_remote_imports, redirect_model_capture_import
    from minimal_features import PROFILE, minimal_core, manifest
    main_payload = redirect_remote_imports(inject(minimal_core(binary.read_bytes())))
    if args.model_capture:
        main_payload = redirect_model_capture_import(main_payload)
    binary.write_bytes(main_payload)
    license_library = app / "CCCoreLicense.dylib"
    license_library.write_bytes(disable_license_startup(license_library.read_bytes()))
    dylib = app / "CoreBootCache.dylib"
    probe = app / "CoreBootProbe"
    source = ROOT / "patch/CoreBootCache.m"
    trace_source = ROOT / "patch/CoreRemoteTrace.c"
    probe_source = ROOT / "patch/CoreBootProbe.m"
    capture_sources = [ROOT / "patch/CoreModelCapture.m"] if args.model_capture else []
    common = ["-arch", "arm64e", "-miphoneos-version-min=17.0", "-O2", "-Wall", "-Wextra",
              "-Werror", "-Wno-unused-parameter", "-fobjc-arc", "-fblocks"]
    if args.model_capture:
        common.append("-DCORE_MODEL_CAPTURE=1")
    if sys.platform == "darwin":
        sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
        run("xcrun", "--sdk", "iphoneos", "clang", *common, "-isysroot", sdk, "-dynamiclib",
            "-Wl,-install_name,@executable_path/CoreBootCache.dylib", "-framework", "Foundation",
            "-Wl,-current_version,1.0.0,-compatibility_version,1.0.0",
            "-framework", "UIKit", "-framework", "IOKit", "-framework", "Security", source, trace_source, *capture_sources, "-o", dylib)
        run("xcrun", "--sdk", "iphoneos", "clang", "-arch", "arm64", *common[2:],
            "-isysroot", sdk, "-framework", "Foundation", "-framework", "IOKit", probe_source, "-o", probe)
    else:
        if not args.sdk or not args.ldid:
            parser.error("Linux builds require --sdk and --ldid")
        obj = work / "CoreBootCache.o"
        run(args.clang, "-target", "arm64e-apple-ios17.0", *common[2:], "-isysroot", args.sdk,
            "-c", source, "-o", obj)
        trace_obj = work / "CoreRemoteTrace.o"
        run(args.clang, "-target", "arm64e-apple-ios17.0", *common[2:], "-isysroot", args.sdk,
            "-c", trace_source, "-o", trace_obj)
        capture_objects = []
        for capture_source in capture_sources:
            capture_obj = work / "CoreModelCapture.o"
            run(args.clang, "-target", "arm64e-apple-ios17.0", *common[2:], "-isysroot", args.sdk,
                "-c", capture_source, "-o", capture_obj)
            capture_objects.append(capture_obj)
        run(args.linker, "-dylib", "-arch", "arm64e", "-platform_version", "ios", "17.0", "16.5",
            "-syslibroot", args.sdk, "-lSystem", "-lobjc", "-framework", "Foundation", "-framework",
            "UIKit", "-framework", "IOKit", "-framework", "Security", "-install_name", "@executable_path/CoreBootCache.dylib",
            "-current_version", "1.0.0", "-compatibility_version", "1.0.0",
            "-adhoc_codesign", obj, trace_obj, *capture_objects, "-o", dylib)
        probe_obj = work / "CoreBootProbe.o"
        run(args.clang, "-target", "arm64-apple-ios17.0", *common[2:], "-isysroot", args.sdk,
            "-c", probe_source, "-o", probe_obj)
        run(args.linker, "-arch", "arm64", "-platform_version", "ios", "17.0", "16.5",
            "-syslibroot", args.sdk, "-lSystem", "-lobjc", "-framework", "Foundation",
            "-framework", "IOKit", "-e", "_main", "-adhoc_codesign", probe_obj, "-o", probe)
    validate_patch_dependency(binary.read_bytes(), dylib.read_bytes())
    info_path = app / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update(CFBundleVersion=BUNDLE_BUILD, UIFileSharingEnabled=True, LSSupportsOpeningDocumentsInPlace=True,
                CoreBootCachePatchVersion=str(PATCH_VERSION), CoreBootCacheSupportedBuild="21A327",
                CoreFeatureProfile=PROFILE,
                CoreLocalLicenseGate="disabled-startup", CoreRemoteTraceVersion="1")
    if args.model_capture:
        info.update(CFBundleVersion="26", CoreModelCaptureVersion="6")
    info_path.write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    shutil.rmtree(app / "_CodeSignature", ignore_errors=True)
    (app / "embedded.mobileprovision").unlink(missing_ok=True)
    entitlements = ROOT / "patch/TrollStore.entitlements.plist"
    probe_entitlements = ROOT / "patch/CoreBootProbe.entitlements.plist"
    binary.chmod(0o755)
    probe.chmod(0o755)
    for library in app.glob("*.dylib"):
        library.chmod(0o755)
        if sys.platform == "darwin":
            run("codesign", "--force", "--sign", "-", "--timestamp=none", library)
        else:
            run(args.ldid, "-S", library)
    if sys.platform == "darwin":
        run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements", probe_entitlements, probe)
        run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements", entitlements, app)
        run("codesign", "--verify", "--deep", "--strict", app)
    else:
        run(args.ldid, "-S" + str(probe_entitlements), probe)
        run(args.ldid, "-S" + str(entitlements), binary)
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    artifact = dist / ("Core-17.0-21A327-TrollStore-model-capture-v6.ipa" if args.model_capture else ARTIFACT_NAME)
    with zipfile.ZipFile(artifact, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted((work / "Payload").rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(work))
    report = dict(input_sha256=original_hash, artifact=artifact.name,
                  sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                  core_uuid=CORE_UUID.hex(), target_build="21A327", patch_version=PATCH_VERSION,
                  feature_profile=PROFILE, minimal_changes=manifest(),
                  marker_names="original validated lookup and native string copy/cleanup; original bot flag selects name color: human opaque red 0xff0000ff, bot opaque white 0xffffffff; no category suffix; neutral unknown-player fallback; centered name with native black outline, font 14 times native scale",
                  feature_baseline_commit="0fe975fc6053a0af8cac90c4a860696d08dec8c2",
                  marker_count="sum original per-frame human and bot counters after enemy-team and finite inclusive 600-metre checks, before projection; centered red outlined digits at y=64*scale, font=24*scale; shared nearby-enemy checkbox",
                  marker_rays="baseline 0fe975f quantity/opacity: no per-frame cap; top center, 10 drawing units below screen top, connect downward to targets; opaque white through 100 metres, white alpha 0x70 beyond 100 through 600 metres",
                  marker_directions="offscreen only, inclusive 600 metres; native yellow seven-point arrow with black outline and outlined distance; original cached world X/Y relative to camera yaw; validated finite bearing and current screen bounds",
                  marker_diagnostics="read-only local activation, mode, target binding, source availability, readiness and cached joint counts; sampled every 5 seconds",
                  boot_acquisition="separate preparation app with current-boot stamp and digest; direct/probe fallback",
                  local_license_gate="disabled-startup",
                  remote_trace="three libSystem imports forwarded unchanged; original error codes and receive duration",
                  remote_log_rotation=dict(active_limit_bytes=2 * 1024 * 1024,
                                           backup_limit_bytes=2 * 1024 * 1024, backup_count=1,
                                           retain="most recent tail", contention="drop diagnostic record"),
                  license_startup_entries={name: hex(address) for name, address in LICENSE_STARTUP_ENTRIES.items()},
                  build_host=sys.platform, device_runtime_verified=False)
    report["model_runtime_capture"] = dict(enabled=args.model_capture, capture_version=6 if args.model_capture else 0,
        acquisition="full located component transform array, component transform, native projection, bounded original geometry ray distances",
        report="Core-model-runtime-report.json", max_targets=3, snapshots_per_target=2,
        component_layout_sample_limit_bytes=4096,
        reference_candidates=dict(limit=16, reads_per_snapshot=56, prefix_bytes_max=4096, class_header_bytes=80,
            array_entries_per_slot=16, asset_candidate_offset=0x828, recursive_scan=False, semantic_types_verified=False),
        binding_evidence=dict(fixed_child_resource_offset=0x828, component_resource_links=16,
            extra_asset_reads=56, root_asset_array_reads=8, root_asset_arrays=4, array_count_limit=256,
            names=256, name_reads=640, name_text_bytes=256, native_name_source="Core-D6794-live-profile",
            semantic_types_verified=False),
        mesh_evidence=dict(resources=12, components=16, reads=96, reference_bytes=32768,
            child_pose_bytes=16384, native_nodes=12, native_prefix_bytes=1024,
            bone_record_stride=16, reference_transform_stride=48,
            bone_identity="name index and number", matrix_offset_0x260_hypothesis_rejected=True,
            render_buffers_verified=False, semantic_child_pose_space_verified=False),
        minimum_sample_spacing_seconds=10,
        capture_window_seconds=180, ray_budget_ms_per_snapshot=10,
        entry_return="ARM64 veneer passes original LR in x4 before the C prologue",
        entry_diagnostics="all forwarding entries, unmatched callers, expected reader calls, disabled and recursive reader calls",
        live_mesh_binding_verified=False, full_render_mesh_verified=False, silhouette_implemented=False)
    (dist / "build-metadata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
