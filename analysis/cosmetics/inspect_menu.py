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
execution_symbols = []
execution_terms = ['remote', 'invoke', 'rcall', 'kcall', 'task_for_pid', 'task_threads',
                   'thread_create', 'thread_set_state', 'thread_get_state', 'pthread_create',
                   'vm_allocate', 'vm_protect', 'dlopen', 'exception_ports']
for index in range(count):
    name_offset, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, symoff + index * 16)
    end = data.find(b'\0', stroff + name_offset, stroff + strsize)
    name = data[stroff + name_offset:end].decode('utf-8', errors='replace')
    names.append(name)
    if any(term in name.lower() for term in execution_terms):
        execution_symbols.append(dict(name=name, kind=kind, section=section, rva=hex(value - 0x100000000) if value else None))
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
    if name not in ['__stubs', '__auth_stubs']:
        continue
    for index in range(length // reserved2):
        symbol_index = core.u32(indirect[0] + (reserved1 + index) * 4)
        symbol = names[symbol_index]
        if any(term in symbol for term in ['dlsym', 'objc_msgSend', *execution_terms]):
            records.append(dict(symbol=symbol, rva=hex(address + index * reserved2 - 0x100000000),
                                instructions=disassemble(core, address + index * reserved2, reserved2)))
(output / 'native-menu-symbols.json').write_text(json.dumps(dict(stubs=records, symbols=[name for name in names
    if any(term in name for term in ['ImGui', 'Button', 'TextUnformatted', 'Selectable'])],
    prefixes={hex(rva): data[core.offset(0x100000000 + rva):core.offset(0x100000000 + rva) + 32].hex()
        for rva in [0xC7A60, 0xC88B0, 0xC8928, 0x12E620, 0x133168, 0x14F980]},
    ui_constants={hex(rva): list(struct.unpack_from('<2f', data, core.offset(0x100000000 + rva)))
        for rva in [0x8A9E50, 0x8A9EC8]},
    sections=[dict(name=name, address=hex(address), length=hex(length), reserved1=reserved1, reserved2=reserved2)
        for name, address, length, reserved1, reserved2 in sections if 'stub' in name],
    strings={hex(rva): core.string(0x100000000 + rva) for rva in [0x744522, 0x744529, 0x744530]},
    removed_tabs_sha256=hashlib.sha256(data[0xD07E0:0xD097C]).hexdigest()), indent=2))
section = core.cstrings
execution_strings = sorted({value.decode('utf-8', errors='replace')
    for value in data[section['offset']:section['offset'] + section['length']].split(b'\0')
    if 0 < len(value) <= 180 and any(term.encode() in value.lower() for term in execution_terms)})
if len(execution_strings) > 512:
    raise ValueError('Core execution-name review exceeded bounded output')
(output / 'core-execution-candidates.json').write_text(json.dumps(dict(
    status='static-candidates-only', runtime_invocation_verified=False,
    symbols=execution_symbols, strings=execution_strings), indent=2) + '\n')
binary.unlink()
