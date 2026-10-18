"""Read-only pinned Core acquisition review; run only in GitHub Actions."""
from pathlib import Path
import hashlib
import json
import re
import sys
import zipfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from minimal_features import minimal_core

out = ROOT / 'analysis/far-targets/output'
out.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(ROOT / 'input/Core.ipa') as z:
    original = z.read('Payload/Core.app/Core')
patched = minimal_core(original)
decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
windows = {'acquisition': (0xD7A88, 0xDB144),
           'render_filter': (0xDB144, 0xDB6A0),
           'original_geometry_query': (0xD33F4, 0xD3650)}
summary = {'original_core_sha256': hashlib.sha256(original).hexdigest(),
           'profile': 'v19 minimal marker patch with 600 metre range',
           'windows': {}, 'literal_candidates': []}
for name, (start, end) in windows.items():
    instructions = list(decoder.disasm(patched[start:end], start))
    lines = [f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in instructions]
    (out / f'{name}.txt').write_text('\n'.join(lines) + '\n')
    interesting = []
    for index, ins in enumerate(instructions):
        if ins.mnemonic.startswith(('fcmp', 'fcmpe')) or re.search(r'#(?:0x12c|0x258|0x4396|0x4416)(?:\b|,)', ins.op_str):
            interesting.append({'address': hex(ins.address), 'instruction': f'{ins.mnemonic} {ins.op_str}',
                                'context': lines[max(0, index-4):index+5]})
    summary['windows'][name] = interesting
    print(f'{name}: {len(instructions)} instructions, {len(interesting)} range/float candidates')
for value, label in [(300, 'integer_300'), (600, 'integer_600'), (0x43960000, 'float_300'), (0x44160000, 'float_600')]:
    needle = value.to_bytes(4, 'little')
    found = []
    offset = original.find(needle)
    while offset >= 0:
        if len(found) < 128: found.append(hex(offset))
        offset = original.find(needle, offset + 1)
    summary['literal_candidates'].append({'label': label, 'offsets_up_to_128': found})
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('Read-only original collector/range review written; no application build or target changes.')
