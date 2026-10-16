"""Pin the adapter's original native entries; executed only in Actions."""
from pathlib import Path
import re
import struct
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class CosmeticsProfileTests(unittest.TestCase):
    def test_original_read_entries_match_reviewed_device_guards(self):
        source = (ROOT / 'patch/CoreCosmeticsProbe.m').read_text()
        with zipfile.ZipFile(ROOT / 'input/Core.ipa') as archive:
            original = archive.read('Payload/Core.app/Core')
        for name, rva in [('SnapshotPrefix', 0x64C7C), ('ReadPrefix', 0x64D04)]:
            body = re.search(rf'{name}\[32\] = \{{(.*?)\}};', source, re.S).group(1)
            expected = bytes(int(value, 16) for value in re.findall(r'0x([a-f0-9]{2})', body))
            self.assertEqual(expected, original[rva:rva + 32])
        word = struct.unpack_from('<I', original, 0x64D04)[0]
        immediate = word & 0x3FFFFFF
        if immediate & (1 << 25):
            immediate -= 1 << 26
        self.assertEqual(word >> 26, 0b000101)
        self.assertEqual(0x64D04 + immediate * 4, 0x60818)


if __name__ == '__main__':
    unittest.main()
