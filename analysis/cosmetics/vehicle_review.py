#!/usr/bin/env python3
"""Focused, static vehicle review; run only in GitHub Actions."""
import argparse
import hashlib
import json
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
