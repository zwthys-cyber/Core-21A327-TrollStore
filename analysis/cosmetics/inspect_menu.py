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
export_trie = None
export_meta = None
for _ in range(core.u32(16)):
    command, size = struct.unpack_from('<II', data, position)
    if command == 0xB:
        indirect = struct.unpack_from('<II', data, position + 56)
    if command == 0x26:
        function_starts_data = struct.unpack_from('<II', data, position + 8)
    if command in (0x22, 0x80000022, 0x80000033):
        field = 8 if command == 0x80000033 else 40
        export_off, export_size = struct.unpack_from('<II', data, position + field)
        export_meta = dict(command=hex(command), offset=export_off, size=export_size)
        if export_size <= 0x200000 and export_off + export_size <= len(data):
            export_trie = data[export_off:export_off + export_size]
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
    (0x5625C, 'Core.springboard_session_attempt'),
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
veneer_raw = data[veneer_offset:veneer_offset + 0x100] if veneer_offset is not None else b''
bootstrap_veneer = dict(address=hex(veneer), inside_text=core.code_address(veneer),
    segment=veneer_segment, section=veneer_section, raw_hex=veneer_raw.hex())
if bootstrap_veneer['inside_text']:
    execution_lines += ['\n### Core remote bootstrap veneer 0x100003800'] + bounded_text(veneer, 0, 0x180)
elif veneer_offset is not None:
    execution_lines += ['\n### Core remote bootstrap veneer outside text 0x100003800'] + disassemble(core, veneer, 0x100)
session_labels = {'Core.remote_bootstrap_veneer', 'Core.remote_bootstrap_entry', 'Core.remote_bootstrap_body',
    'Core.remote_session_teardown', 'Core.remote_thread_state_writer', 'Core.launchd_session_wrapper',
    'Core.springboard_session_attempt'}
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
def read_uleb(blob, cursor):
    value = shift = 0
    while True:
        if cursor >= len(blob) or shift > 63:
            raise ValueError('Core export trie exceeded its bounded encoding')
        byte = blob[cursor]
        cursor += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, cursor
        shift += 7

def export_matches(blob):
    if not blob or len(blob) > 0x200000:
        return [], 'absent-or-unbounded'
    stack = [(0, b'', 0)]
    seen = set()
    matches = []
    while stack:
        offset, prefix, depth = stack.pop()
        if offset in seen or offset >= len(blob) or depth > 24 or len(seen) > 8192:
            continue
        seen.add(offset)
        cursor = offset
        terminal, cursor = read_uleb(blob, cursor)
        payload = cursor
        if cursor + terminal >= len(blob):
            return matches, 'truncated'
        cursor += terminal
        children = blob[cursor]
        cursor += 1
        for _ in range(children):
            end = blob.find(b'\0', cursor, min(cursor + 96, len(blob)))
            if end < 0:
                return matches, 'truncated'
            edge = blob[cursor:end]
            cursor = end + 1
            child, cursor = read_uleb(blob, cursor)
            name = prefix + edge
            if b'CoreRemote' in name:
                address = None
                if terminal:
                    flags, value_cursor = read_uleb(blob, payload)
                    if not flags & 0x18:
                        address, _ = read_uleb(blob, value_cursor)
                matches.append(dict(name=name.decode('utf-8', 'replace'), address=hex(address) if address is not None else None))
                if len(matches) > 32:
                    return matches, 'match-cap'
            stack.append((child, name, depth + 1))
    return matches, 'complete'

remote_exports, export_status = export_matches(export_trie)
string_sites = []
cursor = 0
while len(string_sites) < 8:
    hit = data.find(b'CoreRemoteOpen\0', cursor)
    if hit < 0:
        break
    string_sites.append(hex(core.address(hit)))
    cursor = hit + 1
gap = data[max(0, core.text['offset'] - 0x1800):core.text['offset']]
gap_strings = []
for part in gap.split(b'\0'):
    if 4 <= len(part) <= 80 and all(32 <= byte < 127 for byte in part):
        gap_strings.append(part.decode('ascii'))
        if len(gap_strings) == 64:
            break
