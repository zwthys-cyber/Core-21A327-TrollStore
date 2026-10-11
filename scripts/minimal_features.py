"""Reduce the reviewed Core binary to its native position/distance display.

This changes the application's UI and feature execution only. It does not
change the game, the kernel backend, target process names, or detection paths.
All changes are static, before signing; no executable memory is written at run
time. Unused original routines remain in the binary.
"""
import hashlib
import struct

CORE_SHA256 = "ed69f33902e17c499aa7904751a01ce3521a663503fb8958ea01a7c5489d5c58"
PROFILE = "position-distance-only"


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


# Original instruction encodings recorded from the pinned executable.
# Replacing reads with MOV #0 makes old preferences unable to reactivate these
# paths; the registers, subsequent conditions, and original cleanup stay intact.
DISABLED_READS = {
    0xDB1B0: "68a34539",  # aim
    0xDB2FC: "68f34339",  # radar frame
    0xDB3CC: "68234439",  # threat list collection
    0xDB46C: "6a974039",  # offscreen distance/indicator
    0xDB470: "69874039",  # offscreen indicator
    0xDB5EC: "08014039",  # name/health panel style: native fallback is none
    0xDB630: "08014039",  # line (both player categories)
    0xDB644: "08014039",  # box (both player categories)
    0xDB66C: "08014039",  # skeleton (both player categories)
    0xDB680: "08014039",  # weapon (both player categories)
    0xDB69C: "68f34339",  # radar player markers
    0xDB864: "68234439",  # threat collection
    0xDBBC4: "68b34039",  # tracked equipment
    0xDCBCC: "88be4039",  # skeleton visibility style
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
        add(offset, expected, word(0x52800000 | destination), "disable non-distance render gate")
    # One checkbox controls native distance text at each projected enemy position
    # for both human players and bots, without reading the old category flags.
    add(0xDB658, "08014039", word(0x39400000 | (0x23 << 10) | (27 << 5) | 8), "shared position/distance toggle")
    for offset, old, new in [(0x743F3D, "玩家显示", "位置距离"),
                             (0x743F64, "显示距离", "位置距离"),
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
