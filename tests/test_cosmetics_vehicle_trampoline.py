"""Assembler verification for the remote GameThread callable; Actions only."""
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build import commands


class VehicleTrampolineTests(unittest.TestCase):
    def test_callable_machine_code_matches_apple_assembler(self):
        header = (ROOT / "patch/CoreCosmeticsVehicleEnqueue.h").read_text()
        body = re.search(r"const uint32_t putOnCode\[\] = \{(.*?)\};", header, re.S).group(1)
        words = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", body)]
        encoded = b"".join(struct.pack("<I", word) for word in words)
        source = """
.text
.globl _vehicle_callable
_vehicle_callable:
 stp x29, x30, [sp, #-16]!
 mov x29, sp
 ldr x0, avatar
 ldr x2, vehicle
 ldr x4, vehicle_user
 ldr x5, controller
 ldr x6, component_offset
 ldr x7, component_heap
 ldr x8, [x5, #0x460]
 cmp x8, x7
 b.ne stale
 ldr x7, component_bitmap_word
 ldr w8, [x7]
 ldr x7, component_bit
 tst w8, w7
 b.eq stale
 ldr x5, [x5, #0x440]
 add x5, x5, x6
 ldr x5, [x5]
 cmp x5, x4
 b.ne stale
 ldr x3, [x4, #0x158]
 cmp x3, x2
 b.ne stale
 ldr x3, [x0, #0x20]
 cmp x3, x2
 b.ne stale
 ldr x3, [x0, #0x10b8]
 cmp x3, x2
 b.ne stale
 ldr x3, [x2, #0x1ed8]
 cmp x3, x0
 b.ne stale
 ldr w3, [x2, #0x20b0]
 ldr x7, expected_skin
 cmp w3, w7
 b.eq apply
 ldr x7, alternate_skin
 cbz w7, stale
 cmp w3, w7
 b.ne stale
apply:
 ldr x1, resource
 ldr x16, puton
 blr x16
 mov w1, #1
 b done
stale:
 mov w1, #2
done:
 ldr x0, completion
 ldr x16, completion_gadget
 ldp x29, x30, [sp], #16
 br x16
 .space 116
avatar: .quad 0
vehicle: .quad 0
vehicle_user: .quad 0
controller: .quad 0
component_offset: .quad 0
component_heap: .quad 0
component_bitmap_word: .quad 0
component_bit: .quad 0
expected_skin: .quad 0
alternate_skin: .quad 0
resource: .quad 0
puton: .quad 0
completion: .quad 0
completion_gadget: .quad 0
"""
        with tempfile.TemporaryDirectory() as directory:
            asm, obj = Path(directory) / "call.s", Path(directory) / "call.o"
            asm.write_text(source)
            subprocess.run(
                ["xcrun", "clang", "-arch", "arm64", "-c", str(asm), "-o", str(obj)],
                check=True,
            )
            data = obj.read_bytes()
            for position, kind, _ in commands(data):
                if kind != 0x19:
                    continue
                for index in range(struct.unpack_from("<I", data, position + 64)[0]):
                    section = position + 72 + index * 80
                    if data[section:section + 16].rstrip(b"\0") == b"__text":
                        offset = struct.unpack_from("<I", data, section + 48)[0]
                        self.assertEqual(encoded, data[offset:offset + len(encoded)])
                        return
            self.fail("Assembler output missing __text")

    def test_task_storage_regions_do_not_overlap(self):
        header = (ROOT / "patch/CoreCosmeticsVehicleEnqueue.h").read_text()
        values = {
            name: int(value, 16)
            for name, value in re.findall(
                r"#define CCP_ENQUEUE_(TFUNCTION|EVENT|COMPLETION)_OFFSET 0x([0-9A-Fa-f]+)u",
                header,
            )
        }
        self.assertGreaterEqual(values["EVENT"] - values["TFUNCTION"], 0x80)
        self.assertGreaterEqual(values["COMPLETION"] - values["EVENT"], 0x80)


if __name__ == "__main__":
    unittest.main()