image_hits = []
with zipfile.ZipFile(root / 'input/Core.ipa') as archive:
    machos = []
    for info in archive.infolist():
        if info.is_dir() or not 32 <= info.file_size <= 32_000_000:
            continue
        with archive.open(info) as handle:
            header = handle.read(8)
        if header[:4] == b'\xcf\xfa\xed\xfe':
            machos.append(info.filename)
        if len(machos) == 16:
            break
    for name in machos:
        blob = archive.read(name)
        image_hits.append(dict(name=name, size=len(blob), core_remote_open=blob.find(b'CoreRemoteOpen\0') >= 0))
    offline_path = output / 'CoreOffline-input'
    offline_path.write_bytes(archive.read('Payload/Core.app/CoreOffline.work.dylib'))
offline = MachO(offline_path)
offline_symbols = []
symoff, count, stroff, strsize = offline.symbols
offline_data = offline_path.read_bytes()
for index in range(min(count, 8000)):
    name_offset, kind, section, desc, value = struct.unpack_from('<IBBHQ', offline_data, symoff + index * 16)
    end = offline_data.find(b'\0', stroff + name_offset, stroff + strsize)
    name = offline_data[stroff + name_offset:end].decode('utf-8', 'replace')
    if 'CoreRemote' in name:
        offline_symbols.append(dict(name=name, kind=kind, section=section, value=hex(value)))
        if len(offline_symbols) == 32:
            break
offline_export = None
offline_dylibs = []
chained_blob = None
offline_position = 32
for _ in range(struct.unpack_from('<I', offline_data, 16)[0]):
    command, size = struct.unpack_from('<II', offline_data, offline_position)
    if command in (0x22, 0x80000022, 0x80000033):
        field = 8 if command == 0x80000033 else 40
        export_off, export_size = struct.unpack_from('<II', offline_data, offline_position + field)
        if export_size <= 0x200000 and export_off + export_size <= len(offline_data):
            offline_matches, offline_export_status = export_matches(offline_data[export_off:export_off + export_size])
            offline_export = dict(command=hex(command), size=export_size, status=offline_export_status, symbols=offline_matches)
    if command in (0xC, 0x80000018, 0x8000001F, 0x80000023):
        name_off = struct.unpack_from('<I', offline_data, offline_position + 8)[0]
        start = offline_position + name_off
        end = offline_data.find(b'\0', start, offline_position + size)
        offline_dylibs.append(offline_data[start:end].decode('utf-8', 'replace'))
    if command == 0x80000034:
        chained_off, chained_size = struct.unpack_from('<II', offline_data, offline_position + 8)
        if chained_size <= 0x100000 and chained_off + chained_size <= len(offline_data):
            chained_blob = offline_data[chained_off:chained_off + chained_size]
    offline_position += size
offline_strings = []
cursor = 0
while len(offline_strings) < 4:
    hit = offline_data.find(b'CoreRemoteOpen\0', cursor)
    if hit < 0:
        break
    previous = offline_data.rfind(b'\0', max(0, hit - 80), hit)
    start = 0 if previous < 0 else previous + 1
    offline_strings.append(dict(offset=hex(hit), text=offline_data[start:hit + len(b'CoreRemoteOpen')].decode('utf-8', 'replace')))
    cursor = hit + 1
for row in offline_symbols:
    address = int(row['value'], 16)
    if offline.code_address(address):
        execution_lines += [f'\n### CoreOffline {row["name"]} {address:#x}'] + disassemble(offline, address, 0x160)
