"""Execute the actual selective weapon UI, classifier literals and native loops.

Run in GitHub Actions only. Synthetic records do not verify live game offsets.
"""
import math
import struct
import unittest
from itertools import product
import test_minimal_features as baseline
import test_overlay_latency as latency
from weapon_display import weapon_display, changes, PREDICATE, CROSSBOW_CLASS
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE, STACK, CONFIG, RETURN = baseline.BASE, baseline.STACK, baseline.CONFIG, baseline.RETURN

def bits(value):
    return struct.unpack('<I',struct.pack('<f',value))[0]

class WeaponDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        latency.OverlayLatencyTests.setUpClass()
        cls.before=latency.OverlayLatencyTests.binary
        cls.binary=weapon_display(cls.before)

    emulator=baseline.MinimalFeatureTests.emulator

    def test_exact_guarded_changes_and_reapplication_rejected(self):
        allowed={p for start,old,new,reason in changes() for p in range(start,start+len(old))}
        changed={i for i,(a,b) in enumerate(zip(self.before,self.binary)) if a!=b}
        self.assertTrue(changed and changed<=allowed)
        self.assertEqual(len(self.binary),len(self.before))
        for data in [self.binary,latency.OverlayLatencyTests.original]:
            with self.assertRaisesRegex(ValueError,'Unexpected weapon display'):
                weapon_display(data)
        self.assertEqual(self.binary[0xC1A04:0xC1A08],bytes.fromhex('c0035fd6'))

    def test_five_independent_controls_and_saved_changes(self):
        controls=[(0x23,0x743F64),(0x22,0x743F71),(0x20,0x743F4A),(0x21,0x743F57),(0x24,0x743F7E)]
        for values in product([0,1],repeat=5):
            for changed_index in range(-1,5):
                mu=self.emulator();seen=[];ends=[]
                for (preference,label),value in zip(controls,values):mu.mem_write(CONFIG+preference,bytes([value]))
                def hook(uc,address,size,_):
                    ins=struct.unpack('<I',uc.mem_read(address,4))[0]
                    if ins==0xD503237F:uc.reg_write(r.UC_ARM64_REG_PC,address+4)
                    elif ins==0xD65F0FFF:uc.reg_write(r.UC_ARM64_REG_PC,uc.reg_read(r.UC_ARM64_REG_X30))
                    elif ins&0xFC000000==0x94000000:
                        delta=ins&0x3ffffff
                        if delta&0x2000000:delta-=0x4000000
                        target=address+delta*4-BASE
                        if target==0xC8928:
                            index=len(seen);preference,label=controls[index]
                            self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X1),CONFIG+preference)
                            self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0),BASE+label)
                            changed=index==changed_index
                            if changed:uc.mem_write(CONFIG+preference,bytes([values[index]^1]))
                            seen.append(preference);uc.reg_write(r.UC_ARM64_REG_W0,changed)
                        if target==0x12E620:ends.append(target)
                        uc.reg_write(r.UC_ARM64_REG_PC,address+4)
                mu.hook_add(UC_HOOK_CODE,hook);mu.emu_start(BASE+0xCD6A4,RETURN,count=700)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),RETURN)
                self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP),STACK+0x8000)
                self.assertEqual(seen,[p for p,label in controls]);self.assertEqual(len(ends),1)
                self.assertEqual(mu.mem_read(BASE+0xC4DF86,1),bytes([changed_index>=0]))
                for i,(preference,label) in enumerate(controls):self.assertEqual(mu.mem_read(CONFIG+preference,1),bytes([values[i]^(i==changed_index)]))

    @staticmethod
    def metadata(mu,address,name,long=False,vehicle=0):
        raw=name.encode();mu.mem_write(address,b'\0'*0x50);mu.mem_write(address+0x15,bytes([vehicle]))
        if long:
            pointer=address+0x80;mu.mem_write(pointer,raw+b'\0');mu.mem_write(address+0x18,struct.pack('<QQQ',pointer,len(raw),0x8000000000000100))
        else:mu.mem_write(address+0x18,raw.ljust(23,b'\0')+bytes([len(raw)]))

    def test_exact_two_names_short_long_and_live_registers_preserved(self):
        names=['爆炸猎弓','十字弩','M416','爆炸弓','燃点复合弓','战术弩','十字弩箭矢','7.62mm','烟雾弹','']
        for name,long in product(names,[False,True]):
            mu=self.emulator();meta=STACK+0x2000;self.metadata(mu,meta,name,long)
            mu.reg_write(r.UC_ARM64_REG_X0,meta);mu.reg_write(r.UC_ARM64_REG_X30,RETURN)
            mu.reg_write(r.UC_ARM64_REG_X8,0xAABBCCDD);mu.reg_write(r.UC_ARM64_REG_X12,0x1122334455667788)
            mu.emu_start(BASE+PREDICATE,RETURN,count=100)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_W0),name in ['爆炸猎弓','十字弩'])
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_X8),0xAABBCCDD)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_X12),0x1122334455667788)

    def test_classifier_constructor_receives_crossbow_and_canonical_bow_literals(self):
        mu=self.emulator();seen=[]
        def hook(uc,address,size,_):
            if address in [BASE+0xF562C,BASE+0xF563C,BASE+0xF5654,BASE+0xF5664]:
                pointer=uc.reg_read(r.UC_ARM64_REG_X1);raw=bytes(uc.mem_read(pointer,64)).split(b'\0')[0];seen.append(raw.decode());uc.reg_write(r.UC_ARM64_REG_PC,address+4)
        mu.hook_add(UC_HOOK_CODE,hook);mu.emu_start(BASE+0xF5614,BASE+0xF5668,count=50)
        self.assertEqual(seen,['BP_Other_CrossBow_Wrapper_C','十字弩','BP_Other_HuntingBow_Wrapper_C','爆炸猎弓'])
        expected=b'BP_Other_CrossBow_Wrapper_C\0'
        self.assertEqual(self.binary[CROSSBOW_CLASS:CROSSBOW_CLASS+len(expected)],expected)

    def test_filter_rejects_other_loot_before_any_position_read(self):
        mu=self.emulator();source,meta=STACK+0x2000,STACK+0x3000
        destinations={BASE+0xD95C0,BASE+0xD9EF4}
        mu.hook_add(UC_HOOK_CODE,lambda uc,a,s,_:uc.emu_stop() if a in destinations else None)
        for vehicle_on,bow_on,projectile,name,vehicle in product([0,1],[0,1],[0,1],['爆炸猎弓','十字弩','M416','箭矢'],[0,1]):
            mu.mem_write(CONFIG,b'\xff'*0x500);mu.mem_write(CONFIG+0x21,bytes([bow_on,vehicle_on]))
            mu.mem_write(source+8,struct.pack('<Q',meta));mu.mem_write(source+0x10,bytes([projectile]));self.metadata(mu,meta,name,vehicle=vehicle)
            mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.reg_write(r.UC_ARM64_REG_X24,source)
            mu.emu_start(BASE+0xD959C,BASE+0xD9EF8,count=120)
            accepted=not projectile and (vehicle_on if vehicle else bow_on and name in ['爆炸猎弓','十字弩'])
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+(0xD95C0 if accepted else 0xD9EF4))

    def test_full_native_collector_preserves_vehicles_and_two_bows_only(self):
        rows=[('爆炸猎弓',450,0,0),('十字弩',600,0,0),('吉普车',300,1,0),('M416',50,0,0),('燃点复合弓',20,0,0),('十字弩',600.01,0,0),('爆炸猎弓',math.nan,0,0),('十字弩',20,0,1)]
        for vehicle_on,bow_on in product([0,1],repeat=2):
            mu=self.emulator();source,metas=STACK+0x2000,STACK+0x4000
            mu.mem_write(CONFIG,b'\xff'*0x500);mu.mem_write(CONFIG,struct.pack('<3f',1,800,600));mu.mem_write(CONFIG+0x21,bytes([bow_on,vehicle_on]));mu.mem_write(CONFIG+0x1F4,b'\0'*52)
            mu.mem_write(CONFIG+0x3D8,struct.pack('<2Q',source,source+len(rows)*32));mu.mem_write(STACK+0x80B8,struct.pack('<Q',CONFIG+0x1F4))
            for i,(name,distance,vehicle,projectile) in enumerate(rows):
                meta=metas+i*0x100;mu.mem_write(source+i*32,struct.pack('<2Q',0x200000000+i*0x1000,meta));mu.mem_write(source+i*32+0x10,bytes([projectile,vehicle]));self.metadata(mu,meta,name,vehicle=vehicle)
            appends=[];reads=[]
            def hook(uc,address,size,_):
                if address==BASE+0xD9F10:uc.emu_stop();return
                ins=struct.unpack('<I',uc.mem_read(address,4))[0]
                if ins&0xFC000000!=0x94000000:return
                delta=ins&0x3ffffff
                if delta&0x2000000:delta-=0x4000000
                target=address+delta*4-BASE
                if target==PREDICATE:return
                if target==0x123D2C:uc.reg_write(r.UC_ARM64_REG_D0,struct.unpack('<Q',struct.pack('<d',5))[0])
                elif target==0xDA474:
                    i=(uc.reg_read(r.UC_ARM64_REG_X0)-0x200000000)//0x1000;reads.append(i);uc.reg_write(r.UC_ARM64_REG_S0,bits(rows[i][1]*100));uc.reg_write(r.UC_ARM64_REG_S1,0);uc.reg_write(r.UC_ARM64_REG_S2,0)
                elif target==0xDA704:uc.reg_write(r.UC_ARM64_REG_S0,bits(250));uc.reg_write(r.UC_ARM64_REG_S1,bits(200))
                elif target==0x29410:appends.append(bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X1),0x58)))
                else:self.fail(f'Unexpected collector call {target:#x}')
                uc.reg_write(r.UC_ARM64_REG_PC,address+4)
            mu.hook_add(UC_HOOK_CODE,hook);mu.reg_write(r.UC_ARM64_REG_X19,CONFIG);mu.emu_start(BASE+0xD9514,BASE+0xD9F14,count=5000)
            self.assertEqual(reads,([0,1] if bow_on else [])+([2] if vehicle_on else [])+([5,6] if bow_on else []))
            self.assertEqual([struct.unpack_from('<Q',row)[0] for row in appends],([0x200000000,0x200001000] if bow_on else [])+([0x200002000] if vehicle_on else []))
            self.assertEqual([row[0x4D] for row in appends],([0,0] if bow_on else [])+([1] if vehicle_on else []))
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+0xD9F10)

    def test_held_name_toggle_uses_text_path_and_fixed_readable_style(self):
        mu=self.emulator();mu.reg_write(r.UC_ARM64_REG_X27,CONFIG)
        for enabled in [0,1]:
            mu.mem_write(CONFIG+0x24,bytes([enabled]));mu.emu_start(BASE+0xDB680,BASE+0xDB688,count=3)
            self.assertEqual(struct.unpack('<I',mu.mem_read(STACK+0x8138,4))[0],enabled)
        mu.reg_write(r.UC_ARM64_REG_W8,0);mu.emu_start(BASE+0xDD32C,BASE+0xDD338,count=4);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),BASE+0xDD338)
        mu.emu_start(BASE+0xDD398,BASE+0xDD3A4,count=4);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_W3),0xffffffff)
        mu.emu_start(BASE+0xDD3A8,BASE+0xDD3AC,count=1);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_S1),bits(14))
