#!/usr/bin/env python3
"""Focused, static vehicle review; run only in GitHub Actions."""
import argparse
import hashlib
import json
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
    analyze.TERMS = config['terms']
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
                  properties=properties, functions=functions, clusters=clusters, pointer_tables=tables)
    lines = []
    windows = [(entry['label'], 0x100000000 + int(entry['offset'], 16), int(entry['length'], 16))
               for entry in config['windows']]
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
    wanted = {int(value, 16) for value in config.get('global_field_loads', [])}
    call_targets = {0x100000000 + int(value, 16) for value in config.get('call_targets', [])}
    code_offset, code_size = image.text['offset'], image.text['length']
    code = image.data[code_offset:code_offset + (code_size & ~3)]
    for index, (word,) in enumerate(struct.iter_unpack('<I', code)):
        pc = image.text['address'] + index * 4
        kind = word & 0xFFC00000
        scale = 4 if kind == 0xB9400000 else 8 if kind == 0xF9400000 else 0
        displacement = ((word >> 10) & 4095) * scale
        if scale and displacement in wanted:
            global_references.append(dict(address=hex(pc), displacement=hex(displacement)))
            windows.append(('vehicle field load ' + hex(displacement), pc - 0x100, 0x300))
        if word & 0xFC000000 in (0x14000000, 0x94000000):
            relative = word & 0x3FFFFFF
            if relative & (1 << 25):
                relative -= 1 << 26
            target = pc + relative * 4
            if target in call_targets:
                global_references.append(dict(address=hex(pc), target=hex(target)))
                windows.append(('vehicle native update caller', pc - 0x100, 0x300))
    report['global_references'] = global_references
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
