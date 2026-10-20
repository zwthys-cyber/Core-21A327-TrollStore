"""Run actual patched ARM64 whitelist and complete ground label paths in Actions."""
import math
import struct
from itertools import product
import test_weapon_display as weapons
from ground_essentials import ground_essentials,changes,ITEMS,PREDICATE
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE,STACK,CONFIG,RETURN=weapons.BASE,weapons.STACK,weapons.CONFIG,weapons.RETURN
bits=weapons.bits


class GroundEssentialsTests(weapons.WeaponDisplayTests):
    @classmethod
    def setUpClass(cls):
        weapons.WeaponDisplayTests.setUpClass()
        cls.before=weapons.WeaponDisplayTests.binary
        cls.binary=ground_essentials(cls.before)

    def test_exact_guarded_changes_and_reapplication_rejected(self):
        allowed={p for start,old,new,reason in changes() for p in range(start,start+len(old))}
        changed={i for i,(a,b) in enumerate(zip(self.before,self.binary)) if a!=b}
        self.assertTrue(changed and changed<=allowed)
        self.assertEqual(len(self.binary),len(self.before))
        for data in [self.binary,weapons.latency.OverlayLatencyTests.binary]:
            with self.assertRaisesRegex(ValueError,'Unexpected ground essentials'):
                ground_essentials(data)
        for offset in [0xDB680,0xDD32C,0xDD398,0xDD39C,0xDD3A8,0xD959C,0xD97AC,0xDDEBC,0xDDF5C]:
            self.assertEqual(self.binary[offset:offset+4],self.before[offset:offset+4])
        self.assertEqual(self.binary[0x743F57:0x743F64],'物资位置'.encode()+b'\0')

    def test_exact_two_names_short_long_and_live_registers_preserved(self):
        accepted=['爆炸猎弓','十字弩','止痛药','三级包','可乐']
        names=accepted+['M416','爆炸弓','绷带','肾上腺素','三 甲','三 头','三 包','二级包','三级甲','止痛苭','三级包弹药','十字弩箭矢','可乐弹药','']
        for name,long in product(names,[False,True]):
            mu=self.emulator();meta=STACK+0x2000;self.metadata(mu,meta,name,long)
            mu.reg_write(r.UC_ARM64_REG_X0,meta);mu.reg_write(r.UC_ARM64_REG_X30,RETURN)
            canaries={r.UC_ARM64_REG_X8:0xAABBCCDD,r.UC_ARM64_REG_X12:0x1122334455667788,r.UC_ARM64_REG_X19:0xABCDEF,r.UC_ARM64_REG_D8:0x1234}
            for register,value in canaries.items():mu.reg_write(register,value)
            mu.emu_start(BASE+PREDICATE,RETURN,count=700)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),RETURN)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_W0),name in accepted,name)
            for register,value in canaries.items():self.assertEqual(mu.reg_read(register),value)
        mu=self.emulator();mu.reg_write(r.UC_ARM64_REG_X0,0);mu.emu_start(BASE+PREDICATE,RETURN,count=700)
        self.assertEqual(mu.reg_read(r.UC_ARM64_REG_W0),0)

    def test_original_medicine_and_equipment_constructor_names(self):
        # Run the real constructor instructions. Stub only std::string's
        # constructor, recording its real literal argument instead of guessing IDs.
        for start,end,expected in [
            (0xF6AB0,0xF6B4C,['ink_Pickup_C','可乐','lls_Pickup_C','止痛药','jection_Pickup_C','肾上腺素','rstAidbox_Pickup_C','医疗箱']),
            (0xF6BA0,0xF6C14,['BP_Armor_Lv3_C','三 甲','Bag_Lv3','三级包','BP_Helmet_Lv3_C','三 头'])]:
            mu=self.emulator();seen=[]
            def hook(uc,address,size,_):
                instruction=struct.unpack('<I',uc.mem_read(address,4))[0]
                if instruction&0xFC000000==0x94000000:
                    delta=instruction&0x3ffffff
                    if delta&0x2000000:delta-=0x4000000
                    self.assertEqual(address+delta*4-BASE,0x6790)
                    pointer=uc.reg_read(r.UC_ARM64_REG_X1)
                    seen.append(bytes(uc.mem_read(pointer,64)).split(b'\0')[0].decode())
                    uc.reg_write(r.UC_ARM64_REG_PC,address+4)
            mu.hook_add(UC_HOOK_CODE,hook);mu.emu_start(BASE+start,BASE+end,count=200)
            self.assertEqual(seen,expected)

    def test_filter_rejects_other_loot_before_any_position_read(self):
        mu=self.emulator();source,meta=STACK+0x2000,STACK+0x3000
        destinations={BASE+0xD95C0,BASE+0xD9EF4}
        mu.hook_add(UC_HOOK_CODE,lambda uc,a,s,_:uc.emu_stop() if a in destinations else None)
        names=['爆炸猎弓','十字弩','止痛药','三级包','可乐','M416','饮料弹药','三 甲','三 头','二级包']
        whitelist={'爆炸猎弓','十字弩','止痛药','三级包','可乐'}
        for vehicle_on,ground_on,projectile,name,vehicle in product([0,1],[0,1],[0,1],names,[0,1]):
            mu.mem_write(CONFIG,b'\xff'*0x500);mu.mem_write(CONFIG+0x21,bytes([ground_on,vehicle_on]))
            mu.mem_write(source+8,struct.pack('<Q',meta));mu.mem_write(source+0x10,bytes([projectile]));self.metadata(mu,meta,name,vehicle=vehicle)
            mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X24,source)
            # Five complete byte comparisons need a higher synthetic instruction
            # ceiling than v15's unrolled two-name 120-instruction test cap.
            mu.emu_start(BASE+0xD959C,BASE+0xD9EF8,count=700)
            accepted=not projectile and (vehicle_on if vehicle else ground_on and name in whitelist)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+(0xD95C0 if accepted else 0xD9EF4))

    def test_new_items_through_native_collection_and_render_with_range_switches_and_projection(self):
        rows=[('爆炸猎弓',450,0,0),('十字弩',600,0,0),('止痛药',30,0,0),('三级包',50,0,0),('吉普车',300,1,0),
              ('可乐',20,0,0),('三 甲',20,0,0),('三 头',20,0,0),('M416',20,0,0),('二级包',20,0,0),
              ('止痛药',600.01,0,0),('三级包',math.nan,0,0),('止痛药',20,0,1),('三级包',600,0,0),('止痛药',0,0,0),('三级包',20,0,0)]
        whitelist={'爆炸猎弓','十字弩','止痛药','三级包','可乐'}
        for vehicle_on,ground_on in product([0,1],repeat=2):
            mu=self.emulator();source,metas,cache=STACK+0x2000,STACK+0x4000,STACK+0x6000
            mu.mem_write(CONFIG,b'\xff'*0x500);mu.mem_write(CONFIG,struct.pack('<3f',1,800,600));mu.mem_write(CONFIG+0x21,bytes([ground_on,vehicle_on]));mu.mem_write(CONFIG+0x1F4,b'\0'*52)
            mu.mem_write(CONFIG+0x3D8,struct.pack('<2Q',source,source+len(rows)*32));mu.mem_write(STACK+0x80B8,struct.pack('<Q',CONFIG+0x1F4))
            for i,(name,distance,vehicle,projectile) in enumerate(rows):
                meta=metas+i*0x100;self.metadata(mu,meta,name,long=i%2==0,vehicle=vehicle)
                mu.mem_write(source+i*32,struct.pack('<2Q',0x200000000+i*0x1000,meta));mu.mem_write(source+i*32+0x10,bytes([projectile,vehicle]))
            appends=[];reads=[];labels=[]
            def string(uc,pointer):return bytes(uc.mem_read(pointer,128)).split(b'\0')[0].decode()
            def hook(uc,address,size,_):
                if address in [BASE+0xD9F10,BASE+0xDE5A0]:uc.emu_stop();return
                instruction=struct.unpack('<I',uc.mem_read(address,4))[0]
                if instruction&0xFC000000!=0x94000000:return
                delta=instruction&0x3ffffff
                if delta&0x2000000:delta-=0x4000000
                target=address+delta*4-BASE
                if target==PREDICATE:return
                if target==0x123D2C:uc.reg_write(r.UC_ARM64_REG_D0,struct.unpack('<Q',struct.pack('<d',5))[0])
                elif target==0xDA474:
                    i=(uc.reg_read(r.UC_ARM64_REG_X0)-0x200000000)//0x1000;reads.append(i);uc.reg_write(r.UC_ARM64_REG_S0,bits(rows[i][1]*100));uc.reg_write(r.UC_ARM64_REG_S1,0);uc.reg_write(r.UC_ARM64_REG_S2,0)
                elif target==0xDA704:uc.reg_write(r.UC_ARM64_REG_S0,bits(250));uc.reg_write(r.UC_ARM64_REG_S1,bits(200))
                elif target==0x29410:appends.append(bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X1),0x58)))
                elif target==0x726A30:
                    fmt=string(uc,uc.reg_read(r.UC_ARM64_REG_X2));self.assertIn('%s',fmt);self.assertIn('%d',fmt)
                    text,distance=struct.unpack('<2Q',uc.mem_read(uc.reg_read(r.UC_ARM64_REG_SP),16));value=f'{string(uc,text)} {distance}m'.encode()
                    self.assertLess(len(value),uc.reg_read(r.UC_ARM64_REG_X1));uc.mem_write(uc.reg_read(r.UC_ARM64_REG_X0),value+b'\0')
                elif target==0x726B20:uc.reg_write(r.UC_ARM64_REG_X0,len(string(uc,uc.reg_read(r.UC_ARM64_REG_X0)).encode()))
                elif target==0xDF47C:
                    labels.append(bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X0),uc.reg_read(r.UC_ARM64_REG_X1))).decode());self.assertEqual(uc.reg_read(r.UC_ARM64_REG_W3),0xff00ffff)
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_S2),bits(14));self.assertEqual(uc.reg_read(r.UC_ARM64_REG_W2),1)
                else:self.fail(f'Unexpected native call {target:#x}')
                uc.reg_write(r.UC_ARM64_REG_PC,address+4)
            mu.hook_add(UC_HOOK_CODE,hook);mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.emu_start(BASE+0xD9514,BASE+0xD9F14,count=20000)
            eligible=[i for i,(name,distance,vehicle,projectile) in enumerate(rows) if not projectile and (vehicle_on if vehicle else ground_on and name in whitelist)]
            accepted=[i for i in eligible if math.isfinite(rows[i][1]) and 0<=rows[i][1]<=600]
            self.assertEqual(reads,eligible)
            self.assertEqual([struct.unpack_from('<Q',row)[0] for row in appends],[0x200000000+i*0x1000 for i in accepted])
            for i,row in zip(accepted,appends):
                entry=bytearray(row)
                # The final selected bag remains cached but fails native
                # projection; rendering must skip it without a stale label.
                if i==15:entry[0x50]=0
                mu.mem_write(cache+accepted.index(i)*0x58,bytes(entry))
            mu.mem_write(CONFIG+0x408,struct.pack('<2Q',cache,cache+len(appends)*0x58))
            mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X27,CONFIG);mu.emu_start(BASE+0xDDEBC,BASE+0xDE5A4,count=20000)
            self.assertEqual(labels,[f'{rows[i][0]} {int(rows[i][1])}m' for i in accepted if i!=15])
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+0xDE5A0)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP),STACK+0x8000)
