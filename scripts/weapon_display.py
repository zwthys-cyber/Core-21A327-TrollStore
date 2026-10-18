"""Opt-in: only two ground bows; independent held-weapon text.

Apply after nearby_vehicles and overlay_latency. Instruction/data space was
reviewed in Actions run 39167783197. No new game offsets or weapon IDs.
"""
import json
from pathlib import Path
from minimal_features import adrp, branch, call, cbz, word
from nearby_vehicles import changes as vehicle_changes, _ldrb

VERSION = "1"
PROFILE = "enemy-markers-600m-vehicles-two-ground-bows-held-weapon-text"
PREDICATE = 0xCDA00
CROSSBOW_CLASS = 0xCDAC0


class Code:
    def __init__(self, start):
        self.start, self.parts, self.labels = start, [], {}

    def put(self, value):
        self.parts.append(value)

    def mark(self, name):
        self.labels[name] = self.start + len(self.parts) * 4

    def jump(self, target, condition=None):
        site = self.start + len(self.parts) * 4
        self.put(lambda: branch(site, self.labels[target] if isinstance(target, str) else target, condition))

    def zero(self, target, register, wide=False, nonzero=False):
        site = self.start + len(self.parts) * 4
        self.put(lambda: word(int.from_bytes(cbz(site, self.labels[target] if isinstance(target, str) else target, register, wide), "little") | (0x01000000 if nonzero else 0)))

    def bit(self, target, register, bit=0, nonzero=False):
        site = self.start + len(self.parts) * 4
        def encode():
            dest = self.labels[target] if isinstance(target, str) else target
            delta = dest - site
            if delta % 4 or not -(1 << 15) <= delta < (1 << 15):
                raise ValueError("Weapon TBZ target")
            return word(0x36000000 | (0x01000000 if nonzero else 0) | ((bit & 32) << 26) | ((bit & 31) << 19) | (((delta // 4) & 0x3fff) << 5) | register)
        self.put(encode)

    def finish(self):
        return b"".join(part() if callable(part) else part for part in self.parts)


def predicate():
    # x0 metadata -> w0 Boolean. Preserve x8 (collector source flag), x12
    # (renderer text buffer), callee-saved registers and every FP register.
    c = Code(PREDICATE)
    c.zero("no", 0, True)
    c.put(_ldrb(10, 0, 0x2f))
    c.bit("small", 10, 7)
    c.put(word(0xF940100A))  # ldr x10,[x0,#0x20] : long length
    c.put(word(0xF9400C09))  # ldr x9,[x0,#0x18] : long pointer
    c.zero("no", 9, True)
    c.jump("compare")
    c.mark("small")
    c.put(word(0x91006009))  # add x9,x0,#0x18
    c.mark("compare")
    for i, (length, literal) in enumerate([(9, 0x744C30), (12, 0x744C4A)]):
        c.put(word(0xF100015F | (length << 10)))  # cmp x10,#length
        c.jump(f"next{i}", 1)
        c.put(word(0xF940012B))  # ldr x11,[x9]
        site = c.start + len(c.parts) * 4
        c.put(adrp(site, literal, 13));c.put(word(0x910001AD | ((literal & 0xfff) << 10)))
        c.put(word(0xF94001AE));c.put(word(0xEB0E017F))  # ldr x14,[x13]; cmp x11,x14
        c.jump(f"next{i}", 1)
        c.put(_ldrb(0, 9, 8) if length == 9 else word(0xB9400920))
        c.put(_ldrb(15, 13, 8) if length == 9 else word(0xB94009AF))
        c.put(word(0x6B0F001F));c.jump("yes", 0)
        c.mark(f"next{i}")
    c.mark("no");c.put(word(0x52800000));c.put(word(0xD65F03C0))
    c.mark("yes");c.put(word(0x52800020));c.put(word(0xD65F03C0))
    return c.finish()


def helpers():
    regions = []
    c = Code(0xCD900)
    c.put(_ldrb(8, 24, 0x10));c.zero(0xD9EF4, 8, nonzero=True)
    c.put(word(0xF9400700));c.zero(0xD9EF4, 0, True)
    c.put(_ldrb(8, 0, 0x15));c.bit("ground", 8)
    c.put(_ldrb(8, 19, 0x22));c.zero(0xD9EF4, 8);c.jump(0xD95C0)
    c.mark("ground");c.put(_ldrb(8, 19, 0x21));c.zero(0xD9EF4, 8)
    c.put(call(c.start + len(c.parts) * 4, PREDICATE));c.zero(0xD9EF4, 0);c.jump(0xD95C0)
    regions.append((c.start, c.finish()))
    c = Code(0xCD950)
    c.put(_ldrb(9, 26, 0x15));c.bit("accept", 9, nonzero=True)
    c.put(_ldrb(9, 19, 0x21));c.zero(0xD9EF4, 9)
    c.put(word(0xAA1A03E0));c.put(call(c.start + len(c.parts) * 4, PREDICATE));c.zero(0xD9EF4, 0)
    c.mark("accept");c.put(word(0x52800029));c.jump(0xD97B0)
    regions.append((c.start, c.finish()))
    c = Code(0xCD980)
    c.put(_ldrb(8, 27, 0x22));c.put(_ldrb(9, 27, 0x21));c.put(word(0x2A090108));c.jump(0xDDEC0)
    regions.append((c.start, c.finish()))
    c = Code(0xCD9A0)
    c.put(_ldrb(8, 20, 0x50));c.zero(0xDE570, 8)
    c.put(_ldrb(8, 20, 8));c.zero(0xDE570, 8, nonzero=True)
    c.put(_ldrb(8, 20, 0x4D));c.bit("ground", 8)
    c.put(_ldrb(8, 27, 0x22));c.zero(0xDE570, 8);c.jump(0xDDF7C)
    c.mark("ground");c.put(_ldrb(8, 27, 0x21));c.zero(0xDE570, 8)
    c.put(word(0xF9400A80));c.put(call(c.start + len(c.parts) * 4, PREDICATE));c.zero(0xDE570, 0);c.jump(0xDDF7C)
    regions.append((c.start, c.finish()))
    regions.extend([(PREDICATE, predicate()), (CROSSBOW_CLASS, b"BP_Other_CrossBow_Wrapper_C\0")])
    buffer = bytearray(word(0xD503201F) * 128)
    end = 0xCD900
    for start, data in sorted(regions):
        if start < end or start + len(data) > 0xCDB00:
            raise ValueError("Overlapping weapon helper space")
        buffer[start - 0xCD900:start - 0xCD900 + len(data)] = data;end = start + len(data)
    return bytes(buffer)


def changes():
    reviewed = json.loads((Path(__file__).resolve().parents[1] / "analysis/weapon-display/source-layout.json").read_text())
    patches = []
    def add(offset, old, new, reason):
        patches.append((offset, bytes.fromhex(old) if isinstance(old, str) else old, new, reason))
    add(0xCD858, cbz(0xCD858, 0xCD868, 0), cbz(0xCD858, 0xCD870, 0), "fill unchanged continues to weapon controls")
    add(0xCD868, call(0xCD868, 0x12E620), branch(0xCD868, 0xCD870), "fill changed continues to weapon controls")
    menu = []
    for start, label, preference in [(0xCD870, 0x743F57, 0x21), (0xCD894, 0x743F7E, 0x24)]:
        menu.extend([adrp(start, label, 0), word(0x91000000 | ((label & 0xfff) << 10)),
                     word(0x91000000 | (preference << 10) | (23 << 5) | 1), word(0x2F00E400),
                     call(start+16, 0xC8928), cbz(start+20, start+36, 0), adrp(start+24, 0xC4D000, 8),
                     word(0x52800029), word(0x39000000 | (0xF86 << 10) | (8 << 5) | 9)])
    menu.extend([call(0xCD8B8, 0x12E620), branch(0xCD8BC, 0xCE0A0), word(0xD503201F), word(0xD503201F)])
    add(0xCD870, reviewed['0xcd870'], b''.join(menu), "independent persisted ground-bow and held-name controls; one EndChild")
    add(0x743F57, "显示方框".encode(), "弓弩位置".encode(), "ground bows control")
    add(0x743F7E, "人机显示".encode(), "手持武器".encode(), "held weapon name control")
    add(0xCD900, reviewed['0xcd900'], helpers(), "bounded name predicate and existing native collection/render bridges")
    old_vehicle = {offset: new for offset, old, new, reason in vehicle_changes()}
    add(0xD959C, old_vehicle[0xD959C], branch(0xD959C, 0xCD900) + word(0xD503201F)*8, "reject unrelated loot before position reads")
    add(0xD97AC, old_vehicle[0xD97AC], branch(0xD97AC, 0xCD950), "allow only vehicles or the two selected ground weapon names")
    add(0xDDEBC, old_vehicle[0xDDEBC], branch(0xDDEBC, 0xCD980), "independent vehicle-or-bow shared vector gate")
    add(0xDDF5C, old_vehicle[0xDDF5C], branch(0xDDF5C, 0xCD9A0) + word(0xD503201F)*7, "recheck whitelist, independent switch and native screen validity")
    # Repurpose only the unused ground Mortar entry in this minimal profile;
    # keep the native category, source reader and classifier implementation.
    add(0xF5614, "81320090", adrp(0xF5614, CROSSBOW_CLASS, 1), "ground crossbow wrapper classifier pattern")
    add(0xF5618, "21ec0391", word(0x91000021 | ((CROSSBOW_CLASS & 0xfff) << 10)), "ground crossbow wrapper pattern offset")
    add(0xF5630, "81320090", adrp(0xF5630, 0x744C30, 1), "crossbow ground name literal")
    add(0xF5634, "21540491", word(0x91000021 | (0xC30 << 10)), "crossbow ground name")
    add(0xF5658, "81320090", adrp(0xF5658, 0x744C4A, 1), "canonical explosive hunting bow label")
    add(0xF565C, "21f40491", word(0x91000021 | (0xC4A << 10)), "canonical explosive hunting bow name")
    add(0xDB680, word(0x52800008), _ldrb(8, 27, 0x24), "independent held weapon name gate for existing enemy markers")
    add(0xDD32C, "e82341b9", word(0x52800028), "held weapon text selection independent of old icon preference")
    add(0xDD398, "60820c91", word(0x12800000), "white held weapon text")
    add(0xDD39C, "26000194", word(0xD503201F), "keep fixed text color without old color conversion")
    add(0xDD3A8, "0190241e", word(0x1E259001), "held weapon text 14 times native scale")
    return sorted(patches)


def weapon_display(data):
    result=bytearray(data);end=0
    for offset, old, new, reason in changes():
        if offset<end or len(old)!=len(new):raise ValueError("Overlapping or resizing weapon display change")
        if data[offset:offset+len(old)]!=old:raise ValueError(f"Unexpected weapon display source at {offset:#x}: {reason}")
        result[offset:offset+len(old)]=new;end=offset+len(old)
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset),before=old.hex(),after=new.hex(),reason=reason) for offset,old,new,reason in changes()]
