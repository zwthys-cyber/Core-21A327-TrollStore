"""Extend v16's ground whitelist using the original medicine/equipment names.

Apply after weapon_display. Keep its native source, position, range and draw
paths; replace only the exact-name predicate in the previously reviewed helper
space and the shared ground-items checkbox label.
"""
import struct
from minimal_features import adrp,word
from nearby_vehicles import _ldrb
from weapon_display import Code,PREDICATE,helpers

VERSION='1'
PROFILE='enemy-markers-600m-vehicles-two-bows-painkiller-level3-bag-energy-drink-held-weapon-text'
TABLE=0xCDA88
BAG_NAME=0xCDAB0
# Names/addresses are from the pinned original native classifier, reviewed in
# Actions. The original classifier still constructs these metadata records.
ITEMS=[('十字弩',0x744C30),('爆炸猎弓',0x744C4A),('止痛药',0x745D94),('三级包',BAG_NAME),('可乐',0x745D80)]


def predicate():
    # Leaf function: preserve x8 (source flag), x12 (native text buffer), all
    # callee-saved and FP registers. Compare complete strings byte by byte;
    # length must match before reading any text. At most five short names.
    c=Code(PREDICATE)
    c.zero('no',0,True);c.put(_ldrb(10,0,0x2f));c.bit('small',10,7)
    c.put(word(0xF940100A));c.put(word(0xF9400C09));c.zero('no',9,True);c.jump('lookup')
    c.mark('small');c.put(word(0x91006009))
    c.mark('lookup');site=c.start+len(c.parts)*4;c.put(adrp(site,TABLE,13));c.put(word(0x910001AD|((TABLE&0xfff)<<10)))
    c.put(word(0x5280000F|(len(ITEMS)<<5)))  # mov w15,#number of names
    c.mark('entry');c.put(word(0xB94001AB));c.put(word(0xB94005A0))  # literal offset, length
    site=c.start+len(c.parts)*4;c.put(adrp(site,0,14));c.put(word(0x8B0B01CB))  # image base + literal offset
    c.put(word(0xEB0A001F));c.jump('next',1)  # cmp x0,x10
    c.put(word(0xAA0903EE));c.put(word(0x2A0A03E0))  # input cursor; remaining bytes
    c.mark('byte');c.put(_ldrb(16,14,0));c.put(_ldrb(17,11,0));c.put(word(0x6B11021F));c.jump('next',1)
    c.put(word(0x910005CE));c.put(word(0x9100056B));c.put(word(0x71000400));c.jump('byte',1)
    c.put(word(0x52800020));c.put(word(0xD65F03C0))
    c.mark('next');c.put(word(0x910021AD));c.put(word(0x710005EF));c.jump('entry',1)
    c.mark('no');c.put(word(0x52800000));c.put(word(0xD65F03C0))
    code=c.finish()
    if PREDICATE+len(code)>TABLE:raise ValueError('Ground predicate overlaps table')
    table=b''.join(struct.pack('<II',address,len(name.encode())) for name,address in ITEMS)
    if TABLE+len(table)!=BAG_NAME:raise ValueError('Ground table overlaps level 3 bag label')
    label='三级包'.encode()+b'\0'
    if BAG_NAME+len(label)>0xCDAC0:raise ValueError('Ground label overlaps existing crossbow class')
    return code+word(0xD503201F)*((TABLE-PREDICATE-len(code))//4)+table+label.ljust(0xCDAC0-BAG_NAME,b'\0')


def changes():
    old=helpers()[PREDICATE-0xCD900:0xCDAC0-0xCD900]
    return [(PREDICATE,old,predicate(),'exact names for two bows, painkiller, level 3 backpack and energy drink'),
            (0xF6BDC,bytes.fromhex('613200f0'),adrp(0xF6BDC,BAG_NAME,1),'canonical level 3 bag name for original Bag_Lv3 entry'),
            (0xF6BE0,bytes.fromhex('21e03791'),word(0x91000021|((BAG_NAME&0xfff)<<10)),'clear level 3 bag label instead of original spaced abbreviation'),
            (0x743F57,'弓弩位置'.encode(),'物资位置'.encode(),'shared persisted ground-items control'),
            (0x745D80,'饮料'.encode(),'可乐'.encode(),'requested energy drink name; original ink_Pickup_C classifier unchanged')]


def ground_essentials(data):
    result=bytearray(data);end=0
    for offset,old,new,reason in sorted(changes()):
        if offset<end or len(old)!=len(new):raise ValueError('Overlapping or resizing ground essentials change')
        if data[offset:offset+len(old)]!=old:raise ValueError(f'Unexpected ground essentials source at {offset:#x}: {reason}')
        result[offset:offset+len(old)]=new;end=offset+len(old)
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset),before=old.hex(),after=new.hex(),reason=reason) for offset,old,new,reason in sorted(changes())]
