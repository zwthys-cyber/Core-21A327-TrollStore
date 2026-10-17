#!/usr/bin/env python3
"""Cloud-only review of the pinned Core menu and its UI/import helpers."""
import hashlib
import json
from bisect import bisect_right
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
function_starts_data = None
for _ in range(core.u32(16)):
    command, size = struct.unpack_from('<II', data, position)
    if command == 0xB:
        indirect = struct.unpack_from('<II', data, position + 56)
    if command == 0x26:
        function_starts_data = struct.unpack_from('<II', data, position + 8)
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
starts = []
if function_starts_data:
    cursor, length = function_starts_data
    limit = cursor + length
    address = 0x100000000
    while cursor < limit:
        delta = shift = 0
        while True:
            if cursor >= limit or shift > 63:
                raise ValueError('Invalid Core function-start encoding')
            value = data[cursor]
            cursor += 1
            delta |= (value & 127) << shift
            shift += 7
            if not value & 128:
                break
        if not delta:
            break
        address += delta
        if not core.code_address(address):
            raise ValueError('Core function start outside reviewed code')
        starts.append(address)
literal_targets = {address: name for name in ['remote_call', 'remote-call-quarantined', 'invoke']
                   for address in core.string_addresses(name)}
stub_targets = {0x100000000 + int(row['rva'], 16): row['symbol'] for row in records
                if any(term in row['symbol'] for term in execution_terms)}
execution_references = []
code = data[core.text['offset']:core.text['offset'] + (core.text['length'] & ~3)]
for index, (word,) in enumerate(struct.iter_unpack('<I', code)):
    pc = core.text['address'] + index * 4
    target, label = None, None
    if word & 0x9F000000 == 0x90000000:
        relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
        if relative & (1 << 20):
            relative -= 1 << 21
        page, register = (pc & ~4095) + relative * 4096, word & 31
        if any(0 <= address - page < 4096 for address in literal_targets):
            for delta in range(1, 9):
                offset = (index + delta) * 4
                if offset + 4 > len(code):
                    break
                following = struct.unpack_from('<I', code, offset)[0]
                if following & 0xFFC00000 == 0x91000000 and ((following >> 5) & 31) == register:
                    candidate = page + ((following >> 10) & 4095)
                    if candidate in literal_targets:
                        target, label = candidate, literal_targets[candidate]
                        break
    if word & 0xFC000000 in (0x14000000, 0x94000000):
        relative = word & 0x3FFFFFF
        if relative & (1 << 25):
            relative -= 1 << 26
        candidate = pc + relative * 4
        if candidate in stub_targets:
            target, label = candidate, stub_targets[candidate]
    if target is not None:
        position = bisect_right(starts, pc) - 1
        start = starts[position] if position >= 0 else None
        end = starts[position + 1] if position >= 0 and position + 1 < len(starts) else None
        execution_references.append(dict(label=label, address=hex(pc), target=hex(target),
            function_start=hex(start) if start else None, function_end=hex(end) if end else None))
review_functions = sorted({int(row['function_start'], 16) for row in execution_references if row['function_start']})
execution_lines = []
for address in review_functions[:64]:
    index = bisect_right(starts, address)
    end = starts[index] if index < len(starts) else address + 0x1000
    execution_lines += [f'\n### Core execution candidate {address:#x}'] + disassemble(core, address, min(end - address, 0x1800))
for row in execution_references:
    if row['label'] in literal_targets.values():
        address = int(row['address'], 16)
        execution_lines += [f'\n### Core {row["label"]} reference {address:#x}'] + disassemble(core, address - 0x100, 0x500)
(output / 'core-execution.txt').write_text('\n'.join(execution_lines) + '\n')
(output / 'core-execution-candidates.json').write_text(json.dumps(dict(
    status='static-candidates-only', runtime_invocation_verified=False,
    symbols=execution_symbols, strings=execution_strings, references=execution_references,
    function_windows_limited_to=64, function_candidates=len(review_functions)), indent=2) + '\n')
binary.unlink()
