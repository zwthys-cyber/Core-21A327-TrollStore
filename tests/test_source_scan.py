"""Execute the actual native actor snapshot with original versus bridged reads."""
import struct,unittest
import test_ground_item_controls as controls
from source_scan_trace import source_scan_trace,changes
from model_render_patch import DRAW_IMPORT_STUB
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn import arm64_const as r

BASE=0x100000000
STACK=0x300000000
RETURN=BASE+0x1000


class SourceScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        controls.GroundItemControlsTests.setUpClass();cls.before=controls.GroundItemControlsTests.binary;cls.binary=source_scan_trace(cls.before)

    def test_only_three_guarded_call_destinations_change(self):
        allowed={p for site,old,new,reason in changes() for p in range(site,site+4)}
        self.assertEqual(len(self.binary),len(self.before))
        self.assertTrue({i for i,(a,b) in enumerate(zip(self.before,self.binary)) if a!=b}<=allowed)
        for data in [self.binary,bytes(len(self.binary))]:
            with self.assertRaisesRegex(ValueError,'Unexpected source scan call'):source_scan_trace(data)
        self.assertEqual(self.binary[0xD4A08:0xD53E0],self.before[0xD4A08:0xD53E0])
        self.assertEqual(self.binary[0xD5408:0xD5AD4],self.before[0xD5408:0xD5AD4])

    def snapshot(self,binary,count,chunk_failure=False,pointer_failure=-1):
        mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM);mu.ctl_set_cpu_model(r.UC_CPU_ARM64_MAX)
        mu.mem_map(BASE,(len(binary)+4095)&~4095);mu.mem_write(BASE,binary);mu.mem_map(STACK,0x20000)
        header=STACK+0x1000;destination=STACK+0x3000;remote=STACK+0x8000
        actors=[0x700000000+i*0x1000 for i in range(count)]
        raw=struct.pack('<'+'Q'*count,*actors);calls=[]
        def hook(uc,address,size,_):
            word=struct.unpack('<I',uc.mem_read(address,4))[0];site=address-BASE
            if word==0xD503237F:uc.reg_write(r.UC_ARM64_REG_PC,address+4);return
            if word==0xD65F0FFF:uc.reg_write(r.UC_ARM64_REG_PC,uc.reg_read(r.UC_ARM64_REG_X30));return
            if word&0xFC000000!=0x94000000:return
            delta=word&0x3ffffff
            if delta&0x2000000:delta-=0x4000000
            target=site+4*delta
            if target==0xF30CC:
                self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0),header)
                uc.mem_write(header,struct.pack('<QQQ',destination,destination+count*8,destination+count*8));uc.mem_write(destination,bytes(count*8))
            elif target==0x725660:
                uc.mem_write(uc.reg_read(r.UC_ARM64_REG_X0),bytes(uc.reg_read(r.UC_ARM64_REG_X1)))
            else:
                self.assertEqual(target,DRAW_IMPORT_STUB if binary is self.binary else 0xE1908)
                self.assertIn(site,(0xD63E0,0xD6410))
                source=uc.reg_read(r.UC_ARM64_REG_X0);out=uc.reg_read(r.UC_ARM64_REG_X1);length=uc.reg_read(r.UC_ARM64_REG_X2)
                calls.append((site,source-remote,length))
                ok=not (site==0xD63E0 and chunk_failure) and not (site==0xD6410 and source==remote+pointer_failure*8)
                if ok:uc.mem_write(out,raw[source-remote:source-remote+length])
                uc.reg_write(r.UC_ARM64_REG_W0,int(ok))
            uc.reg_write(r.UC_ARM64_REG_PC,address+4)
        mu.hook_add(UC_HOOK_CODE,hook)
        mu.reg_write(r.UC_ARM64_REG_X0,remote);mu.reg_write(r.UC_ARM64_REG_X1,count);mu.reg_write(r.UC_ARM64_REG_X8,header)
        mu.reg_write(r.UC_ARM64_REG_X30,RETURN);mu.reg_write(r.UC_ARM64_REG_SP,STACK+0x1F000)
        saved={getattr(r,f'UC_ARM64_REG_X{i}'):0xAB0000+i for i in range(19,30)}
        for reg,value in saved.items():mu.reg_write(reg,value)
        mu.emu_start(BASE+0xD632C,RETURN,count=80000)
        self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),RETURN);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP),STACK+0x1F000)
        for reg,value in saved.items():self.assertEqual(mu.reg_read(reg),value)
        self.assertEqual(bytes(mu.mem_read(header,24)),struct.pack('<QQQ',destination,destination+count*8,destination+count*8))
        return bytes(mu.mem_read(destination,count*8)),calls

    def test_actual_native_copy_read_failure_fallback_and_result_are_unchanged(self):
        for count,fail,pointer in [(1,False,-1),(1,True,0),(513,False,-1),(513,True,-1),(513,True,2),(1024,True,600)]:
            with self.subTest(count=count,chunk_failure=fail,pointer_failure=pointer):
                before=self.snapshot(self.before,count,fail,pointer);after=self.snapshot(self.binary,count,fail,pointer)
                self.assertEqual(after,before)
                values=struct.unpack('<'+'Q'*count,after[0])
                self.assertEqual(values,tuple(0 if i==pointer and fail else 0x700000000+i*0x1000 for i in range(count)))
