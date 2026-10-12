"""Limit the reviewed Core binary to named enemy markers within 300 metres.

This changes the application's UI and feature execution only. It does not
change the game, the kernel backend, target process names, or detection paths.
All changes are static, before signing; no executable memory is written at run
time. Unused original routines remain in the binary.
"""
import hashlib
import struct

CORE_SHA256 = "ed69f33902e17c499aa7904751a01ce3521a663503fb8958ea01a7c5489d5c58"
PROFILE = "enemy-markers-300m-count-tags"
RANGE_METRES = 300


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


def call(source, target):
    return word(int.from_bytes(branch(source, target), "little") | 0x80000000)


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
    0xDB680: "08014039",  # weapon (both player categories)
    0xDB69C: "68f34339",  # radar player markers
    0xDB864: "68234439",  # threat collection
    0xDBBC4: "68b34039",  # tracked equipment
    0xDCDC0: "68bb4039",  # trajectory
    # The former aim-circle gates at DD490/DD4B0 are inside the unreachable
    # candidate block reused below; its unconditional bypass disables them.
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
    # Native line, box, distance, name and skeleton use the same preference for both
    # player categories. Old category switches cannot enable another feature.
    for offset in (0xDB630, 0xDB644, 0xDB658, 0xDB66C):
        add(offset, "08014039", word(0x39400000 | (0x23 << 10) | (27 << 5) | 8), "shared nearby-enemy toggle")

    # Offscreen arrows and their distance follow the same sole checkbox. The
    # native actor/team, finite-distance, 300-metre and on-screen tests run first.
    for offset, expected, register in [(0xDB46C, "6a974039", 10), (0xDB470, "69874039", 9)]:
        add(offset, expected, word(0x39400000 | (0x23 << 10) | (27 << 5) | register), "shared offscreen arrow/distance toggle")
    add(0xDBD00, "cd050194", word(0x12A01FE0), "opaque yellow direction arrow and distance")
    add(0xDBD18, "684341b9", word(0x52800008), "fixed native arrow style zero")
    add(0xDBD28, "684741b9", word(0x52800508), "fixed native arrow size 40 drawing units")
    add(0xDBD48, "6087402d", word(0x2D408660), "arrow bounds from current screen context")
    add(0xDC244, "6c0340bd", word(0xBD40026C), "offscreen distance font uses current native scale")
    add(0xDC2AC, "6497402d", word(0x2D409664), "offscreen label bounds from current screen context")

    # The radar gate remains disabled. Reuse only its unreachable math block
    # to derive a horizontal bearing from original cached world X/Y and camera
    # yaw, rather than the mirrored projection of a target behind the camera.
    # Preserve the native edge-clamp inputs across libc math calls; return to
    # its original placement, arrow renderer, distance text and loop cleanup.
    direction = bytearray.fromhex(
        "e24302bde34702bde44b02bd011b40bd64f641bd2138241e001f40bd64fa41bd0038241e6b1242bd"
        "0910b0120800261e087900121f01096be80400542800261e087900121f01096b680400546801261e"
        "087900121f01096be80300544800261e087900121f01096b680300544801261e087900121f01096b"
        "e802005404102e1e4020241e8d0200544021241e4d0200540820201e610000542820201ec0010054"
        "000000940800009001f142bd6109211e0038211e000000940440201e2040211e8140201ee24342bd"
        "e34742bde44b42bd00000014e24342bde34742bde44b42bdf47f41b900000014"
    )
    direction[40 * 4:41 * 4] = call(0xDB748, 0x7255E0)  # atan2f(delta_y, delta_x)
    direction[41 * 4:42 * 4] = adrp(0xDB74C, 0xAC8000, 8)
    direction[45 * 4:46 * 4] = call(0xDB75C, 0x725390)  # sin/cos(bearing - camera yaw)
    direction[52 * 4:53 * 4] = branch(0xDB778, 0xDBD98)
    direction[57 * 4:58 * 4] = branch(0xDB78C, 0xDBDC0)
    add(0xDB6A8,
        "e043c03d01102e1e0020211e8d0d0054e0fb40bda021201e280d0054e8cb40f9011940bd2800261e"
        "087900120910b0121f01096b4c0c0054e8cb40f9021d40bd4800261e087900121f01096b8c0b0054"
        "601242bd0800261e087900121f01096bec0a00548856b8520301271e0028231e684f00b003f142bd"
        "0008231e63f641bd6138211ee35740bd6808211e61fa41bd2138221e6a08211e1427199402082a1e"
        "0209011f21082a1e0085001f0108201e4104021f21c0211ee343c03d2020231e2d010054683e00d0"
        "034d4abd2020231ead000054e343c03d6118211e4208211e0008211ee1fb41bd",
        bytes(direction), "validated camera-relative bearing in disabled radar block")
    add(0xDBD90, "2139221e", branch(0xDBD90, 0xDB6A8), "derive offscreen bearing independently of screen projection")
    add(0xDBD98, "27d5a27e", word(0x1E20C027), "horizontal edge magnitude from bearing vector")
    add(0xDBD9C, "65d5aa7e", word(0x1E20C005), "vertical edge magnitude from bearing vector")

    # Append a category suffix to the original local string copy. Native append
    # handles short/long growth; the existing cleanup frees that copy. The name
    # cache is unchanged and classification uses the original bot flag, not text.
    tag = b"".join([
        word(0xBD00EFEB),  # replay original save of actor vertical coordinate
        word(0x39400000 | (0x240 << 10) | (19 << 5) | 8),
        adrp(0xDB798, 0x743000, 1),
        word(0x91000000 | (0xF71 << 10) | (1 << 5) | 1),
        adrp(0xDB7A0, 0x744000, 9),
        word(0x91000000 | (0x8C9 << 10) | (9 << 5) | 9),
        word(0x7100011F),  # cmp w8, #0
        word(0x9A890021),  # csel x1, x1, x9, eq
        word(0x910C03E0),  # add x0, sp, #0x300 (local std::string)
        word(0x52800182),  # mov w2, #12 (UTF-8 suffix bytes)
        call(0xDB7B8, 0x724E50),
        branch(0xDB7BC, 0xDBF64),
    ])
    add(0xDB790, "e3ff41bd4128211e0028231ee11302bde01702bd9f020071e83f083215019f5ae1430891e00316aae07740bde20315aa", tag, "append original bot/human category in disabled radar block")
    add(0xDBF60, "ebef00bd", branch(0xDBF60, 0xDB790), "tag name copy before font measurement")
    add(0x743F71, "显示骨骼".encode(), "【真人】".encode(), "human suffix stored in unreachable old checkbox label")

    # Original counters reset every frame and increment after enemy-team and
    # finite 300-metre checks, before projection. Display their combined total,
    # including offscreen actors, as one centered red number with native outline.
    counter = bytearray.fromhex(
        "688f4039480700346826402d6a0a40bd0910b0120801261e087900121f01096b680600540821201e"
        "2d0600542801261e087900121f01096ba80500542821201e6d0500544801261e087900121f01096b"
        "e80400544821201ead0400540110271e0b09211e0850a8520101271e0c09211e60292c1e00202a1e"
        "aa03005400102c1e2909201e0800009009b148b908b548b90801098b090080121f0109eb0891899a"
        "e80300f9e0030c91010280520200009042600091000000941f0000718d0100541f4000714a010054"
        "e10300aae0030c9122008052e31f805203e0bf722041201e8141201e6241201e0000009400000014"
    )
    counter[33 * 4:34 * 4] = adrp(0xDEE10, 0xC54000, 8)
    counter[43 * 4:44 * 4] = adrp(0xDEE38, 0x73D000, 2)
    counter[45 * 4:46 * 4] = call(0xDEE40, 0x726A30)
    counter[50 * 4:51 * 4] = word(0x2A0003E1)  # snprintf returns int; explicitly clear length's upper bits
    counter[58 * 4:59 * 4] = call(0xDEE74, 0xDF47C)
    counter[59 * 4:60 * 4] = branch(0xDEE78, 0xDEF0C)
    add(0xDED8C,
        "689f40398800003768d342391f050071810b0054343300b094ca2b91682b41b9753741b976d20991"
        "293300d029d927911f05007182b2899ae8b248b9e80300f9e00316aa01028052171f1994283300d0"
        "08012891bf06007182b2889a74120a9128b748b9e80300f9e00314aa010280520d1f1994689f4039"
        "2805003677d34239e00316aa441f1994f50300aa680640bdff06007181050054690240bd60930291"
        "82f90094e30300aa00102c1e0009201e01d0371e2001011f0190361e0028211e610240bd02d0261e"
        "2208221e0849a8520101271ee00316aae10315aa2200805284010094e00314aa2b1f1994f50300aa",
        bytes(counter), "red top-center total from original per-frame enemy counters")

    # Enter only the original compact name path, independently of old panel
    # preferences. Keep the validated name lookup, native string copy, empty
    # human/bot fallback and cleanup. Skip team numbers and all health drawing.
    add(0xDB5EC, "08014039", word(0x39400000 | (0x23 << 10) | (27 << 5) | 8), "shared name toggle")
    add(0xDB604, "686b68b8", word(0x52800028), "fixed compact name path")
    add(0xDBF84, "fa0300f9", word(0x2F00E40C), "no team-number width")
    add(0xDBF88, "e0430891", branch(0xDBF88, 0xDBFC0), "skip team-number formatting")
    add(0xDC014, "82292d1e", word(0x2F00E402), "center name using its measured width")
    add(0xDC050, "487fba0a", branch(0xDC050, 0xDC088), "skip team-number drawing")
    add(0xDC08C, "a029201e", word(0xD503201F), "remove team-prefix padding")
    add(0xDC09C, "e6040194", word(0x12800000), "white name text")
    add(0xDBF7C, "01d0241e", word(0x1E259001), "name font 14 drawing units times native scale")
    add(0xDC0E0, "527e0194", call(0xDC0E0, 0xDC0FC), "outlined name text through native text helper")
    restore = b"".join([
        word(0x1E2041EE),  # fmov s14, s15: restore original horizontal position
        word(0xBD400000 | ((0xE0 // 4) << 10) | (31 << 5) | 13),
        word(0xBD400000 | ((0x164 // 4) << 10) | (31 << 5) | 15),
        word(0xBD400000 | ((0xDC // 4) << 10) | (31 << 5) | 10),
        word(0xBD400000 | ((0xEC // 4) << 10) | (31 << 5) | 11),
        branch(0xDC0F8, 0xDC358),  # original string cleanup and remaining restores
    ])
    add(0xDC0E4, "604642bd0859a8520101271e0018211e01102e1e0020211e", restore, "restore actor geometry and distance; skip health panel")

    # The compact name path bypasses this original health block. Tail-call the
    # existing outlined-text helper with the measured name position, preserving
    # its caller's LR, native string lifetime and authenticated return frame.
    name_outline = b"".join([
        word(0xAA0403E0),  # mov x0, x4 (text)
        word(0xCB0400A1),  # sub x1, x5, x4 (byte length)
        word(0xBD400040),  # ldr s0, [x2] (left position already centered)
        word(0xBD400441),  # ldr s1, [x2, #4]
        word(0x1E204102),  # fmov s2, s8 (font size)
        word(0x52800002),  # mov w2, #0 (do not center a second time)
        branch(0xDC114, 0xDF47C),
    ])
    add(0xDC0FC, "21cc201e0820201e00e4002f0c4c211e600240bd0190201e0108211e", name_outline, "native outlined-name argument bridge in bypassed health block")
    ray_origin = b"".join([
        word(0xBD400A62),  # ldr s2, [x19, #8] (screen height)
        word(0x1E349003),  # fmov s3, #-10
        word(0x1E232842),  # fadd s2, s2, s3
        word(0xBD0307E2),  # str s2, [sp, #0x304]
        branch(0xDC130, 0xDC774),
    ])
    add(0xDC120, "0008031f03102c1eee41201e22bd031fe25b02bd", ray_origin, "bottom-center ray origin in bypassed health block")
    ray_alpha = b"".join([
        word(0x52A85908),  # mov w8, #100.0f bits
        word(0x1E270100),  # fmov s0, w8
        word(0x1E2021A0),  # fcmp s13, s0 (original finite actor distance)
        word(0x12800000),  # near: opaque white
        word(0x12B1E008),  # far: white with alpha 0x70
        word(0x1A88D000),  # csel w0, w0, w8, le
        branch(0xDC158, 0xDC78C),
    ])
    add(0xDC140, "e24302bde04702bd2808231ee1630991e2030991e00316aa63648452", ray_alpha, "dim rays beyond 100 metres in bypassed health block")
    add(0xDC76C, "0824a852", branch(0xDC76C, 0xDC120), "ray begins ten drawing units above screen bottom")

    # The native collector has separate human/bot skeleton gates. Updating
    # render gates alone leaves joint validity unset when old skeleton settings
    # are off. Its x19 is the current configuration/context snapshot; keep the
    # original bone reads and validation, but use the same nearby-enemy toggle
    # and range as the renderer before it chooses how many joints to acquire.
    for offset, expected in [(0xD88C0, "68e64239"), (0xD88D0, "688a4039")]:
        add(offset, expected, word(0x39400000 | (0x23 << 10) | (19 << 5) | 8), "shared toggle in native skeleton collector")
    add(0xD88DC, "604240bd", word(0x52800008 | (RANGE_METRES << 5)), "collector skeleton range 300 metres")
    add(0xD88E0, "00d8215e", word(0x1E220100), "convert fixed collector range to float")

    # Use unreachable aim-candidate instructions inside the same renderer frame;
    # preserve its PAC, stack and unwind layout. Native activation uses the same
    # existing caller frame as other renderer calls, with its saved outer LR.
    # The unconditional loop continuation prevents normal fallthrough into them.
    add(0xDD464, "e86f41b9", branch(0xDD464, 0xDBDC8), "disable aim-candidate processing")
    add(0xDB1B0, "68a34539", branch(0xDB1B0, 0xDD468), "synchronize visibility and restore original activation")
    helper = b"".join([
        word(0x39400000 | (0x23 << 10) | (27 << 5) | 9),  # ldrb w9, [x27, #0x23]
        word(0x39400000 | (0x2F << 10) | (27 << 5) | 10), # old visibility request
        word(0x39000000 | (0x2F << 10) | (27 << 5) | 9),  # strb w9, [x27, #0x2f]
        cbz(0xDD474, 0xDD4BC, 9),                        # no activation while off
        word(0xF9400000 | ((0x18 // 8) << 10) | (27 << 5) | 8),
        cbz(0xDD47C, 0xDD4BC, 8, True),                  # original target binding gate
        word(0xB9400000 | ((0x130 // 4) << 10) | (27 << 5) | 11),
        word(0x52800028),                                # original 'global' mode = 1
        word(0xB9000000 | ((0x130 // 4) << 10) | (27 << 5) | 8),
        word(0x7100055F),                                # cmp w10, #1
        branch(0xDD490, 0xDD4B8, 1),                     # request changed to on
        word(0x7100057F),                                # cmp w11, #1
        branch(0xDD498, 0xDD4B8, 1),                     # mode changed to global
        adrp(0xDD49C, 0xC4E000, 8),
        word(0x39400000 | (0x400 << 10) | (8 << 5) | 8),
        cbz(0xDD4A4, 0xDD4B8, 8),                       # saved settings: arm once
        branch(0xDD4A8, 0xDD4BC),
        word(0xD503201F), word(0xD503201F), word(0xD503201F),
        call(0xDD4B8, 0xD3EAC),                         # original UI activation
        word(0x52800008),                                # aim stays off
        branch(0xDD4C0, 0xDB1B4),
        word(0xD503201F), word(0xD503201F), word(0xD503201F),
    ])
    add(0xDD468, "084bff34085340390a1340b95f010c72e9179f1aaa00a03788000037604642bd0820201ee9d79f1a6aab45398a0000362a0080526949ff34040000146a0249394a010052e948ff3469a745390901090a894807376a480736606341bd00d8215ea021201ee847ff54", helper, "restore native global visibility activation in disabled candidate block")
    readiness = b"".join([
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
    add(0xDD4D0, "096341392801082a8847073689020034695341b908b307913f090071800300543f050071c10c005400eb41bd01f741bd", readiness, "visibility readiness helper in disabled candidate block")
    add(0xDCBCC, "88be4039", word(0x39400000 | (0x23 << 10) | (20 << 5) | 8), "nearby markers request per-joint visibility")
    add(0xDCBD8, "7aa64739", branch(0xDCBD8, 0xDD4D0), "omit red/white skeleton when visibility is unavailable")

    # Fix the original range clamp input to 300.0f. Keep native finite-distance,
    # enemy-team and screen projection checks and original actor-loop cleanup.
    add(0xDB278, "603340bd", word(0x52A00008 | (0x4396 << 5)), "fixed 300-metre range")
    add(0xDB27C, "488fa852", word(0x1E270100), "range clamp input 300.0f")
    add(0xDCAA4, "004140bd", word(0x52800008 | (RANGE_METRES << 5)), "skeleton range 300 metres")
    add(0xDCAA8, "00d8215e", word(0x1E220100), "convert fixed skeleton range to float")
    add(0xDC788, "2b030194", branch(0xDC788, 0xDC140), "distance-based ray opacity")
    for offset, expected in [(0xDC7DC, "16030194"),
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
    for offset, old, new in [(0x743F3D, "玩家显示", "300米标记"),
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
