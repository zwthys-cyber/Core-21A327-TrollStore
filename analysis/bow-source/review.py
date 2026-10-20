"""Read-only native source/range review, executed in GitHub Actions only."""
from pathlib import Path
import hashlib,json,struct,sys,zipfile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from minimal_features import CORE_SHA256,minimal_core
from nearby_vehicles import nearby_vehicles
from overlay_latency import overlay_latency
from weapon_display import weapon_display
with zipfile.ZipFile(ROOT/'input/Core.ipa') as z:
    original=z.read('Payload/Core.app/Core')
assert hashlib.sha256(original).hexdigest()==CORE_SHA256
patched=weapon_display(overlay_latency(nearby_vehicles(minimal_core(original))))
out=ROOT/'analysis/bow-source/output';out.mkdir(parents=True,exist_ok=True)
d=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
for label,data in [('original',original),('v15',patched)]:
    for name,start,end in [('source',0xD53E0,0xD5AB0),('source_helpers',0xD632C,0xD6FB8),('collect',0xD9514,0xD9F10),('world_position',0xDA474,0xDA704),('item_render',0xDDEBC,0xDE5A4)]:
        (out/f'{label}-{name}.txt').write_text('\n'.join(f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in d.disasm(data[start:end],start))+'\n')
constants={}
for address in [0xAC82D4,0x797978,0xAC8B40,0xAC8B48,0xAC8B50]:
    constants[hex(address)]={'hex':original[address:address+8].hex(),'float':struct.unpack_from('<f',original,address)[0],'double':struct.unpack_from('<d',original,address)[0]}
(out/'constants.json').write_text(json.dumps(constants,indent=2)+'\n')
print('Reviewed complete original/patched source, acquisition, position and render paths; constants decoded only in Actions.')
