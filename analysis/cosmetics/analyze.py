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
from capstone.arm64 import ARM64_OP_MEM

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
    "UAEDataTable", "UsedCompressedRowData",
    "LobbyCharacter", "LobbyPawn", "PreviewCharacter", "LobbyAvatarComponent",
    "GetLobbyCharacter", "GetPreviewCharacter", "LobbyPlayerController", "LobbyCharacterManager",
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
    cstrings = game.data[game.cstrings['offset']:game.cstrings['offset'] + game.cstrings['length']]
    report['lobby_name_candidates'] = sorted({name.decode('ascii') for name in cstrings.split(b'\0')
        if len(name) <= 160 and b'Lobby' in name and all(32 <= c < 127 for c in name) and
        any(term in name for term in [b'Character', b'Avatar', b'Preview', b'PlayerController'])})[:300]
    # Evidence for the small, read-only device adapter. These bytes are checked
    # again in the final IPA in Actions and before invoking an original entry.
    report["core_adapter_evidence"] = {
        "uuid": core.uuid,
        "entries": {name: {"rva": hex(rva), "prefix": core.data[core.offset(0x100000000 + rva):
                         core.offset(0x100000000 + rva) + 32].hex()}
                    for name, rva in [("snapshot", 0x64C7C), ("read", 0x64D04), ("compare_write", 0x620B8)]},
        "static_configuration": [hex(core.qword(core.offset(0x100AC83F0) + 8 * i)) for i in range(75)],
    }
    menu_symbols = []
    symoff, count, stroff, strsize = core.symbols
    for index in range(count):
        name_offset, kind, section, desc, value = struct.unpack_from('<IBBHQ', core.data, symoff + index * 16)
        if name_offset >= strsize:
            raise ValueError('Invalid Core symbol name offset')
        end = core.data.find(b'\0', stroff + name_offset, stroff + strsize)
        name = core.data[stroff + name_offset:end].decode('utf-8', errors='replace')
        if any(term in name for term in ['dlsym', 'ImGui', 'Button', 'TextUnformatted', 'Selectable']):
            menu_symbols.append(dict(name=name, kind=kind, section=section, value=hex(value)))
    report['core_menu_symbols'] = menu_symbols
    report["reviewed_struct_candidates"] = []
    trial_profile = json.loads(Path(__file__).with_name('trial-profile.json').read_text())
    trial_profile['refresh_mode_static_default'] = game.data[game.offset(0x111069CD5)]
    for address, slot in [(0x10E6189A8 + 0x1320, 'onrep_rva'), (0x10E6189A8 + 0xC68, 'deferred_update_rva'),
                          (0x10E6189A8 + 0x4C0, 'tick_rva')]:
        if game.qword(game.offset(address)) != 0x100000000 + int(trial_profile[slot], 16):
            raise ValueError('Trial refresh virtual method differs from reviewed profile')
    trial_profile['tick_pending_read'] = disassemble(game, 0x1043D6D78, 4)
    trial_profile['deferred_onrep_call'] = disassemble(game, 0x1043D265C, 0x14)
    trial_profile['native_reset'] = disassemble(game, 0x1042E8C34, 0x100)
    report['trial_profile'] = trial_profile
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
    clusters = []
    for label, start, end in [("character avatar", 0x10EFD4000, 0x10EFD6D00),
                              ("weapon avatar", 0x10FD10000, 0x10FD19000)]:
        rows = []
        for address in range(start, end, 8):
            offset = game.offset(address)
            words = [game.qword(offset + 8 * i) for i in range(9)]
            name = game.string(words[1])
            if name and 0 < words[0] <= 0x40 and words[2] == 0x45 and 0 < words[4] <= 4096:
                rows.append(dict(name=name, address=hex(address), tag=words[0],
                                 offset_candidate=hex(words[6]), raw_qwords=[hex(v) for v in words]))
        clusters.append(dict(label=label, properties=rows))
    (args.output / "avatar-property-clusters.json").write_text(json.dumps(clusters, indent=2) + "\n")
    # Resolve ADRP+ADD references to a small group of serialization strings.
    # This instruction scan runs only on the Actions runner.
    targets = {}
    for term in ["UAEDataTable", "UsedCompressedRowData", "CompressedRowData", "OnRep_AvatarMeshChanged"]:
        for address in game.string_addresses(term):
            targets[address] = term
    for term in ["UAEDataTable"]:
        needle = term.encode('utf-16-le') + b'\0\0'
        cursor = 0
        while (hit := game.data.find(needle, cursor)) >= 0:
            targets[game.address(hit)] = term + ' (UTF16)'
            cursor = hit + len(needle)
    pages = {address & ~4095 for address in targets}
    references = []
    code = game.data[game.text['offset']:game.text['offset'] + game.text['length']]
    for index, (word,) in enumerate(struct.iter_unpack('<I', code[:len(code) & ~3])):
        if word & 0x9F000000 != 0x90000000:
            continue
        pc = game.text['address'] + index * 4
        immediate = ((word >> 5) & 0x7FFFF) << 2 | ((word >> 29) & 3)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        page = (pc & ~4095) + (immediate << 12)
        if page not in pages:
            continue
        register = word & 31
        for distance in range(1, 5):
            if index * 4 + distance * 4 + 4 > len(code):
                break
            following = struct.unpack_from('<I', code, index * 4 + distance * 4)[0]
            if following & 0xFFC00000 != 0x91000000 or (following >> 5) & 31 != register:
                continue
            target = page + ((following >> 10) & 4095)
            if target in targets:
                references.append(dict(name=targets[target], instruction=hex(pc), target=hex(target)))
    (args.output / "serialization-code-references.json").write_text(json.dumps(references, indent=2) + "\n")
    windows = []
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    decoder.detail = True
    field_references = []
    region = 0x10439F000
    offset = game.offset(region)
    for instruction in decoder.disasm(game.data[offset:offset + 0x61000], region):
        for operand in instruction.operands:
            if operand.type == ARM64_OP_MEM and operand.mem.disp in [0xF50, 0xF7A, 0x18C8, 0x11A0]:
                field_references.append(dict(address=hex(instruction.address), offset=hex(operand.mem.disp),
                    instruction=f'{instruction.mnemonic} {instruction.op_str}'))
                windows.append((f'character appearance field {operand.mem.disp:#x}', game,
                                instruction.address - 0x80, 0x280))
    (args.output / 'avatar-refresh-field-references.json').write_text(json.dumps(field_references, indent=2) + '\n')
    virtual_tables = []
    for label, address, length in [("character avatar", 0x10E6189A8, 0x13A8),
                                    ("weapon avatar", 0x10E6DEAB0, 0x1008)]:
        slots = []
        for slot in range(0, length, 8):
            function = game.qword(game.offset(address + slot))
            slots.append(dict(slot=hex(slot), function=hex(function), executable=game.code_address(function)))
            if label == 'character avatar' and slot == 0x1320:
                if not game.code_address(function):
                    raise ValueError('Unreviewed character OnRep table entry')
                windows.append(('character OnRep implementation', game, function, 0x1800))
        virtual_tables.append(dict(label=label, table=hex(address), slots=slots))
    (args.output / 'avatar-virtual-tables.json').write_text(json.dumps(virtual_tables, indent=2) + '\n')
    for record in references:
        windows.append((f"game serialization reference: {record['name']}", game,
                        max(game.text['address'], int(record['instruction'], 16) - 0x80), 0x300))
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
