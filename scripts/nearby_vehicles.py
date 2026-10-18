"""Opt-in native vehicle labels, applied after the pinned minimal profile.

The existing vehicle metadata bit (+0x15, copied to cached record +0x4d)
identifies vehicles. Never enable the general loot or projectile switches.
Compilation, instruction execution and binary inspection belong in Actions.
"""
from minimal_features import adrp, branch, call, cbz, word

VERSION = "1"
PROFILE = "enemy-markers-600m-identity-colors-nearby-vehicles"
ARTIFACT = "Core-17.0-21A327-TrollStore-model-render-v12-nearby-vehicles.ipa"


def _ldrb(dst, base, offset):
    return word(0x39400000 | (offset << 10) | (base << 5) | dst)


def _cbnz(source, target, register):
    return word(int.from_bytes(cbz(source, target, register), "little") | 0x01000000)


def _tbz(source, target, register):
    delta = target - source
    if delta % 4 or not -(1 << 15) <= delta < (1 << 15):
        raise ValueError("TBZ out of range/alignment")
    return word(0x36000000 | (((delta // 4) & 0x3fff) << 5) | register)


def _range_check(start, scalar, reject, accept):
    # Inclusive finite [0,600], without changing the cached native distance.
    words = [word(0x1E260008 | (scalar << 5)),  # fmov w8, sN
             word(0x12007908),                # and w8, w8, #0x7fffffff
             word(0x12B01009),                # mov w9, #0x7f7fffff
             word(0x6B09011F),                # cmp w8, w9
             branch(start + 16, reject, 8),   # b.hi: NaN and infinities
             word(0x1E202008 | (scalar << 5)),# fcmp sN, #0
             branch(start + 24, reject, 11),  # b.lt
             word(0x52A882C8),                # mov w8, #0x44160000 (600f)
             word(0x1E270101),                # fmov s1, w8
             word(0x1E212000 | (scalar << 5)),# fcmp sN, s1
             branch(start + 40, reject, 12),  # b.gt
             branch(start + 44, accept)]
    return b"".join(words)


def changes():
    patches = []

    def add(offset, old, new, reason):
        if isinstance(old, str):
            old = bytes.fromhex(old)
        patches.append((offset, old, new, reason))

    # The enemy checkbox remains first. Both changed returns set the original
    # preference dirty bit; EndChild runs exactly once after the two checkboxes.
    add(0xCD7F4, cbz(0xCD7F4, 0xCD814, 0), cbz(0xCD7F4, 0xCD804, 0),
        "unchanged enemy checkbox continues to independent vehicle checkbox")
    add(0xCD804, "00e4002f", branch(0xCD804, 0xCD818), "show vehicle checkbox inside original child")
    menu = b"".join([
        adrp(0xCD818, 0x743000, 0), word(0x91000000 | (0xF71 << 10)),
        word(0x91000000 | (0x22 << 10) | (23 << 5) | 1),
        word(0x2F00E400), call(0xCD828, 0xC8928),
        cbz(0xCD82C, 0xCD83C, 0), adrp(0xCD830, 0xC4D000, 8),
        word(0x52800029), word(0x39000000 | (0xF86 << 10) | (8 << 5) | 9),
        call(0xCD83C, 0x12E620), branch(0xCD840, 0xCE0A0),
    ])
    add(0xCD818, branch(0xCD818, 0xCE0A0) + bytes.fromhex(
        "01082d1e0104081f2108291e4865a8520201271e0104021f0210251e0104021f0843a8520201271e"),
        menu, "independent persisted nearby-vehicle checkbox in bypassed UI panel")
    add(0x743F71, "显示骨骼".encode(), "附近载具".encode(), "vehicle checkbox label")

    # Filter before DA474 reads position. The source record has +0x10 type:
    # projectiles are 1, ordinary metadata-backed objects are 0. Only metadata
    # +0x15's original vehicle bit enters this path, irrespective of old loot,
    # item-enable, weapon-hiding, oil/health or category-distance preferences.
    collect = b"".join([
        _ldrb(8, 19, 0x22), cbz(0xD95A0, 0xD9EF4, 8),
        _ldrb(8, 24, 0x10), _cbnz(0xD95A8, 0xD9EF4, 8),
        word(0xF9400708), cbz(0xD95B0, 0xD9EF4, 8, True),
        _ldrb(8, 8, 0x15), _tbz(0xD95B8, 0xD9EF4, 8), word(0xD503201F),
    ])
    add(0xD959C, "084340391f0500718100005468b24039a80000375102001468aa40391f050071c1490054",
        collect, "collect only vehicle metadata while its independent switch is enabled")
    add(0xD97AC, "49034039", _ldrb(9, 26, 0x15), "vehicle bit replaces legacy item-enable gate")
    add(0xD9954, "48534039", branch(0xD9954, 0xD9E3C), "skip optional loot classification and vehicle oil reads")
    # Use a bypassed optional oil read block for the fixed finite range check.
    # Rejection restores w28 at D9EC8; on-screen acceptance uses native append.
    gather_range = _range_check(0xD9E3C, 11, 0xD9EC8, 0xD9E6C)
    gather_range += _tbz(0xD9E6C, 0xD9EC8, 20) + branch(0xD9E70, 0xD9EB0)
    gather_range += word(0xD503201F) * ((0x74 - len(gather_range)) // 4)
    add(0xD9E3C, "400740bd6021201e24040054400b40bd6021201e8802005208d59f1afca340b9c8040037e8b759391f05007141020054682641b908020034080f40f9c80100b400011b8bd3f0ff97ff533879ff130ef9e1074091218030914c030094c0000034e0134efde883189100c103fce8537879e8df0c79",
        gather_range, "finite inclusive 600-metre vehicle range and original screen-validity check")

    # The native loot vector is shared: explicitly reject projectiles, nonvehicle
    # records and invalid projections, then use the original name/string helper.
    add(0xDDEBC, word(0x52800008), _ldrb(8, 27, 0x22), "independent nearby-vehicle render switch")
    record_filter = b"".join([
        _ldrb(8, 20, 0x4D), cbz(0xDDF60, 0xDE570, 8),
        _ldrb(8, 20, 0x50), cbz(0xDDF68, 0xDE570, 8),
        _ldrb(8, 20, 8), _cbnz(0xDDF70, 0xDE570, 8),
        branch(0xDDF74, 0xDDF7C), word(0xD503201F),
    ])
    add(0xDDF5C, bytes.fromhex("89224039884241393f050071c102005428300034") + word(0x52800008) + bytes.fromhex("1f050071c12f0054"),
        record_filter, "render only on-screen vehicle records from the shared item vector")
    draw_range = word(0xBD403A80) + _range_check(0xDDF80, 0, 0xDE570, 0xDDFD0)
    add(0xDDF7C, "820e40fd883241391f050071211900544840201e4820201e0d060054600640bd0021201ea5050054600a40bd41040c5e2820201e",
        draw_range, "recheck finite 600-metre distance immediately before vehicle labels")
    add(0xDE3C4, "89364139", branch(0xDE3C4, 0xDE460), "vehicle name and distance without oil or health annotation")
    add(0xDE540, "623740bd42d8215e", word(0x1E259002) + word(0xD503201F), "vehicle label font 14 times native scale")
    add(0xDE558, "e30319aa", word(0x12A01FE3), "yellow vehicle labels with original black outline")
    return sorted(patches)


def nearby_vehicles(data):
    result = bytearray(data)
    end = 0
    for offset, old, new, reason in changes():
        if offset < end or len(old) != len(new):
            raise ValueError(f"Overlapping or resizing vehicle change: {offset:#x}")
        if data[offset:offset + len(old)] != old:
            raise ValueError(f"Unexpected vehicle instruction/string at {offset:#x}: {reason}")
        result[offset:offset + len(old)] = new
        end = offset + len(old)
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset), before=old.hex(), after=new.hex(), reason=reason)
            for offset, old, new, reason in changes()]
