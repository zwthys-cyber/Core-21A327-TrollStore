"""Redirect three imports without moving code, fixups, strings, or stub indices."""
import struct
from build import commands, CORE_UUID, LOAD_PATH

ALIASES = {
    b"_mach_msg": b"_cbr_msg_",
    b"_thread_set_exception_ports": b"_cbr_set_exception_ports___",
    b"_thread_resume": b"_cbr_th_resume",
}
MODEL_CAPTURE_ALIASES = {b"_bzero": b"_cmbzr"}


def records(data):
    """Read the analyzed chained-import and nlist tables, rejecting other layouts."""
    chained = symbols = None
    uuid = None
    libraries = []
    for pos, command, size in commands(data):
        if command == 0x1B:
            uuid = bytes(data[pos + 8:pos + 24])
        elif command in (0xC, 0x80000018, 0x8000001F, 0x80000023):
            start = struct.unpack_from("<I", data, pos + 8)[0]
            libraries.append(bytes(data[pos + start:pos + size]).split(b"\0")[0])
        elif command == 0x80000034:
            if chained is not None:
                raise ValueError("Duplicate chained fixups")
            chained = struct.unpack_from("<II", data, pos + 8)
        elif command == 0x2:
            symbols = struct.unpack_from("<4I", data, pos + 8)
        elif command in (0x22, 0x80000022):
            raise ValueError("Unexpected legacy binding table")
    if uuid != CORE_UUID or chained is None or symbols is None:
        raise ValueError("Unsupported Core import layout")
    offset, length = chained
    if length < 28 or offset + length > len(data):
        raise ValueError("Truncated fixup table")
    version, _, imports, strings, count, kind, compressed = struct.unpack_from("<7I", data, offset)
    if version != 0 or kind != 2 or compressed != 0 or imports < 28 or imports + count * 8 > strings or strings >= length:
        raise ValueError("Unsupported chained import encoding")

    def name_at(start, end):
        if not 0 <= start < end <= len(data):
            raise ValueError("Invalid import string offset")
        stop = data.find(b"\0", start, end)
        if stop < 0:
            raise ValueError("Unterminated import name")
        return bytes(data[start:stop])

    chain_records = []
    for index in range(count):
        pos = offset + imports + index * 8
        value, addend = struct.unpack_from("<Ii", data, pos)
        start = offset + strings + (value >> 9)
        chain_records.append(dict(name=name_at(start, offset + length), pos=pos, start=start,
                                  ordinal=value & 255, weak=(value >> 8) & 1, addend=addend))
    symoff, nsyms, stroff, strsize = symbols
    if symoff + nsyms * 16 > len(data) or stroff + strsize > len(data):
        raise ValueError("Truncated symbol table")
    symbol_records = []
    for index in range(nsyms):
        pos = symoff + index * 16
        string, typ, section, desc, address = struct.unpack_from("<IBBHQ", data, pos)
        start = stroff + string
        symbol_records.append(dict(name=name_at(start, stroff + strsize), pos=pos, start=start,
            ordinal=desc >> 8, typ=typ, section=section, address=address))
    return libraries, chain_records, symbol_records


def _redirect_imports(data, aliases):
    libraries, chains, symbols = records(data)
    if len(libraries) != 52 or libraries[-1] != LOAD_PATH[:-1] or libraries[27] != b"/usr/lib/libSystem.B.dylib":
        raise ValueError("Unexpected trace library ordinal")
    result = bytearray(data)
    for old, alias in aliases.items():
        if len(alias) != len(old) or any(r["name"] == alias for r in chains + symbols):
            raise ValueError("Import alias collision")
        for table, is_chain in [(chains, True), (symbols, False)]:
            entries = [r for r in table if r["name"] == old]
            if len(entries) != 1:
                raise ValueError("Expected one original import per table")
            entry = entries[0]
            if entry["ordinal"] != 28:
                raise ValueError("Original import does not bind libSystem")
            if is_chain and (entry["weak"] or entry["addend"]):
                raise ValueError("Unexpected weak import or addend")
            if not is_chain and (entry["typ"] != 1 or entry["section"] or entry["address"]):
                raise ValueError("Expected undefined external symbol")
            start, end = entry["start"], entry["start"] + len(old)
            if any(r is not entry and r["start"] < end and r["start"] + len(r["name"]) > start for r in table):
                raise ValueError("Shared import string overlaps replacement")
            result[start:end] = alias
            if is_chain:
                value = struct.unpack_from("<I", data, entry["pos"])[0]
                struct.pack_into("<I", result, entry["pos"], (value & ~255) | 52)
            else:
                desc = struct.unpack_from("<H", data, entry["pos"] + 6)[0]
                struct.pack_into("<H", result, entry["pos"] + 6, (desc & 255) | (52 << 8))
    return bytes(result)


def redirect_remote_imports(data):
    return _redirect_imports(data, ALIASES)


def redirect_model_capture_import(data):
    return _redirect_imports(data, MODEL_CAPTURE_ALIASES)
