#!/usr/bin/env python3
"""Check instruction preservation, load dependencies, architectures and TrollStore signing."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import sys
import zipfile
from build import commands, LOAD_PATH, CORE_UUID, ROOT, validate_patch_dependency, disable_license_startup, inject, PATCH_VERSION, BUNDLE_BUILD
from remote_imports import ALIASES, MODEL_CAPTURE_ALIASES, redirect_remote_imports, redirect_model_capture_import, records
from minimal_features import PROFILE, minimal_core


def sections(data):
    result = {}
    for pos, cmd, size in commands(data):
        if cmd != 0x19:
            continue
        for index in range(struct.unpack_from("<I", data, pos + 64)[0]):
            section = pos + 72 + index * 80
            name = bytes(data[section:section + 16]).split(b"\0")[0]
            segment = bytes(data[section + 16:section + 32]).split(b"\0")[0]
            length = struct.unpack_from("<Q", data, section + 40)[0]
            offset = struct.unpack_from("<I", data, section + 48)[0]
            if offset:
                result[(segment, name)] = data[offset:offset + length]
    return result


def signature(data):
    for pos, cmd, size in commands(data):
        if cmd != 0x1D:
            continue
        offset, length = struct.unpack_from("<II", data, pos + 8)
        blob = data[offset:offset + length]
        magic, total, count = struct.unpack_from(">3I", blob)
        assert magic == 0xFADE0CC0 and total <= length
        slots = {}
        for index in range(count):
            kind, start = struct.unpack_from(">2I", blob, 12 + 8 * index)
            _, size = struct.unpack_from(">2I", blob, start)
            slots[kind] = blob[start:start + size]
        assert 0 in slots, "No code directory"
        directory = slots[0]
        _, _, version, flags, hash_offset, ident, special, pages, limit = struct.unpack_from(">9I", directory)
        hash_size, hash_type, _, page_size = struct.unpack_from("4B", directory, 36)
        assert hash_type == 2, "Expected SHA-256 code signature"
        assert limit == offset, "Unexpected signing boundary"
        for index in range(pages):
            chunk = data[index * (1 << page_size):min((index + 1) * (1 << page_size), limit)]
            assert hashlib.sha256(chunk).digest()[:hash_size] == directory[
                hash_offset + index * hash_size:hash_offset + (index + 1) * hash_size], "Bad signature page"
        for kind in [5, 7]:
            if kind in slots:
                assert kind <= special
                digest = directory[hash_offset - kind * hash_size:hash_offset - (kind - 1) * hash_size]
                assert hashlib.sha256(slots[kind]).digest()[:hash_size] == digest, "Bad entitlement signature"
        return plistlib.loads(slots[5][8:]) if 5 in slots else {}
    raise AssertionError("No embedded signature")


def main(ipa):
    with zipfile.ZipFile(ipa) as archive, zipfile.ZipFile(ROOT / "input/Core.ipa") as original:
        app = "Payload/Core.app/"
        main = archive.read(app + "Core")
        info = plistlib.loads(archive.read(app + "Info.plist"))
        capture_version = info.get("CoreModelCaptureVersion")
        assert capture_version in (None, "1", "2", "3", "4", "5", "6", "7"), "Unknown capture protocol"
        model_capture = capture_version is not None
        render_version=info.get("CoreModelRenderVersion")
        assert render_version in (None,"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18"), "Unknown renderer protocol"
        model_render=render_version is not None
        assert not model_render or capture_version=="7"
        vehicle_version = info.get("CoreNearbyVehiclesVersion")
        assert vehicle_version in (None, "1"), "Unknown vehicle marker protocol"
        assert (vehicle_version == "1") == (render_version in ("12", "13", "14", "15", "16", "17", "18")), "Vehicle build identity mismatch"
        latency_version = info.get("CoreOverlayLatencyVersion")
        assert latency_version in (None, "1"), "Unknown overlay-latency protocol"
        assert (latency_version == "1") == (render_version in ("13", "14", "15", "16", "17", "18")), "Latency build identity mismatch"
        work_version = info.get("CoreFillWorkVersion")
        assert work_version in (None, "1"), "Unknown fill-work protocol"
        assert (work_version == "1") == (render_version in ("14", "15", "16", "17", "18")), "Fill-work build identity mismatch"
        actor_trace_version=info.get("CoreActorVisibilityTraceVersion")
        assert actor_trace_version in (None,"1"), "Unknown actor-trace protocol"
        assert (actor_trace_version=="1")== (render_version in ("15","16","17","18")), "Actor-trace build identity mismatch"
        weapon_version=info.get("CoreWeaponDisplayVersion")
        assert weapon_version in (None,"1"), "Unknown weapon display protocol"
        assert not weapon_version or actor_trace_version=="1", "Weapon display requires actor-trace build"
        bow_trace_version=info.get("CoreGroundBowTraceVersion")
        assert bow_trace_version in (None,"1"), "Unknown ground bow trace protocol"
        assert (bow_trace_version=="1")== (render_version in ("16","17","18")), "Ground-bow trace build identity mismatch"
        assert not bow_trace_version or weapon_version=="1", "Ground-bow trace requires selective weapon display"
        essentials_version=info.get("CoreGroundEssentialsVersion")
        assert essentials_version in (None,"1"), "Unknown ground essentials protocol"
        assert (essentials_version=="1")== (render_version in ("17","18")), "Ground essentials build identity mismatch"
        assert not essentials_version or bow_trace_version=="1", "Ground essentials requires bow source trace"
        late_camera_version=info.get("CoreGroundLateCameraVersion")
        assert late_camera_version in (None,"1"), "Unknown ground late-camera protocol"
        assert (late_camera_version=="1")== (render_version=="18"), "Ground late-camera build identity mismatch"
        assert not late_camera_version or essentials_version=="1", "Ground late-camera requires selected essentials"
        controls_version=info.get("CoreGroundItemControlsVersion")
        assert controls_version in (None,"1"), "Unknown ground item controls protocol"
        assert not controls_version or late_camera_version=="1", "Ground item controls requires final-camera build"
        libs = [name for name in archive.namelist() if name.endswith(".dylib")]
        paths = []
        uuid = None
        for pos, cmd, size in commands(main):
            if cmd == 0xC:
                offset = struct.unpack_from("<I", main, pos + 8)[0]
                paths.append(main[pos + offset:pos + size].split(b"\0")[0])
            if cmd == 0x1B:
                uuid = main[pos + 8:pos + 24]
        assert paths.count(LOAD_PATH[:-1]) == 1
        assert uuid == CORE_UUID
        validate_patch_dependency(main, archive.read(app + "CoreBootCache.dylib"))
        expected_main = redirect_remote_imports(inject(minimal_core(original.read(app + "Core"))))
        if vehicle_version:
            from nearby_vehicles import nearby_vehicles
            expected_main = nearby_vehicles(expected_main)
        if latency_version:
            from overlay_latency import overlay_latency
            expected_main = overlay_latency(expected_main)
        if weapon_version:
            from weapon_display import weapon_display
            expected_main = weapon_display(expected_main)
        if essentials_version:
            from ground_essentials import ground_essentials
            expected_main = ground_essentials(expected_main)
        if late_camera_version:
            from ground_late_camera import ground_late_camera
            expected_main = ground_late_camera(expected_main)
        if controls_version:
            from ground_item_controls import ground_item_controls
            expected_main = ground_item_controls(expected_main)
        if model_capture:
            expected_main = redirect_model_capture_import(expected_main)
        if model_render:
            from model_render_patch import redirect_model_render,MODEL_RENDER_ALIASES
            expected_main=redirect_model_render(expected_main)
        expected_aliases = {**ALIASES, **MODEL_CAPTURE_ALIASES} if model_capture else ALIASES
        if model_render:
            expected_aliases={**expected_aliases,**MODEL_RENDER_ALIASES}
        # codesign may only resize __LINKEDIT and replace the signature. Check
        # every other byte, including bind records, import names, and header stubs.
        signature_offset = None
        assert main[:32] == expected_main[:32]
        actual_commands = list(commands(main))
        expected_commands = list(commands(expected_main))
        assert actual_commands == expected_commands
        for (pos, cmd, size) in expected_commands:
            actual = bytearray(main[pos:pos + size])
            expected = bytearray(expected_main[pos:pos + size])
            if cmd == 0x19 and expected[8:24].rstrip(b"\0") == b"__LINKEDIT":
                actual[32:40] = expected[32:40]  # vmsize
                actual[48:56] = expected[48:56]  # filesize
            elif cmd == 0x1D:
                signature_offset = struct.unpack_from("<I", expected, 8)[0]
                actual[12:16] = expected[12:16]  # signature length
            assert actual == expected, "Unexpected main load-command change"
        assert signature_offset is not None
        payload_start = 32 + struct.unpack_from("<I", expected_main, 20)[0]
        assert main[payload_start:signature_offset] == expected_main[payload_start:signature_offset], "Unexpected unsigned program or import-table change"
        _, chained_imports, symbol_imports = records(main)
        for alias in expected_aliases.values():
            for table in (chained_imports, symbol_imports):
                entries = [r for r in table if r["name"] == alias]
                assert len(entries) == 1 and entries[0]["ordinal"] == 52
        patch_library = archive.read(app + "CoreBootCache.dylib")
        symbol_command = next(pos for pos, cmd, _ in commands(patch_library) if cmd == 2)
        symoff, nsyms, stroff, strsize = struct.unpack_from("<4I", patch_library, symbol_command + 8)
        exported = set()
        for index in range(nsyms):
            string, typ, _, _, _ = struct.unpack_from("<IBBHQ", patch_library, symoff + index * 16)
            if typ == 0xF:
                start = stroff + string
                exported.add(patch_library[start:patch_library.index(b"\0", start, stroff + strsize)])
        assert set(expected_aliases.values()) <= exported, "Missing forwarding exports"
        if model_capture:
            assert b"_CoreModelCaptureStartup" in exported and b"_CoreModelBzeroCapture" in exported
        if model_render:
            assert b"_CoreModelRenderDispatch" in exported and b"_cmdraw" in exported
        assert b"_CoreRemoteTraceTestScope" not in exported, "Test scope override leaked into device build"
        entitlements = signature(main)
        expected_entitlements = plistlib.loads((ROOT / "patch/TrollStore.entitlements.plist").read_bytes())
        assert entitlements == expected_entitlements, "Core signing permissions differ from the reviewed configuration"
        assert entitlements.get("platform-application") is True
        assert entitlements.get("com.apple.private.security.no-sandbox") is True
        assert entitlements.get("com.apple.private.security.storage.AppDataContainers") is True
        assert entitlements.get("com.apple.private.security.container-required") == "qingxiugai.qingxiugai.qinxiugai"
        assert set(entitlements.get("com.apple.security.exception.iokit-user-client-class", [])) == {
            "AGXDeviceUserClient", "AGXSharedUserClient", "AGXGLContext", "AGXCommandQueue", "IOSurfaceRootUserClient"}
        assert not any(key in entitlements for key in ["dynamic-codesigning", "com.apple.private.cs.debugger",
                                                     "com.apple.private.skip-library-validation"])
        main_arch = struct.unpack_from("<II", main, 4)
        probe = archive.read(app + "CoreBootProbe")
        assert struct.unpack_from("<III", probe, 4) == (0x100000C, 0, 2), "Probe must be an independent arm64 executable"
        probe_entitlements = signature(probe)
        assert probe_entitlements == {"platform-application": True, "com.apple.private.security.no-sandbox": True}
        for pos, cmd, size in commands(probe):
            if cmd == 0xC:
                offset = struct.unpack_from("<I", probe, pos + 8)[0]
                path = probe[pos + offset:pos + size].split(b"\0")[0]
                assert not path.startswith((b"@executable_path", b"@loader_path", b"@rpath")), "Probe must not load Core's libraries"
        for name in [app + "Core", *libs]:
            binary = archive.read(name)
            signature(binary)
            assert struct.unpack_from("<II", binary, 4) == main_arch, f"ABI mismatch: {name}"
            if name in original.namelist():
                expected = original.read(name)
                if name == app + "Core":
                    expected = expected_main
                if name == app + "CCCoreLicense.dylib":
                    expected = disable_license_startup(expected)
                assert sections(binary) == sections(expected), f"Unexpected program section change: {name}"
        for name in original.namelist():
            if name.endswith("/") or name.endswith(("Core", ".dylib", "Info.plist")) or "_CodeSignature/" in name:
                continue
            assert archive.read(name) == original.read(name), f"Resource changed: {name}"
        info = plistlib.loads(archive.read(app + "Info.plist"))
        assert info["CoreBootCacheSupportedBuild"] == "21A327"
        expected_build = {"1":"21", "2":"22", "3":"23", "4":"24", "5":"25", "6":"26", "7":"27"}.get(capture_version, BUNDLE_BUILD)
        if model_render:
            expected_build={"1":"28","2":"29","3":"30","4":"31","5":"32","6":"33","7":"34","8":"35","9":"37","10":"38","11":"39","12":"40","13":"41","14":"42","15":"43","16":"44","17":"45","18":"46"}[render_version]
        assert info["CFBundleVersion"] == expected_build and info["CoreBootCachePatchVersion"] == str(PATCH_VERSION)
        expected_profile = PROFILE
        if vehicle_version:
            from nearby_vehicles import PROFILE as expected_profile
        if latency_version:
            from overlay_latency import PROFILE as expected_profile
        if weapon_version:
            from weapon_display import PROFILE as expected_profile
        if essentials_version:
            from ground_essentials import PROFILE as expected_profile
        if late_camera_version:
            from ground_late_camera import PROFILE as expected_profile
        if controls_version:
            from ground_item_controls import PROFILE as expected_profile
        assert info["CoreFeatureProfile"] == expected_profile
        assert info["CoreRemoteTraceVersion"] == "1"
        assert info["CoreLocalLicenseGate"] == "disabled-startup"
        assert info["UIFileSharingEnabled"] is True
        assert info["CFBundleIdentifier"] == plistlib.loads(original.read(app + "Info.plist"))["CFBundleIdentifier"]
    print("PASS: exact minimal-profile payload and selected trace/capture imports; forwarding exports; all other Core/offline sections and cache/kernel instructions; license startup changes; ABI, dependencies, permissions, resources and signatures verified")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
