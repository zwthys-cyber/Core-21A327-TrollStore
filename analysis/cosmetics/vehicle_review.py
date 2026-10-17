#!/usr/bin/env python3
"""Focused, static vehicle review; run only in GitHub Actions."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

import analyze


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(Path(__file__).with_name('vehicle-windows.json').read_text())
    image = analyze.MachO(args.game)
    if image.uuid != analyze.GAME_UUID or hashlib.sha256(image.data).hexdigest() != analyze.GAME_SHA256:
        raise ValueError('Unreviewed vehicle analysis input')
    discovered = []
    fragments = config.get('discover_string_fragments', [])
    section = image.cstrings
    for value in image.data[section['offset']:section['offset'] + section['length']].split(b'\0'):
        if len(value) <= 96 and any(fragment.encode() in value for fragment in fragments):
            name = value.decode('ascii', errors='ignore')
            if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9:]*', name):
                discovered.append(name)
    discovered = sorted(set(discovered))
    if len(discovered) > 256:
        raise ValueError('Vehicle discovery exceeded the reviewed name bound')
    scheduler_names = []
    scheduler_fragments = [term.encode() for term in config.get('scheduler_terms', [])]
    cstring_data = image.data[section['offset']:section['offset'] + section['length']]
    for value in cstring_data.split(b'\0'):
        if len(value) <= 128 and any(term.lower() in value.lower() for term in scheduler_fragments):
            name = value.decode('ascii', errors='ignore')
            if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9:]*', name):
                scheduler_names.append(name)
    scheduler_names = sorted(set(scheduler_names))
    if len(scheduler_names) > 256:
        raise ValueError('Scheduler name discovery exceeded the reviewed bound')
    analyze.TERMS = list(dict.fromkeys(config['terms'] + discovered + scheduler_names))
    properties, functions = analyze.inspect_properties(image)
    clusters = []
    for region in config['property_regions']:
        rows = []
        for address in range(int(region['start'], 16), int(region['end'], 16), 8):
            offset = image.offset(address)
            words = [image.qword(offset + i * 8) for i in range(9)]
            name = image.string(words[1])
            if name and 0 < words[0] <= 0x40 and words[2] == 0x45 and 0 < words[4] <= 4096:
                rows.append(dict(name=name, address=hex(address), offset_candidate=hex(words[6]),
                                 raw_qwords=[hex(v) for v in words]))
        clusters.append(dict(label=region['label'], properties=rows))
    tables = []
    for entry in config['pointer_tables']:
        address = int(entry['address'], 16)
        rows = []
        for slot in range(0, int(entry['length'], 16), 8):
            value = image.qword(image.offset(address + slot))
            rows.append(dict(slot=hex(slot), value=hex(value), executable=image.code_address(value),
                             string=image.string(value)))
        tables.append(dict(label=entry['label'], address=hex(address), rows=rows))
    report = dict(status='static-vehicle-candidates-only', runtime_validated=False,
                  properties=properties, functions=functions, clusters=clusters, pointer_tables=tables,
                  discovered_names=discovered, scheduler_names=scheduler_names)
    report['reviewed_strings'] = []
    for entry in config.get('strings', []):
        offset = image.offset(int(entry['address'], 16))
        data = image.data[offset:offset + 512]
        end = next((i for i in range(0, len(data) - 1, 2) if data[i:i + 2] == b'\0\0'), None)
        value = data[:end].decode('utf-16-le') if end is not None else None
        report['reviewed_strings'].append(dict(label=entry['label'], address=entry['address'], value=value))
    literal_terms = []
    for name in config.get('utf16_terms', []):
        needle = name.encode('utf-16-le') + b'\0\0'
        cursor = 0
        while True:
            hit = image.data.find(needle, cursor)
            if hit < 0:
                break
            literal_terms.append(dict(name=name, address=hex(image.address(hit))))
            if len(literal_terms) > 64:
                raise ValueError('UTF-16 literal discovery exceeded its bound')
            cursor = hit + len(needle)
    report['utf16_literals'] = literal_terms
    literal_addresses = {int(row['address'], 16) for row in literal_terms}
    scheduler_strings = []
    scheduler_terms = [term.encode() for term in config.get('scheduler_terms', [])]
    cstrings = image.data[section['offset']:section['offset'] + section['length']]
    cursor = 0
    while cursor < len(cstrings):
        end = cstrings.find(b'\0', cursor)
        if end < 0:
            break
        value = cstrings[cursor:end]
        if value and any(term.lower() in value.lower() for term in scheduler_terms):
            scheduler_strings.append(dict(address=hex(image.address(section['offset'] + cursor)),
                                          value=value.decode('utf-8', errors='replace')))
            if len(scheduler_strings) > 128:
                raise ValueError('Scheduler string discovery exceeded its bound')
        cursor = end + 1
    scheduler_symbols = []
    symbol_names = []
    if image.symbols:
        symoff, count, stroff, strsize = image.symbols
        for index in range(count):
            name_offset, kind, symbol_section, desc, value = struct.unpack_from(
                '<IBBHQ', image.data, symoff + index * 16)
            if name_offset >= strsize:
                symbol_names.append('')
                continue
            end = image.data.find(b'\0', stroff + name_offset, stroff + strsize)
            name = image.data[stroff + name_offset:end]
            decoded = name.decode('utf-8', errors='replace')
            symbol_names.append(decoded)
            if any(term.lower() in name.lower() for term in scheduler_terms):
                scheduler_symbols.append(dict(name=decoded, kind=kind, section=symbol_section,
                                              value=hex(value)))
                if len(scheduler_symbols) > 256:
                    raise ValueError('Scheduler symbol discovery exceeded its bound')
    report['scheduler_strings'] = scheduler_strings
    report['scheduler_symbols'] = scheduler_symbols
    # Resolve imported scheduler stubs (__dispatch_async / __dispatch_main_q / ...)
    # the same way Core menu inspection maps __stubs through the indirect symbol table.
    scheduler_stubs = []
    indirect = None
    stub_sections = []
    position = 32
    command_end = 32 + image.u32(20)
    for _ in range(image.u32(16)):
        kind, size = struct.unpack_from('<II', image.data, position)
        if kind == 0xB:
            indirect = struct.unpack_from('<II', image.data, position + 56)
        if kind == 0x19:
            for index in range(image.u32(position + 64)):
                start = position + 72 + index * 80
                section_name = image.data[start:start + 16].split(b'\0')[0].decode()
                section_address, section_length = struct.unpack_from('<QQ', image.data, start + 32)
                reserved1, reserved2 = struct.unpack_from('<II', image.data, start + 68)
                if section_name in ('__stubs', '__auth_stubs') and reserved2:
                    stub_sections.append((section_name, section_address, section_length,
                                         reserved1, reserved2))
        position += size
        if position > command_end:
            break
    if indirect and symbol_names:
        for section_name, section_address, section_length, reserved1, reserved2 in stub_sections:
            for index in range(section_length // reserved2):
                symbol_index = image.u32(indirect[0] + (reserved1 + index) * 4)
                if symbol_index >= len(symbol_names):
                    continue
                symbol = symbol_names[symbol_index]
                if any(term.lower() in symbol.lower()
                       for term in config.get('scheduler_terms', [])):
                    stub_address = section_address + index * reserved2
                    scheduler_stubs.append(dict(
                        symbol=symbol, address=hex(stub_address), section=section_name,
                        stub_size=reserved2))
                    if len(scheduler_stubs) > 64:
                        raise ValueError('Scheduler stub discovery exceeded its bound')
    report['scheduler_stubs'] = scheduler_stubs
    scheduler_addresses = {int(row['address'], 16) for row in scheduler_strings}
    scheduler_pointer_slots = []
    for literal in scheduler_addresses:
        needle = struct.pack('<Q', literal)
        for segment in image.segments:
            if segment['name'] == '__LINKEDIT':
                continue
            cursor = segment['offset']
            end = cursor + segment['length']
            while True:
                hit = image.data.find(needle, cursor, end)
                if hit < 0:
                    break
                if (hit - segment['offset']) % 8 == 0:
                    scheduler_pointer_slots.append(dict(address=hex(image.address(hit)), literal=hex(literal)))
                    if len(scheduler_pointer_slots) > 1024:
                        raise ValueError('Scheduler pointer discovery exceeded its bound')
                cursor = hit + 1
    report['scheduler_pointer_slots'] = scheduler_pointer_slots
    scheduler_pointer_addresses = {int(row['address'], 16) for row in scheduler_pointer_slots}
    lines = []
    windows = [(entry['label'], 0x100000000 + int(entry['offset'], 16), int(entry['length'], 16))
               for entry in config['windows']]
    registrations = []
    for entry in config.get('native_registration_tables', []):
        count = int(entry['count'])
        if not 0 < count <= 128:
            raise ValueError('Native registration count outside reviewed bound')
        stride = int(entry.get('stride', 16))
        if stride not in (16, 24, 48):
            raise ValueError('Native registration stride outside reviewed layouts')
        address = int(entry['address'], 16)
        rows = []
        for index in range(count):
            offset = image.offset(address + index * stride)
            name_address, function = image.qword(offset), image.qword(offset + 8)
            name = image.string(name_address)
            name_encoding = 'utf-8-cstring'
            if name is None:
                # Native registration names may be wide literals outside the
                # __cstring section, as the reviewed class names already are.
                try:
                    name_offset = image.offset(name_address)
                    data = image.data[name_offset:name_offset + 256]
                    end = next((i for i in range(0, len(data) - 1, 2) if data[i:i + 2] == b'\0\0'), None)
                    name = data[:end].decode('utf-16-le') if end is not None else None
                    name_encoding = 'utf-16-le-literal'
                except (ValueError, UnicodeDecodeError):
                    name = None
            confirmed = bool(name and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9:]*', name) and image.code_address(function))
            rows.append(dict(name=name, name_address=hex(name_address), code_address=hex(function),
                             table_index=index, confirmed=confirmed, name_encoding=name_encoding))
            if confirmed:
                windows.append((entry['label'] + ': ' + name, function, 0x300))
        registrations.append(dict(label=entry['label'], address=hex(address), stride=stride, rows=rows))
    report['native_registrations'] = registrations
    for table, entry in zip(tables, config['pointer_tables']):
        for row in table['rows']:
            if row['executable'] and row['slot'] in entry.get('disassemble_slots', []):
                windows.append((table['label'] + ' slot ' + row['slot'], int(row['value'], 16), 0x1400))
    decoder = analyze.Cs(analyze.CS_ARCH_ARM64, analyze.CS_MODE_ARM)
    decoder.detail = True
    fields = []
    for region in config.get('field_regions', []):
        address = 0x100000000 + int(region['offset'], 16)
        offset = image.offset(address)
        data = image.data[offset:offset + int(region['length'], 16)]
        displacements = [int(value, 16) for value in region['displacements']]
        for instruction in decoder.disasm(data, address):
            if any(operand.type == analyze.ARM64_OP_MEM and operand.mem.disp in displacements
                   for operand in instruction.operands):
                fields.append(dict(address=hex(instruction.address), instruction=instruction.mnemonic + ' ' + instruction.op_str))
                windows.append(('vehicle field access', instruction.address - 0x80, 0x200))
    report['field_references'] = fields
    global_references = []
    scheduler_adrp_add_count = 0
    scheduler_adrp_ldr_count = 0
    game_thread_global_count = 0
    wanted = {int(value, 16) for value in config.get('global_field_loads', [])}
    field_constants = {int(value, 16) for value in config.get('global_field_constants', [])}
    call_targets = {0x100000000 + int(value, 16) for value in config.get('call_targets', [])}
    for stub in scheduler_stubs:
        call_targets.add(int(stub['address'], 16))
    call_target_window_counts = {}
    game_thread_pages = {}
    for entry in config.get('game_thread_globals', []):
        page = int(entry['page'], 16)
        game_thread_pages[page] = {
            int(offset, 16): label for offset, label in entry.get('offsets', {}).items()
        }
    for entry in config.get('taskgraph_pool_pages', []):
        page = int(entry['page'], 16)
        game_thread_pages.setdefault(page, {}).update({
            int(offset, 16): label for offset, label in entry.get('offsets', {}).items()
        })
    code_offset, code_size = image.text['offset'], image.text['length']
    code = image.data[code_offset:code_offset + (code_size & ~3)]
    for index, (word,) in enumerate(struct.iter_unpack('<I', code)):
        pc = image.text['address'] + index * 4
        if word & 0x9F000000 == 0x90000000 and game_thread_pages:
            relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
            if relative & (1 << 20):
                relative -= 1 << 21
            page = (pc & ~0xFFF) + relative * 4096
            offsets = game_thread_pages.get(page)
            if offsets:
                register = word & 31
                for delta in range(1, 12):
                    position = (index + delta) * 4
                    if position + 4 > len(code):
                        break
                    following = struct.unpack_from('<I', code, position)[0]
                    # LDRB/STRB imm12 and LDR/STR imm12 (W/X) against the page register.
                    kinds = []
                    if following & 0xFFC00000 == 0x39400000 and ((following >> 5) & 31) == register:
                        kinds.append(('ldrb', (following >> 10) & 4095))
                    elif following & 0xFFC00000 == 0x39000000 and ((following >> 5) & 31) == register:
                        kinds.append(('strb', (following >> 10) & 4095))
                    elif following & 0xFFC00000 == 0xB9400000 and ((following >> 5) & 31) == register:
                        kinds.append(('ldr-w', ((following >> 10) & 4095) * 4))
                    elif following & 0xFFC00000 == 0xB9000000 and ((following >> 5) & 31) == register:
                        kinds.append(('str-w', ((following >> 10) & 4095) * 4))
                    elif following & 0xFFC00000 == 0xF9400000 and ((following >> 5) & 31) == register:
                        kinds.append(('ldr-x', ((following >> 10) & 4095) * 8))
                    elif following & 0xFFC00000 == 0xF9000000 and ((following >> 5) & 31) == register:
                        kinds.append(('str-x', ((following >> 10) & 4095) * 8))
                    elif following & 0xFFC00000 == 0x91000000 and ((following >> 5) & 31) == register:
                        add_imm = (following >> 10) & 4095
                        add_reg = following & 31
                        for add_delta in range(1, 8):
                            add_position = (index + delta + add_delta) * 4
                            if add_position + 4 > len(code):
                                break
                            add_following = struct.unpack_from('<I', code, add_position)[0]
                            if ((add_following >> 5) & 31) != add_reg:
                                continue
                            if add_following & 0xFFC00000 == 0x39400000:
                                kinds.append(('add-ldrb', add_imm + ((add_following >> 10) & 4095)))
                            elif add_following & 0xFFC00000 == 0x39000000:
                                kinds.append(('add-strb', add_imm + ((add_following >> 10) & 4095)))
                            elif add_following & 0xFFC00000 == 0xB9400000:
                                kinds.append(('add-ldr-w', add_imm + ((add_following >> 10) & 4095) * 4))
                            elif add_following & 0xFFC00000 == 0xB9000000:
                                kinds.append(('add-str-w', add_imm + ((add_following >> 10) & 4095) * 4))
                    for access, offset in kinds:
                        label = offsets.get(offset)
                        if not label:
                            continue
                        global_references.append(dict(
                            address=hex(pc), game_thread_global=label, offset=hex(offset),
                            access=access, use_address=hex(pc + delta * 4), kind='game-thread-global'))
                        game_thread_global_count += 1
                        # Prefer stores and TaskGraph pool touches for disassembly;
                        # ordinary IsInGameThread loads are too numerous.
                        interesting = ('str' in access) or label.startswith('FBaseGraph') or label.startswith('TaskGraph')
                        if interesting or game_thread_global_count <= 32:
                            windows.append(('game thread global ' + label, pc - 0x80, 0x200))
        if word & 0x9F000000 == 0x10000000 and scheduler_addresses:
            relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
            if relative & (1 << 20):
                relative -= 1 << 21
            target = pc + relative
            if target in scheduler_addresses:
                global_references.append(dict(address=hex(pc), scheduler_literal=hex(target), kind='adr'))
                windows.append(('scheduler literal reference', pc - 0x100, 0x400))
        # Reviewed class registration names use ADRP + ADD and UTF-16 strings.
        # Keep raw references as candidates; the surrounding function still
        # needs manual review before assigning a class or constructor identity.
        if word & 0x9F000000 == 0x90000000 and (
                literal_addresses or scheduler_addresses or scheduler_pointer_addresses):
            relative = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
            if relative & (1 << 20):
                relative -= 1 << 21
            page = (pc & ~0xFFF) + relative * 4096
            register = word & 31
            page_has_literal = any(0 <= address - page < 4096 for address in literal_addresses)
            page_has_scheduler = any(0 <= address - page < 4096 for address in scheduler_addresses)
            page_has_scheduler_pointer = any(
                0 <= address - page < 4096 for address in scheduler_pointer_addresses)
            # Scheduler cstrings and UTF-16 class names live on different pages.
            # Do not gate scheduler ADRP/ADD discovery on UTF-16 literal hits.
            if page_has_literal or page_has_scheduler or page_has_scheduler_pointer:
                for delta in range(1, 12):
                    position = (index + delta) * 4
                    if position + 4 > len(code):
                        break
                    following = struct.unpack_from('<I', code, position)[0]
                    if following & 0xFFC00000 == 0x91000000 and ((following >> 5) & 31) == register:
                        address = page + ((following >> 10) & 4095)
                        if address in literal_addresses:
                            global_references.append(dict(address=hex(pc), literal=hex(address), add_address=hex(pc + delta * 4)))
                            windows.append(('native class literal reference', pc - 0x100, 0x500))
                        if address in scheduler_addresses:
                            global_references.append(dict(address=hex(pc), scheduler_literal=hex(address),
                                                          add_address=hex(pc + delta * 4), kind='adrp-add'))
                            scheduler_adrp_add_count += 1
                            # Keep every reference in JSON; only disassemble a
                            # reviewed sample so the artifact stays bounded.
                            if scheduler_adrp_add_count <= 96:
                                windows.append(('scheduler literal reference', pc - 0x100, 0x400))
                    if following & 0xFFC00000 == 0xF9400000 and ((following >> 5) & 31) == register:
                        address = page + ((following >> 10) & 4095) * 8
                        if address in scheduler_pointer_addresses:
                            global_references.append(dict(address=hex(pc), scheduler_pointer=hex(address),
                                                          load_address=hex(pc + delta * 4), kind='adrp-ldr'))
                            scheduler_adrp_ldr_count += 1
                            if scheduler_adrp_ldr_count <= 48:
                                windows.append(('scheduler pointer reference', pc - 0x100, 0x400))
        kind = word & 0xFFC00000
        scale = 4 if kind == 0xB9400000 else 8 if kind == 0xF9400000 else 0
        displacement = ((word >> 10) & 4095) * scale
        if scale and displacement in wanted:
            global_references.append(dict(address=hex(pc), displacement=hex(displacement)))
            windows.append(('vehicle field load ' + hex(displacement), pc - 0x100, 0x300))
        # Large member offsets are also materialized with MOVZ before a
        # register-indexed load/store; immediate-LDR-only review misses them.
        if word & 0x7F800000 == 0x52800000 and ((word >> 21) & 3) == 0:
            value = (word >> 5) & 0xFFFF
            if value in field_constants:
                global_references.append(dict(address=hex(pc), field_constant=hex(value)))
                windows.append(('vehicle field offset constant ' + hex(value), pc - 0x100, 0x400))
        if word & 0xFC000000 in (0x14000000, 0x94000000):
            relative = word & 0x3FFFFFF
            if relative & (1 << 25):
                relative -= 1 << 26
            target = pc + relative * 4
            if target in call_targets:
                # Capture nearby immediates so GameThread (#4 / #0x104) vs AnyThread
                # (#0xff / #0x6ff) CreateAndDispatch samples can be sorted without a
                # second full-binary pass.
                nearby = []
                for back in range(1, 12):
                    prior_offset = (index - back) * 4
                    if prior_offset < 0:
                        break
                    prior = struct.unpack_from('<I', code, prior_offset)[0]
                    if prior & 0x7F800000 == 0x52800000 and ((prior >> 21) & 3) == 0:
                        nearby.append(dict(
                            address=hex(pc - back * 4),
                            register=prior & 31,
                            value=hex((prior >> 5) & 0xFFFF)))
                # Last write to x8 before the call (sret for CreateAndDispatch).
                x8_setup = None
                for back in range(1, 16):
                    prior_offset = (index - back) * 4
                    if prior_offset < 0:
                        break
                    prior = struct.unpack_from('<I', code, prior_offset)[0]
                    # mov x8, xN  => ORR Xd=8, XZR, Xm=N
                    if prior & 0xFFE0FFE0 == 0xAA0003E0 and (prior & 31) == 8:
                        x8_setup = dict(address=hex(pc - back * 4), kind='mov-xn',
                                        source_register=(prior >> 16) & 31)
                        break
                    # mov x8, sp / add x8, sp, #imm
                    if prior == 0x910003E8:
                        x8_setup = dict(address=hex(pc - back * 4), kind='mov-sp')
                        break
                    if prior & 0xFFC003FF == 0x910003E8:
                        x8_setup = dict(address=hex(pc - back * 4), kind='add-sp',
                                        imm=hex((prior >> 10) & 4095))
                        break
                global_references.append(dict(address=hex(pc), target=hex(target),
                                              nearby_immediates=nearby, x8_setup=x8_setup))
                shown = call_target_window_counts.get(target, 0)
                if shown < 24:
                    call_target_window_counts[target] = shown + 1
                    windows.append(('vehicle native update caller', pc - 0x100, 0x300))
    # Hunt executable gadgets usable as Core 0x39A80 trampolines for CAD's x8 sret:
    # mov x8, xN ; b/bl CreateAndDispatch  or  mov x8, xN ; br xM
    cad = 0x100000000 + 0x3765444
    trampoline_gadgets = []
    for index in range(len(code) // 4 - 1):
        word = struct.unpack_from('<I', code, index * 4)[0]
        if word & 0xFFE0FFE0 != 0xAA0003E0 or (word & 31) != 8:
            continue
        source = (word >> 16) & 31
        if source > 7:
            continue
        pc = image.text['address'] + index * 4
        nxt = struct.unpack_from('<I', code, (index + 1) * 4)[0]
        entry = dict(address=hex(pc), mov='mov x8, x%d' % source, next=hex(nxt))
        if nxt & 0xFC000000 in (0x14000000, 0x94000000):
            relative = nxt & 0x3FFFFFF
            if relative & (1 << 25):
                relative -= 1 << 26
            target = pc + 4 + relative * 4
            if target == cad:
                entry.update(kind='mov-x8-branch-cad', branch='b' if nxt & 0xFC000000 == 0x14000000 else 'bl')
                trampoline_gadgets.append(entry)
                if len(trampoline_gadgets) <= 16:
                    windows.append(('CAD x8 trampoline gadget', pc, 0x20))
        elif nxt & 0xFFFFFC1F == 0xD61F0000:
            entry.update(kind='mov-x8-br', branch_register=(nxt >> 5) & 31)
            trampoline_gadgets.append(entry)
            if len(trampoline_gadgets) <= 16:
                windows.append(('mov-x8 br trampoline gadget', pc, 0x20))
        if len(trampoline_gadgets) >= 64:
            break
    report['global_references'] = global_references
    report['cad_x8_trampoline_gadgets'] = trampoline_gadgets
    report['scheduler_reference_counts'] = dict(
        adrp_add=sum(1 for row in global_references if row.get('kind') == 'adrp-add'),
        adrp_ldr=sum(1 for row in global_references if row.get('kind') == 'adrp-ldr'),
        adr=sum(1 for row in global_references if row.get('kind') == 'adr'),
        game_thread_global=sum(1 for row in global_references if row.get('kind') == 'game-thread-global'),
        is_safe_native_callers=sum(1 for row in global_references if row.get('target') == '0x1042c4c9c'),
        put_on_native_callers=sum(1 for row in global_references if row.get('target') == '0x1047f2824'),
        taskgraph_get_callers=sum(1 for row in global_references if row.get('target') == '0x108af6894'),
        create_task_callers=sum(1 for row in global_references if row.get('target') == '0x1035e3c10'),
        dispatch_when_ready_callers=sum(1 for row in global_references if row.get('target') == '0x1035e3d50'),
        create_and_dispatch_callers=sum(1 for row in global_references if row.get('target') == '0x103765444'),
        scheduler_stub_callers=sum(1 for row in global_references
                                   if any(row.get('target') == stub['address'] for stub in scheduler_stubs)),
        cad_x8_trampoline_gadgets=len(trampoline_gadgets),
        cad_callers_with_x8_from_arg=sum(
            1 for row in global_references
            if row.get('target') == '0x103765444' and (row.get('x8_setup') or {}).get('kind') == 'mov-xn'),
    )
    for record in functions:
        windows.append((record['name'], int(record['code_address'], 16), 0x200))
    for record in properties:
        if 'type_constructor_candidate' in record:
            windows.append(('type of ' + record['name'], int(record['type_constructor_candidate'], 16), 0x200))
    visited = set()
    for label, address, length in windows:
        if (address, length) in visited:
            continue
        visited.add((address, length))
        lines += ['', f'### {label} ({address:#x})'] + analyze.disassemble(image, address, length)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'vehicle-review.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    (args.output / 'vehicle-review.txt').write_text('\n'.join(lines) + '\n')
    print(json.dumps(dict(properties=len(properties), functions=len(functions), windows=len(visited))))


if __name__ == '__main__':
    main()
