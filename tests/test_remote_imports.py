import struct
import unittest
import zipfile
from test_patch import ROOT
from build import commands, inject
from remote_imports import ALIASES, records, redirect_remote_imports
from verify import sections


class RemoteImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with zipfile.ZipFile(ROOT / 'input/Core.ipa') as archive:
            cls.original = archive.read('Payload/Core.app/Core')

    def test_trace_preserves_all_original_program_sections(self):
        before = inject(self.original)
        after = redirect_remote_imports(before)
        self.assertEqual(sections(before), sections(after))
        self.assertEqual(list(commands(before)), list(commands(after)))
        self.assertEqual(len(before), len(after))
        # Independent table locations recorded from the original binary.
        imports_offset = 0xC18000 + 176
        for index in [520, 662, 663]:
            value, addend = struct.unpack_from('<Ii', after, imports_offset + index * 8)
            original_value, original_addend = struct.unpack_from('<Ii', before, imports_offset + index * 8)
            self.assertEqual(value & 255, 52)
            self.assertEqual(value >> 8, original_value >> 8)
            self.assertEqual(addend, original_addend)

    def test_only_reviewed_import_bytes_change(self):
        before = inject(self.original)
        libraries, chains, symbols = records(before)
        after = redirect_remote_imports(before)
        permitted = set()
        for table, chain in [(chains, True), (symbols, False)]:
            for r in table:
                if r['name'] not in ALIASES:
                    continue
                permitted.update(range(r['start'], r['start'] + len(r['name'])))
                permitted.add(r['pos'] if chain else r['pos'] + 7)
        changed = {i for i, (a, b) in enumerate(zip(before, after)) if a != b}
        self.assertTrue(changed <= permitted)
        new_libraries, new_chains, new_symbols = records(after)
        self.assertEqual(libraries, new_libraries)
        for old, new in [(chains, new_chains), (symbols, new_symbols)]:
            self.assertEqual(len(old), len(new))
            for a, b in zip(old, new):
                expected = a.copy()
                if a['name'] in ALIASES:
                    expected.update(name=ALIASES[a['name']], ordinal=52)
                self.assertEqual(expected, b)

    def test_refuses_unknown_binding_layout(self):
        original = inject(self.original)
        for index, value, message in [(0xC18000 + 20, 1, 'encoding'),
                                      (0xC18000 + 176 + 520 * 8, 29, 'libSystem')]:
            changed = bytearray(original)
            if index == 0xC18000 + 20:
                struct.pack_into('<I', changed, index, value)
            else:
                changed[index] = value
            with self.assertRaisesRegex(ValueError, message):
                redirect_remote_imports(changed)
        with self.assertRaisesRegex(ValueError, 'ordinal'):
            redirect_remote_imports(self.original)
        with self.assertRaises(ValueError):
            redirect_remote_imports(redirect_remote_imports(original))


if __name__ == '__main__':
    unittest.main()
