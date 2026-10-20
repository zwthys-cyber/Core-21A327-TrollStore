"""Original airdrop classifier and saved boolean slots; Actions only."""
from pathlib import Path
import hashlib,json,sys,zipfile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from minimal_features import CORE_SHA256
with zipfile.ZipFile(ROOT/'input/Core.ipa') as z:data=z.read('Payload/Core.app/Core')
assert hashlib.sha256(data).hexdigest()==CORE_SHA256
out=ROOT/'analysis/ground-item-controls/output';out.mkdir(parents=True,exist_ok=True)
d=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);d.detail=True
layout={hex(0xCDB00):data[0xCDB00:0xCDE00].hex()}
(out/'source-layout.json').write_text(json.dumps(layout,indent=2)+'\n')
for name,start,end in [('ui',0xCD6A4,0xCE0EC),('airdrop_constructor',0xF6300,0xF6700)]:
    (out/f'{name}.txt').write_text('\n'.join(f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in d.disasm(data[start:end],start))+'\n')
strings={};cursor=0x7459F3
while cursor<0x745A80:
    end=data.find(b'\0',cursor,0x745B00)
    assert end>=cursor
    strings[hex(cursor)]=data[cursor:end].decode();cursor=end+1
(out/'airdrop-strings.json').write_text(json.dumps(strings,ensure_ascii=False,indent=2)+'\n')
# Record references to known globals and strings, with short native windows.
refs=[];incoming=[];last={};targets={0x7459F3,0x745A00,0x745A07,0x745A12,0x745A2E,0xC54270,0xC4DF86}
for i in d.disasm(data[0x4000:0x723B80],0x4000):
    if i.mnemonic=='adrp' and len(i.operands)==2:last[i.operands[0].reg]=(i.address,i.operands[1].imm)
    elif i.mnemonic=='add' and len(i.operands)==3 and i.operands[2].type==2:
        page=last.get(i.operands[1].reg)
        if page and i.address-page[0]<96 and page[1]+i.operands[2].imm in targets:
            refs.append({'at':hex(i.address),'target':hex(page[1]+i.operands[2].imm)})
    if i.mnemonic.startswith(('b','cb','tb')):
        for operand in i.operands:
            if operand.type==2 and 0xCDB00<=operand.imm<0xCDE00 and not 0xCD6A4<=i.address<0xCE0EC:
                incoming.append({'at':hex(i.address),'target':hex(operand.imm),'instruction':i.mnemonic+' '+i.op_str})
(out/'references.json').write_text(json.dumps(refs,indent=2)+'\n')
(out/'external-incoming.json').write_text(json.dumps(incoming,indent=2)+'\n')
with (out/'reference-windows.txt').open('w') as f:
    for ref in refs:
        start=max(0x4000,int(ref['at'],16)-64);end=int(ref['at'],16)+100
        f.write(f'REF {ref}\n')
        f.write('\n'.join(f'{i.address:08x} {i.bytes.hex():8s} {i.mnemonic:8s} {i.op_str}' for i in d.disasm(data[start:end],start))+'\n')
print('Original airdrop names, constructor references, UI helper space and preference-global call sites reviewed.')
