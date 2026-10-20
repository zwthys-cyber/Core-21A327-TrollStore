"""Read-only v17 ground cache gates/projection review in Actions only."""
from pathlib import Path
import hashlib,json,sys,zipfile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from minimal_features import CORE_SHA256,minimal_core
from nearby_vehicles import nearby_vehicles
from overlay_latency import overlay_latency
from weapon_display import weapon_display
from ground_essentials import ground_essentials
with zipfile.ZipFile(ROOT/'input/Core.ipa') as z:
    original=z.read('Payload/Core.app/Core')
assert hashlib.sha256(original).hexdigest()==CORE_SHA256
patched=ground_essentials(weapon_display(overlay_latency(nearby_vehicles(minimal_core(original)))))
out=ROOT/'analysis/ground-cache/output';out.mkdir(parents=True,exist_ok=True)
decoder=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
for label,data in [('original',original),('v17',patched)]:
    for name,start,end in [('projector',0xDA704,0xDB144),('collector_prelude',0xD88E4,0xD8C00),('collector_gates',0xD9514,0xD97C8),('collector_append',0xD9948,0xD9958),('range_append',0xD9E3C,0xD9F10),('helpers',0xCD900,0xCDA88)]:
        (out/f'{label}-{name}.txt').write_text('\n'.join(f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in decoder.disasm(data[start:end],start))+'\n')
print('Reviewed exact v17 projector, pre-cache screen gates, range append and helper register use; no device execution.')