offline.file.close()
offline_path.unlink()
def chained_slot(blob, image, target):
    if blob is None or len(blob) < 28:
        return dict(status='absent')
    version, starts_offset, imports_offset, symbols_offset, imports_count, imports_format, symbols_format = struct.unpack_from('<7I', blob, 0)
    if version != 0 or starts_offset >= len(blob) or imports_count > 4096:
        return dict(status='unrecognized', version=version)
    starts = starts_offset
    seg_count = struct.unpack_from('<I', blob, starts)[0]
    if seg_count > 16:
        return dict(status='segment-cap', segments=seg_count)
    specials = {0: 'self', 0xFD: 'weak-lookup', 0xFE: 'flat-lookup', 0xFF: 'main-executable'}
    strides = {1: 8, 7: 8, 9: 8, 10: 8, 12: 8, 2: 4, 6: 4, 8: 4}
    coverage = []
    for index in range(seg_count):
        info_offset = struct.unpack_from('<I', blob, starts + 4 + index * 4)[0]
        if not info_offset:
            continue
        info = starts + info_offset
        size, page_size, pointer_format = struct.unpack_from('<IHH', blob, info)
        segment_offset, max_valid, page_count = struct.unpack_from('<QIH', blob, info + 8)
        if index >= len(image.segments) or size < 22 + page_count * 2 or page_count > 256 or pointer_format not in strides or info + size > len(blob):
            return dict(status='page-or-format-cap', pointer_format=pointer_format, page_count=page_count, segment_index=index)
        words = list(struct.unpack_from('<' + 'H' * ((size - 22) // 2), blob, info + 22))
        segment = image.segments[index]
        if page_size and segment['address'] + segment_offset <= target < segment['address'] + segment_offset + page_count * page_size:
            page_index = (target - segment['address'] - segment_offset) // page_size
            coverage.append(dict(segment=segment['name'], page_index=page_index, page_start=hex(words[page_index]),
                page_size=page_size, segment_offset=hex(segment_offset)))
        stride = strides[pointer_format]
        for page in range(page_count):
            start = words[page]
            offsets = []
            if start == 0xFFFF:
                continue
            if start & 0x8000:
                overflow = start & 0x7FFF
                for _ in range(32):
                    if overflow >= len(words):
                        break
                    entry = words[overflow]
                    offsets.append(entry & 0x7FFF)
                    overflow += 1
                    if entry & 0x8000:
                        break
            else:
                offsets.append(start)
            for offset in offsets:
                address = segment['address'] + segment_offset + page * page_size + offset
                for _ in range(2048):
                    if address == target:
                        raw = struct.unpack_from('<Q', offline_data, image.offset(address))[0]
                        auth = raw >> 63 & 1
                        bind = raw >> 62 & 1
                        nxt = raw >> 51 & 0x7FF
                        low = raw & ((1 << 51) - 1)
                        record = dict(status='found', segment=segment['name'], pointer_format=pointer_format,
                            raw=hex(raw), auth=auth, bind=bind, next=nxt)
                        if bind:
                            index = low & (0xFFFFFF if pointer_format == 12 else 0xFFFF)
                            record['import_index'] = index
                            name_offset = None
                            ordinal = None
                            if index < imports_count and imports_format in (1, 2):
                                word = struct.unpack_from('<I', blob, imports_offset + index * (4 if imports_format == 1 else 8))[0]
                                ordinal = word & 0xFF
                                name_offset = word >> 9
                            symbol = None
                            if name_offset is not None and symbols_offset + name_offset < len(blob):
                                symbol_end = blob.find(b'\0', symbols_offset + name_offset, symbols_offset + name_offset + 96)
                                symbol = blob[symbols_offset + name_offset:symbol_end].decode('utf-8', 'replace')
                            library = specials.get(ordinal)
                            if library is None and ordinal is not None and 1 <= ordinal <= len(offline_dylibs):
                                library = offline_dylibs[ordinal - 1]
                            record.update(ordinal=ordinal, library=library, symbol=symbol)
                        else:
                            record['target'] = hex(low & (0xFFFFFFFF if auth else (1 << 43) - 1))
                        return record
                    if not any(item['address'] <= address < item['address'] + item['length'] for item in image.segments):
                        break
                    step = nxt_from(address)
                    if step == 0:
                        break
                    address += step * stride
    return dict(status='not-in-chain', imports_format=imports_format, imports_count=imports_count, symbols_format=symbols_format,
        coverage=coverage,
        segments=[dict(name=item['name'], address=hex(item['address']), file_length=hex(item['length'])) for item in image.segments])

def nxt_from(address):
    if not any(item['address'] <= address < item['address'] + item['length'] for item in offline.segments):
        return 0
    raw = struct.unpack_from('<Q', offline_data, offline.offset(address))[0]
    return raw >> 51 & 0x7FF

fixup = chained_slot(chained_blob, offline, 0x102F0)
if fixup.get('status') == 'found' and not fixup.get('bind'):
    target = int(fixup['target'], 16) + 0x3710
    if any(item['address'] <= target < item['address'] + item['length'] for item in offline.segments):
        execution_lines += [f'\n### CoreOffline fixup target plus 0x3710 {target:#x}'] + disassemble(offline, target, 0x60)
        fixup['resolved_plus_3710'] = hex(target)
slot_refs = []
text_bytes = offline_data[offline.text['offset']:offline.text['offset'] + (offline.text['length'] & ~3)]
for index, (word,) in enumerate(struct.iter_unpack('<I', text_bytes)):
    pc = offline.text['address'] + index * 4
    if word & 0x9F000000 != 0x90000000:
        continue
    relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
    if relative & (1 << 20):
        relative -= 1 << 21
    page, register = (pc & ~4095) + relative * 4096, word & 31
    if page != 0x10000:
        continue
    for delta in range(1, 8):
        if (index + delta) * 4 + 4 > len(text_bytes):
            break
        following = struct.unpack_from('<I', text_bytes, (index + delta) * 4)[0]
        if following & 0xFFC00000 in (0xF9400000, 0xF9000000) and ((following >> 5) & 31) == register and ((following >> 10) & 4095) * 8 == 0x2F0:
            slot_refs.append(hex(pc + delta * 4))
            break
    if len(slot_refs) == 8:
        break
for site in slot_refs:
    address = int(site, 16)
    execution_lines += [f'\n### CoreOffline image-slot reference {address:#x}'] + disassemble(offline, max(offline.text['address'], address - 0x60), 0xC0)
slot_stores = []
for index, (word,) in enumerate(struct.iter_unpack('<I', text_bytes)):
    if word & 0xFFC00000 != 0xF9000000 or ((word >> 10) & 4095) * 8 != 0x2F0:
        continue
    register = (word >> 5) & 31
    page = None
    for back in range(1, 49):
        if index - back < 0:
            break
        earlier = struct.unpack_from('<I', text_bytes, (index - back) * 4)[0]
        if earlier & 0x9F000000 != 0x90000000 or (earlier & 31) != register:
            continue
        relative = (((earlier >> 5) & 0x7FFFF) << 2) | ((earlier >> 29) & 3)
        if relative & (1 << 20):
            relative -= 1 << 21
        page = ((offline.text['address'] + (index - back) * 4) & ~4095) + relative * 4096
        break
    if page == 0x10000:
        slot_stores.append(hex(offline.text['address'] + index * 4))
    if len(slot_stores) == 12:
        break
for site in slot_stores:
    address = int(site, 16)
    execution_lines += [f'\n### CoreOffline image-slot store {address:#x}'] + disassemble(offline, max(offline.text['address'], address - 0x80), 0xC0)
publisher_entries = []
for site in slot_stores:
    store = int(site, 16)
    for back in range(0, 0x240, 4):
        address = store - back
        if address < offline.text['address']:
            break
        word = struct.unpack_from('<I', offline_data, offline.offset(address))[0]
        if word == 0xD503237F:
            publisher_entries.append(hex(address))
            execution_lines += [f'\n### CoreOffline base publisher {address:#x}'] + disassemble(offline, address, min(back + 0x40, 0x280))
            break
publisher_calls = []
for index, (word,) in enumerate(struct.iter_unpack('<I', text_bytes)):
    if word & 0xFC000000 not in (0x14000000, 0x94000000):
        continue
    relative = word & 0x3FFFFFF
    if relative & (1 << 25):
        relative -= 1 << 26
    target = offline.text['address'] + index * 4 + relative * 4
    if hex(target) in publisher_entries:
        publisher_calls.append(hex(offline.text['address'] + index * 4))
initializers = []
for section in offline.sections:
    if section['name'] not in ('__mod_init_func', '__init_offsets'):
        continue
    count = min(section['length'] // 8, 8)
    for item in range(count):
        pointer = struct.unpack_from('<Q', offline_data, section['offset'] + item * 8)[0]
        initializers.append(dict(section=section['name'], value=hex(pointer)))
        if offline.code_address(pointer & 0xFFFFFFFFFFFF):
            execution_lines += [f'\n### CoreOffline initializer {pointer:#x}'] + disassemble(offline, pointer & ~3, 0x180)
veneer_symbols = []
cursor = core.offset(0x100003000)
limit = core.offset(0x100004000)
while cursor < limit and len(veneer_symbols) < 32:
    if data[cursor] == 0:
        cursor += 1
        continue
    end = data.find(b'\0', cursor, limit)
    if end < 0:
        break
    blob = data[cursor:end]
    if 3 <= len(blob) <= 80 and all(32 <= byte < 127 for byte in blob):
        veneer_symbols.append(dict(address=hex(core.address(cursor)), text=blob.decode('ascii')))
    cursor = end + 1
veneer_calls = []
main_code = data[core.text['offset']:core.text['offset'] + (core.text['length'] & ~3)]
for index, (word,) in enumerate(struct.iter_unpack('<I', main_code)):
    if word & 0xFC000000 not in (0x14000000, 0x94000000):
        continue
    relative = word & 0x3FFFFFF
    if relative & (1 << 25):
        relative -= 1 << 26
    target = core.text['address'] + index * 4 + relative * 4
    if 0x100003000 <= target < 0x100004000:
        veneer_calls.append(dict(address=hex(core.text['address'] + index * 4), target=hex(target)))
    if len(veneer_calls) == 48:
        break
pretext_end = data.find(b'\0', core.offset(0x100003664), core.offset(0x100003664) + 64)
pretext_symbol = data[core.offset(0x100003664):pretext_end].decode('utf-8', 'replace')
execution_lines += ['\n### Core pre-text target 0x100003710'] + disassemble(core, 0x100003710, 0x80)
offline_literals = {hex(address): offline.string(address) for address in (0x7823, 0x784A, 0x784C, 0x7877, 0x787C, 0x78ED)}
base_symbols = []
for index in range(min(count, 8000)):
    name_offset, kind, section, desc, value = struct.unpack_from('<IBBHQ', offline_data, symoff + index * 16)
    end = offline_data.find(b'\0', stroff + name_offset, stroff + strsize)
    name = offline_data[stroff + name_offset:end].decode('utf-8', 'replace')
    if any(term in name for term in ('dyld', 'strcmp', 'strstr', 'image', 'MachHeader')):
        base_symbols.append(dict(name=name, section=section, value=hex(value)))
    if len(base_symbols) == 24:
        break
pointer_bits = None
if offline.code_address(0x102F0) or any(segment['address'] <= 0x102F0 < segment['address'] + segment['length'] for segment in offline.segments):
    pointer_bits = hex(struct.unpack_from('<Q', offline_data, offline.offset(0x102F0))[0])
offline_report = dict(symbols=offline_symbols, exports=offline_export, strings=offline_strings,
    literals=offline_literals, image_pointer_raw=pointer_bits, fixup=fixup,
    loaded_libraries=offline_dylibs, pretext_symbol=pretext_symbol, image_slot_references=slot_refs,
    image_slot_stores=slot_stores, publisher_entries=publisher_entries, publisher_calls=publisher_calls,
    initializers=initializers, base_symbols=base_symbols,
    veneer_symbols=veneer_symbols,
    veneer_calls=veneer_calls,
    text=dict(address=hex(offline.text['address']), length=hex(offline.text['length'])))
(output / 'core-execution.txt').write_text('\n'.join(execution_lines) + '\n')
(output / 'core-execution-candidates.json').write_text(json.dumps(dict(
    status='static-candidates-only', runtime_invocation_verified=False, game_thread_dispatch_verified=False,
    symbols=execution_symbols, strings=execution_strings, references=execution_references,
    literal_data_references=literal_data_references,
    text=dict(address=hex(core.text['address']), length=hex(core.text['length'])),
    bootstrap_veneer=bootstrap_veneer,
    core_remote_open_strings=string_sites,
    core_remote_exports=remote_exports,
    export_trie_status=export_status,
    export_trie_meta=export_meta,
    core_offline=offline_report,
    pre_text_strings=gap_strings,
    ipa_core_remote_open=image_hits,
    process_name_literals=process_literal_records,
    function_windows_limited_to=64, function_candidates=len(review_functions),
    session_reference_windows_limited_to=40), indent=2) + '\n')
binary.unlink()
