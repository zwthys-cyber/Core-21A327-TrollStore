"""Add the cosmetics UI and suppress Core's competing launchd remote-open.

Executed in GitHub Actions only. The original input hash is enforced by
minimal_core before any instruction or menu edits.
"""
import hashlib
from minimal_features import minimal_core, word, branch, cbz, call

MENU_SYMBOL = b'CoreCosmeticsRenderPage\0'
MENU_BRIDGE = 0xD0818
MENU_SYMBOL_RVA = 0xD0840
MENU_RETURN = 0xD09E4
DLSYM_STUB = 0x725A10
LAUNCHD_OPEN_CALL_RVA = 0x2E7D4
LAUNCHD_OPEN_CALL = bytes.fromhex('00300094')
REMOTE_OPEN_FAILURE = word(0x12800000)  # mov w0, #-1
REMOVED_TABS_SHA256 = '5a033074e6ee5168867e716bf338705af6878c648437f6e909cd775b38270504'


def adr(source, target, register):
    delta = target - source
    if not -(1 << 20) <= delta < (1 << 20):
        raise ValueError('ADR out of range')
    return word(0x10000000 | ((delta & 3) << 29) | (((delta >> 2) & 0x7FFFF) << 5) | register)


def cosmetics_core(original):
    result = bytearray(minimal_core(original))
    if original[LAUNCHD_OPEN_CALL_RVA:LAUNCHD_OPEN_CALL_RVA + 4] != LAUNCHD_OPEN_CALL:
        raise ValueError('Unreviewed launchd remote-open call')
    # Keep SpringBoard initialization intact, but make the later automatic
    # launchd open take its existing failure/teardown path. Otherwise launchd
    # races the cosmetics worker and records a first-exception timeout before
    # the game can be opened through the original remote-open veneer.
    result[LAUNCHD_OPEN_CALL_RVA:LAUNCHD_OPEN_CALL_RVA + 4] = REMOTE_OPEN_FAILURE
    if hashlib.sha256(original[0xD07E0:0xD097C]).hexdigest() != REMOVED_TABS_SHA256:
        raise ValueError('Unreviewed native sidebar region')
    if original[0x744529:0x744530] != '物资'.encode() + b'\0':
        raise ValueError('Unreviewed native menu label')
    # Restore only the original second row after Position. It uses native
    # spacing, hit testing, selection and the existing page-selection word.
    result[0xD07E0:0xD07E4] = original[0xD07E0:0xD07E4]
    result[0xD0814:0xD0818] = branch(0xD0814, 0xD097C)
    result[0x744529:0x74452F] = '美化'.encode()
    # Resolve the injected C page through the existing authenticated dlsym
    # stub. Null resolution returns through the original frame's epilogue.
    # BLRAAZ uses the standard arm64e C-function pointer convention.
    bridge = b''.join([
        word(0x92800020),                         # mov x0, #-2 / RTLD_DEFAULT
        adr(MENU_BRIDGE + 4, MENU_SYMBOL_RVA, 1),
        call(MENU_BRIDGE + 8, DLSYM_STUB),
        cbz(MENU_BRIDGE + 12, MENU_RETURN, 0, wide=True),
        word(0xD63F081F),                         # blraaz x0
        branch(MENU_BRIDGE + 20, MENU_RETURN),
    ])
    result[MENU_BRIDGE:MENU_BRIDGE + len(bridge)] = bridge
    result[MENU_SYMBOL_RVA:MENU_SYMBOL_RVA + len(MENU_SYMBOL)] = MENU_SYMBOL
    # Position=2 retains the v19 route. Cosmetics=1 enters our page. All other
    # old preference values retain the initialization fallback.
    result[0xD098C:0xD0998] = b''.join([
        word(0x7100051F),                         # cmp w8, #1
        branch(0xD0990, MENU_BRIDGE, 0),
        branch(0xD0994, 0xD09C8),
    ])
    return bytes(result)
