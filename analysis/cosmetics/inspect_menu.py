#!/usr/bin/env python3
"""Cloud-only review of the pinned Core menu and its UI/import helpers."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile
from analyze import MachO, CORE_SHA256, disassemble

root = Path(__file__).resolve().parents[2]
output = root / 'menu-output'
output.mkdir(exist_ok=True)
with zipfile.ZipFile(root / 'input/Core.ipa') as archive:
    data = archive.read('Payload/Core.app/Core')
if hashlib.sha256(data).hexdigest() != CORE_SHA256:
    raise ValueError('Unreviewed Core input')
binary = output / 'Core-input'
binary.write_bytes(data)
core = MachO(binary)
windows = json.loads(Path(__file__).with_name('menu-windows.json').read_text())
lines = []
for window in windows:
    address = 0x100000000 + int(window['rva'], 16)
    lines.append(f"\n### {window['label']} {address:#x}")
    lines.extend(disassemble(core, address, int(window['length'], 16)))
(output / 'native-menu.txt').write_text('\n'.join(lines) + '\n')
symoff, count, stroff, strsize = core.symbols
names = []
for index in range(count):
    name_offset, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, symoff + index * 16)
    end = data.find(b'\0', stroff + name_offset, stroff + strsize)
    names.append(data[stroff + name_offset:end].decode('utf-8', errors='replace'))
records = []
position = 32
sections = []
indirect = None
for _ in range(core.u32(16)):
    command, size = struct.unpack_from('<II', data, position)
    if command == 0xB:
        indirect = struct.unpack_from('<II', data, position + 56)
    if command == 0x19:
        for index in range(core.u32(position + 64)):
            start = position + 72 + index * 80
            sections.append((data[start:start + 16].split(b'\0')[0].decode(),
                             *struct.unpack_from('<QQ', data, start + 32),
                             *struct.unpack_from('<II', data, start + 68)))
    position += size
for name, address, length, reserved1, reserved2 in sections:
    if name != '__stubs':
        continue
    for index in range(length // reserved2):
        symbol_index = core.u32(indirect[0] + (reserved1 + index) * 4)
        symbol = names[symbol_index]
        if any(term in symbol for term in ['dlsym', 'objc_msgSend']):
            records.append(dict(symbol=symbol, rva=hex(address + index * reserved2 - 0x100000000),
                                instructions=disassemble(core, address + index * reserved2, reserved2)))
(output / 'native-menu-symbols.json').write_text(json.dumps(dict(stubs=records, symbols=[name for name in names
    if any(term in name for term in ['ImGui', 'Button', 'TextUnformatted', 'Selectable'])]), indent=2))
binary.unlink()
