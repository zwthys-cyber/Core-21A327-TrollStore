#!/usr/bin/env python3
"""Check instruction preservation, load dependencies, architectures and TrollStore signing."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import sys
import zipfile
from build import commands, LOAD_PATH, CORE_UUID, ROOT, validate_patch_dependency, disable_license_startup


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
        entitlements = signature(main)
        assert entitlements.get("com.apple.private.security.no-sandbox") is True
        assert entitlements.get("com.apple.private.security.storage.AppDataContainers") is True
        assert entitlements.get("com.apple.private.security.container-required") == "qingxiugai.qingxiugai.qinxiugai"
        assert not any(key in entitlements for key in ["dynamic-codesigning", "com.apple.private.cs.debugger",
                                                     "com.apple.private.skip-library-validation"])
        main_arch = struct.unpack_from("<II", main, 4)
        for name in [app + "Core", *libs]:
            binary = archive.read(name)
            signature(binary)
            assert struct.unpack_from("<II", binary, 4) == main_arch, f"ABI mismatch: {name}"
            if name in original.namelist():
                expected = original.read(name)
                if name == app + "CCCoreLicense.dylib":
                    expected = disable_license_startup(expected)
                assert sections(binary) == sections(expected), f"Unexpected program section change: {name}"
        for name in original.namelist():
            if name.endswith("/") or name.endswith(("Core", ".dylib", "Info.plist")) or "_CodeSignature/" in name:
                continue
            assert archive.read(name) == original.read(name), f"Resource changed: {name}"
        info = plistlib.loads(archive.read(app + "Info.plist"))
        assert info["CoreBootCacheSupportedBuild"] == "21A327"
        assert info["CFBundleVersion"] == "5" and info["CoreBootCachePatchVersion"] == "4"
        assert info["CoreLocalLicenseGate"] == "disabled-startup"
        assert info["UIFileSharingEnabled"] is True
        assert info["CFBundleIdentifier"] == plistlib.loads(original.read(app + "Info.plist"))["CFBundleIdentifier"]
    print("PASS: only the two local license startup instructions changed; original Core and offline sections, ABI, dependencies, signed entitlements and resources verified")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
