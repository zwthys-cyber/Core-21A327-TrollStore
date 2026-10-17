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
            hits = [r for name, values in exact.items()
                    if min(len(name), len(key)) >= 6 and (key.startswith(name) or name.startswith(key))
                    for r in values]
            method = 'bidirectional-catalog-prefix'
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
    # Menu candidates use only exact, unique target labels from the pinned
    # catalog that also occur in the supplied Item name map. They are grouped
    # solely into the five character slots already observed on this device.
    groups = [('外套', 5, 403000, 404000), ('头发', 2, 40600000, 40700000),
              ('裤子', 6, 404000, 405000), ('鞋子', 7, 405000, 406000),
              ('降落伞', 11, 703000, 704000)]
    presets = {}
    for recipe in recipes:
        match = recipe['target_candidates']
        if match['method'] != 'normalized-exact' or len(match['candidates']) != 1:
            continue
        row = match['candidates'][0]
        if any(ord(c) < 32 for c in row['label']):
            continue
        for category, slot, lower, upper in groups:
            if lower <= row['item_id'] < upper:
                presets.setdefault(row['item_id'], dict(category=category, slot=slot, target_id=row['item_id'],
                    label=row['label'], exact_label_and_item_row_confirmed=True,
                    device_visual_verified=row['item_id'] == trial['target_id'],
                    source_recipe=dict(archive=recipe['archive'], text=recipe['text'], line=recipe['line'])))
    order = {category: index for index, (category, *_) in enumerate(groups)}
    presets = sorted(presets.values(), key=lambda p: (order[p['category']], p['target_id'] != trial['target_id'], p['target_id']))
    catalog = dict(schema_version=1, status='character-menu-candidates', source=source, presets=presets,
        scope='Five character appearance slots; weapon and custom PAK effects are not implemented by this catalog.',
        category_counts={category: sum(p['category'] == category for p in presets) for category, *_ in groups})
    (args.output / 'selected-avatar-catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')
    header = ['#ifndef CORE_COSMETICS_CATALOG_H', '#define CORE_COSMETICS_CATALOG_H', '#include <stdint.h>',
              'typedef struct { uint32_t slot, target; const char *category, *label; } CCPAppearancePreset;',
              'static const CCPAppearancePreset CCPAppearancePresets[] = {']
    for preset in presets:
        header.append('    {%du, %du, %s, %s},' % (preset['slot'], preset['target_id'],
            json.dumps(preset['category'], ensure_ascii=False), json.dumps(preset['label'], ensure_ascii=False)))
    header += ['};', '#define CCP_PRESET_COUNT (sizeof(CCPAppearancePresets) / sizeof(CCPAppearancePresets[0]))', '#endif', '']
    (args.output / 'CoreCosmeticsCatalog.h').write_text('\n'.join(header))
    # Vehicle recipes are kept source-specific: applying a target to an
    # incompatible vehicle family is unsafe. Item IDs are the state/catalog
    # identity; PutOnEquipmentByResID receives the separately recorded ResID.
    vehicle_min, vehicle_max = 3300000, 3400000
    vehicle_presets, unresolved = {}, []
    for recipe in recipes:
        source_rows = recipe['source_candidates']['candidates']
        target_rows = recipe['target_candidates']['candidates']
        source_rows = [r for r in source_rows if vehicle_min <= r['item_id'] < vehicle_max]
        target_rows = [r for r in target_rows if vehicle_min <= r['item_id'] < vehicle_max]
        if not source_rows:
            continue
        if len(target_rows) != 1:
            unresolved.append(dict(source=recipe['source'], target=recipe['target'],
                archive=recipe['archive'], text=recipe['text'], line=recipe['line'],
                source_candidates=source_rows, target_candidates=target_rows))
            continue
        target = target_rows[0]
        for source_row in source_rows:
            key = (source_row['item_id'], target['item_id'])
            vehicle_presets.setdefault(key, dict(source_item_id=source_row['item_id'],
                source_resource_id=source_row['resource_id'], source_label=source_row['label'],
                target_item_id=target['item_id'], target_resource_id=target['resource_id'],
                target_label=target['label'], source_recipe=dict(archive=recipe['archive'],
                text=recipe['text'], line=recipe['line'])))
    vehicle_presets = sorted(vehicle_presets.values(),
        key=lambda p: (p['source_item_id'], p['target_item_id']))
    vehicle_catalog = dict(schema_version=1, status='vehicle-menu-candidates',
        item_id_contract='Vehicle state uses item_id; PutOnEquipmentByResID uses resource_id.',
        source_compatibility='Exact source item ID required before enqueue.',
        presets=vehicle_presets, unresolved=unresolved,
        resolved_pair_count=len(vehicle_presets), unresolved_recipe_count=len(unresolved))
    (args.output / 'selected-vehicle-catalog.json').write_text(
        json.dumps(vehicle_catalog, ensure_ascii=False, indent=2) + '\n')
    vehicle_header = ['#ifndef CORE_COSMETICS_VEHICLE_CATALOG_H',
        '#define CORE_COSMETICS_VEHICLE_CATALOG_H', '#include <stdint.h>',
        'typedef struct { uint32_t sourceItem, targetItem, targetResource; const char *sourceLabel, *targetLabel; } CCPVehiclePreset;',
        'static const CCPVehiclePreset CCPVehiclePresets[] = {']
    for preset in vehicle_presets:
        vehicle_header.append('    {%du, %du, %du, %s, %s},' % (
            preset['source_item_id'], preset['target_item_id'], preset['target_resource_id'],
            json.dumps(preset['source_label'], ensure_ascii=False),
            json.dumps(preset['target_label'], ensure_ascii=False)))
    vehicle_header += ['};',
        '#define CCP_VEHICLE_PRESET_COUNT (sizeof(CCPVehiclePresets) / sizeof(CCPVehiclePresets[0]))',
        '#endif', '']
    (args.output / 'CoreCosmeticsVehicleCatalog.h').write_text('\n'.join(vehicle_header))
    print(json.dumps(catalog['category_counts'], ensure_ascii=False))
    print(json.dumps(dict(**summary, vehicle_pairs=len(vehicle_presets),
                          unresolved_vehicle_recipes=len(unresolved))))


if __name__ == '__main__':
    main()
