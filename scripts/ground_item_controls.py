"""Six exact ground names with independent persisted controls; after v18 gates."""
import json,struct
from pathlib import Path
from minimal_features import adrp,branch,call,cbz,word
from nearby_vehicles import _ldrb
from weapon_display import Code,helpers
from model_render_patch import DRAW_IMPORT_STUB

VERSION='1'
PROFILE='enemy-markers-600m-six-independent-ground-items-final-camera'
ARTIFACT='Core-17.0-21A327-TrollStore-model-render-v18-ground-controls.ipa'
MENU=0xCDB00
PREDICATE=0xCDC00
TABLE=0xCDCA0
GLOBAL_GATE=0xCDCF0
COMMIT=0xCDD40
OPEN_RETURN=MENU+8
COMMIT_RETURN=COMMIT+8
# Slots are only local mirrors. Dedicated NSUserDefaults mask is authoritative;
# old enum/preference serialization cannot couple the new item controls.
ITEMS=[('爆炸猎弓',0x744C4A,0x21),('十字弩',0x744C30,0x25),('止痛药',0x745D94,0x26),('三级包',0xCDAB0,0x27),('可乐',0x745D80,0x2A),('空投',0x745A00,0x29)]


def menu():
    c=Code(MENU);c.put(word(0xAA1703E0));c.put(call(MENU+4,DRAW_IMPORT_STUB));c.put(word(0xAA0003F4))  # x20 editable flag array
    for i,(name,label,preference) in enumerate(ITEMS):
        start=c.start+len(c.parts)*4
        c.put(adrp(start,label,0));c.put(word(0x91000000|((label&0xfff)<<10)))
        c.put(word(0x91000000|(i<<10)|(20<<5)|1));c.put(word(0x2F00E400))
        c.put(call(start+16,0xC8928));c.put(cbz(start+20,start+32,0))
        c.put(word(0xAA1703E0));c.put(call(start+28,COMMIT))
    start=c.start+len(c.parts)*4;label=0x743F7E
    for ins in [adrp(start,label,0),word(0x91000000|((label&0xfff)<<10)),word(0x91000000|(0x24<<10)|(23<<5)|1),word(0x2F00E400),call(start+16,0xC8928),cbz(start+20,start+36,0),adrp(start+24,0xC4D000,8),word(0x52800029),word(0x39000000|(0xF86<<10)|(8<<5)|9)]:c.put(ins)
    c.put(call(c.start+len(c.parts)*4,0x12E620));c.jump(0xCE0A0)
    return c.finish()


def predicate():
    # Metadata x0, preference mirrors x1. Leaf; x8/x12 and all FP preserved.
    c=Code(PREDICATE);c.zero('no',1,True);c.zero('no',0,True);c.put(_ldrb(10,0,0x2f));c.bit('small',10,7)
    c.put(word(0xF940100A));c.put(word(0xF9400C09));c.zero('no',9,True);c.jump('lookup')
    c.mark('small');c.put(word(0x91006009))
    c.mark('lookup');site=c.start+len(c.parts)*4;c.put(adrp(site,TABLE,13));c.put(word(0x910001AD|((TABLE&0xfff)<<10)));c.put(word(0x5280000F|(len(ITEMS)<<5)))
    c.mark('entry');c.put(word(0xB94001AB));c.put(word(0xB94005A0));site=c.start+len(c.parts)*4;c.put(adrp(site,0,14));c.put(word(0x8B0B01CB))
    c.put(word(0xEB0A001F));c.jump('next',1);c.put(word(0xAA0903EE));c.put(word(0x2A0A03E0))
    c.mark('byte');c.put(_ldrb(16,14,0));c.put(_ldrb(17,11,0));c.put(word(0x6B11021F));c.jump('next',1)
    c.put(word(0x910005CE));c.put(word(0x9100056B));c.put(word(0x71000400));c.jump('byte',1)
    c.put(word(0xB94009AF));c.put(word(0x386F6820));c.put(word(0x7100001F));c.put(word(0x1A9F07E0));c.put(word(0xD65F03C0))
    c.mark('next');c.put(word(0x910031AD));c.put(word(0x710005EF));c.jump('entry',1)
    c.mark('no');c.put(word(0x52800000));c.put(word(0xD65F03C0))
    return c.finish()


def collector_helper():
    c=Code(0xCD900);c.put(_ldrb(8,24,0x10));c.zero(0xD9EF4,8,nonzero=True);c.put(word(0xF9400700));c.zero(0xD9EF4,0,True)
    c.put(_ldrb(8,0,0x15));c.bit('ground',8);c.put(_ldrb(8,19,0x22));c.zero(0xD9EF4,8);c.jump(0xD95C0)
    c.mark('ground');c.put(word(0xAA1303E1));c.put(call(c.start+len(c.parts)*4,PREDICATE));c.zero(0xD9EF4,0);c.jump(0xD95C0)
    return c.finish()


