#!/usr/bin/env python3
"""Cross-reference recipe labels against pinned factual ID evidence, in Actions."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unicodedata
import zipfile
from csv_metadata import names
from pak_inventory import entries, extract


def normalize(label):
    return re.sub(r'[^a-z0-9\u3400-\u9fff]', '', unicodedata.normalize('NFKC', label).lower())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    directory = Path(__file__).parent
    source = json.loads((directory / 'catalog-source.json').read_text())
    endpoint = f"repos/{source['repository']}/contents/{source['path']}?ref={source['commit']}"
    raw = subprocess.check_output(['gh', 'api', '-H', 'Accept: application/vnd.github.raw+json', endpoint])
    blob = b'blob ' + str(len(raw)).encode() + b'\0' + raw
    if len(raw) != source['size'] or hashlib.sha1(blob).hexdigest() != source['git_blob_sha1']:
        raise ValueError('Unreviewed external catalog input')
    rows = []
    for line in raw.decode('utf-8', errors='replace').splitlines():
        match = re.fullmatch(r'\{([0-9]+)\}--\{([0-9]+)\}--\[(.*?)\]--\[(0x[0-9a-fA-F]+)\]', line)
        if match:
            rows.append(dict(item_id=int(match[1]), resource_id=int(match[2]), label=match[3]))
    if not 10000 < len(rows) < 100000:
        raise ValueError('Unexpected external catalog row count')
    manifest = json.loads((directory / 'inputs.json').read_text())
    item_names = set()
    recipes = []
    for record in manifest['files']:
        if not record['name'].endswith('.zip'):
            continue
        with zipfile.ZipFile(args.inputs / record['name']) as archive:
            for info in archive.infolist():
                if info.filename.endswith('.pak'):
                    data = archive.read(info)
                    for entry in entries(data)['entries']:
                        if entry['path'].endswith('/CSV/Item.uasset'):
                            _, name_map = names(extract(data, entry))
                            item_names.update(name_map)
                elif info.filename.endswith('.txt') and info.file_size < 1024 * 1024:
                    raw_text = archive.read(info)
                    try:
                        content = raw_text.decode('utf-8-sig')
                    except UnicodeDecodeError:
                        content = raw_text.decode('gb18030')
                    for number, line in enumerate(content.splitlines(), 1):
                        parts = re.split(r'[=＝]+', line.strip(), maxsplit=1)
                        if len(parts) == 2 and all(normalize(part) for part in parts):
                            recipes.append(dict(source=parts[0], target=parts[1], archive=record['name'],
                                                text=Path(info.filename).name, line=number))
    exact = defaultdict(list)
    for row in rows:
        if str(row['item_id']) in item_names and '\ufffd' not in row['label']:
            exact[normalize(row['label'])].append(row)

    def candidates(label):
        key = normalize(label)
        hits = exact.get(key, [])
        method = 'normalized-exact'
        if not hits:
            hits = [r for name, values in exact.items() if len(name) >= 6 and key.startswith(name) for r in values]
            method = 'truncated-catalog-prefix'
        return dict(method=method, candidates=list({r['item_id']: r for r in hits}.values()))

    for recipe in recipes:
        recipe['source_candidates'] = candidates(recipe['source'])
        recipe['target_candidates'] = candidates(recipe['target'])
    device = json.loads((directory / 'device-evidence-r1.json').read_text())
    device_matches = []
    for slot in device['nonzero_sync_slots']:
        item_id = slot['type_specific_id']
        device_matches.append(dict(**slot, item_row_present=str(item_id) in item_names,
                                   catalog_labels=[r['label'] for r in rows if r['item_id'] == item_id]))
    trial = json.loads((directory / 'trial-profile.json').read_text())
    target_rows = [r for r in rows if r['item_id'] == trial['target_id'] and r['label'] == trial['target_label']]
    if len(target_rows) != 1 or any(str(trial[key]) not in item_names for key in ['original_id', 'target_id']):
        raise ValueError('Trial coat identity is absent or ambiguous in the reviewed sources')
    summary = dict(catalog_rows=len(rows), supplied_item_names=len(item_names), recipe_rows=len(recipes),
                   unique_source_candidates=sum(len(r['source_candidates']['candidates']) == 1 for r in recipes),
                   unique_target_candidates=sum(len(r['target_candidates']['candidates']) == 1 for r in recipes),
                   unique_pairs=sum(len(r['source_candidates']['candidates']) == len(r['target_candidates']['candidates']) == 1 for r in recipes))
    output = dict(status='external-name-id-candidates-only', runtime_item_ids_validated=False,
                  usable_as_kernel_configuration=False, source=source, summary=summary,
                  device_initial_outfit=device_matches, trial_target=target_rows[0], recipes=recipes)
    (args.output / 'recipe-id-candidates.json').write_text(json.dumps(output, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
