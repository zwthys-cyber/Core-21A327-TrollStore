#!/usr/bin/env python3
"""Inspect cooked appearance tables in Actions, without treating guesses as IDs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

from pak_inventory import Reader, entries, extract


def fstring(reader):
    length = reader.number('<i')
    if abs(length) > 8192:
        raise ValueError('Oversized FString')
    if not length:
        return ''
    raw = reader.take(abs(length) * (2 if length < 0 else 1))
    terminator = b'\0\0' if length < 0 else b'\0'
    if not raw.endswith(terminator):
        raise ValueError('Unterminated FString')
    return raw[:-len(terminator)].decode('utf-16-le' if length < 0 else 'utf-8')


def names(data):
    reader = Reader(data)
    if reader.number('<I') != 0x9E2A83C1:
        raise ValueError('Unexpected uasset magic')
    legacy = reader.number('<i')
    if legacy != -7:
        raise ValueError(f'Unreviewed legacy package version {legacy}')
    ue3, ue4, licensee = [reader.number('<i') for _ in range(3)]
    custom_count = reader.number('<i')
    if not 0 <= custom_count <= 100:
        raise ValueError('Unexpected custom version count')
    reader.take(custom_count * 20)
    header_size = reader.number('<i')
    package_name = fstring(reader)
    flags, count, offset = [reader.number('<I') for _ in range(3)]
    if not 0 < count <= 100000 or not reader.position <= offset < len(data):
        raise ValueError('Invalid name map')
    candidates = []
    for hash_bytes in [0, 4]:
        source = Reader(data)
        source.position = offset
        try:
            result = []
            for _ in range(count):
                result.append(fstring(source))
                source.take(hash_bytes)
            candidates.append((hash_bytes, source.position, result))
        except (ValueError, UnicodeError):
            pass
    if len(candidates) != 1:
        raise ValueError(f'Ambiguous name map layout: {len(candidates)} candidates')
    hash_bytes, end, result = candidates[0]
    return dict(legacy=legacy, ue3=ue3, ue4=ue4, licensee=licensee,
                header_size=header_size, package_name=package_name, flags=hex(flags),
                name_count=count, name_offset=offset, name_end=end, hash_bytes=hash_bytes), result


def inspect(data, name_map):
    result = dict(size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  head_hex=data[:256].hex(), tail_hex=data[-256:].hex())
    result['head_names'] = [dict(offset=i, index=value, name=name_map[value])
                            for i in range(0, min(256, len(data) - 4), 4)
                            if (value := struct.unpack_from('<I', data, i)[0]) < len(name_map)]
    matches = []
    for name in ['墨龙九天', '灵猫魔法师', '电玩时刻', 'M416', '至尊金龙']:
        for encoding in ['utf-8', 'utf-16-le']:
            needle = name.encode(encoding)
            cursor = 0
            for _ in range(4):
                hit = data.find(needle, cursor)
                if hit < 0:
                    break
                start, end = max(0, hit - 96), min(len(data), hit + len(needle) + 128)
                matches.append(dict(label=name, encoding=encoding, offset=hit,
                                    context_start=start, context_hex=data[start:end].hex()))
                cursor = hit + len(needle)
    result['label_matches'] = matches
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('inputs.json').read_text())
    tables = []
    for source in manifest['files']:
        if not source['name'].endswith('.zip'):
            continue
        with zipfile.ZipFile(args.inputs / source['name']) as archive:
            for info in archive.infolist():
                if not info.filename.endswith('.pak'):
                    continue
                data = archive.read(info)
                package = entries(data)
                paths = {e['path']: e for e in package['entries']}
                for path in paths:
                    if not re.search(r'/CSV/(Item|AvatarBPTable|WeaponSkinMapping|WeaponBPTable)\.uasset$', path):
                        continue
                    record = dict(archive=source['name'], pak=Path(info.filename).name, path=path)
                    try:
                        asset = extract(data, paths[path])
                        metadata, name_map = names(asset)
                        record.update(summary=metadata, header_hex=asset[:384].hex(),
                            name_sample=name_map[:80],
                            numeric_row_name_sample=[n for n in name_map if re.fullmatch(r'[1-9][0-9]{4,9}', n)][:80],
                            relevant_names=[n for n in name_map if any(label in n for label in ['Name', 'Item', 'Skin', 'Row', 'ID'])][:80])
                        export_path = path[:-7] + '.uexp'
                        if export_path in paths:
                            record['export'] = inspect(extract(data, paths[export_path]), name_map)
                    except (ValueError, UnicodeError) as error:
                        record['analysis_error'] = str(error)
                    tables.append(record)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'csv-metadata.json').write_text(json.dumps(dict(status='static-table-evidence-only',
        runtime_item_ids_validated=False, tables=tables), ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(dict(tables=len(tables), errors=sum('analysis_error' in t for t in tables))))


if __name__ == '__main__':
    main()
