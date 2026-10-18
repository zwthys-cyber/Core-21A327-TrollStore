"""Review the pinned native vehicle collection/UI/render paths in Actions only."""
from pathlib import Path
import hashlib
import json
import sys
import zipfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from minimal_features import CORE_SHA256

out = ROOT / 'analysis/nearby-vehicles/output'
out.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(ROOT / 'input/Core.ipa') as z:
    data = z.read('Payload/Core.app/Core')
assert hashlib.sha256(data).hexdigest() == CORE_SHA256
decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
for name, start, end in [('ui', 0xCD6A4, 0xCE0EC),
                         ('collector', 0xD4A08, 0xDB144),
                         ('render', 0xDB144, 0xDF47C)]:
    lines = [f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}'
             for i in decoder.disasm(data[start:end], start)]
    (out / f'{name}.txt').write_text('\n'.join(lines) + '\n')
strings = []
cursor = 0x73D000
while cursor < 0x750000:
    end = data.find(b'\0', cursor, 0x750000)
    if end < 0:
        break
    try:
        value = data[cursor:end].decode('utf-8')
        if value and value.isprintable():
            strings.append({'offset': hex(cursor), 'text': value})
    except UnicodeError:
        pass
    cursor = end + 1
(out / 'strings.json').write_text(json.dumps(strings, ensure_ascii=False, indent=2) + '\n')
print('Pinned native UI, collector, renderer and string references written.')
