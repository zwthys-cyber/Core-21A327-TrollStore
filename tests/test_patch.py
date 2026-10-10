import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build import CORE_UUID, INPUT_SHA256, LOAD_PATH, commands, inject


class PackagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with zipfile.ZipFile(ROOT / "input/Core.ipa") as archive:
            cls.original = archive.read("Payload/Core.app/Core")

    def test_analyzed_input(self):
        self.assertEqual(hashlib.sha256((ROOT / "input/Core.ipa").read_bytes()).hexdigest(), INPUT_SHA256)

    def test_injection_preserves_existing_dylib_ordinals_and_program_bytes(self):
        result = inject(self.original)
        original_commands = list(commands(self.original))
        patched_commands = list(commands(result))
        self.assertEqual(patched_commands[:-1], original_commands)
        # All existing commands remain byte-for-byte unchanged, including chained fixups.
        self.assertEqual(self.original[32:32 + struct.unpack_from("<I", self.original, 20)[0]],
                         result[32:32 + struct.unpack_from("<I", self.original, 20)[0]])
        self.assertEqual(self.original[0x4000:], result[0x4000:])
        pos, cmd, size = patched_commands[-1]
        self.assertEqual(cmd, 0xC)
        self.assertIn(LOAD_PATH, result[pos:pos + size])
        self.assertEqual(len(result), len(self.original))

    def test_rejects_double_patch(self):
        with self.assertRaisesRegex(ValueError, "already injected"):
            inject(inject(self.original))

    def test_rejects_unknown_app_build(self):
        modified = self.original.replace(CORE_UUID, bytes(16), 1)
        with self.assertRaisesRegex(ValueError, "UUID"):
            inject(modified)

    def test_rejects_nonempty_header_padding(self):
        modified = bytearray(self.original)
        modified[32 + struct.unpack_from("<I", modified, 20)[0]] = 1
        with self.assertRaisesRegex(ValueError, "padding"):
            inject(modified)


class DeviceIdentityPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        source = Path(cls.temp.name) / "policy.c"
        source.write_text('#include "CoreBootPolicy.h"\n'
            'bool identity(const char*a,const char*b,const char*c,const char*d,const char*e,const char*f)'
            '{return CBCIdentityMatches(a,b,c,d,e,f);}\n'
            'bool path(const char*a,const char*b){return CBCPathMatchesBoot(a,b);}\n')
        output = Path(cls.temp.name) / "policy.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-I", str(ROOT / "patch"), str(source), "-o", str(output)], check=True)
        cls.lib = ctypes.CDLL(str(output))
        cls.lib.identity.argtypes = [ctypes.c_char_p] * 6
        cls.lib.identity.restype = ctypes.c_bool
        cls.lib.path.argtypes = [ctypes.c_char_p] * 2
        cls.lib.path.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_current_system_identity_required(self):
        args = [b"iPhone16,2", b"21A327", b"D84AP", b"iPhone16,2", b"21A327", b"d84ap"]
        self.assertTrue(self.lib.identity(*args))
        for index, value in [(1, b"21A329"), (4, b"21A329"), (3, b"iPhone16,1"),
                             (5, b"D83AP"), (0, b""), (2, None)]:
            wrong = args.copy()
            wrong[index] = value
            self.assertFalse(self.lib.identity(*wrong))

    def test_rejects_other_boots_and_path_escapes(self):
        boot = b"A" * 96
        current = b"/private/preboot/" + boot + b"/System/Library/Caches/com.apple.kernelcaches/kernelcache"
        self.assertTrue(self.lib.path(current, boot))
        for wrong in [current.replace(b"A", b"B"), current.replace(boot + b"/", boot + b"B/"),
                      current.replace(b"/System/", b"/../System/"),
                      b"/System/Library/Caches/com.apple.kernelcaches/kernelcache"]:
            self.assertFalse(self.lib.path(wrong, boot))
        self.assertFalse(self.lib.path(current, b"../"))


if __name__ == "__main__":
    unittest.main()
