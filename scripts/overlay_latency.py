"""Independent fill switch after the reviewed nearby-vehicle menu patch."""
from minimal_features import adrp, branch, call, cbz, word

VERSION = "1"
ARTIFACT = "Core-17.0-21A327-TrollStore-model-render-v13-overlay-latency.ipa"
PROFILE = "enemy-markers-600m-identity-colors-vehicles-independent-fill"


def changes():
    menu = b"".join([
        adrp(0xCD844, 0x743000, 0), word(0x91000000 | (0xF4A << 10)),
        word(0x91000000 | (0x20 << 10) | (23 << 5) | 1), word(0x2F00E400),
        call(0xCD854, 0xC8928), cbz(0xCD858, 0xCD868, 0),
        adrp(0xCD85C, 0xC4D000, 8), word(0x52800029),
        word(0x39000000 | (0xF86 << 10) | (8 << 5) | 9),
        call(0xCD868, 0x12E620), branch(0xCD86C, 0xCE0A0),
    ])
    return [
        (0xCD82C, cbz(0xCD82C, 0xCD83C, 0), cbz(0xCD82C, 0xCD844, 0), "unchanged vehicle checkbox continues to fill switch"),
        (0xCD83C, call(0xCD83C, 0x12E620), branch(0xCD83C, 0xCD844), "changed vehicle checkbox continues to fill switch"),
        (0xCD844, bytes.fromhex("0008221ee103142de08302914696019400834abd01082d1e0004081f0008291ee02f142da03300d000f83d91"), menu, "independent persisted fill checkbox; end original child exactly once"),
        (0x743F4A, "显示射线".encode(), "人物填色".encode(), "independent fill switch label"),
    ]


def overlay_latency(data):
    result = bytearray(data)
    end = 0
    for offset, old, new, reason in changes():
        if offset < end or len(old) != len(new):
            raise ValueError("Overlapping or resizing independent-fill change")
        if data[offset:offset + len(old)] != old:
            raise ValueError(f"Unexpected independent-fill source at {offset:#x}: {reason}")
        result[offset:offset + len(old)] = new
        end = offset + len(old)
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset), before=old.hex(), after=new.hex(), reason=reason)
            for offset, old, new, reason in changes()]
