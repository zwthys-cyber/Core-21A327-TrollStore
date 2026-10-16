#!/usr/bin/env python3
"""Inspect the pinned game and Core binaries without executing either one."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import plistlib
import struct
import zipfile

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

GAME_SHA256 = "93e52106e465717b43ea13c65ff5ee8e5d392696f90fe3bfaa90fd9069aeb280"
GAME_UUID = "34b785b20dab3992985d359e6bf45585"
CORE_SHA256 = "ed69f33902e17c499aa7904751a01ce3521a663503fb8958ea01a7c5489d5c58"
TERMS = [
    "CharacterAvatarComponent", "WeaponAvatarComponent", "AvatarComponent",
    "CharacterAvatar", "WeaponAvatar", "AvatarSyncData", "ClientAvatarData",
    "ItemID", "ItemId", "SlotID", "SkinID", "WeaponSkinID", "AvatarID",
    "AvatarItemID", "ItemDefineID", "DefineID", "TypeSpecificID", "InstanceID",
    "NetAvatarSyncData", "ReplicatedAvatarData", "SlotSyncData", "AvatarSlot",
    "bIsAvatarReady", "OnRep_AvatarMeshChanged", "OnRep_AvatarMeshChangedInner",
    "AvatarMeshChangedCharacter", "AvatarMeshChangedWeapon", "ApplyAvatarData",
    "PutOnEquipByAvatarItemID", "PutOffEquipByAvatarItemID", "GetAvatarComponent",
    "GetWeaponSkinID", "GetSkinID", "ChangeAvatar", "ItemList", "AvatarList",
    "BackpackSkinID", "HelmetSkinID", "WeaponAvatarData", "WeaponEntityComp",
    "WeaponManagerComponent", "CurrentWeaponReplicated", "LocalPlayerController",
    "AcknowledgedPawn", "CharacterAvatarComp", "NetAvatarSyncData_PushModel",
]


class MachO:
    def __init__(self, path):
        self.file = Path(path).open("rb")
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        if self.u32(0) != 0xFEEDFACF or self.u32(4) != 0x100000C:
            raise ValueError("Expected thin ARM64 Mach-O")
        self.segments = []
        self.sections = []
        self.uuid = None
        self.encryption = []
        self.symbols = None
        position = 32
        command_end = 32 + self.u32(20)
        if command_end > len(self.data):
            raise ValueError("Truncated Mach-O load commands")
        for _ in range(self.u32(16)):
            kind, size = struct.unpack_from("<II", self.data, position)
            if size < 8 or position + size > command_end:
                raise ValueError("Invalid load command")
            if kind == 0x19:
                name = self.data[position + 8:position + 24].split(b"\0")[0].decode()
                address, virtual_size, offset, length = struct.unpack_from("<4Q", self.data, position + 24)
                self.segments.append(dict(name=name, address=address, virtual_size=virtual_size,
                                          offset=offset, length=length))
                for index in range(self.u32(position + 64)):
                    start = position + 72 + index * 80
                    if start + 80 > position + size:
                        raise ValueError("Invalid section table")
                    section_name = self.data[start:start + 16].split(b"\0")[0].decode()
                    section_address, section_length = struct.unpack_from("<QQ", self.data, start + 32)
                    self.sections.append(dict(segment=name, name=section_name, address=section_address,
                                              offset=self.u32(start + 48), length=section_length))
            elif kind == 0x1B:
                self.uuid = self.data[position + 8:position + 24].hex()
            elif kind in (0x21, 0x2C):
                self.encryption.append(dict(zip(["offset", "length", "cryptid"],
                                               struct.unpack_from("<III", self.data, position + 8))))
            elif kind == 2:
                self.symbols = struct.unpack_from("<4I", self.data, position + 8)
            position += size
        if position != command_end:
            raise ValueError("Load command length mismatch")
        self.text = next(s for s in self.sections if s["name"] == "__text")
        self.cstrings = next(s for s in self.sections if s["name"] == "__cstring")

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def qword(self, offset):
        return struct.unpack_from("<Q", self.data, offset)[0]

    def offset(self, address):
        for segment in self.segments:
            delta = address - segment["address"]
            if 0 <= delta < segment["length"]:
                return segment["offset"] + delta
        raise ValueError(f"Unmapped address {address:#x}")

    def address(self, offset):
        for segment in self.segments:
            delta = offset - segment["offset"]
            if 0 <= delta < segment["length"]:
                return segment["address"] + delta
        raise ValueError(f"Unmapped file offset {offset:#x}")

    def string(self, address):
        section = self.cstrings
        if not section["address"] <= address < section["address"] + section["length"]:
            return None
        offset = self.offset(address)
        end = self.data.find(b"\0", offset, min(offset + 512, len(self.data)))
        if end < 0:
            return None
        try:
            return self.data[offset:end].decode("utf-8")
        except UnicodeDecodeError:
            return None

    def code_address(self, value):
        return self.text["address"] <= value < self.text["address"] + self.text["length"] and value % 4 == 0

    def string_addresses(self, name):
        start = self.cstrings["offset"]
        end = start + self.cstrings["length"]
        cursor = start
        needle = name.encode() + b"\0"
        while True:
            hit = self.data.find(needle, cursor, end)
            if hit < 0:
                return
            # Literal suffix merging is permitted; retain suffix candidates too.
            yield self.address(hit)
            cursor = hit + 1

    def references(self, address):
        needle = struct.pack("<Q", address)
        for segment in self.segments:
            if segment["name"] != "__DATA":
                continue
            cursor = segment["offset"]
            end = cursor + segment["length"]
            while True:
                hit = self.data.find(needle, cursor, end)
                if hit < 0:
                    break
                if hit % 8 == 0:
                    yield hit
                cursor = hit + 1

    def describe(self):
        return dict(uuid=self.uuid, sha256=hashlib.sha256(self.data).hexdigest(),
                    size=len(self.data), encryption=self.encryption,
                    segments=self.segments, sections=self.sections)


def inspect_properties(image):
    properties = []
    functions = []
    seen = set()
    for name in TERMS:
        for string_address in image.string_addresses(name):
            for hit in image.references(string_address):
                if hit in seen:
                    continue
                seen.add(hit)
                # Preserve raw evidence. Ownership and the meaning of the offset
                # require constructor/code review and device validation.
                words = [image.qword(hit + 8 * i) for i in range(-2, 9)]
                if words[3] == 0x45 and 1 <= words[1] <= 0x40 and words[5] <= 4096:
                    record = dict(name=name, record_address=hex(image.address(hit - 8)),
                                  name_address=hex(string_address), record_tag=words[1],
                                  object_flags=hex(words[3]), property_flags=hex(words[4]),
                                  array_dimension=words[5], rep_notify=image.string(words[6]),
                                  candidate_offset=hex(words[7]), raw_qwords=[hex(v) for v in words],
                                  owner=None, runtime_validated=False)
                    if image.code_address(words[8]):
                        record["type_constructor_candidate"] = hex(words[8])
                    properties.append(record)
                following = words[3]
                if image.code_address(following):
                    functions.append(dict(name=name, table_address=hex(image.address(hit)),
                                          code_address=hex(following), runtime_validated=False))
    return properties, functions


def disassemble(image, address, length):
    offset = image.offset(address)
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    return [f"{pc:#x}: {mnemonic:<9} {operands}"
            for pc, size, mnemonic, operands in decoder.disasm_lite(image.data[offset:offset + length], address)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--core-ipa", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    plist = plistlib.loads((args.inputs / "Info.plist").read_bytes())
    expected = {"CFBundleIdentifier": "com.tencent.tmgp.pubgmhd",
                "CFBundleShortVersionString": "1.38.12", "CFBundleVersion": "15915",
                "CFBundleExecutable": "ShadowTrackerExtra"}
    if any(plist.get(key) != value for key, value in expected.items()):
        raise ValueError("Game Info.plist does not match the reviewed input")
    game = MachO(args.inputs / "ShadowTrackerExtra")
    description = game.describe()
    if description["sha256"] != GAME_SHA256 or game.uuid != GAME_UUID or any(e["cryptid"] for e in game.encryption):
        raise ValueError("Unreviewed or encrypted game input")
    core_path = args.output / "Core-analysis-input"
    with zipfile.ZipFile(args.core_ipa) as archive:
        core_path.write_bytes(archive.read("Payload/Core.app/Core"))
    core = MachO(core_path)
    if hashlib.sha256(core.data).hexdigest() != CORE_SHA256:
        raise ValueError("Unexpected original Core executable")
    properties, functions = inspect_properties(game)
    report = dict(schema_version=1, status="static-candidates-only", kernel_writes_implemented=False,
                  identity=expected, game=description, property_candidates=properties,
                  function_candidates=functions,
                  unresolved=["property ownership and ABI", "runtime object and item ID validation",
                              "reviewed Core target-process write interface", "appearance refresh semantics"])
    # Evidence for the small, read-only device adapter. These bytes are checked
    # again in the final IPA in Actions and before invoking an original entry.
    report["core_adapter_evidence"] = {
        "uuid": core.uuid,
        "entries": {name: {"rva": hex(rva), "prefix": core.data[core.offset(0x100000000 + rva):
                         core.offset(0x100000000 + rva) + 32].hex()}
                    for name, rva in [("snapshot", 0x64C7C), ("read", 0x64D04), ("compare_write", 0x620B8)]},
        "static_configuration": [hex(core.qword(core.offset(0x100AC83F0) + 8 * i)) for i in range(75)],
    }
    report["reviewed_struct_candidates"] = []
    for label, address in [("MeshSynData", 0x10EDC1580), ("ItemDefineID", 0x10FDD8F98)]:
        offset = game.offset(address)
        words = [game.qword(offset + 8 * i) for i in range(10)]
        record = dict(name=label, parameters=hex(address), raw_qwords=[hex(v) for v in words],
                      runtime_validated=False, properties=[])
        if words[8] > 32:
            raise ValueError("Unexpected reviewed structure property count")
        for i in range(words[8]):
            pointer = game.qword(game.offset(words[7]) + 8 * i)
            prop = [game.qword(game.offset(pointer) + 8 * j) for j in range(9)]
            record["properties"].append(dict(address=hex(pointer), name=game.string(prop[1]),
                                              raw_qwords=[hex(v) for v in prop]))
        report["reviewed_struct_candidates"].append(record)
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    windows = []
    for record in properties:
        if "type_constructor_candidate" in record and record["name"] in ["AvatarComponent", "CharacterAvatarComponent", "WeaponAvatarComponent"]:
            windows.append((f"game constructor candidate for {record['name']}", game,
                            int(record["type_constructor_candidate"], 16), 0x180))
    for record in functions:
        windows.append((f"game native table: {record['name']}", game, int(record["code_address"], 16), 0x180))
    core_base = next(s["address"] for s in core.segments if s["name"] == "__TEXT")
    for relative, length in [(0xD4A08, 0x500), (0xD59D8, 0x180), (0xC1A04, 0x180)]:
        windows.append((f"original Core at {relative:#x}", core, core_base + relative, length))
    window_config = json.loads(Path(__file__).with_name("windows.json").read_text())
    for key, image in [("core", core), ("game", game)]:
        base = next(s["address"] for s in image.segments if s["name"] == "__TEXT")
        for entry in window_config[key]:
            windows.append((f"{key}: {entry['label']}", image,
                            base + int(entry["offset"], 16), int(entry["length"], 16)))
    lines = []
    visited = set()
    for label, image, address, length in windows:
        key = (image is core, address, length)
        if key in visited:
            continue
        visited.add(key)
        lines += ["", f"### {label} ({address:#x})"] + disassemble(image, address, length)
    (args.output / "disassembly.txt").write_text("\n".join(lines) + "\n")
    core.data.close()
    core.file.close()
    core_path.unlink()
    print(json.dumps(dict(identity=expected, property_candidates=len(properties),
                          function_candidates=len(functions), disassembly_windows=len(visited))))


if __name__ == "__main__":
    main()