def late_helper():
    c=Code(0xCD950);c.put(_ldrb(9,26,0x15));c.bit('accept',9,nonzero=True);c.put(word(0xAA1A03E0));c.put(word(0xAA1303E1))
    c.put(call(c.start+len(c.parts)*4,PREDICATE));c.zero(0xD9EF4,0);c.mark('accept');c.put(word(0x52800029));c.jump(0xD97B0)
    return c.finish()


def render_helper():
    c=Code(0xCD9A0);c.put(_ldrb(8,20,0x50));c.zero(0xDE570,8);c.put(_ldrb(8,20,8));c.zero(0xDE570,8,nonzero=True)
    c.put(_ldrb(8,20,0x4D));c.bit('ground',8);c.put(_ldrb(8,27,0x22));c.zero(0xDE570,8);c.jump(0xDDF7C)
    c.mark('ground');c.put(word(0xF9400A80));c.put(word(0xAA1B03E1));c.put(call(c.start+len(c.parts)*4,PREDICATE));c.zero(0xDE570,0);c.jump(0xDDF7C)
    return c.finish()


def extra_code():
    gate=Code(GLOBAL_GATE)
    for register,offset in [(8,0x21),(9,0x22),(10,0x25),(11,0x26),(13,0x27),(14,0x2A),(15,0x29)]:gate.put(_ldrb(register,27,offset))
    for register in [9,10,11,13,14,15]:gate.put(word(0x2A000000|(register<<16)|(8<<5)|8))
    gate.put(word(0x7100011F));gate.put(word(0x1A9F07E8));gate.jump(0xDDEC0)
    commit=word(0xA9BF7BFD)+call(COMMIT+4,DRAW_IMPORT_STUB)+word(0xA8C17BFD)+word(0xD65F03C0)
    table=b''.join(struct.pack('<III',label,len(name.encode()),offset) for name,label,offset in ITEMS)
    regions=[(MENU,menu()),(PREDICATE,predicate()),(TABLE,table),(GLOBAL_GATE,gate.finish()),(COMMIT,commit)]
    result=bytearray(word(0xD503201F)*((0xCDE00-MENU)//4));end=MENU
    for start,code in regions:
        if start<end or start+len(code)>0xCDE00:raise ValueError('Ground control helper overlap')
        result[start-MENU:start-MENU+len(code)]=code;end=start+len(code)
    return bytes(result)


def changes():
    from weapon_display import changes as weapon_changes
    old={offset:new for offset,old,new,reason in weapon_changes()}
    layout=json.loads((Path(__file__).resolve().parents[1]/'analysis/ground-item-controls/source-layout.json').read_text())
    original_helpers=helpers();changes=[(0xCD6F0,bytes.fromhex('286ca852'),word(0x52800008),'use available native child height for all ten controls and its scrollbar'),
        (0xCD870,old[0xCD870],branch(0xCD870,MENU)+word(0xD503201F)*((len(old[0xCD870])-4)//4),'six independent item checkboxes with dedicated persisted mask; held control remains separate'),
        (0xDDEBC,branch(0xDDEBC,0xCD980),branch(0xDDEBC,GLOBAL_GATE),'any enabled item or vehicle can enter native shared record rendering'),
        (MENU,bytes.fromhex(layout[hex(MENU)]),extra_code(),'reviewed unreachable UI space for six controls and exact enabled-name predicate')]
    for start,end,code in [(0xCD900,0xCD950,collector_helper()),(0xCD950,0xCD980,late_helper()),(0xCD9A0,0xCDA00,render_helper())]:
        if start+len(code)>end:raise ValueError('Ground bridge exceeds original helper space')
        changes.append((start,original_helpers[start-0xCD900:end-0xCD900],code+word(0xD503201F)*((end-start-len(code))//4),'independent enabled-name selection without the former ground master gate'))
    return sorted(changes)


def ground_item_controls(data):
    result=bytearray(data);end=0
    for offset,old,new,reason in changes():
        if offset<end or len(old)!=len(new):raise ValueError('Overlapping or resizing ground item controls')
        if data[offset:offset+len(old)]!=old:raise ValueError(f'Unexpected ground item control source at {offset:#x}: {reason}')
        result[offset:offset+len(old)]=new;end=offset+len(old)
    return bytes(result)


def manifest():return [dict(offset=hex(offset),before=old.hex(),after=new.hex(),reason=reason) for offset,old,new,reason in changes()]
