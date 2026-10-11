"""Limit the reviewed Core binary to enemy markers within 200 metres.

This changes the application's UI and feature execution only. It does not
change the game, the kernel backend, target process names, or detection paths.
All changes are static, before signing; no executable memory is written at run
time. Unused original routines remain in the binary.
"""
import hashlib
import struct

CORE_SHA256 = "ed69f33902e17c499aa7904751a01ce3521a663503fb8958ea01a7c5489d5c58"
PROFILE = "enemy-markers-200m"


def word(value):
    return struct.pack("<I", value)


def branch(source, target, condition=None):
    delta = target - source
    if delta % 4:
        raise ValueError("Unaligned branch")
    if condition is None:
        if not -(1 << 27) <= delta < (1 << 27):
            raise ValueError("Branch out of range")
        return word(0x14000000 | ((delta // 4) & 0x3FFFFFF))
    if not -(1 << 20) <= delta < (1 << 20):
        raise ValueError("Conditional branch out of range")
    return word(0x54000000 | (((delta // 4) & 0x7FFFF) << 5) | condition)


def cbz(source, target, register, wide=False):
    delta = target - source
    if delta % 4 or not -(1 << 20) <= delta < (1 << 20):
        raise ValueError("CBZ out of range/alignment")
    return word((0xB4000000 if wide else 0x34000000) |
                (((delta // 4) & 0x7FFFF) << 5) | register)


def adrp(source, target, register):
    pages = ((target & ~0xFFF) - (source & ~0xFFF)) // 4096
    if not -(1 << 20) <= pages < (1 << 20):
        raise ValueError("ADRP out of range")
    return word(0x90000000 | ((pages & 3) << 29) |
                (((pages >> 2) & 0x7FFFF) << 5) | register)


# Original instruction encodings recorded from the pinned executable.
# Replacing reads with MOV #0 makes old preferences unable to reactivate these
# paths; the registers, subsequent conditions, and original cleanup stay intact.
DISABLED_READS = {
    0xDB2FC: "68f34339",  # radar frame
    0xDB3CC: "68234439",  # threat list collection
    0xDB46C: "6a974039",  # offscreen distance/indicator
    0xDB470: "69874039",  # offscreen indicator
    0xDB5EC: "08014039",  # name/health panel style: native fallback is none
    0xDB680: "08014039",  # weapon (both player categories)
    0xDB69C: "68f34339",  # radar player markers
    0xDB864: "68234439",  # threat collection
    0xDBBC4: "68b34039",  # tracked equipment
    0xDCDC0: "68bb4039",  # trajectory
    0xDD490: "6aab4539",  # aim circle
    0xDD4B0: "69a74539",  # aim circle
    0xDD5F0: "683b4639",  # recoil circle
    0xDD758: "68234439",  # threat list display
    0xDDEBC: "68ab4039",  # loot
    0xDDEC8: "68b34039",  # equipment
    0xDDF70: "68b34039",  # equipment
    0xDDFC4: "68ab4039",  # loot
    0xDE014: "6a574439",  # supplementary item markers
    0xDE5B4: "68334639",  # recoil
    0xDE5D0: "683b4639",  # recoil circle
    0xDE734: "68b34539",  # auxiliary aim display
    0xDEBE8: "68134739",  # auxiliary input display
    0xDEC30: "68af4539",  # aim connection line
    0xDED8C: "689f4039",  # player count
    0xDED94: "68d34239",  # bot count
    0xDEE00: "689f4039",  # player count layout
    0xDEE08: "77d34239",  # bot count layout
}


def changes():
    patches = []

    def add(offset, expected, replacement, reason):
        expected = bytes.fromhex(expected) if isinstance(expected, str) else expected
        patches.append((offset, expected, replacement, reason))

    # Void input worker: return before PACIBSP, with incoming SP/LR unchanged.
    # Covers aim and recoil processing even if a saved toggle was enabled.
    add(0xC1A04, "7f2303d5", bytes.fromhex("c0035fd6"), "disable input worker")
    for offset, target, expected, reason in [
        (0xCBE64, 0xCC0A4, "545c00b0", "remove cover controls"),
        (0xCC948, 0xCD2DC, "40834abd", "remove theme/FPS/performance panels after EndChild"),
        (0xD07E0, 0xD097C, "0041201e", "remove all other sidebar tabs"),
        (0xD098C, 0xD09C8, "1f110071", "map stale other tabs to initialization"),
        (0xCD778, 0xCD79C, "00008052", "remove category/style controls"),
        (0xCD7A4, 0xCD7DC, "b33300d0", "remove line/box controls"),
        (0xCD804, 0xCD814, "00e4002f", "finish the sole checkbox"),
        (0xCD818, 0xCE0A0, "00834abd", "remove remaining player panels after EndChild"),
        (0xDC858, 0xDC9C0, "e8524039", "keep distance text instead of rescue countdown"),
    ]:
        add(offset, expected, branch(offset, target), reason)
    add(0xD0988, "ed000054", branch(0xD0988, 0xD09D0, 0), "dispatch only position page or initialization")
    # The checkbox's changed return still sets the original preference dirty bit.
    add(0xCD7F4, "e04f00b9", word(0x34000000 | (8 << 5)), "unchanged checkbox skips dirty write")
    delta_pages = (0xC4D000 - (0xCD7F8 & ~0xFFF)) // 4096
    adrp_x8 = 0x90000008 | ((delta_pages & 3) << 29) | (((delta_pages >> 2) & 0x7FFFF) << 5)
    add(0xCD7F8, "b63300d0", word(adrp_x8), "original dirty-bit page")
    add(0xCD7FC, "d6c63d91", word(0x52800029), "changed checkbox sets dirty")
    add(0xCD800, "e18a0091", word(0x39000000 | (0xF86 << 10) | (8 << 5) | 9), "original dirty bit")
    for offset, expected in DISABLED_READS.items():
        destination = int.from_bytes(bytes.fromhex(expected), "little") & 31
        add(offset, expected, word(0x52800000 | destination), "disable unrelated render gate")
    # Native line, box, distance and skeleton use the same preference for both
    # player categories. Old category switches cannot enable another feature.
    for offset in (0xDB630, 0xDB644, 0xDB658, 0xDB66C):
        add(offset, "08014039", word(0x39400000 | (0x23 << 10) | (27 << 5) | 8), "shared nearby-enemy toggle")

    # Use unreachable aim-candidate instructions inside the same renderer frame
    # for two branch-only helpers; preserve its PAC, stack and unwind layout.
    # The unconditional loop continuation prevents normal fallthrough into them.
    add(0xDD464, "e86f41b9", branch(0xDD464, 0xDBDC8), "disable aim-candidate processing")
    add(0xDB1B0, "68a34539", branch(0xDB1B0, 0xDD4C0), "synchronize visibility request and keep aim off")
    helper = b"".join([
        word(0x39400000 | (0x23 << 10) | (27 << 5) | 9),  # ldrb w9, [x27, #0x23]
        word(0x39000000 | (0x2F << 10) | (27 << 5) | 9),  # strb w9, [x27, #0x2f]
        word(0x52800008),                                # mov w8, #0 (aim)
        branch(0xDD4CC, 0xDB1B4),
        word(0x39400000 | (0x1E9 << 10) | (19 << 5) | 26),
        cbz(0xDD4D4, 0xDCDB8, 26),                      # native context readiness
        word(0x39400000 | (0x1E9 << 10) | (20 << 5) | 8),
        cbz(0xDD4DC, 0xDCDB8, 8),                       # geometry engine readiness
        adrp(0xDD4E0, 0xC15000, 8),
        word(0xF9400000 | ((0xEC8 // 8) << 10) | (8 << 5) | 9),
        cbz(0xDD4E8, 0xDCDB8, 9, True),
        word(0xF9400000 | ((0xED0 // 8) << 10) | (8 << 5) | 9),
        cbz(0xDD4F0, 0xDCDB8, 9, True),
        word(0xF9400000 | ((0xED8 // 8) << 10) | (8 << 5) | 9),
        cbz(0xDD4F8, 0xDCDB8, 9, True),
        branch(0xDD4FC, 0xDCBDC),
    ])
    add(0xDD4C0, "606341bd00d8215ea021201ee847ff54096341392801082a8847073689020034695341b908b307913f090071800300543f050071c10c005400eb41bd01f741bd", helper, "visibility request/readiness helpers in disabled candidate block")
    add(0xDCBCC, "88be4039", word(0x39400000 | (0x23 << 10) | (20 << 5) | 8), "nearby markers request per-joint visibility")
    add(0xDCBD8, "7aa64739", branch(0xDCBD8, 0xDD4D0), "omit red/white skeleton when visibility is unavailable")

    # Fix the original range clamp input to 200.0f. Keep native finite-distance,
    # enemy-team and screen projection checks and original actor-loop cleanup.
    add(0xDB278, "603340bd", word(0x52A00008 | (0x4348 << 5)), "fixed 200-metre range")
    add(0xDB27C, "488fa852", word(0x1E270100), "range clamp input 200.0f")
    add(0xDCAA4, "004140bd", word(0x52800008 | (200 << 5)), "skeleton range 200 metres")
    add(0xDCAA8, "00d8215e", word(0x1E220100), "convert fixed skeleton range to float")
    for offset, expected in [(0xDC788, "2b030194"), (0xDC7DC, "16030194"),
                              (0xDCA3C, "7e020194")]:
        add(offset, expected, word(0x12800000), "white line/box/distance independent of old colors")
    for offset in (0xDC790, 0xDC7E4):
        add(offset, "603f40bd", word(0x1E2E1000), "one-pixel marker width")
        add(offset + 4, "00d8215e", word(0xD503201F), "remove old marker-width conversion")
    add(0xDCD78, "1c3940b9", word(0x5280003C), "one-pixel skeleton width")

    # Original query returns nonzero for an obstruction hit. A segment with an
    # obstructed endpoint is red; two unobstructed endpoints are white. This is
    # joint/segment classification, not a filled model or pixel-accurate contour.
    color = b"".join([
        adrp(0xDCD40, 0xC4E000, 9),
        word(0x91000000 | (0x18C << 10) | (9 << 5) | 9),
        word(0x38786929),                                # ldrb w9, [x9, x24]
        word(0x2A090108),                                # orr w8, w8, w9
        word(0x52801FE0),                                # mov w0, #0xff
        word(0x72A00000 | (0xFF00 << 5)),                 # movk w0, #0xff00, lsl #16
        word(0x12800009),                                # mov w9, #-1
        word(0x7100011F),                                # cmp w8, #0
        word(0x1A800120),                                # csel w0, w9, w0, eq
        word(0xD503201F), word(0xD503201F),
    ])
    add(0xDCD40, "c95b009029c1099120910291e8000037885b00d008310691086978381f010071e87b40f90010889ab3010194", color, "fixed white-visible / red-obstructed skeleton colors")
    for offset, old, new in [(0x743F3D, "玩家显示", "200米标记"),
                             (0x743F64, "显示距离", "附近敌人"),
                             (0x744522, "玩家", "位置")]:
        add(offset, old.encode(), new.encode(), "minimal display label")
    return sorted(patches)


def minimal_core(data):
    if hashlib.sha256(data).hexdigest() != CORE_SHA256:
        raise ValueError("Unsupported minimal-profile Core hash")
    result = bytearray(data)
    end = 0
    for offset, expected, replacement, reason in changes():
        if offset < end or len(expected) != len(replacement):
            raise ValueError("Overlapping or resizing minimal change")
        if data[offset:offset + len(expected)] != expected:
            raise ValueError(f"Unexpected instruction/string at {offset:#x}: {reason}")
        result[offset:offset + len(expected)] = replacement
        end = offset + len(expected)
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset), before=old.hex(), after=new.hex(), reason=reason)
            for offset, old, new, reason in changes()]
