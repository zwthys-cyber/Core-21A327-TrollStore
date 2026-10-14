import struct
import unittest
import zipfile
from test_patch import ROOT
from build import inject, commands
from remote_imports import records, redirect_remote_imports, redirect_model_capture_import
from verify import sections


class ModelCaptureImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with zipfile.ZipFile(ROOT / 'input/Core.ipa') as archive:
            cls.before = redirect_remote_imports(inject(archive.read('Payload/Core.app/Core')))

    def test_capture_redirect_preserves_code_stubs_and_other_imports(self):
        after = redirect_model_capture_import(self.before)
        self.assertEqual(sections(after), sections(self.before))
        self.assertEqual(list(commands(after)), list(commands(self.before)))
        self.assertEqual(len(after), len(self.before))
        old_libraries, old_chains, old_symbols = records(self.before)
        libraries, chains, symbols = records(after)
        self.assertEqual(libraries, old_libraries)
        permitted = set()
        for old_table, new_table, chain in [(old_chains, chains, True), (old_symbols, symbols, False)]:
            for old, new in zip(old_table, new_table):
                if old['name'] == b'_bzero':
                    expected = {**old, 'name': b'_cmbzr', 'ordinal': 52}
                    permitted.update(range(old['start'], old['start'] + 6))
                    permitted.add(old['pos'] if chain else old['pos'] + 7)
                else:
                    expected = old
                self.assertEqual(new, expected)
        changed = {i for i, (a, b) in enumerate(zip(self.before, after)) if a != b}
        self.assertTrue(changed <= permitted)

    def test_rejects_double_redirect_and_wrong_original_library(self):
        with self.assertRaisesRegex(ValueError, 'collision'):
            redirect_model_capture_import(redirect_model_capture_import(self.before))
        _, chains, _ = records(self.before)
        entry = next(x for x in chains if x['name'] == b'_bzero')
        broken = bytearray(self.before)
        value = struct.unpack_from('<I', broken, entry['pos'])[0]
        struct.pack_into('<I', broken, entry['pos'], (value & ~255) | 29)
        with self.assertRaisesRegex(ValueError, 'libSystem'):
            redirect_model_capture_import(broken)
