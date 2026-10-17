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
    length = int(window['length'], 16)
    if window.get('kind') == 'cstring':
        section = core.cstrings
        if length > 0x2000 or not (section['address'] <= address and
                address + length <= section['address'] + section['length']):
            raise ValueError('Core literal review outside bounded cstring range')
        offset = core.offset(address)
        cursor = 0
        for value in data[offset:offset + length].split(b'\0'):
            if value:
                lines.append(f'{address + cursor:#x}: {value.decode("utf-8", errors="replace")!r}')
            cursor += len(value) + 1
    else:
        lines.extend(disassemble(core, address, length))
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
process_literals = {}
for name in ['SpringBoard', 'launchd', 'ShadowTrackerExtra', 'backboardd', 'com.tencent.tmgp.pubgmhd']:
    found = 0
    for address in core.string_addresses(name):
        process_literals[address] = name
        found += 1
        if found == 4:
            break
code_literals = dict(literal_targets)
code_literals.update(process_literals)
literal_data_references = []
for segment in core.segments:
    if not segment['name'].startswith('__DATA'):
        continue
    for offset in range(segment['offset'], segment['offset'] + segment['length'] - 7, 8):
        value = core.qword(offset)
        for address, name in literal_targets.items():
            if value != address and (value & 0xFFFFFFFF) != address - 0x100000000:
                continue
            context = []
            for cursor in range(max(segment['offset'], offset - 48), min(offset + 56, segment['offset'] + segment['length']), 8):
                word = core.qword(cursor)
                candidate = 0x100000000 + (word & 0xFFFFFFFF)
                context.append(dict(address=hex(core.address(cursor)), raw=hex(word),
                    string=core.string(word), code=core.code_address(word),
                    low32_code_candidate=hex(candidate) if core.code_address(candidate) else None))
            literal_data_references.append(dict(name=name, address=hex(core.address(offset)), raw=hex(value), context=context))
            if len(literal_data_references) > 128:
                raise ValueError('Core literal pointer review exceeded bounded output')
stub_targets = {0x100000000 + int(row['rva'], 16): row['symbol'] for row in records
                if any(term in row['symbol'] for term in execution_terms)}
stub_targets.update({0x100000000 + rva: label for rva, label in [
    (0x38D80, 'Core.current_remote_context_candidate'),
    (0x398C8, 'Core.remote_name_call_candidate'),
    (0x39A80, 'Core.remote_function_call_candidate'),
    (0x3D624, 'Core.remote_objc_call_candidate'),
    (0x3800, 'Core.remote_bootstrap_veneer'),
    (0x3A7D4, 'Core.remote_bootstrap_entry'),
    (0x3A7D8, 'Core.remote_bootstrap_body'),
    (0x39F00, 'Core.remote_session_teardown'),
    (0x3B7F4, 'Core.remote_thread_state_writer'),
    (0x2E76C, 'Core.launchd_session_wrapper'),
]})
execution_references = []
process_reference_count = 0
code = data[core.text['offset']:core.text['offset'] + (core.text['length'] & ~3)]
for index, (word,) in enumerate(struct.iter_unpack('<I', code)):
    pc = core.text['address'] + index * 4
    target, label = None, None
    if word & 0x9F000000 == 0x90000000:
        relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
        if relative & (1 << 20):
            relative -= 1 << 21
        page, register = (pc & ~4095) + relative * 4096, word & 31
        if any(0 <= address - page < 4096 for address in code_literals):
            for delta in range(1, 9):
                offset = (index + delta) * 4
                if offset + 4 > len(code):
                    break
                following = struct.unpack_from('<I', code, offset)[0]
                if following & 0xFFC00000 == 0x91000000 and ((following >> 5) & 31) == register:
                    candidate = page + ((following >> 10) & 4095)
                    if candidate in code_literals:
                        target, label = candidate, code_literals[candidate]
                        break
    if word & 0xFC000000 in (0x14000000, 0x94000000):
        relative = word & 0x3FFFFFF
        if relative & (1 << 25):
            relative -= 1 << 26
        candidate = pc + relative * 4
        if candidate in stub_targets:
            target, label = candidate, stub_targets[candidate]
    if target is not None:
        if label in process_literals.values():
            if process_reference_count >= 16:
                continue
            process_reference_count += 1
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
def bounded_text(address, before, total):
    if not core.code_address(address):
        return []
    start = max(core.text['address'], (address - before) & ~3)
    end = min(core.text['address'] + (core.text['length'] & ~3), start + total)
    if end <= start:
        return []
    return disassemble(core, start, end - start)

