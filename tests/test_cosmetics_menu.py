"""Cloud-only ARM64 execution checks for the optional native cosmetics menu."""
import re
import struct
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from test_patch import ROOT
import test_minimal_features as minimal_tests
from build import commands
from minimal_features import minimal_core
from cosmetics_menu import cosmetics_core, MENU_SYMBOL, MENU_SYMBOL_RVA, MENU_BRIDGE, MENU_RETURN, DLSYM_STUB
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r
BASE = minimal_tests.BASE


class CosmeticsMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with zipfile.ZipFile(ROOT / 'input/Core.ipa') as archive:
            cls.original = archive.read('Payload/Core.app/Core')
        cls.binary = cosmetics_core(cls.original)

    def emulator(self):
        helper = minimal_tests.MinimalFeatureTests()
        helper.binary = self.binary
        return helper.emulator()

    def test_only_native_row_page_dispatch_and_callback_region_change(self):
        baseline = minimal_core(self.original)
        allowed = {i for start, size in [(0xD07E0, 4), (0xD0814, 28),
                    (MENU_SYMBOL_RVA, len(MENU_SYMBOL)), (0xD098C, 12), (0x744529, 6)]
                   for i in range(start, start + size)}
        changed = {i for i, (a, b) in enumerate(zip(baseline, self.binary)) if a != b}
        self.assertTrue(changed)
        self.assertTrue(changed <= allowed)
        self.assertEqual(len(baseline), len(self.binary))
        # Original selection/hit testing of the one restored native row.
        self.assertEqual(self.binary[0xD07E0:0xD0814], self.original[0xD07E0:0xD0814])
        self.assertEqual(self.binary[0x744529:0x744530], '美化'.encode() + b'\0')
        for offset, size in [(0x620B8, 0x2000), (0xC54270, 0x400), (0xD33F4, 0x54CC)]:
            self.assertEqual(self.binary[offset:offset + size], baseline[offset:offset + size])

    def test_page_routing_and_stale_preferences(self):
        destinations = {BASE + 0xD09C8, BASE + 0xD09D0, BASE + MENU_BRIDGE}
        for selected in [0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF, 0x7FFFFFFF]:
            mu = self.emulator()
            mu.reg_write(r.UC_ARM64_REG_W8, selected)
            mu.hook_add(UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop() if address in destinations else None)
            mu.emu_start(BASE + 0xD0984, BASE + 0xD09E4, count=20)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE +
                (MENU_BRIDGE if selected == 1 else 0xD09D0 if selected == 2 else 0xD09C8))

    def test_restored_sidebar_row_uses_native_label_selection_and_click(self):
        for selected in [0, 1, 2, 6]:
            for clicked in [False, True]:
                mu = self.emulator()
                mu.reg_write(r.UC_ARM64_REG_X22, BASE + 0xC4D000)
                mu.mem_write(BASE + 0xC4DF64, struct.pack('<I', selected))
                calls = []

                def hook(uc, address, size, _):
                    if address == BASE + 0xD097C:
                        uc.emu_stop()
                    elif address == BASE + 0x13319C:
                        uc.reg_write(r.UC_ARM64_REG_PC, uc.reg_read(r.UC_ARM64_REG_X30))
                    elif address == BASE + 0xC7A60:
                        label = uc.reg_read(r.UC_ARM64_REG_X1)
                        self.assertEqual(bytes(uc.mem_read(label, 7)), '美化'.encode() + b'\0')
                        self.assertEqual(uc.reg_read(r.UC_ARM64_REG_W2), int(selected == 1))
                        calls.append(address)
                        uc.reg_write(r.UC_ARM64_REG_W0, int(clicked))
                        uc.reg_write(r.UC_ARM64_REG_PC, uc.reg_read(r.UC_ARM64_REG_X30))

                mu.hook_add(UC_HOOK_CODE, hook)
                mu.emu_start(BASE + 0xD07E0, BASE + 0xD09E4, count=50)
                self.assertEqual(len(calls), 1)
                self.assertEqual(struct.unpack('<I', mu.mem_read(BASE + 0xC4DF64, 4))[0], 1 if clicked else selected)

    def test_resolver_null_and_callback_return_preserve_the_native_frame(self):
        callback = BASE + 0x3E00
        for available in [False, True]:
            mu = self.emulator()
            sp = mu.reg_read(r.UC_ARM64_REG_SP)
            saved = {getattr(r, f'UC_ARM64_REG_X{i}'): 0xABC000 + i for i in range(19, 30)}
            for register, value in saved.items():
                mu.reg_write(register, value)
            calls = []

            def hook(uc, address, size, _):
                if address == BASE + MENU_RETURN:
                    uc.emu_stop()
                elif address == BASE + DLSYM_STUB:
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0), (1 << 64) - 2)
                    name = uc.reg_read(r.UC_ARM64_REG_X1)
                    self.assertEqual(bytes(uc.mem_read(name, len(MENU_SYMBOL))), MENU_SYMBOL)
                    uc.reg_write(r.UC_ARM64_REG_X0, callback if available else 0)
                    uc.reg_write(r.UC_ARM64_REG_PC, uc.reg_read(r.UC_ARM64_REG_X30))
                elif address == BASE + MENU_BRIDGE + 16:
                    # Unicorn does not implement arm64e authentication. Only
                    # this indirect call is emulated; its encoding is separately
                    # checked against Apple's assembler below.
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0), callback)
                    uc.reg_write(r.UC_ARM64_REG_X30, address + 4)
                    uc.reg_write(r.UC_ARM64_REG_PC, callback)
                elif address == callback:
                    calls.append(address)
                    uc.reg_write(r.UC_ARM64_REG_PC, uc.reg_read(r.UC_ARM64_REG_X30))

            mu.hook_add(UC_HOOK_CODE, hook)
            mu.emu_start(BASE + MENU_BRIDGE, BASE + MENU_RETURN + 4, count=50)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC), BASE + MENU_RETURN)
            self.assertEqual(len(calls), int(available))
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP), sp)
            for register, value in saved.items():
                self.assertEqual(mu.reg_read(register), value)

    def test_authenticated_callback_instruction_matches_apple_assembler(self):
        with tempfile.TemporaryDirectory() as directory:
            source, obj = Path(directory) / 'call.s', Path(directory) / 'call.o'
            source.write_text('.text\n.globl _test_call\n_test_call:\n blraaz x0\n ret\n')
            subprocess.run(['xcrun', 'clang', '-arch', 'arm64e', '-c', str(source), '-o', str(obj)], check=True)
            data = obj.read_bytes()
            for position, kind, _ in commands(data):
                if kind == 0x19:
                    for i in range(struct.unpack_from('<I', data, position + 64)[0]):
                        section = position + 72 + i * 80
                        if data[section:section + 16].rstrip(b'\0') == b'__text':
                            offset = struct.unpack_from('<I', data, section + 48)[0]
                            self.assertEqual(self.binary[MENU_BRIDGE + 16:MENU_BRIDGE + 20], data[offset:offset + 4])
                            return
            self.fail('Assembler output missing __text')

    def test_reviewed_ui_helpers_match_the_pinned_original(self):
        header = (ROOT / 'patch/CoreCosmeticsUIProfile.h').read_text()
        entries = re.findall(r'\{0x([A-F0-9]+), "([0-9a-f]+)"\}', header)
        self.assertEqual(len(entries), 5)
        for rva, prefix in entries:
            offset = int(rva, 16)
            self.assertEqual(self.original[offset:offset + 32], bytes.fromhex(prefix))
        source = (ROOT / 'patch/CoreCosmeticsProbe.m').read_text()
        self.assertNotIn('UIButton', source)
        self.assertNotIn('[controller attach]', source)


if __name__ == '__main__':
    unittest.main()
