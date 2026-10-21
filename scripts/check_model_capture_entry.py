#!/usr/bin/env python3
"""CI-only checks of the compiled entry using a synthetic ARM64 call frame.

No iOS application, game, remote reader, or kernel backend is executed.
Pointer authentication is skipped in this synthetic prologue check only.
"""
import struct
import sys
import zipfile

from build import commands
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as r


def check(ipa):
    with zipfile.ZipFile(ipa) as archive:
        data = archive.read("Payload/Core.app/CoreBootCache.dylib")
    segments = []
    symbols = {}
    for pos, command, _ in commands(data):
        if command == 0x19:
            segments.append(struct.unpack_from("<4Q", data, pos + 24))
        if command == 2:
            symoff, count, stroff, size = struct.unpack_from("<4I", data, pos + 8)
            for index in range(count):
                string, typ, section, _, address = struct.unpack_from("<IBBHQ", data, symoff + index * 16)
                if (typ & 0xE) == 0xE and section:
                    start = stroff + string
                    name = data[start:data.index(b"\0", start, stroff + size)].decode()
                    symbols[name] = address
    entry = symbols["_cmbzr"]
    forwarding = symbols["_CoreModelBzeroCapture"]
    assert "_CMTryCapture" in symbols, "Large sampling body was inlined into forwarding"
    slide = 0x100000000
    mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    mu.ctl_set_cpu_model(r.UC_CPU_ARM64_MAX)
    extent = max(address + size for address, size, _, _ in segments)
    assert extent < 0x1000000
    mu.mem_map(slide, (extent + 4095) & ~4095)
    # Only code is used. Synthetic zeroed data/guard replace dyld bindings;
    # execution stops before the first system or sampling call.
    for address, _, offset, length in segments:
        if offset == 0 and length:
            mu.mem_write(slide + address, data[offset:offset + length])
    mu.mem_map(0, 4096)
    stack = 0x3000000000
    mu.mem_map(stack, 0x20000)
    sp = stack + 0x10000
    arguments = {r.UC_ARM64_REG_X0: 0x2000000000,
                 r.UC_ARM64_REG_X1: 44,
                 r.UC_ARM64_REG_X23: 0x200001200,
                 r.UC_ARM64_REG_X29: stack + 0x18000,
                 r.UC_ARM64_REG_X30: 0x1000E1960,
                 r.UC_ARM64_REG_X4: 0xBAD}
    for register, value in arguments.items():
        mu.reg_write(register, value)
    mu.reg_write(r.UC_ARM64_REG_SP, sp)
    visited_forwarding = False
    stopped_at_call = False

    def step(uc, address, size, _):
        nonlocal visited_forwarding, stopped_at_call
        if address == slide + forwarding:
            visited_forwarding = True
            assert uc.reg_read(r.UC_ARM64_REG_X2) == arguments[r.UC_ARM64_REG_X29]
            assert uc.reg_read(r.UC_ARM64_REG_X3) == arguments[r.UC_ARM64_REG_X23]
            assert uc.reg_read(r.UC_ARM64_REG_X4) == arguments[r.UC_ARM64_REG_X30], "Entry lost the original caller LR"
            assert uc.reg_read(r.UC_ARM64_REG_X30) == arguments[r.UC_ARM64_REG_X30]
            assert uc.reg_read(r.UC_ARM64_REG_SP) == sp
        word = struct.unpack("<I", uc.mem_read(address, 4))[0]
        if word in (0xD503233F, 0xD503237F):  # PACIASP / PACIBSP
            uc.reg_write(r.UC_ARM64_REG_PC, address + 4)
        elif word & 0xFC000000 == 0x94000000:  # BL to the original bzero stub
            assert visited_forwarding
            assert 0 <= sp - uc.reg_read(r.UC_ARM64_REG_SP) <= 512, "Forwarding still allocates a large sampling stack"
            assert uc.reg_read(r.UC_ARM64_REG_X0) == arguments[r.UC_ARM64_REG_X0]
            assert uc.reg_read(r.UC_ARM64_REG_X1) == arguments[r.UC_ARM64_REG_X1]
            stopped_at_call = True
            uc.emu_stop()
        elif word in (0xD73F0A11, 0xD63F0200):  # prologue BLRAA x16,x17 / BLR x16
            raise AssertionError("Unexpected indirect stack-probing call in forwarding prologue")

    entry_hook=mu.hook_add(UC_HOOK_CODE, step)
    mu.emu_start(slide + entry, slide + extent, count=96)
    assert visited_forwarding and stopped_at_call, "Compiled forwarding did not reach its first call"
    if "_cmdraw" in symbols:
        assert "_CoreModelRenderDispatch" in symbols and "_CMLDraw" in symbols
        mu.hook_del(entry_hook)
        passed=False
        mu.reg_write(r.UC_ARM64_REG_SP,sp)
        mu.reg_write(r.UC_ARM64_REG_X0,0x100C54270)
        mu.reg_write(r.UC_ARM64_REG_X1,513)
        mu.reg_write(r.UC_ARM64_REG_X2,4096)
        mu.reg_write(r.UC_ARM64_REG_X8,stack+0x1800)
        mu.reg_write(r.UC_ARM64_REG_X30,0x100021BC4)
        source_bridge='_CoreSourceNativeDispatch' in symbols
        target=symbols['_CoreSourceNativeDispatch' if source_bridge else '_CoreModelRenderDispatch']
        def render_step(uc,address,size,_):
            nonlocal passed
            if address==slide+target:
                assert uc.reg_read(r.UC_ARM64_REG_X0)==0x100C54270
                if source_bridge:
                    assert uc.reg_read(r.UC_ARM64_REG_X1)==513
                    assert uc.reg_read(r.UC_ARM64_REG_X2)==4096
                    assert uc.reg_read(r.UC_ARM64_REG_X3)==stack+0x1800
                    assert uc.reg_read(r.UC_ARM64_REG_X4)==0x100021BC4
                else:assert uc.reg_read(r.UC_ARM64_REG_X1)==0x100021BC4
                assert uc.reg_read(r.UC_ARM64_REG_X30)==0x100021BC4
                assert uc.reg_read(r.UC_ARM64_REG_SP)==sp
                passed=True;uc.emu_stop()
        mu.hook_add(UC_HOOK_CODE,render_step)
        mu.emu_start(slide+symbols['_cmdraw'],slide+extent,count=24)
        assert passed,'Renderer import lost the native context or LR'
        if source_bridge:print('PASS: compiled source veneer also retains original x1/x2 and indirect result x8 before any C prologue')
        print('PASS: compiled render veneer passes native context and caller LR before any prologue; live draw body remains separate')
    print("PASS: compiled ARM64 entry retains caller LR, FP, remote address, arguments and SP; forwarding stack <=512 bytes; large sampler remains separate")


if __name__ == "__main__":
    check(sys.argv[1])