for row in execution_references:
    if row['label'] in literal_targets.values():
        address = int(row['address'], 16)
        execution_lines += [f'\n### Core {row["label"]} reference {address:#x}'] + bounded_text(address, 0x100, 0x500)
veneer = 0x100003800
veneer_section = next((dict(segment=section['segment'], name=section['name'], address=hex(section['address']),
    length=hex(section['length'])) for section in core.sections
    if section['address'] <= veneer < section['address'] + section['length']), None)
veneer_segment = next((segment['name'] for segment in core.segments
    if segment['address'] <= veneer < segment['address'] + segment['virtual_size']), None)
veneer_offset = next((segment['offset'] + (veneer - segment['address']) for segment in core.segments
    if segment['address'] <= veneer < segment['address'] + segment['length']), None)
veneer_raw = data[veneer_offset:veneer_offset + 0x40] if veneer_offset is not None else b''
bootstrap_veneer = dict(address=hex(veneer), inside_text=core.code_address(veneer),
    segment=veneer_segment, section=veneer_section, raw_hex=veneer_raw.hex())
if bootstrap_veneer['inside_text']:
    execution_lines += ['\n### Core remote bootstrap veneer 0x100003800'] + bounded_text(veneer, 0, 0x180)
elif veneer_offset is not None:
    execution_lines += ['\n### Core remote bootstrap veneer outside text 0x100003800'] + disassemble(core, veneer, 0x40)
session_labels = {'Core.remote_bootstrap_veneer', 'Core.remote_bootstrap_entry', 'Core.remote_bootstrap_body',
    'Core.remote_session_teardown', 'Core.remote_thread_state_writer', 'Core.launchd_session_wrapper'}
session_rows = [row for row in execution_references if row['label'] in session_labels][:40]
for row in session_rows:
    address = int(row['address'], 16)
    execution_lines += [f'\n### Core {row["label"]} caller {address:#x}'] + bounded_text(address, 0x120, 0x280)
def literal_record(address):
    section = core.cstrings
    if not section['address'] <= address < section['address'] + section['length']:
        return dict(address=hex(address), text=None)
    offset = core.offset(address)
    prefix = offset
    while prefix > section['offset'] and offset - prefix < 64 and data[prefix - 1] != 0:
        prefix -= 1
    end = data.find(b'\0', offset, min(offset + 96, section['offset'] + section['length']))
    blob = data[prefix:end if end >= 0 else offset]
    return dict(address=hex(address), text=blob.decode('utf-8', 'replace'), match_offset=offset - prefix)

process_literal_records = {name: [literal_record(address) for address, label in process_literals.items() if label == name]
    for name in ['SpringBoard', 'launchd', 'ShadowTrackerExtra', 'backboardd', 'com.tencent.tmgp.pubgmhd']}
process_rows = [row for row in execution_references if row['label'] in process_literals.values()][:8]
for row in process_rows:
    address = int(row['address'], 16)
    execution_lines += [f'\n### Core process literal {row["label"]} reference {address:#x}'] + bounded_text(address, 0x80, 0x180)
(output / 'core-execution.txt').write_text('\n'.join(execution_lines) + '\n')
(output / 'core-execution-candidates.json').write_text(json.dumps(dict(
    status='static-candidates-only', runtime_invocation_verified=False, game_thread_dispatch_verified=False,
    symbols=execution_symbols, strings=execution_strings, references=execution_references,
    literal_data_references=literal_data_references,
    text=dict(address=hex(core.text['address']), length=hex(core.text['length'])),
    bootstrap_veneer=bootstrap_veneer,
    process_name_literals=process_literal_records,
    function_windows_limited_to=64, function_candidates=len(review_functions),
    session_reference_windows_limited_to=40), indent=2) + '\n')
binary.unlink()
