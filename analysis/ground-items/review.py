"""Read-only original ground item identifiers and classifier review; Actions only."""
from pathlib import Path
import hashlib,json,struct,sys,zipfile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from minimal_features import CORE_SHA256
with zipfile.ZipFile(ROOT/'input/Core.ipa') as z:
    original=z.read('Payload/Core.app/Core')
assert hashlib.sha256(original).hexdigest()==CORE_SHA256
out=ROOT/'analysis/ground-items/output';out.mkdir(parents=True,exist_ok=True)
d=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);d.detail=True
strings={}
for term in ['止痛药','三级包','三级背包','背包','Painkiller','PainKiller','Bag_Lv3','Bag_Lv','Backpack']:
    needle=term.encode();position=0
    while (position:=original.find(needle,position))>=0:
        start=original.rfind(b'\0',max(0,position-120),position)+1
        end=original.find(b'\0',position,min(len(original),position+160))
        if end>=0:
            strings[hex(start)]=original[start:end].decode('utf-8',errors='replace')
        position+=len(needle)
(out/'strings.json').write_text(json.dumps(strings,ensure_ascii=False,indent=2)+'\n')
for name,start,end in [('ground_classifier',0xF6950,0xF6D40),('collector',0xD9514,0xD9F10),('renderer',0xDDEBC,0xDE5A4)]:
    (out/f'{name}.txt').write_text('\n'.join(f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in d.disasm(original[start:end],start))+'\n')
# Report code xrefs to the existing literal starts. Decode only on the runner.
wanted={int(a,16) for a in strings};refs=[];last={}
for i in d.disasm(original[0x4000:0x740000],0x4000):
    if i.mnemonic=='adrp' and len(i.operands)==2:
        last[i.operands[0].reg]=(i.address,i.operands[1].imm)
    elif i.mnemonic=='add' and len(i.operands)==3 and i.operands[2].type==2:
        page=last.get(i.operands[1].reg)
        if page and i.address-page[0]<=96 and page[1]+i.operands[2].imm in wanted:
            address=page[1]+i.operands[2].imm
            refs.append({'adrp':hex(page[0]),'add':hex(i.address),'literal':hex(address),'text':strings[hex(address)]})
(out/'literal-references.json').write_text(json.dumps(refs,ensure_ascii=False,indent=2)+'\n')
neighbors={};cursor=original.rfind(b'\0',0x745D00,0x745D10)+1
while cursor<0x745E70:
    end=original.find(b'\0',cursor)
    if end<0:break
    neighbors[hex(cursor)]=original[cursor:end].decode('utf-8',errors='replace');cursor=end+1
(out/'neighbor-strings.json').write_text(json.dumps(neighbors,ensure_ascii=False,indent=2)+'\n')
print('Reviewed existing painkiller/backpack names, class strings and native constructor sites without game execution.')
