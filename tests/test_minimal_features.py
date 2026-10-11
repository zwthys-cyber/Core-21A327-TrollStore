"""Execute modified ARM64 control flow with synthetic local UI/configuration.

These tests do not run iOS, the game, an exploit, or remote process bootstrap.
Pointer authentication is skipped only in the UI emulator; its prologue and
epilogue instructions must be unchanged in the packaged application.
"""
import random
import struct
import unittest
import zipfile

from test_patch import ROOT
from build import commands
from minimal_features import minimal_core
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X8, UC_ARM64_REG_X20, UC_ARM64_REG_X23, UC_ARM64_REG_X25, UC_ARM64_REG_X27, UC_ARM64_REG_X30, UC_ARM64_REG_W0, UC_ARM64_REG_W8, UC_ARM64_REG_W9, UC_ARM64_REG_W10, UC_ARM64_REG_W23, UC_ARM64_REG_W25

BASE = 0x100000000
STACK = 0x3000000000
CONFIG = BASE + 0xC54270
RETURN = BASE + 0x3F00


class MinimalFeatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with zipfile.ZipFile(ROOT / "input/Core.ipa") as ipa:
            cls.original = ipa.read("Payload/Core.app/Core")
        cls.binary = minimal_core(cls.original)

    def emulator(self):
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(BASE, 0x1000000)
        for pos, command, _ in commands(self.binary):
            if command == 0x19:
                address, _, offset, length = struct.unpack_from("<4Q", self.binary, pos + 24)
                if BASE <= address < BASE + 0x1000000 and length:
                    mu.mem_write(address, self.binary[offset:offset + length])
        mu.mem_map(STACK, 0x10000)
        mu.reg_write(UC_ARM64_REG_SP, STACK + 0x8000)
        mu.reg_write(UC_ARM64_REG_X30, RETURN)
        # Synthetic stack guard; the application retains its native guard.
        mu.mem_write(BASE + 0xB89B58, struct.pack("<Q", STACK + 0x100))
        mu.mem_write(STACK + 0x100, struct.pack("<Q", 0xABCD1234))
        return mu

    def test_only_reviewed_ranges_change(self):
        # Independent manifest of reviewed instruction/label locations.
        words = {0xC1A04, 0xCBE64, 0xCC948, 0xD07E0, 0xD0988, 0xD098C,
                 0xCD778, 0xCD7A4, 0xCD7F4, 0xCD7F8, 0xCD7FC, 0xCD800,
                 0xCD804, 0xCD818, 0xDB658, 0xDC858,
                 0xDB1B0, 0xDB2FC, 0xDB3CC, 0xDB46C, 0xDB470, 0xDB5EC,
                 0xDB630, 0xDB644, 0xDB66C, 0xDB680, 0xDB69C, 0xDB864,
                 0xDBBC4, 0xDCBCC, 0xDCDC0, 0xDD490, 0xDD4B0, 0xDD5F0,
                 0xDD758, 0xDDEBC, 0xDDEC8, 0xDDF70, 0xDDFC4, 0xDE014,
                 0xDE5B4, 0xDE5D0, 0xDE734, 0xDEBE8, 0xDEC30,
                 0xDED8C, 0xDED94, 0xDEE00, 0xDEE08}
        allowed = {i for offset in words for i in range(offset, offset + 4)}
        for offset, length in [(0x743F3D, 12), (0x743F64, 12), (0x744522, 6)]:
            allowed.update(range(offset, offset + length))
        changed = {i for i, (a, b) in enumerate(zip(self.original, self.binary)) if a != b}
        self.assertTrue(changed <= allowed)
        self.assertEqual(len(self.original), len(self.binary))
        # Header, original injected trampolines, kernel/cache/backend operations.
        self.assertEqual(self.original[:0xC1A04], self.binary[:0xC1A04])
        self.assertEqual(self.original[0xC1A08:0xCBE64], self.binary[0xC1A08:0xCBE64])

    def test_rejects_unknown_input_and_reapplication(self):
        modified = bytearray(self.original)
        modified[0xDB658] ^= 1
        for data in [modified, self.binary]:
            with self.assertRaisesRegex(ValueError, "hash"):
                minimal_core(data)

    def test_input_worker_returns_without_accessing_configuration_or_input(self):
        mu = self.emulator()
        accesses = []
        mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                    lambda *args: accesses.append(args[2:5]))
        rng = random.Random(9)
        # Invalid addresses would fail if any of the old worker actually ran.
        registers = {UC_ARM64_REG_X0: rng.getrandbits(48),
                     UC_ARM64_REG_X20: rng.getrandbits(48),
                     UC_ARM64_REG_X23: rng.getrandbits(48),
                     UC_ARM64_REG_X25: rng.getrandbits(48),
                     UC_ARM64_REG_X27: rng.getrandbits(48)}
        for reg, value in registers.items():
            mu.reg_write(reg, value)
        mu.emu_start(BASE + 0xC1A04, RETURN, count=5)
        self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), RETURN)
        self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
        self.assertEqual(accesses, [])
        for reg, value in registers.items():
            self.assertEqual(mu.reg_read(reg), value)

    def test_old_tabs_cannot_reach_removed_menus(self):
        mu = self.emulator()
        destinations = {BASE + 0xD09C8, BASE + 0xD09D0}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                    if address in destinations else None)
        for tab in [0, 1, 2, 3, 4, 5, 6, 7, 0x7FFFFFFF, 0xFFFFFFFF]:
            with self.subTest(tab=tab):
                mu.reg_write(UC_ARM64_REG_W8, tab)
                mu.emu_start(BASE + 0xD0984, BASE + 0xD09E4, count=20)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC),
                                 BASE + (0xD09D0 if tab == 2 else 0xD09C8))

    def test_render_gates_ignore_enabled_old_preferences(self):
        mu = self.emulator()
        mu.mem_write(CONFIG, b"\xff" * 0x500)
        for offset in [0xDB1B0, 0xDB2FC, 0xDB3CC, 0xDB46C, 0xDB470, 0xDB5EC,
                       0xDB630, 0xDB644, 0xDB66C, 0xDB680, 0xDB69C, 0xDB864,
                       0xDBBC4, 0xDCDC0, 0xDD490, 0xDD4B0, 0xDD5F0, 0xDD758,
                       0xDDEBC, 0xDDEC8, 0xDDF70, 0xDDFC4, 0xDE014, 0xDE5B4,
                       0xDE5D0, 0xDE734, 0xDEBE8, 0xDEC30, 0xDED8C, 0xDED94,
                       0xDEE00, 0xDEE08, 0xDCBCC]:
            with self.subTest(offset=hex(offset)):
                for reg in [UC_ARM64_REG_W8, UC_ARM64_REG_W9, UC_ARM64_REG_W10, UC_ARM64_REG_W23]:
                    mu.reg_write(reg, 0xFFFFFFFF)
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                mu.reg_write(UC_ARM64_REG_X20, CONFIG)
                mu.reg_write(UC_ARM64_REG_X8, CONFIG)
                mu.emu_start(BASE + offset, BASE + offset + 4, count=1)
                index = struct.unpack_from("<I", self.original, offset)[0] & 31
                register = {8: UC_ARM64_REG_W8, 9: UC_ARM64_REG_W9,
                            10: UC_ARM64_REG_W10, 23: UC_ARM64_REG_W23}[index]
                self.assertEqual(mu.reg_read(register), 0)
        for enabled in [0, 1]:
            mu.mem_write(CONFIG + 0x23, bytes([enabled]))
            mu.reg_write(UC_ARM64_REG_X27, CONFIG)
            mu.reg_write(UC_ARM64_REG_X8, 1)  # old category pointer is irrelevant
            mu.emu_start(BASE + 0xDB658, BASE + 0xDB65C, count=1)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_W8), enabled)
        for bot in [0, 1]:
            mu.reg_write(UC_ARM64_REG_X27, CONFIG)
            mu.reg_write(UC_ARM64_REG_X20, bot)
            mu.emu_start(BASE + 0xDB5DC, BASE + 0xDB618, count=30)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_W25), 2)  # native no-panel style

    def test_position_menu_balances_child_and_saves_only_its_checkbox(self):
        for changed in [False, True]:
            with self.subTest(changed=changed):
                mu = self.emulator()
                calls = []

                def mock_ui(uc, address, size, _):
                    instruction = struct.unpack("<I", uc.mem_read(address, 4))[0]
                    if instruction == 0xD503237F:  # PACIBSP: emulator only
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)
                    elif instruction == 0xD65F0FFF:  # RETAB: emulator only
                        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
                    elif instruction & 0xFC000000 == 0x94000000:
                        delta = instruction & 0x3FFFFFF
                        if delta & 0x2000000:
                            delta -= 0x4000000
                        target = address + delta * 4
                        calls.append(target - BASE)
                        if target == BASE + 0xC8928:
                            self.assertEqual(uc.reg_read(UC_ARM64_REG_X1), CONFIG + 0x23)
                            self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), BASE + 0x743F64)
                            uc.reg_write(UC_ARM64_REG_W0, int(changed))
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, mock_ui)
                mu.emu_start(BASE + 0xCD6A4, RETURN, count=500)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), RETURN)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                self.assertEqual(calls.count(0xC88B0), 1)
                self.assertEqual(calls.count(0x12E620), 1)
                self.assertEqual(calls.count(0xC8928), 1)
                self.assertFalse(set(calls) & {0xCE0EC, 0xCE210, 0xCE344})
                self.assertEqual(mu.mem_read(BASE + 0xC4DF86, 1), bytes([changed]))


if __name__ == "__main__":
    unittest.main()
