"""Inspect pinned native ray entry points in GitHub Actions only."""
from pathlib import Path
import hashlib
import re
import sys
import zipfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from minimal_features import CORE_SHA256

out = ROOT / 'analysis/fill-query/output'
out.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(ROOT / 'input/Core.ipa') as z:
    data = z.read('Payload/Core.app/Core')
assert hashlib.sha256(data).hexdigest() == CORE_SHA256
decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
for name, start, end in [('ray_api', 0x2ACC00, 0x2B0400),
                         ('ray_wrappers', 0xE0B00, 0xE0EB0),
                         ('actor_sources', 0xD4A08, 0xDB144)]:
    lines = [f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}'
             for i in decoder.disasm(data[start:end], start)]
    (out / f'{name}.txt').write_text('\n'.join(lines) + '\n')
strings = []
for m in re.finditer(rb'[\x20-\x7e]{8,}', data):
    if re.search(rb'(?i)rtcIntersect|rtcOccluded|embree|intersect1|thread.?safe', m[0]):
        strings.append(f'{m.start():08x} {m[0].decode("ascii")}')
(out / 'query_strings.txt').write_text('\n'.join(strings) + '\n')
print('Pinned native ray APIs, callers, actor sources and query strings written.')
