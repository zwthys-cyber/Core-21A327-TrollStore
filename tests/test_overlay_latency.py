"""Actual three-checkbox ARM64 UI after vehicle and fill control patches."""
import struct
import unittest
from itertools import product
import test_nearby_vehicles as vehicles
import test_minimal_features as baseline
from overlay_latency import overlay_latency
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE, STACK, CONFIG, RETURN = baseline.BASE, baseline.STACK, baseline.CONFIG, baseline.RETURN


class OverlayLatencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        vehicles.NearbyVehicleTests.setUpClass()
        cls.original = vehicles.NearbyVehicleTests.original
        cls.before = vehicles.NearbyVehicleTests.binary
        cls.binary = overlay_latency(cls.before)

    emulator = baseline.MinimalFeatureTests.emulator

    def test_only_independent_fill_ui_changes_and_reapplication_rejected(self):
        allowed = {i for offset, size in [(0xCD82C, 4), (0xCD83C, 4), (0xCD844, 44), (0x743F4A, 12)]
                   for i in range(offset, offset + size)}
        changed = {i for i, (a, b) in enumerate(zip(self.before, self.binary)) if a != b}
        self.assertTrue(changed and changed <= allowed)
        self.assertEqual(len(self.before), len(self.binary))
        for unsupported in [self.original, self.binary]:
            with self.assertRaisesRegex(ValueError, 'Unexpected independent-fill source'):
                overlay_latency(unsupported)

    def test_enemy_vehicle_fill_controls_are_independent_and_save_every_changed_return(self):
        for enemy, vehicle, fill, enemy_change, vehicle_change, fill_change in product([0, 1], repeat=6):
            with self.subTest(enemy=enemy, vehicle=vehicle, fill=fill, changes=(enemy_change, vehicle_change, fill_change)):
                mu = self.emulator()
                mu.mem_write(CONFIG + 0x20, bytes([fill, 0, vehicle, enemy]))
                controls = [(CONFIG + 0x23, BASE + 0x743F64, enemy, enemy_change),
                            (CONFIG + 0x22, BASE + 0x743F71, vehicle, vehicle_change),
                            (CONFIG + 0x20, BASE + 0x743F4A, fill, fill_change)]
                calls, seen = [], []

                def ui(uc, address, size, _):
                    ins = struct.unpack('<I', uc.mem_read(address, 4))[0]
                    if ins == 0xD503237F:
                        uc.reg_write(r.UC_ARM64_REG_PC, address + 4)
                    elif ins == 0xD65F0FFF:
                        uc.reg_write(r.UC_ARM64_REG_PC, uc.reg_read(r.UC_ARM64_REG_X30))
                    elif ins & 0xFC000000 == 0x94000000:
                        delta = ins & 0x3FFFFFF
                        if delta & 0x2000000:
                            delta -= 0x4000000
                        target = address + delta * 4 - BASE
                        calls.append(target)
                        if target == 0xC8928:
                            pointer, label, value, changed = controls[len(seen)]
                            self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X1), pointer)
                            self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0), label)
                            if changed:
                                uc.mem_write(pointer, bytes([1 - value]))
                            seen.append(pointer)
                            uc.reg_write(r.UC_ARM64_REG_W0, changed)
                        uc.reg_write(r.UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, ui)
                mu.emu_start(BASE + 0xCD6A4, RETURN, count=600)
                self.assertEqual(seen, [c[0] for c in controls])
                self.assertEqual(mu.mem_read(CONFIG + 0x20, 4), bytes([fill ^ fill_change, 0, vehicle ^ vehicle_change, enemy ^ enemy_change]))
                self.assertEqual(mu.mem_read(BASE + 0xC4DF86, 1), bytes([enemy_change | vehicle_change | fill_change]))
                self.assertEqual(calls.count(0x12E620), 1)
                self.assertEqual(calls.count(0xC88B0), 1)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), RETURN)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP), STACK + 0x8000)
