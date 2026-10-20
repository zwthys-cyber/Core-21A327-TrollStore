"""Execute the real cached projector and item re-projection on both builds.

Only game position reads, TLS accessors, allocation and external UI/string
calls are stubbed. DA704's projection math, collecting, late-camera cache
updates, distance/screen flags and the native renderer execute in Unicorn.
Run in GitHub Actions only.
"""
import math
import struct
import unittest
import test_ground_essentials as essentials
from ground_late_camera import ground_late_camera,changes
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE,STACK,CONFIG,RETURN=essentials.BASE,essentials.STACK,essentials.CONFIG,essentials.RETURN
bits=essentials.bits


class GroundLateCameraTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        essentials.GroundEssentialsTests.setUpClass()
        cls.before=essentials.GroundEssentialsTests.binary
        cls.binary=ground_late_camera(cls.before)

    emulator=essentials.weapons.baseline.MinimalFeatureTests.emulator

    def test_exact_two_instruction_changes_and_guarded_input(self):
        changed={i for i,(a,b) in enumerate(zip(self.before,self.binary)) if a!=b}
        allowed={i for offset,old,new,reason in changes() for i in range(offset,offset+len(old))}
        self.assertTrue(changed and changed<=allowed)
        self.assertEqual(len(self.before),len(self.binary))
        self.assertEqual(self.before[0xDDF7C:0xDDFB0],self.binary[0xDDF7C:0xDDFB0])
        self.assertEqual(self.before[0xD9F10:0xDA1DC],self.binary[0xD9F10:0xDA1DC])
        for data in [self.binary,essentials.weapons.latency.OverlayLatencyTests.original]:
            with self.assertRaisesRegex(ValueError,'Unexpected ground late-camera'):
                ground_late_camera(data)

    def run_frame(self,old_build=False,name='爆炸猎弓',world=(4000,0,0),first_camera=(0,0,0),last_camera=(0,0,0),first_yaw=-90,last_yaw=0,vehicle=False,enabled=True):
        # Each build gets identical source records and two camera states.
        saved=self.binary
        if old_build:self.binary=self.before
        mu=self.emulator();self.binary=saved
        source,meta,cache,tls,initialized=STACK+0x2000,STACK+0x4000,STACK+0x6000,STACK+0x1000,STACK+0x1800
        mu.mem_write(CONFIG,b'\0'*0x500);mu.mem_write(CONFIG,struct.pack('<3f',1,800,600));mu.mem_write(CONFIG+0x21,bytes([enabled,enabled]))
        mu.mem_write(CONFIG+0x3D8,struct.pack('<2Q',source,source+32));mu.mem_write(CONFIG+0x408,struct.pack('<2Q',cache,cache))
        mu.mem_write(source,struct.pack('<2Q',0x200000000,meta));mu.mem_write(source+0x10,bytes([0,vehicle]))
        essentials.weapons.WeaponDisplayTests.metadata(mu,meta,name,long=True,vehicle=vehicle)
        mu.mem_write(STACK+0x80B8,struct.pack('<Q',CONFIG+0x1F4));mu.mem_write(initialized,b'\1')
        labels=[];reads=[];appended=[]
        def camera(position,yaw):
            # Real DA704 cache layout, independently derived from DA2E4/DA704.
            angle=math.radians(yaw);forward=(math.cos(angle),math.sin(angle),0);right=(-math.sin(angle),math.cos(angle),0)
            mu.mem_write(tls,struct.pack('<15f',*position,*forward,*right,0,0,1,400,300,400)+b'\1')
            mu.mem_write(CONFIG+0x1F4,struct.pack('<13f',*position,0,0,0,0,yaw,0,0,0,0,90))
        def string(uc,pointer):return bytes(uc.mem_read(pointer,128)).split(b'\0')[0].decode()
        def hook(uc,address,size,_):
            if address in [BASE+0xD9F10,BASE+0xDA1DC,BASE+0xDE5A0]:uc.emu_stop();return
            ins=struct.unpack('<I',uc.mem_read(address,4))[0]
            if ins==0xD503237F:uc.reg_write(r.UC_ARM64_REG_PC,address+4);return
            if ins==0xD65F0FFF:uc.reg_write(r.UC_ARM64_REG_PC,uc.reg_read(r.UC_ARM64_REG_X30));return
            if address==BASE+0xDA738:uc.reg_write(r.UC_ARM64_REG_X0,initialized);uc.reg_write(r.UC_ARM64_REG_PC,address+4);return
            if address==BASE+0xDA754:uc.reg_write(r.UC_ARM64_REG_X0,tls);uc.reg_write(r.UC_ARM64_REG_PC,address+4);return
            if ins&0xFC000000!=0x94000000:return
            delta=ins&0x3ffffff
            if delta&0x2000000:delta-=0x4000000
            target=address+delta*4-BASE
            if target in [0xCDA00,0xDA704]:return
            if target==0x123D2C:uc.reg_write(r.UC_ARM64_REG_D0,struct.unpack('<Q',struct.pack('<d',5))[0])
            elif target==0xDA474:
                reads.append(uc.reg_read(r.UC_ARM64_REG_X0))
                for register,value in zip([r.UC_ARM64_REG_S0,r.UC_ARM64_REG_S1,r.UC_ARM64_REG_S2],world):uc.reg_write(register,bits(value))
            elif target==0x29410:
                row=bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X1),88));appended.append(row);uc.mem_write(cache+(len(appended)-1)*88,row);uc.mem_write(CONFIG+0x410,struct.pack('<Q',cache+len(appended)*88))
            elif target==0x726A30:
                fmt=string(uc,uc.reg_read(r.UC_ARM64_REG_X2));self.assertIn('%s',fmt);self.assertIn('%d',fmt)
                text,distance=struct.unpack('<2Q',uc.mem_read(uc.reg_read(r.UC_ARM64_REG_SP),16));value=f'{string(uc,text)} {distance}m'.encode();uc.mem_write(uc.reg_read(r.UC_ARM64_REG_X0),value+b'\0')
            elif target==0x726B20:uc.reg_write(r.UC_ARM64_REG_X0,len(string(uc,uc.reg_read(r.UC_ARM64_REG_X0)).encode()))
            elif target==0xDF47C:labels.append(bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X0),uc.reg_read(r.UC_ARM64_REG_X1))).decode())
            else:self.fail(f'Unexpected native call {target:#x}')
            uc.reg_write(r.UC_ARM64_REG_PC,address+4)
        mu.hook_add(UC_HOOK_CODE,hook)
        camera(first_camera,first_yaw)
        mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.emu_start(BASE+0xD9514,BASE+0xD9F14,count=20000)
        # The unchanged native code refreshes the camera after collecting. Run
        # its real cache re-projection and distance/screen calculations next.
        camera(last_camera,last_yaw)
        mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X21,800);mu.reg_write(r.UC_ARM64_REG_X22,600)
        mu.emu_start(BASE+0xDA130,BASE+0xDA1E0,count=10000)
        refreshed=bytes(mu.mem_read(cache,88)) if appended else None
        mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X27,CONFIG);mu.emu_start(BASE+0xDDEBC,BASE+0xDE5A4,count=15000)
        self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+0xDE5A0)
        self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP),STACK+0x8000)
        # Execute the real next-frame vector clear: removed sources never
        # retain a label or get replaced with an earlier screen position.
        mu.reg_write(r.UC_ARM64_REG_X0,CONFIG);mu.emu_start(BASE+0xD7AE8,BASE+0xD7AF8,count=8)
        self.assertEqual(struct.unpack('<Q',mu.mem_read(CONFIG+0x410,8))[0],cache)
        drawn=len(labels);mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X27,CONFIG);mu.emu_start(BASE+0xDDEBC,BASE+0xDE5A4,count=15000)
        self.assertEqual(len(labels),drawn)
        return dict(labels=labels,reads=reads,initial=appended,final=refreshed)

    def test_old_camera_screen_cull_loses_currently_visible_items(self):
        for name in ['爆炸猎弓','十字弩','止痛药','三级包','可乐']:
            old=self.run_frame(old_build=True,name=name)
            new=self.run_frame(name=name)
            self.assertEqual(old['initial'],[]);self.assertEqual(old['labels'],[])
            self.assertEqual(len(new['initial']),1);self.assertEqual(new['initial'][0][0x50],0)
            self.assertEqual(new['final'][0x50],1);self.assertEqual(struct.unpack_from('<2f',new['final'],0x18),(400,300))
            self.assertEqual(new['labels'],[f'{name} 40m'])
            self.assertEqual(old['reads'],new['reads'])

    def test_final_projection_still_rejects_currently_offscreen_items(self):
        for old in [False,True]:
            frame=self.run_frame(old_build=old,first_yaw=0,last_yaw=-90)
            self.assertEqual(len(frame['initial']),1);self.assertEqual(frame['initial'][0][0x50],1)
            self.assertEqual(frame['final'][0x50],0);self.assertEqual(frame['labels'],[])

    def test_range_follows_final_camera_on_both_sides_of_600m(self):
        old=self.run_frame(old_build=True,world=(60000,0,0),first_yaw=0,first_camera=(-100,0,0),last_camera=(100,0,0))
        new=self.run_frame(world=(60000,0,0),first_yaw=0,first_camera=(-100,0,0),last_camera=(100,0,0))
        self.assertEqual(old['initial'],[]);self.assertEqual(new['labels'],['爆炸猎弓 599m'])
        frame=self.run_frame(world=(60000,0,0),first_yaw=0,first_camera=(100,0,0),last_camera=(-100,0,0))
        self.assertEqual(struct.unpack_from('<f',frame['final'],0x38)[0],601);self.assertEqual(frame['labels'],[])
        for distance in [600,600.01]:
            frame=self.run_frame(world=(distance*100,0,0),first_yaw=0)
            self.assertEqual(len(frame['initial']),1)
            self.assertEqual(frame['labels'],['爆炸猎弓 600m'] if distance==600 else [])

    def test_switches_unrelated_loot_nonfinite_positions_and_vehicles(self):
        self.assertEqual(self.run_frame(enabled=False)['reads'],[])
        for name in ['M416','绷带','三 甲','三 头','二级包','箭矢']:
            frame=self.run_frame(name=name);self.assertEqual(frame['reads'],[]);self.assertEqual(frame['labels'],[])
        for value in [math.nan,math.inf]:
            frame=self.run_frame(world=(value,0,0));self.assertEqual(frame['initial'],[]);self.assertEqual(frame['labels'],[])
        self.assertEqual(self.run_frame(name='吉普车',vehicle=True)['labels'],['吉普车 40m'])
