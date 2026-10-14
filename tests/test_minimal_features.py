"""Execute modified ARM64 control flow with synthetic local UI/configuration.

These tests do not run iOS, the game, an exploit, or remote process bootstrap.
Pointer authentication is skipped only in synthetic call-frame tests; native
prologue and epilogue instructions remain in the packaged application.
"""
import random
import math
import struct
import unittest
import zipfile
from itertools import product

from test_patch import ROOT
from build import commands
from minimal_features import minimal_core
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X8, UC_ARM64_REG_X20, UC_ARM64_REG_X23, UC_ARM64_REG_X25, UC_ARM64_REG_X27, UC_ARM64_REG_X30, UC_ARM64_REG_W0, UC_ARM64_REG_W8, UC_ARM64_REG_W9, UC_ARM64_REG_W10, UC_ARM64_REG_W23, UC_ARM64_REG_W25
from unicorn import arm64_const as registers

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
        mu.ctl_set_cpu_model(registers.UC_CPU_ARM64_MAX)  # native RCpc LDAPRB
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
                 0xDBBC4, 0xDCBCC, 0xDCDC0, 0xDD5F0,
                 0xDD758, 0xDDEBC, 0xDDEC8, 0xDDF70, 0xDDFC4, 0xDE014,
                 0xDE5B4, 0xDE5D0, 0xDE734, 0xDEBE8, 0xDEC30,
                 0xDED8C, 0xDED94, 0xDEE00, 0xDEE08}
        allowed = {i for offset in words for i in range(offset, offset + 4)}
        for offset in [0xDD464, 0xDB278, 0xDB27C, 0xDC788, 0xDC790, 0xDC794,
                       0xDC7DC, 0xDC7E4, 0xDC7E8, 0xDCA3C, 0xDCAA4,
                       0xDCAA8, 0xDCBD8, 0xDCD78,
                       0xD88C0, 0xD88D0, 0xD88DC, 0xD88E0,
                       0xDB604, 0xDBF84, 0xDBF88, 0xDC014, 0xDC050,
                       0xDC08C, 0xDC09C, 0xDBF7C, 0xDC0E0, 0xDC76C, 0xDBF54]:
            allowed.update(range(offset, offset + 4))
        for offset in [0xDBD00, 0xDBD18, 0xDBD28, 0xDBD48, 0xDBD90, 0xDBD98, 0xDBD9C, 0xDC244, 0xDC2AC]:
            allowed.update(range(offset, offset + 4))
        for offset, length in [(0xDD468, 152), (0xDCD40, 44), (0xDC0E4, 24),
                               (0xDC0FC, 28), (0xDC120, 20), (0xDC140, 28), (0xDC15C, 28), (0xDB6A8, 232),
                               (0xDED8C, 240)]:
            allowed.update(range(offset, offset + length))
        for offset, length in [(0x743F3D, 12), (0x743F64, 12), (0x744522, 6)]:
            allowed.update(range(offset, offset + length))
        changed = {i for i, (a, b) in enumerate(zip(self.original, self.binary)) if a != b}
        self.assertTrue(changed <= allowed)
        self.assertEqual(len(self.original), len(self.binary))
        # Header, original injected trampolines, kernel/cache/backend operations.
        self.assertEqual(self.original[:0xC1A04], self.binary[:0xC1A04])
        self.assertEqual(self.original[0xC1A08:0xCBE64], self.binary[0xC1A08:0xCBE64])
        # Native geometry acquisition, readiness management and query routines.
        self.assertEqual(self.original[0xD33F4:0xD88C0], self.binary[0xD33F4:0xD88C0])
        self.assertEqual(self.original[0xD88E4:0xDB144], self.binary[0xD88E4:0xDB144])
        # Former category append helper, entry and literal are no longer patched.
        for offset, length in [(0xDB790, 48), (0xDBF60, 4), (0x743F71, 12)]:
            self.assertEqual(self.original[offset:offset + length], self.binary[offset:offset + length])

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
        for offset in [0xDB2FC, 0xDB3CC,
                       0xDB680, 0xDB69C, 0xDB864,
                       0xDBBC4, 0xDCDC0, 0xDD5F0, 0xDD758,
                       0xDDEBC, 0xDDEC8, 0xDDF70, 0xDDFC4, 0xDE014, 0xDE5B4,
                       0xDE5D0, 0xDE734, 0xDEBE8, 0xDEC30]:
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
            for offset, reg in [(0xDB46C, UC_ARM64_REG_W10), (0xDB470, UC_ARM64_REG_W9)]:
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                mu.emu_start(BASE + offset, BASE + offset + 4, count=1)
                self.assertEqual(mu.reg_read(reg), enabled)
            for offset in [0xDB630, 0xDB644, 0xDB658, 0xDB66C, 0xDCBCC]:
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                mu.reg_write(UC_ARM64_REG_X20, CONFIG)
                mu.reg_write(UC_ARM64_REG_X8, 1)  # old category pointer is irrelevant
                mu.emu_start(BASE + offset, BASE + offset + 4, count=1)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_W8), enabled)
        for enabled in [0, 1]:
            for bot in [0, 1]:
                for old_style in [0, 1, 2, 255]:
                    mu.mem_write(CONFIG + 0x23, bytes([enabled]))
                    mu.mem_write(CONFIG + 0x1C8, struct.pack('<2I', old_style, old_style))
                    mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                    mu.reg_write(UC_ARM64_REG_X20, bot)
                    mu.emu_start(BASE + 0xDB5DC, BASE + 0xDB618, count=30)
                    self.assertEqual(mu.reg_read(UC_ARM64_REG_W25), 1 if enabled else 2)

    def test_native_name_path_draws_only_name_and_preserves_geometry(self):
        # Native lookup and graphics use synthetic results; execute original
        # short/long string selection, empty fallback, positioning and cleanup.
        for enabled in [0, 1]:
            for bot in [0, 1, 255]:
                for name in ['Enemy', '测试玩家', 'LongEnemy_' * 5, '', 'x' * 22, 'x' * 23]:
                    with self.subTest(enabled=enabled, bot=bot, name=name):
                        mu = self.emulator()
                        context, actor, name_object = STACK + 0x1000, STACK + 0x2000, STACK + 0x3000
                        source_heap, copy_heap, font_owner = STACK + 0x4000, STACK + 0x5000, STACK + 0x6000
                        raw = name.encode()

                        def put_string(address, value, heap):
                            if len(value) <= 22:
                                obj = value.ljust(23, b'\0') + bytes([len(value)])
                            else:
                                mu.mem_write(heap, value + b'\0')
                                obj = struct.pack('<3Q', heap, len(value), 0x8000000000000000 | (len(value) + 1))
                            mu.mem_write(address, obj)

                        put_string(name_object, raw, source_heap)
                        original_string = bytes(mu.mem_read(name_object, 24))
                        mu.mem_write(context, struct.pack('<f', 1))
                        mu.mem_write(context + 0x240, bytes([bot]))
                        mu.mem_write(actor + 0x30, struct.pack('<4f', 400, 200, 600, 700))
                        mu.mem_write(actor + 0x4C, b'\x01')
                        mu.mem_write(CONFIG + 0x23, bytes([enabled]))
                        mu.mem_write(BASE + 0xC54940, struct.pack('<Q', font_owner))
                        mu.mem_write(font_owner + 0x11C0, struct.pack('<Q', 0x789))
                        mu.mem_write(STACK + 0x8190, struct.pack('<Q', actor))
                        mu.mem_write(STACK + 0x8180, struct.pack('<Q', 0x123123))
                        mu.reg_write(registers.UC_ARM64_REG_X19, context)
                        mu.reg_write(registers.UC_ARM64_REG_X22, 0x1234)
                        mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                        mu.reg_write(UC_ARM64_REG_X20, bot)
                        mu.emu_start(BASE + 0xDB5DC, BASE + 0xDB618, count=30)
                        mu.reg_write(registers.UC_ARM64_REG_S13, struct.unpack('<I', struct.pack('<f', 250))[0])
                        mu.reg_write(registers.UC_ARM64_REG_S15, struct.unpack('<I', struct.pack('<f', 8))[0])
                        draws, frees, lookups = [], [], []

                        def external_call(uc, address, size, _):
                            ins = struct.unpack('<I', uc.mem_read(address, 4))[0]
                            if ins == 0xD503237F:  # synthetic PACIBSP frame only
                                uc.reg_write(UC_ARM64_REG_PC, address + 4)
                                return
                            if ins == 0xD65F0FFF:  # synthetic RETAB only
                                uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
                                return
                            if ins & 0xFC000000 != 0x94000000:
                                return
                            delta = ins & 0x3FFFFFF
                            if delta & 0x2000000:
                                delta -= 0x4000000
                            target = address + delta * 4 - BASE
                            if target == 0xDC0FC:
                                return  # execute the actual argument bridge and native helper
                            if target == 0xDF55C:
                                lookups.append(uc.reg_read(UC_ARM64_REG_X0))
                                uc.reg_write(UC_ARM64_REG_X0, name_object)
                            elif target == 0x6C34:
                                value = bytes(uc.mem_read(uc.reg_read(UC_ARM64_REG_X1), uc.reg_read(registers.UC_ARM64_REG_X2)))
                                put_string(uc.reg_read(UC_ARM64_REG_X0), value, copy_heap)
                            elif target == 0x6EC0:
                                pointer = uc.reg_read(UC_ARM64_REG_X1)
                                value = bytearray()
                                while uc.mem_read(pointer + len(value), 1) != b'\0':
                                    value.extend(uc.mem_read(pointer + len(value), 1))
                                put_string(uc.reg_read(UC_ARM64_REG_X0), bytes(value), copy_heap)
                            elif target == 0x143400:
                                pointer, end = uc.reg_read(UC_ARM64_REG_X1), uc.reg_read(registers.UC_ARM64_REG_X2)
                                value = bytes(uc.mem_read(pointer, end - pointer)).decode()
                                self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S0), 0x41600000)
                                for reg, result in [(registers.UC_ARM64_REG_S0, len(value) * 6), (registers.UC_ARM64_REG_S1, 14)]:
                                    uc.reg_write(reg, struct.unpack('<I', struct.pack('<f', result))[0])
                            elif target == 0x123E30:
                                uc.reg_write(UC_ARM64_REG_X0, 0x1234)
                            elif target == 0x130E08:
                                uc.reg_write(UC_ARM64_REG_X0, 0x789)
                            elif target == 0x11D434:
                                self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), BASE + 0xAC8648)
                                uc.reg_write(UC_ARM64_REG_W0, 0xB2000000)
                            elif target == 0x13BAE0:
                                pointer, end = uc.reg_read(registers.UC_ARM64_REG_X5), uc.reg_read(registers.UC_ARM64_REG_X6)
                                value = bytes(uc.mem_read(pointer, end - pointer)).decode()
                                point = struct.unpack('<2f', uc.mem_read(uc.reg_read(registers.UC_ARM64_REG_X2), 8))
                                self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3),
                                                 0xFFFFFFFF if bot else 0xFF0000FF)
                                self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), 0x789)
                                self.assertEqual(uc.reg_read(UC_ARM64_REG_X1), 0x1234)
                                self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S0), 0x41600000)
                                self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S2), 0x3F800000)
                                self.assertEqual(struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM64_REG_SP), 4))[0], 0xB2000000)
                                draws.append((value, point))
                            elif target == 0x725240:
                                frees.append(uc.reg_read(UC_ARM64_REG_X0))
                            else:
                                self.fail(f'Unexpected name-path call {target:#x}; team/health must stay disabled')
                            uc.reg_write(UC_ARM64_REG_PC, address + 4)

                        mu.hook_add(UC_HOOK_CODE, external_call)
                        mu.emu_start(BASE + 0xDBC90, BASE + 0xDC754, count=400)
                        self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xDC754)
                        self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                        self.assertEqual(bytes(mu.mem_read(name_object, 24)), original_string)
                        if len(raw) > 22:
                            self.assertEqual(bytes(mu.mem_read(source_heap, len(raw))), raw)
                        if enabled:
                            shown = name or '未知玩家'
                            self.assertEqual(lookups, [0x123123])
                            self.assertEqual(draws, [(shown, (400 - len(shown) * 3, 176))])
                            self.assertEqual(frees, [copy_heap] if len(shown.encode()) > 22 else [])
                        else:
                            self.assertEqual((lookups, draws, frees), ([], [], []))
                        for reg, expected in [(registers.UC_ARM64_REG_S9, 700), (registers.UC_ARM64_REG_S10, 600),
                                              (registers.UC_ARM64_REG_S11, 200), (registers.UC_ARM64_REG_S13, 250),
                                              (registers.UC_ARM64_REG_S14, 400), (registers.UC_ARM64_REG_S15, 8)]:
                            actual = struct.unpack('<f', struct.pack('<I', mu.reg_read(reg)))[0]
                            self.assertEqual(actual, expected)

    def test_visibility_request_tracks_checkbox_without_enabling_aim(self):
        mu = self.emulator()
        mu.reg_write(UC_ARM64_REG_X27, CONFIG)
        mu.mem_write(CONFIG + 0x18, struct.pack("<Q", 0))  # no bound target yet
        for enabled in [1, 0, 1]:
            mu.mem_write(CONFIG + 0x23, bytes([enabled]))
            mu.mem_write(CONFIG + 0x168, b"\x01")  # old aim setting enabled
            mu.mem_write(CONFIG + 0x1E9, b"\x00")  # native readiness is not faked
            mu.reg_write(UC_ARM64_REG_W8, 0xFFFFFFFF)
            mu.emu_start(BASE + 0xDB1B0, BASE + 0xDB1B4, count=10)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_W8), 0)
            self.assertEqual(mu.mem_read(CONFIG + 0x2F, 1), bytes([enabled]))
            self.assertEqual(mu.mem_read(CONFIG + 0x1E9, 1), b"\x00")
            self.assertEqual(mu.mem_read(CONFIG + 0x168, 1), b"\x01")
            self.assertEqual(mu.reg_read(UC_ARM64_REG_X30), RETURN)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
        # No normal actor-loop path can fall through into the helper block,
        # even when a previous aim-candidate return value was nonzero.
        mu.emu_start(BASE + 0xDD464, BASE + 0xDBDC8, count=3)
        self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xDBDC8)

    def test_visibility_activation_runs_original_global_initializer_once(self):
        # Execute the original activation and initializer. A synthetic failed
        # backend snapshot must leave readiness/scene objects unavailable.
        # This also tests saved-on settings and target binding after first frame.
        mu = self.emulator()
        activations, initializers = [], []

        def native_calls(uc, address, size, _):
            if address == BASE + 0xD3EAC:
                activations.append(address)
            if address == BASE + 0xD36A8:
                initializers.append(address)
            instruction = struct.unpack("<I", uc.mem_read(address, 4))[0]
            if instruction == 0xD503237F:  # emulator-only PACIBSP skip
                uc.reg_write(UC_ARM64_REG_PC, address + 4)
            elif instruction == 0xD65F0FFF:  # emulator-only RETAB skip
                uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
            elif instruction & 0xFC000000 == 0x94000000:
                delta = instruction & 0x3FFFFFF
                if delta & 0x2000000:
                    delta -= 0x4000000
                target = address + delta * 4 - BASE
                if target == 0xD3EAC:
                    return  # execute actual original activation
                self.assertIn(target, [0x725090, 0x7250A0, 0x64C7C])
                if target == 0x64C7C:
                    uc.reg_write(UC_ARM64_REG_W0, 0)  # no validated backend snapshot
                uc.reg_write(UC_ARM64_REG_PC, address + 4)

        mu.hook_add(UC_HOOK_CODE, native_calls)
        for enabled in [0, 1]:
            for bound in [0, 1]:
                for old_request in [0, 1]:
                    for old_mode in [0, 1]:
                        for armed in [0, 1]:
                            with self.subTest(enabled=enabled, bound=bound,
                                              old_request=old_request, mode=old_mode, armed=armed):
                                activations.clear(); initializers.clear()
                                mu.mem_write(CONFIG + 0x18, struct.pack("<Q", 0x100010000 if bound else 0))
                                mu.mem_write(CONFIG + 0x23, bytes([enabled]))
                                mu.mem_write(CONFIG + 0x2F, bytes([old_request]))
                                mu.mem_write(CONFIG + 0x130, struct.pack("<I", old_mode))
                                mu.mem_write(CONFIG + 0x168, b"\x01")
                                mu.mem_write(CONFIG + 0x1E9, b"\x00")
                                mu.mem_write(BASE + 0xC4E400, bytes([armed]))
                                for i in range(3):
                                    mu.mem_write(BASE + 0xC15EC8 + 8*i, struct.pack("<Q", 0))
                                saved = {registers.UC_ARM64_REG_X19: STACK + 0x1000,
                                         registers.UC_ARM64_REG_X22: 0x1234,
                                         UC_ARM64_REG_X27: CONFIG}
                                for reg, value in saved.items():
                                    mu.reg_write(reg, value)
                                mu.reg_write(UC_ARM64_REG_X30, RETURN)
                                mu.emu_start(BASE + 0xDB1B0, BASE + 0xDB1B4, count=400)
                                called = bool(enabled and bound and
                                              (not old_request or not old_mode or not armed))
                                self.assertEqual(len(activations), int(called))
                                self.assertEqual(len(initializers), int(called))
                                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xDB1B4)
                                self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                                for reg, value in saved.items():
                                    self.assertEqual(mu.reg_read(reg), value)
                                self.assertEqual(mu.reg_read(UC_ARM64_REG_W8), 0)
                                self.assertEqual(mu.mem_read(CONFIG + 0x168, 1), b"\x01")
                                self.assertEqual(mu.mem_read(CONFIG + 0x2F, 1), bytes([enabled]))
                                self.assertEqual(mu.mem_read(CONFIG + 0x1E9, 1), b"\x00")
                                self.assertEqual(mu.mem_read(BASE + 0xC15EC8, 24), b"\x00" * 24)
                                mode = struct.unpack("<I", mu.mem_read(CONFIG + 0x130, 4))[0]
                                self.assertEqual(mode, 1 if enabled and bound else old_mode)
                                # A failed original initialization is not retried
                                # on every frame after the activation has armed.
                                mu.emu_start(BASE + 0xDB1B0, BASE + 0xDB1B4, count=400)
                                self.assertEqual(len(activations), int(called))
                                self.assertEqual(len(initializers), int(called))

    def test_native_collector_acquires_bones_with_old_skeleton_settings_off(self):
        mu = self.emulator()
        context = STACK + 0x1000
        mu.reg_write(registers.UC_ARM64_REG_X19, context)
        # Replay v10's collector to demonstrate the missing acquisition gate.
        mu.mem_write(BASE + 0xD88BC, self.original[0xD88BC:0xD88F0])
        mu.mem_write(context + 0x23, b"\x01")
        mu.mem_write(context + 0x22, b"\x00")
        mu.mem_write(context + 0xB9, b"\x00")
        mu.mem_write(context + 0x40, struct.pack("<I", 200))
        mu.reg_write(registers.UC_ARM64_REG_S10, 0x42C80000)  # 100 metres
        for bot in [0, 1]:
            mu.reg_write(registers.UC_ARM64_REG_W28, bot)
            mu.emu_start(BASE + 0xD88BC, BASE + 0xD88EC, count=30)
            self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_W28), 0)
        # A fresh emulator also avoids cached translations of v10 instructions.
        mu = self.emulator()
        mu.reg_write(registers.UC_ARM64_REG_X19, context)
        for enabled in [0, 1]:
            for bot in [0, 1]:
                for old_flag in [0, 1, 255]:
                    for old_range in [1, 200, 1000]:
                        for distance in [100, 200, 250, 300, 300.01, float("nan"), float("inf")]:
                            with self.subTest(enabled=enabled, bot=bot, old_flag=old_flag,
                                              old_range=old_range, distance=distance):
                                mu.mem_write(context + 0x23, bytes([enabled]))
                                mu.mem_write(context + 0x22, bytes([old_flag]))
                                mu.mem_write(context + 0xB9, bytes([old_flag]))
                                mu.mem_write(context + 0x40, struct.pack("<I", old_range))
                                mu.reg_write(registers.UC_ARM64_REG_W28, bot)
                                mu.reg_write(registers.UC_ARM64_REG_S10,
                                             struct.unpack("<I", struct.pack("<f", distance))[0])
                                mu.emu_start(BASE + 0xD88BC, BASE + 0xD88EC, count=30)
                                self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_W28),
                                                 int(enabled and distance <= 300))
        # Native model scan must include all 14 segments when collection is on,
        # rather than only the head/bounding-box endpoints when it is off.
        pairs = struct.unpack("<28i", mu.mem_read(BASE + 0xAC8698, 112))
        mu.mem_write(STACK + 0x804C, struct.pack("<I", 61))
        for enabled in [0, 1]:
            mu.reg_write(registers.UC_ARM64_REG_X8, BASE + 0xAC8698)
            mu.reg_write(registers.UC_ARM64_REG_W28, enabled)
            mu.emu_start(BASE + 0xD8BA0, BASE + 0xD8D30, count=1000)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xD8D30)
            self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_W25),
                             max(pairs) if enabled else max(pairs[:2]))

    def test_fixed_range_and_native_actor_filter(self):
        mu = self.emulator()
        mu.reg_write(UC_ARM64_REG_X27, CONFIG)
        for old_range in [0, 1, 10, 199, 201, 1000, float("inf"), float("nan")]:
            mu.mem_write(CONFIG + 0x30, struct.pack("<f", old_range))
            mu.emu_start(BASE + 0xDB278, BASE + 0xDB2B8, count=40)
            self.assertEqual(struct.unpack("<f", mu.mem_read(STACK + 0x80FC, 4))[0], 300.0)
            mu.emu_start(BASE + 0xDCAA4, BASE + 0xDCAAC, count=3)
            self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_S0), 0x43960000)
        destinations = {BASE + 0xDBB54, BASE + 0xDBDC0}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                    if address in destinations else None)
        for distance, accepted in [(1, True), (200, True), (250, True), (299.99, True), (300, True),
                                   (300.01, False), (800, False), (-1, False),
                                   (float("inf"), False), (float("nan"), False)]:
            with self.subTest(distance=distance):
                mu.reg_write(registers.UC_ARM64_REG_S13,
                             struct.unpack("<I", struct.pack("<f", distance))[0])
                mu.emu_start(BASE + 0xDBAFC, BASE + 0xDBDCC, count=80)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC),
                                 BASE + (0xDBB54 if accepted else 0xDBDC0))

    def test_native_line_and_box_draw_only_when_enabled_with_fixed_style(self):
        for enabled, distance, (width, height) in product([0, 1], [0, 100, 100.01, 250, 300],
                                                        [(800, 600), (1280, 720)]):
            with self.subTest(enabled=enabled, distance=distance, width=width, height=height):
                mu = self.emulator()
                mu.mem_write(CONFIG, b"\xff" * 0x500)  # old widths/colors ignored
                context = STACK + 0x1000
                mu.mem_write(context, struct.pack("<3f", 1, width, height))
                mu.reg_write(registers.UC_ARM64_REG_X19, context)
                mu.reg_write(registers.UC_ARM64_REG_X22, 0x1234)
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                for reg, value in [(registers.UC_ARM64_REG_S14, 100),
                                   (registers.UC_ARM64_REG_S10, 100),
                                   (registers.UC_ARM64_REG_S11, 100),
                                   (registers.UC_ARM64_REG_S9, 300),
                                   (registers.UC_ARM64_REG_S13, distance)]:
                    mu.reg_write(reg, struct.unpack("<I", struct.pack("<f", value))[0])
                for offset in [0x150, 0x160]:
                    mu.mem_write(STACK + 0x8000 + offset, struct.pack("<I", enabled))
                calls = []

                def mock_draw(uc, address, size, _):
                    ins = struct.unpack("<I", uc.mem_read(address, 4))[0]
                    if ins & 0xFC000000 != 0x94000000:
                        return
                    delta = ins & 0x3FFFFFF
                    if delta & 0x2000000:
                        delta -= 0x4000000
                    target = address + delta * 4 - BASE
                    self.assertIn(target, [0x13AD48, 0x13B1C0])
                    points = [struct.unpack("<2f", uc.mem_read(uc.reg_read(reg), 8))
                              for reg in [registers.UC_ARM64_REG_X1, registers.UC_ARM64_REG_X2]]
                    calls.append((target, points))
                    color = 0x70FFFFFF if target == 0x13AD48 and distance > 100 else 0xFFFFFFFF
                    self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), color)
                    self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S0), 0x3F800000)
                    uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, mock_draw)
                mu.emu_start(BASE + 0xDC754, BASE + 0xDC804, count=100)
                self.assertEqual([c[0] for c in calls], [0x13AD48, 0x13B1C0] if enabled else [])
                if enabled:
                    self.assertEqual(calls[0][1], [(width / 2, height - 10), (100.0, 70.0)])
                    self.assertEqual(calls[1][1], [(150.0, 100.0), (50.0, 300.0)])

    def test_offscreen_arrows_keep_correct_bearing_and_distance(self):
        # Run original actor/team/range checks, native edge placement, the native
        # seven-point arrow (fill + outline), and outlined distance text. Only
        # libc math/string, cache cleanup, font lookup and graphics are stubbed.
        diagonal = math.sqrt(0.5)
        directions = [
            (0, (10000, 0), (0, -1)), (0, (0, 10000), (1, 0)),
            (0, (-10000, 0), (0, 1)), (0, (0, -10000), (-1, 0)),
            (0, (10000, 10000), (diagonal, -diagonal)),
            (0, (-10000, 10000), (diagonal, diagonal)),
            (0, (-10000, -10000), (-diagonal, diagonal)),
            (0, (10000, -10000), (-diagonal, -diagonal)),
            (90, (0, 10000), (0, -1)), (90, (-10000, 0), (1, 0)),
            (180, (10000, 0), (0, 1)), (270, (0, 10000), (0, 1)),
            (360, (10000, 0), (0, -1)), (-90, (10000, 0), (1, 0)),
        ]
        cases = [dict(enabled=enabled, bot=bot, width=width, height=height,
                      yaw=yaw, delta=delta, direction=direction)
                 for enabled, bot, (width, height), (yaw, delta, direction)
                 in product([0, 1], [0, 1], [(800, 600), (1280, 720)], directions)]
        defaults = dict(enabled=1, bot=0, width=800, height=600, yaw=0,
                        delta=(-10000, 0), direction=(0, 1))
        for overrides in [dict(distance=300), dict(distance=300.01, invalid=True),
                          dict(distance=-1, invalid=True), dict(distance=float('nan'), invalid=True),
                          dict(distance=float('inf'), invalid=True), dict(team=1, invalid=True),
                          dict(on_screen=1, invalid=True), dict(delta=(0, 0), invalid=True),
                          dict(delta=(float('inf'), 0), invalid=True),
                          dict(delta=(0, float('nan')), invalid=True),
                          dict(yaw=float('nan'), invalid=True), dict(yaw=float('inf'), invalid=True),
                          dict(width=0, invalid=True), dict(height=-1, invalid=True),
                          dict(width=float('nan'), invalid=True), dict(height=float('inf'), invalid=True)]:
            cases.append(defaults | overrides)
        for case in cases:
            with self.subTest(**case):
                mu = self.emulator()
                context, actor, font_owner = STACK + 0x1000, STACK + 0x2000, STACK + 0x6000
                distance = case.get('distance', 100)
                width, height, enabled = case['width'], case['height'], case['enabled']
                mu.mem_write(CONFIG, b'\xff' * 0x500)  # unrelated saved styles/radar/colors ignored
                mu.mem_write(CONFIG + 0x23, bytes([enabled]))
                mu.mem_write(CONFIG + 0x318, struct.pack('<Q', 0x789))
                mu.mem_write(BASE + 0xC54940, struct.pack('<Q', font_owner))
                mu.mem_write(font_owner + 0x11C0, struct.pack('<Q', 0x789))
                mu.mem_write(context, struct.pack('<3f', 1, width, height))
                mu.mem_write(context + 0x3A8, struct.pack('<I', 1))
                mu.mem_write(context + 0x1F4, struct.pack('<2f', 1000, 2000))
                mu.mem_write(context + 0x210, struct.pack('<f', case['yaw']))
                mu.mem_write(actor, struct.pack('<Q', 0x123123))
                mu.mem_write(actor + 8, struct.pack('<I', case.get('team', 2)))
                mu.mem_write(actor + 0xC, bytes([case['bot']]))
                dx, dy = case['delta']
                mu.mem_write(actor + 0x18, struct.pack('<2f', 1000 + dx, 2000 + dy))
                # A misleading/invalid projected coordinate must not reverse the
                # rear arrow; direction is derived from cached world coordinates.
                mu.mem_write(actor + 0x28, struct.pack('<2f', float('nan'), -10000))
                mu.mem_write(actor + 0x40, struct.pack('<f', distance))
                mu.mem_write(actor + 0x4C, bytes([case.get('on_screen', 0)]))
                mu.mem_write(STACK + 0x8118, struct.pack('<Q', actor + 0x1F8))
                mu.mem_write(STACK + 0x80F0, struct.pack('<Q', context + 0x350))
                mu.mem_write(STACK + 0x80FC, struct.pack('<f', 300))
                for reg, value in [(registers.UC_ARM64_REG_X19, context),
                                   (registers.UC_ARM64_REG_X22, 0x1234),
                                   (UC_ARM64_REG_X23, BASE + 0xC54000),
                                   (registers.UC_ARM64_REG_X24, actor),
                                   (UC_ARM64_REG_X25, actor + 0x1F8),
                                   (UC_ARM64_REG_X27, CONFIG),
                                   (registers.UC_ARM64_REG_X28, BASE + 0xC54000)]:
                    mu.reg_write(reg, value)
                mu.reg_write(registers.UC_ARM64_REG_W20, 1)  # prior actor-loop state
                mu.emu_start(BASE + 0xDB46C, BASE + 0xDB480, count=10)
                self.assertEqual(struct.unpack('<I', mu.mem_read(STACK + 0x80B0, 4))[0], enabled)
                self.assertEqual(struct.unpack('<I', mu.mem_read(STACK + 0x80D4, 4))[0], enabled)
                fills, outlines, labels, arrow_arguments = [], [], [], []

                def float_read(reg):
                    return struct.unpack('<f', struct.pack('<I', mu.reg_read(reg)))[0]

                def float_write(reg, value):
                    mu.reg_write(reg, struct.unpack('<I', struct.pack('<f', value))[0])

                def external_call(uc, address, size, _):
                    if address == BASE + 0xDBCA0:
                        uc.emu_stop()  # on-screen actor remains on its original front-marker path
                        return
                    ins = struct.unpack('<I', uc.mem_read(address, 4))[0]
                    if ins == 0xD503237F:
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)
                        return
                    if ins == 0xD65F0FFF:
                        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
                        return
                    if ins & 0xFC000000 != 0x94000000:
                        return
                    delta = ins & 0x3FFFFFF
                    if delta & 0x2000000:
                        delta -= 0x4000000
                    target = address + delta * 4 - BASE
                    if target == 0xD3F70:
                        arrow_arguments.append((float_read(registers.UC_ARM64_REG_S0),
                                                float_read(registers.UC_ARM64_REG_S1)))
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W2), 0)
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W1), 0xFF00FFFF)
                        self.assertEqual(float_read(registers.UC_ARM64_REG_S2), 40)
                        return  # execute original polygon generation and authenticated frame
                    if target == 0xDF47C:
                        return  # execute original outlined text wrapper
                    if target == 0xF38BC:
                        pass  # original unrelated cache cleanup
                    elif target == 0x123E30:
                        uc.reg_write(UC_ARM64_REG_X0, 0x1234)
                    elif target == 0x130E08:
                        uc.reg_write(UC_ARM64_REG_X0, 0x789)
                    elif target in [0x7255E0, 0x725390]:
                        a, b = float_read(registers.UC_ARM64_REG_S0), float_read(registers.UC_ARM64_REG_S1)
                        for reg in range(registers.UC_ARM64_REG_S2, registers.UC_ARM64_REG_S7 + 1):
                            uc.reg_write(reg, 0)  # libc may clobber caller-saved edge inputs
                        if target == 0x7255E0:
                            float_write(registers.UC_ARM64_REG_S0, math.atan2(a, b))
                        else:
                            float_write(registers.UC_ARM64_REG_S0, math.sin(a))
                            float_write(registers.UC_ARM64_REG_S1, math.cos(a))
                    elif target in [0x13C8AC, 0x139458]:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), 0x1234)
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W2), 7)
                        points = struct.unpack('<14f', uc.mem_read(uc.reg_read(UC_ARM64_REG_X1), 56))
                        points = list(zip(points[::2], points[1::2]))
                        self.assertTrue(all(math.isfinite(v) for point in points for v in point))
                        if target == 0x13C8AC:
                            self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), 0xFF00FFFF)
                            fills.append(points)
                        else:
                            self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), 0xFF000000)
                            outlines.append(points)
                    elif target == 0x726A30:
                        dest, length = uc.reg_read(UC_ARM64_REG_X0), uc.reg_read(UC_ARM64_REG_X1)
                        metres = struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM64_REG_SP), 4))[0]
                        value = f'{metres}m'.encode()
                        self.assertLess(len(value), length)
                        uc.mem_write(dest, value + b'\0')
                        uc.reg_write(UC_ARM64_REG_X0, len(value))
                    elif target == 0x726B20:
                        pointer = uc.reg_read(UC_ARM64_REG_X0)
                        value = bytearray()
                        while uc.mem_read(pointer + len(value), 1) != b'\0':
                            value.extend(uc.mem_read(pointer + len(value), 1))
                        uc.reg_write(UC_ARM64_REG_X0, len(value))
                    elif target == 0x143400:
                        pointer = uc.reg_read(UC_ARM64_REG_X1)
                        value = bytearray()
                        while uc.mem_read(pointer + len(value), 1) != b'\0':
                            value.extend(uc.mem_read(pointer + len(value), 1))
                        float_write(registers.UC_ARM64_REG_S0, len(value) * 6)
                        float_write(registers.UC_ARM64_REG_S1, 12)
                    elif target == 0x11D434:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), BASE + 0xAC8648)
                        uc.reg_write(UC_ARM64_REG_W0, 0xB2000000)
                    elif target == 0x13BAE0:
                        pointer, end = uc.reg_read(registers.UC_ARM64_REG_X5), uc.reg_read(registers.UC_ARM64_REG_X6)
                        value = bytes(uc.mem_read(pointer, end - pointer)).decode()
                        point = struct.unpack('<2f', uc.mem_read(uc.reg_read(registers.UC_ARM64_REG_X2), 8))
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), 0xFF00FFFF)
                        self.assertEqual(float_read(registers.UC_ARM64_REG_S0), 12)
                        self.assertEqual(float_read(registers.UC_ARM64_REG_S2), 1)
                        labels.append((value, point))
                    else:
                        self.fail(f'Unexpected offscreen call {target:#x}')
                    uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, external_call)
                mu.emu_start(BASE + 0xDB5A0, BASE + 0xDBDC8, count=1600)
                expected_draw = enabled and not case.get('invalid', False)
                self.assertEqual(len(fills), int(expected_draw))
                self.assertEqual(len(outlines), int(expected_draw))
                self.assertEqual(len(labels), int(expected_draw))
                if expected_draw:
                    dx, dy = case['direction']
                    scale = min((width / 2 - 60) / abs(dx) if dx else float('inf'),
                                (height / 2 - 60) / abs(dy) if dy else float('inf'))
                    tip = (width / 2 + dx * scale, height / 2 + dy * scale)
                    for actual, expected in zip(arrow_arguments[0], tip):
                        self.assertAlmostEqual(actual, expected, delta=0.002)
                    for actual, expected in zip(fills[0][0], tip):
                        self.assertAlmostEqual(actual, expected, delta=0.002)
                    for point in fills[0][1:]:
                        self.assertLess((point[0] - tip[0]) * dx + (point[1] - tip[1]) * dy, 0)
                    self.assertEqual(fills, outlines)
                    self.assertEqual(labels[0][0], f'{round(distance)}m')
                    self.assertTrue(all(0 < x < width and 0 < y < height for x, y in fills[0]))
                    x, y = labels[0][1]
                    self.assertTrue(0 < x < width and 0 < y < height)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                if not case.get('on_screen', 0):
                    self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_W20), 1)

    def test_enemy_counts_reset_and_include_filtered_front_and_rear_targets(self):
        mu = self.emulator()
        context, actor = STACK + 0x1000, STACK + 0x2000
        mu.mem_write(context + 0x3A8, struct.pack('<I', 1))
        mu.mem_write(CONFIG, b'\xff' * 0x500)
        mu.mem_write(CONFIG + 0x23, b'\0')
        mu.reg_write(registers.UC_ARM64_REG_X19, context)
        mu.reg_write(UC_ARM64_REG_X27, CONFIG)
        mu.mem_write(STACK + 0x8118, struct.pack('<Q', actor + 0x1F8))
        mu.mem_write(STACK + 0x80F0, struct.pack('<Q', context + 0x350))
        mu.mem_write(STACK + 0x80FC, struct.pack('<f', 300))
        counters = BASE + 0xC548B0
        mu.mem_write(counters, struct.pack('<2I', 91, 92))
        # Execute the original per-frame clear, rather than resetting in the fixture.
        mu.emu_start(BASE + 0xDB18C, BASE + 0xDB19C, count=4)
        self.assertEqual(struct.unpack('<2I', mu.mem_read(counters, 8)), (0, 0))
        destinations = {BASE + 0xDBBC4, BASE + 0xDBDC8}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                    if address in destinations else None)
        cases = [(0, 1, 2, 15, True), (1, 0, 2, 20, True),
                 (0, 0, 2, 300, True), (1, 1, 2, 299.99, True), (0, 0, 2, 1, True),
                 (0, 0, 2, 300.01, False), (0, 1, 1, 25, False), (1, 0, 1, 25, False),
                 (0, 1, 2, -1, False), (1, 0, 2, float('inf'), False),
                 (0, 0, 2, float('nan'), False)]
        totals = [0, 0]
        for bot, on_screen, team, distance, accepted in cases:
            with self.subTest(bot=bot, on_screen=on_screen, team=team, distance=distance):
                mu.mem_write(actor + 8, struct.pack('<I', team))
                mu.mem_write(actor + 0xC, bytes([bot]))
                mu.mem_write(actor + 0x40, struct.pack('<f', distance))
                mu.mem_write(actor + 0x4C, bytes([on_screen]))
                mu.reg_write(registers.UC_ARM64_REG_X24, actor)
                mu.reg_write(UC_ARM64_REG_X25, actor + 0x1F8)
                mu.reg_write(registers.UC_ARM64_REG_W20, 1)
                mu.emu_start(BASE + 0xDB5A0, BASE + 0xDBDC8, count=250)
                totals[bot] += int(accepted)
                self.assertEqual(struct.unpack('<2I', mu.mem_read(counters, 8)), tuple(totals))
        self.assertEqual(totals, [3, 2])
        mu.emu_start(BASE + 0xDB18C, BASE + 0xDB19C, count=4)
        self.assertEqual(struct.unpack('<2I', mu.mem_read(counters, 8)), (0, 0))

    def test_top_count_draws_one_centered_red_number_with_native_outline(self):
        normal = [dict(enabled=e, humans=h, bots=b, scale=scale, width=w, height=height)
                  for e, (h, b), (scale, w, height) in product([0, 1],
                    [(0, 0), (1, 0), (0, 1), (3, 2), (80, 20), (0xFFFFFFFF, 0xFFFFFFFF)],
                    [(1, 800, 600), (2, 1280, 720), (0.75, 800, 600)])]
        default = dict(enabled=1, humans=3, bots=2, scale=1, width=800, height=600)
        cases = normal + [default | values | dict(invalid=True) for values in
                         [dict(scale=0), dict(scale=-1), dict(scale=float('nan')),
                          dict(scale=float('inf')), dict(width=0), dict(width=float('nan')),
                          dict(width=float('inf')), dict(height=0), dict(height=float('nan')),
                          dict(height=float('inf')), dict(height=88),
                          dict(format_result=-1), dict(format_result=16)]]
        for case in cases:
            with self.subTest(**case):
                mu = self.emulator()
                context, font_owner = STACK + 0x1000, STACK + 0x6000
                scale, width, height = case['scale'], case['width'], case['height']
                mu.mem_write(context, struct.pack('<3f', scale, width, height))
                mu.mem_write(CONFIG, b'\xff' * 0x500)  # old count flags/styles do not matter
                mu.mem_write(CONFIG + 0x23, bytes([case['enabled']]))
                mu.mem_write(CONFIG + 0x318, struct.pack('<Q', 0x789))
                mu.mem_write(BASE + 0xC54940, struct.pack('<Q', font_owner))
                mu.mem_write(font_owner + 0x11C0, struct.pack('<Q', 0x789))
                counters = struct.pack('<2I', case['humans'], case['bots'])
                mu.mem_write(BASE + 0xC548B0, counters)
                mu.reg_write(registers.UC_ARM64_REG_X19, context)
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                draws, formats = [], []
                total = min(case['humans'] + case['bots'], 0xFFFFFFFF)

                def external_call(uc, address, size, _):
                    ins = struct.unpack('<I', uc.mem_read(address, 4))[0]
                    if ins == 0xD503237F:
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)
                        return
                    if ins == 0xD65F0FFF:
                        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
                        return
                    if ins & 0xFC000000 != 0x94000000:
                        return
                    delta = ins & 0x3FFFFFF
                    if delta & 0x2000000:
                        delta -= 0x4000000
                    target = address + delta * 4 - BASE
                    if target == 0xDF47C:
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W2), 1)
                        return  # native centering, outline, draw arguments and frame
                    if target == 0x726A30:
                        fmt = uc.reg_read(registers.UC_ARM64_REG_X2)
                        self.assertEqual(bytes(uc.mem_read(fmt, 3)), b'%u\0')
                        value = struct.unpack('<Q', uc.mem_read(uc.reg_read(UC_ARM64_REG_SP), 8))[0]
                        self.assertEqual(value, total)
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X1), 16)
                        formats.append(value)
                        text = str(value).encode()
                        uc.mem_write(uc.reg_read(UC_ARM64_REG_X0), text + b'\0')
                        uc.reg_write(UC_ARM64_REG_W0, case.get('format_result', len(text)))
                    elif target == 0x143400:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), 0x789)
                        size_value = struct.unpack('<f', struct.pack('<I', uc.reg_read(registers.UC_ARM64_REG_S0)))[0]
                        self.assertEqual(size_value, 24 * scale)
                        uc.reg_write(registers.UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', len(str(total)) * 6 * scale))[0])
                        uc.reg_write(registers.UC_ARM64_REG_S1, struct.unpack('<I', struct.pack('<f', 24 * scale))[0])
                    elif target == 0x123E30:
                        uc.reg_write(UC_ARM64_REG_X0, 0x1234)
                    elif target == 0x130E08:
                        uc.reg_write(UC_ARM64_REG_X0, 0x789)
                    elif target == 0x11D434:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), BASE + 0xAC8648)
                        uc.reg_write(UC_ARM64_REG_W0, 0xB2000000)
                    elif target == 0x13BAE0:
                        pointer, end = uc.reg_read(registers.UC_ARM64_REG_X5), uc.reg_read(registers.UC_ARM64_REG_X6)
                        text = bytes(uc.mem_read(pointer, end - pointer)).decode()
                        point = struct.unpack('<2f', uc.mem_read(uc.reg_read(registers.UC_ARM64_REG_X2), 8))
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), 0xFF0000FF)
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), 0x789)
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X1), 0x1234)
                        self.assertEqual(struct.unpack('<f', struct.pack('<I', uc.reg_read(registers.UC_ARM64_REG_S0)))[0], 24 * scale)
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S2), 0x3F800000)
                        self.assertEqual(struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM64_REG_SP), 4))[0], 0xB2000000)
                        draws.append((text, point))
                    else:
                        self.fail(f'Unexpected top-count call {target:#x}')
                    uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, external_call)
                mu.emu_start(BASE + 0xDED8C, BASE + 0xDEF0C, count=300)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xDEF0C)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                self.assertEqual(bytes(mu.mem_read(BASE + 0xC548B0, 8)), counters)
                if case['enabled'] and not case.get('invalid', False):
                    self.assertEqual(formats, [total])
                    self.assertEqual(draws, [(str(total), (width / 2 - len(str(total)) * 3 * scale, 64 * scale))])
                else:
                    self.assertEqual(draws, [])

    def test_skeleton_requires_native_readiness_and_all_geometry_objects(self):
        mu = self.emulator()
        context = STACK + 0x1000
        mu.reg_write(registers.UC_ARM64_REG_X19, context)
        mu.reg_write(UC_ARM64_REG_X20, CONFIG)
        mu.mem_write(CONFIG + 0x23, b"\x01")
        destinations = {BASE + 0xDCBE4, BASE + 0xDCDB8}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                    if address in destinations else None)
        for context_ready in [0, 1]:
            for engine_ready in [0, 1]:
                for missing in [None, 0, 1, 2]:
                    with self.subTest(context=context_ready, engine=engine_ready, missing=missing):
                        mu.mem_write(context + 0x1E9, bytes([context_ready]))
                        mu.mem_write(CONFIG + 0x1E9, bytes([engine_ready]))
                        for index in range(3):
                            mu.mem_write(BASE + 0xC15EC8 + index * 8,
                                         struct.pack("<Q", 0 if index == missing else 0x12345678))
                        mu.emu_start(BASE + 0xDCBCC, BASE + 0xDCDD0, count=40)
                        ready = context_ready and engine_ready and missing is None
                        self.assertEqual(mu.reg_read(UC_ARM64_REG_PC),
                                         BASE + (0xDCBE4 if ready else 0xDCDB8))
                        self.assertEqual(mu.mem_read(CONFIG + 0x1E9, 1), bytes([engine_ready]))
                        self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)
                        self.assertEqual(mu.reg_read(UC_ARM64_REG_X30), RETURN)

    def test_skeleton_endpoint_obstruction_selects_white_or_red(self):
        mu = self.emulator()
        mu.reg_write(UC_ARM64_REG_X23, 3)
        mu.reg_write(registers.UC_ARM64_REG_X24, 7)
        for first, second in [(0, 0), (0, 1), (1, 0), (1, 1)]:
            with self.subTest(first=first, second=second):
                mu.mem_write(BASE + 0xC4E18C + 3, bytes([first]))
                mu.mem_write(BASE + 0xC4E18C + 7, bytes([second]))
                mu.emu_start(BASE + 0xDCD34, BASE + 0xDCD70, count=20)
                self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_X21),
                                 0xFF0000FF if first or second else 0xFFFFFFFF)

    def test_native_joint_loop_projects_queries_and_draws_each_segment(self):
        # Execute the original 14-segment loop. Only external projection,
        # geometry queries and graphics calls use synthetic data/stubs.
        for enabled, distance, scenario in [(1, 300, "clear"), (1, 250, "blocked"),
                                            (1, 80, "mixed"), (0, 80, "clear"),
                                            (1, 300.01, "clear")]:
            with self.subTest(enabled=enabled, distance=distance, scenario=scenario):
                mu = self.emulator()
                context, actor = STACK + 0x1000, STACK + 0x2000
                pairs = list(zip(*[iter(struct.unpack("<28i", mu.mem_read(BASE + 0xAC8698, 112)))] * 2))
                blocked = {pairs[0][0]} if scenario == "mixed" else set()
                mu.mem_write(context + 4, struct.pack("<2f", 800, 600))
                mu.mem_write(context + 0x1E9, b"\x01")
                mu.mem_write(CONFIG + 0x1E9, b"\x01")
                mu.mem_write(CONFIG + 0x23, bytes([enabled]))
                mu.mem_write(CONFIG + 0x40, struct.pack("<I", 1))  # old bone range ignored
                for index in range(3):
                    mu.mem_write(BASE + 0xC15EC8 + index * 8, struct.pack("<Q", 0x12345678))
                mu.mem_write(BASE + 0xC4E468, b"\x01")  # original mapping initialization complete
                mu.mem_write(actor + 0x50, struct.pack("<I", 61))
                mu.mem_write(actor + 0x59, b"\x01")
                mu.mem_write(actor + 0x1AC, b"\x01" * 28)
                for index, bone in enumerate(sum(([a, b] for a, b in pairs), [])):
                    mu.mem_write(actor + 0x5C + index * 12, struct.pack("<3f", bone + 10, bone + 20, 0))
                mu.mem_write(STACK + 0x8140, struct.pack("<I", enabled))
                mu.mem_write(STACK + 0x8190, struct.pack("<Q", actor))
                mu.reg_write(registers.UC_ARM64_REG_X19, context)
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                mu.reg_write(registers.UC_ARM64_REG_S13, struct.unpack("<I", struct.pack("<f", distance))[0])
                draws, queries = [], []

                def external_call(uc, address, size, _):
                    instruction = struct.unpack("<I", uc.mem_read(address, 4))[0]
                    if instruction & 0xFC000000 != 0x94000000:
                        return
                    delta = instruction & 0x3FFFFFF
                    if delta & 0x2000000:
                        delta -= 0x4000000
                    target = address + delta * 4 - BASE
                    if target == 0xDA704:
                        values = struct.unpack("<3f", uc.mem_read(uc.reg_read(UC_ARM64_REG_X0), 12))
                        for reg, value in zip([registers.UC_ARM64_REG_S0, registers.UC_ARM64_REG_S1], values):
                            uc.reg_write(reg, struct.unpack("<I", struct.pack("<f", value))[0])
                    elif target == 0xD33F4:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), context + 0x1F4)
                        value = struct.unpack("<3f", uc.mem_read(uc.reg_read(UC_ARM64_REG_X1), 12))[0]
                        bone = int(value - 10)
                        queries.append(bone)
                        uc.reg_write(UC_ARM64_REG_W0, int(scenario == "blocked" or bone in blocked))
                    elif target == 0xDF944:
                        uc.reg_write(UC_ARM64_REG_W0, 1)
                    elif target == 0x123E30:
                        uc.reg_write(UC_ARM64_REG_X0, 0x1234)
                    elif target == 0x13AD48:
                        self.assertEqual(uc.reg_read(UC_ARM64_REG_X0), 0x1234)
                        self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S0), 0x3F800000)
                        points = [struct.unpack("<2f", uc.mem_read(uc.reg_read(reg), 8))
                                  for reg in [registers.UC_ARM64_REG_X1, registers.UC_ARM64_REG_X2]]
                        draws.append((points, uc.reg_read(registers.UC_ARM64_REG_W3)))
                    else:
                        self.fail(f"Unexpected native loop call {target:#x}")
                    uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, external_call)
                mu.emu_start(BASE + 0xDCA84, BASE + 0xDCDC0, count=6000)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), BASE + 0xDCDC0)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_X27), CONFIG)
                if not enabled or distance > 300:
                    self.assertEqual(draws, [])
                    self.assertEqual(queries, [])
                    continue
                self.assertEqual(len(draws), 14)
                self.assertEqual(set(queries), set(sum(([a, b] for a, b in pairs), [])))
                for (first, second), (points, color) in zip(pairs, draws):
                    self.assertEqual(points, [(first + 10.0, first + 20.0), (second + 10.0, second + 20.0)])
                    occluded = scenario == "blocked" or first in blocked or second in blocked
                    self.assertEqual(color, 0xFF0000FF if occluded else 0xFFFFFFFF)
                if scenario == "mixed":
                    self.assertEqual({d[1] for d in draws}, {0xFF0000FF, 0xFFFFFFFF})

    def test_native_query_distinguishes_clear_ray_from_geometry_hit(self):
        # Verify the meaning of the original query result, independently of the
        # replacement color instructions. Scene hit records use -1 for no hit.
        for hit_at in [None, 0, 1, 2]:
            with self.subTest(hit_at=hit_at):
                mu = self.emulator()
                mu.mem_write(CONFIG + 0x1E9, b"\x01")
                for index in range(3):
                    mu.mem_write(BASE + 0xC15EC8 + index * 8, struct.pack("<Q", 0x12345678))
                mu.reg_write(UC_ARM64_REG_X0, STACK + 0x1000)
                mu.reg_write(UC_ARM64_REG_X1, STACK + 0x1100)
                queries = []

                def mock_scene(uc, address, size, _):
                    instruction = struct.unpack("<I", uc.mem_read(address, 4))[0]
                    if instruction == 0xD503237F:  # PACIBSP: emulator only
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)
                    elif instruction == 0xD65F0FFF:  # RETAB: emulator only
                        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))
                    elif instruction & 0xFC000000 == 0x94000000:
                        delta = instruction & 0x3FFFFFF
                        if delta & 0x2000000:
                            delta -= 0x4000000
                        target = address + delta * 4 - BASE
                        if target in [0xE0BE8, 0xE0CF0]:
                            hit = len(queries) == hit_at
                            queries.append(target)
                            uc.mem_write(uc.reg_read(UC_ARM64_REG_X8) + 0x48,
                                         struct.pack("<i", 0 if hit else -1))
                        else:
                            self.assertIn(target, [0x725000, 0x725010])  # lock/unlock
                        uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, mock_scene)
                mu.emu_start(BASE + 0xD33F4, RETURN, count=300)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC), RETURN)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_W0), int(hit_at is not None))
                self.assertEqual(len(queries), 3 if hit_at is None else hit_at + 1)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_SP), STACK + 0x8000)

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
