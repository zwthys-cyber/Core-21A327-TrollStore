"""Execute the actual vehicle-patched ARM64 paths with synthetic native data.

Run in GitHub Actions only; these checks do not establish device visibility.
"""
import math
import struct
import unittest
from itertools import product
import test_minimal_features as baseline
from minimal_features import minimal_core
from nearby_vehicles import nearby_vehicles
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE, STACK, CONFIG, RETURN = baseline.BASE, baseline.STACK, baseline.CONFIG, baseline.RETURN


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class NearbyVehicleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        baseline.MinimalFeatureTests.setUpClass()
        cls.original = baseline.MinimalFeatureTests.original
        cls.before = minimal_core(cls.original)
        cls.binary = nearby_vehicles(cls.before)

    emulator = baseline.MinimalFeatureTests.emulator

    def test_exact_reviewed_changes_and_unknown_source_rejected(self):
        locations = [(0xCD7F4, 4), (0xCD804, 4), (0xCD818, 44), (0x743F71, 12),
                     (0xD959C, 36), (0xD97AC, 4), (0xD9954, 4), (0xD9E3C, 116),
                     (0xDDEBC, 4), (0xDDF5C, 32), (0xDDF7C, 52),
                     (0xDE3C4, 4), (0xDE540, 8), (0xDE558, 4)]
        allowed = {i for start, size in locations for i in range(start, start + size)}
        changed = {i for i, (a, b) in enumerate(zip(self.before, self.binary)) if a != b}
        self.assertTrue(changed and changed <= allowed)
        self.assertEqual(len(self.before), len(self.binary))
        self.assertEqual(self.binary[0xC1A04:0xC1A08], bytes.fromhex('c0035fd6'))
        for damaged in [self.original, self.binary]:
            with self.assertRaisesRegex(ValueError, 'Unexpected vehicle'):
                nearby_vehicles(damaged)

    def test_two_independent_checkboxes_save_either_change_and_balance_child(self):
        for enemy, vehicle, enemy_changed, vehicle_changed in product([0, 1], repeat=4):
            with self.subTest(enemy=enemy, vehicle=vehicle, enemy_changed=enemy_changed, vehicle_changed=vehicle_changed):
                mu = self.emulator()
                mu.mem_write(CONFIG + 0x22, bytes([vehicle, enemy]))
                calls, boxes = [], []

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
                            pointer = uc.reg_read(r.UC_ARM64_REG_X1)
                            label = uc.reg_read(r.UC_ARM64_REG_X0)
                            boxes.append(pointer)
                            expected_label = 0x743F64 if pointer == CONFIG + 0x23 else 0x743F71
                            self.assertEqual(label, BASE + expected_label)
                            changed = enemy_changed if pointer == CONFIG + 0x23 else vehicle_changed
                            if changed:
                                uc.mem_write(pointer, bytes([1 - uc.mem_read(pointer, 1)[0]]))
                            uc.reg_write(r.UC_ARM64_REG_W0, changed)
                        uc.reg_write(r.UC_ARM64_REG_PC, address + 4)

                mu.hook_add(UC_HOOK_CODE, ui)
                mu.emu_start(BASE + 0xCD6A4, RETURN, count=500)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), RETURN)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP), STACK + 0x8000)
                self.assertEqual(boxes, [CONFIG + 0x23, CONFIG + 0x22])
                self.assertEqual(calls.count(0xC88B0), 1)
                self.assertEqual(calls.count(0x12E620), 1)
                self.assertFalse(set(calls) & {0xCE0EC, 0xCE210, 0xCE344})
                self.assertEqual(mu.mem_read(BASE + 0xC4DF86, 1), bytes([enemy_changed | vehicle_changed]))
                self.assertEqual(mu.mem_read(CONFIG + 0x22, 2), bytes([vehicle ^ vehicle_changed, enemy ^ enemy_changed]))

    def test_vehicle_filter_precedes_position_reads_and_ignores_old_loot_preferences(self):
        mu = self.emulator()
        source, metadata = STACK + 0x2000, STACK + 0x3000
        stops = {BASE + 0xD95C0, BASE + 0xD9EF4}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop() if address in stops else None)
        for enabled, enemy, projectile, vehicle, has_metadata, old in product([0, 1], repeat=6):
            mu.mem_write(CONFIG, bytes([old * 255]) * 0x500)
            mu.mem_write(CONFIG + 0x22, bytes([enabled, enemy]))
            mu.mem_write(source + 8, struct.pack('<Q', metadata if has_metadata else 0))
            mu.mem_write(source + 0x10, bytes([projectile]))
            mu.mem_write(metadata, b'\0' * 0x40)  # old per-item enabled is off
            mu.mem_write(metadata + 0x15, bytes([vehicle]))
            mu.reg_write(r.UC_ARM64_REG_X19, CONFIG)
            mu.reg_write(r.UC_ARM64_REG_X24, source)
            mu.emu_start(BASE + 0xD959C, BASE + 0xD9EF8, count=30)
            accepted = enabled and not projectile and vehicle and has_metadata
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE + (0xD95C0 if accepted else 0xD9EF4))
        # The follow-up native gate uses vehicle classification, even when the
        # old general item-enable checkbox for this vehicle was saved as off.
        mu.reg_write(r.UC_ARM64_REG_X26, metadata)
        mu.mem_write(metadata + 0x15, b'\x01')
        mu.emu_start(BASE + 0xD97AC, BASE + 0xD97B8, count=5)
        self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE + 0xD97B8)

    def test_gather_range_is_inclusive_finite_and_independent_of_old_category_distance(self):
        mu = self.emulator()
        destinations = {BASE + 0xD9EC8, BASE + 0xD9EB0}
        mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop() if address in destinations else None)
        for distance, on_screen in product([0, 1, 299.99, 300, 300.01, 450, 599.99, 600, 600.01, -1, math.inf, -math.inf, math.nan], [0, 1]):
            mu.reg_write(r.UC_ARM64_REG_S11, fbits(distance))
            mu.reg_write(r.UC_ARM64_REG_W20, on_screen)
            mu.reg_write(r.UC_ARM64_REG_X26, 0)  # no category range or oil pointers may be read
            mu.emu_start(BASE + 0xD9E3C, BASE + 0xD9ED0, count=40)
            accepted = on_screen and math.isfinite(distance) and 0 <= distance <= 600
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE + (0xD9EB0 if accepted else 0xD9EC8))
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_S11), fbits(distance))

    def test_native_record_loop_draws_vehicle_names_and_distance_only(self):
        # Execute the real shared-list traversal and native label formatting
        # branches. Only libc formatting/text submission are synthetic calls.
        rows = [dict(name='吉普车', distance=600, vehicle=1, projected=1, projectile=0),
                dict(name='蹦蹦车', distance=450, vehicle=1, projected=1, projectile=0),
                dict(name='M416', distance=30, vehicle=0, projected=1, projectile=0),
                dict(name='Grenade', distance=10, vehicle=1, projected=1, projectile=1),
                dict(name='轿车', distance=600.01, vehicle=1, projected=1, projectile=0),
                dict(name='快艇', distance=100, vehicle=1, projected=0, projectile=0),
                dict(name='摩托车', distance=math.nan, vehicle=1, projected=1, projectile=0)]
        for enabled, enemy in product([0, 1], repeat=2):
            mu = self.emulator()
            context, records, metadata = STACK + 0x1000, STACK + 0x2000, STACK + 0x4000
            mu.mem_write(context, struct.pack('<3f', 1, 800, 600))
            mu.mem_write(context + 0x408, struct.pack('<2Q', records, records + len(rows) * 0x58))
            mu.mem_write(CONFIG, b'\xff' * 0x500)
            mu.mem_write(CONFIG + 0x22, bytes([enabled, enemy]))
            labels, formats = [], []
            for i, row in enumerate(rows):
                record, meta = records + i * 0x58, metadata + i * 0x80
                raw = row['name'].encode()
                mu.mem_write(meta + 0x18, raw.ljust(23, b'\0') + bytes([len(raw)]))
                mu.mem_write(record + 8, bytes([row['projectile']]))
                mu.mem_write(record + 0x10, struct.pack('<Q', meta))
                mu.mem_write(record + 0x18, struct.pack('<2f', 200 + i * 20, 250))
                mu.mem_write(record + 0x38, struct.pack('<f', row['distance']))
                mu.mem_write(record + 0x4D, bytes([row['vehicle']]))
                mu.mem_write(record + 0x50, bytes([row['projected']]))

            def cstring(uc, pointer):
                raw = bytearray()
                while uc.mem_read(pointer + len(raw), 1) != b'\0':
                    raw.extend(uc.mem_read(pointer + len(raw), 1))
                    self.assertLess(len(raw), 256)
                return raw.decode()

            def native_call(uc, address, size, _):
                if address == BASE + 0xDE5A0:
                    uc.emu_stop()
                    return
                ins = struct.unpack('<I', uc.mem_read(address, 4))[0]
                if ins & 0xFC000000 != 0x94000000:
                    return
                delta = ins & 0x3FFFFFF
                if delta & 0x2000000:
                    delta -= 0x4000000
                target = address + delta * 4 - BASE
                if target == 0x726A30:
                    dest, cap = uc.reg_read(r.UC_ARM64_REG_X0), uc.reg_read(r.UC_ARM64_REG_X1)
                    formats.append(cstring(uc, uc.reg_read(r.UC_ARM64_REG_X2)))
                    sp = uc.reg_read(r.UC_ARM64_REG_SP)
                    text, distance = struct.unpack('<2Q', uc.mem_read(sp, 16))
                    value = f'{cstring(uc, text)} {distance}m'.encode()
                    self.assertLess(len(value), cap)
                    uc.mem_write(dest, value + b'\0')
                elif target == 0x726B20:
                    uc.reg_write(r.UC_ARM64_REG_X0, len(cstring(uc, uc.reg_read(r.UC_ARM64_REG_X0)).encode()))
                elif target == 0xDF47C:
                    ptr, length = uc.reg_read(r.UC_ARM64_REG_X0), uc.reg_read(r.UC_ARM64_REG_X1)
                    labels.append(bytes(uc.mem_read(ptr, length)).decode())
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_W3), 0xFF00FFFF)
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_S2), fbits(14))
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_W2), 1)
                else:
                    self.fail(f'Unexpected vehicle label call {target:#x}')
                uc.reg_write(r.UC_ARM64_REG_PC, address + 4)

            mu.hook_add(UC_HOOK_CODE, native_call)
            mu.reg_write(r.UC_ARM64_REG_X19, context)
            mu.reg_write(r.UC_ARM64_REG_X27, CONFIG)
            mu.emu_start(BASE + 0xDDEBC, BASE + 0xDE5A4, count=2000)
            self.assertEqual(labels, ['吉普车 600m', '蹦蹦车 450m'] if enabled else [])
            self.assertEqual(len(formats), len(labels))
            self.assertTrue(all('%s' in fmt and '%d' in fmt for fmt in formats))
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE + 0xDE5A0)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP), STACK + 0x8000)

    def test_vehicle_source_flag_is_original_metadata_classification(self):
        # Pin the original copying path independently of the new filter.
        for offset, expected in {0xD6F30: '29544039', 0xD6F34: 'e9470039',
                                 0xD9948: '48574039', 0xD994C: '08010012',
                                 0xD9950: 'e8b71939', 0xD96A0: 'e8b71939'}.items():
            self.assertEqual(self.original[offset:offset + 4], bytes.fromhex(expected))
