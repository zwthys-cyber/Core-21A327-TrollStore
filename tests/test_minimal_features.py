"""Execute modified ARM64 control flow with synthetic local UI/configuration.

These tests do not run iOS, the game, an exploit, or remote process bootstrap.
Pointer authentication is skipped only in synthetic call-frame tests; native
prologue and epilogue instructions remain in the packaged application.
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
                       0xD88C0, 0xD88D0, 0xD88DC, 0xD88E0]:
            allowed.update(range(offset, offset + 4))
        for offset, length in [(0xDD468, 152), (0xDCD40, 44)]:
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
        for offset in [0xDB2FC, 0xDB3CC, 0xDB46C, 0xDB470, 0xDB5EC,
                       0xDB680, 0xDB69C, 0xDB864,
                       0xDBBC4, 0xDCDC0, 0xDD5F0, 0xDD758,
                       0xDDEBC, 0xDDEC8, 0xDDF70, 0xDDFC4, 0xDE014, 0xDE5B4,
                       0xDE5D0, 0xDE734, 0xDEBE8, 0xDEC30, 0xDED8C, 0xDED94,
                       0xDEE00, 0xDEE08]:
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
            for offset in [0xDB630, 0xDB644, 0xDB658, 0xDB66C, 0xDCBCC]:
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                mu.reg_write(UC_ARM64_REG_X20, CONFIG)
                mu.reg_write(UC_ARM64_REG_X8, 1)  # old category pointer is irrelevant
                mu.emu_start(BASE + offset, BASE + offset + 4, count=1)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_W8), enabled)
        for bot in [0, 1]:
            mu.reg_write(UC_ARM64_REG_X27, CONFIG)
            mu.reg_write(UC_ARM64_REG_X20, bot)
            mu.emu_start(BASE + 0xDB5DC, BASE + 0xDB618, count=30)
            self.assertEqual(mu.reg_read(UC_ARM64_REG_W25), 2)  # native no-panel style

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
                        for distance in [100, 200, 200.01, float("nan"), float("inf")]:
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
                                                 int(enabled and distance <= 200))
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
            self.assertEqual(struct.unpack("<f", mu.mem_read(STACK + 0x80FC, 4))[0], 200.0)
            mu.emu_start(BASE + 0xDCAA4, BASE + 0xDCAAC, count=3)
            self.assertEqual(mu.reg_read(registers.UC_ARM64_REG_S0), 0x43480000)
        destinations = {BASE + 0xDBB54, BASE + 0xDBDC0}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                    if address in destinations else None)
        for distance, accepted in [(1, True), (199.99, True), (200, True),
                                   (200.01, False), (800, False), (-1, False),
                                   (float("inf"), False), (float("nan"), False)]:
            with self.subTest(distance=distance):
                mu.reg_write(registers.UC_ARM64_REG_S13,
                             struct.unpack("<I", struct.pack("<f", distance))[0])
                mu.emu_start(BASE + 0xDBAFC, BASE + 0xDBDCC, count=80)
                self.assertEqual(mu.reg_read(UC_ARM64_REG_PC),
                                 BASE + (0xDBB54 if accepted else 0xDBDC0))

    def test_native_line_and_box_draw_only_when_enabled_with_fixed_style(self):
        for enabled in [0, 1]:
            with self.subTest(enabled=enabled):
                mu = self.emulator()
                mu.mem_write(CONFIG, b"\xff" * 0x500)  # old widths/colors ignored
                context = STACK + 0x1000
                mu.mem_write(context, struct.pack("<3f", 1, 800, 600))
                mu.reg_write(registers.UC_ARM64_REG_X19, context)
                mu.reg_write(registers.UC_ARM64_REG_X22, 0x1234)
                mu.reg_write(UC_ARM64_REG_X27, CONFIG)
                for reg, value in [(registers.UC_ARM64_REG_S14, 100),
                                   (registers.UC_ARM64_REG_S10, 100),
                                   (registers.UC_ARM64_REG_S11, 100),
                                   (registers.UC_ARM64_REG_S9, 300)]:
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
                    self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_W3), 0xFFFFFFFF)
                    self.assertEqual(uc.reg_read(registers.UC_ARM64_REG_S0), 0x3F800000)
                    uc.reg_write(UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, mock_draw)
                mu.emu_start(BASE + 0xDC754, BASE + 0xDC804, count=100)
                self.assertEqual([c[0] for c in calls], [0x13AD48, 0x13B1C0] if enabled else [])
                if enabled:
                    self.assertEqual(calls[0][1], [(400.0, 10.0), (100.0, 70.0)])
                    self.assertEqual(calls[1][1], [(150.0, 100.0), (50.0, 300.0)])

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
        for enabled, distance, scenario in [(1, 200, "clear"), (1, 100, "blocked"),
                                            (1, 80, "mixed"), (0, 80, "clear"),
                                            (1, 200.01, "clear")]:
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
                if not enabled or distance > 200:
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
