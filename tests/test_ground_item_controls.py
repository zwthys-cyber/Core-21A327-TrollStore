"""Execute six independent menus, predicates and complete native item paths."""
import struct,unittest
from itertools import product
import test_ground_late_camera as late
from ground_item_controls import ground_item_controls,changes,ITEMS,PREDICATE,COMMIT,OPEN_RETURN,COMMIT_RETURN
from model_render_patch import DRAW_IMPORT_STUB
from unicorn import UC_HOOK_CODE
from unicorn import arm64_const as r

BASE,STACK,CONFIG,RETURN=late.BASE,late.STACK,late.CONFIG,late.RETURN


class GroundItemControlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        late.GroundLateCameraTests.setUpClass();cls.before=late.GroundLateCameraTests.binary;cls.binary=ground_item_controls(cls.before)
    emulator=late.GroundLateCameraTests.emulator
    run_frame=late.GroundLateCameraTests.run_frame
    native_calls=(PREDICATE,)

    def test_guarded_native_changes_preserve_late_camera_and_existing_features(self):
        allowed={i for start,old,new,reason in changes() for i in range(start,start+len(old))}
        changed={i for i,(a,b) in enumerate(zip(self.before,self.binary)) if a!=b}
        self.assertTrue(changed and changed<=allowed);self.assertEqual(len(self.binary),len(self.before))
        for data in [self.binary,late.essentials.weapons.latency.OverlayLatencyTests.original]:
            with self.assertRaisesRegex(ValueError,'Unexpected ground item control'):ground_item_controls(data)
        for start,end in [(0xD9E3C,0xD9F10),(0xD9F10,0xDA1DC),(0xDB46C,0xDD338)]:self.assertEqual(self.binary[start:end],self.before[start:end])

    def test_each_exact_name_short_long_with_all_64_independent_masks(self):
        for mask,(index,(name,label,offset)),long in product(range(64),enumerate(ITEMS),[False,True]):
            mu=self.emulator();meta=STACK+0x2000;late.essentials.weapons.WeaponDisplayTests.metadata(mu,meta,name,long)
            for i,(_,_,preference) in enumerate(ITEMS):mu.mem_write(CONFIG+preference,bytes([(mask>>i)&1]))
            mu.reg_write(r.UC_ARM64_REG_X0,meta);mu.reg_write(r.UC_ARM64_REG_X1,CONFIG);mu.reg_write(r.UC_ARM64_REG_X30,RETURN)
            mu.reg_write(r.UC_ARM64_REG_X8,0xAABBCCDD);mu.reg_write(r.UC_ARM64_REG_X12,0x123456789ABC)
            mu.emu_start(BASE+PREDICATE,RETURN,count=900)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_W0),(mask>>index)&1)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_X8),0xAABBCCDD);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_X12),0x123456789ABC)

    def test_ten_controls_commit_only_the_clicked_item_and_balance_native_child(self):
        controls=[('附近敌人',0x23,None),('附近载具',0x22,None),('人物填色',0x20,None)]+[(name,offset,i) for i,(name,label,offset) in enumerate(ITEMS)]+[('手持武器',0x24,None)]
        for mask,changed_index in product(range(64),range(-1,10)):
            mu=self.emulator();flags=STACK+0x3000;seen=[];ends=[];commits=[]
            mu.mem_write(BASE+0xC15A80,struct.pack('<f',1))
            original={offset:(mask>>(i%6))&1 for i,(name,offset,index) in enumerate(controls)}
            for name,offset,index in controls:
                value=((mask>>index)&1) if index is not None else original[offset]
                original[offset]=value;mu.mem_write(CONFIG+offset,bytes([value]))
                if index is not None:mu.mem_write(flags+index,bytes([value]))
            def hook(uc,address,size,_):
                ins=struct.unpack('<I',uc.mem_read(address,4))[0]
                if ins==0xD503237F:uc.reg_write(r.UC_ARM64_REG_PC,address+4);return
                if ins==0xD65F0FFF:uc.reg_write(r.UC_ARM64_REG_PC,uc.reg_read(r.UC_ARM64_REG_X30));return
                if ins&0xFC000000!=0x94000000:return
                delta=ins&0x3ffffff
                if delta&0x2000000:delta-=0x4000000
                target=address+delta*4-BASE
                if target==COMMIT:return
                if target==DRAW_IMPORT_STUB:
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X0),CONFIG)
                    if address+4==BASE+OPEN_RETURN:uc.reg_write(r.UC_ARM64_REG_X0,flags)
                    else:
                        self.assertEqual(address+4,BASE+COMMIT_RETURN);values=bytes(uc.mem_read(flags,6));commits.append(sum(bool(v)<<i for i,v in enumerate(values)))
                        for i,(_,_,offset) in enumerate(ITEMS):uc.mem_write(CONFIG+offset,bytes([values[i]]))
                        uc.reg_write(r.UC_ARM64_REG_X0,0)
                elif target==0xC8928:
                    index=len(seen);name,offset,item_index=controls[index]
                    label=bytes(uc.mem_read(uc.reg_read(r.UC_ARM64_REG_X0),64)).split(b'\0')[0].decode();self.assertEqual(label,name)
                    pointer=flags+item_index if item_index is not None else CONFIG+offset
                    self.assertEqual(uc.reg_read(r.UC_ARM64_REG_X1),pointer)
                    changed=index==changed_index
                    if changed:uc.mem_write(pointer,bytes([original[offset]^1]))
                    seen.append(offset);uc.reg_write(r.UC_ARM64_REG_W0,changed)
                elif target==0xC88B0:
                    size=uc.reg_read(r.UC_ARM64_REG_X1);self.assertEqual(struct.unpack('<f',uc.mem_read(size+4,4))[0],0)
                elif target==0x12E620:ends.append(target)
                uc.reg_write(r.UC_ARM64_REG_PC,address+4)
            mu.hook_add(UC_HOOK_CODE,hook);mu.emu_start(BASE+0xCD6A4,RETURN,count=1400)
            self.assertEqual(mu.reg_read(r.UC_ARM64_REG_PC),RETURN);self.assertEqual(mu.reg_read(r.UC_ARM64_REG_SP),STACK+0x8000)
            self.assertEqual(seen,[o for n,o,i in controls]);self.assertEqual(len(ends),1)
            clicked_item=3<=changed_index<=8
            self.assertEqual(commits,[mask^(1<<(changed_index-3))] if clicked_item else [])
            self.assertEqual(mu.mem_read(BASE+0xC4DF86,1),bytes([changed_index>=0 and not clicked_item]))
            for i,(name,offset,item) in enumerate(controls):self.assertEqual(mu.mem_read(CONFIG+offset,1),bytes([original[offset]^(i==changed_index)]))

    def test_native_collection_to_render_for_each_item_and_all_masks(self):
        for mask in range(64):
            preferences={offset:(mask>>i)&1 for i,(name,label,offset) in enumerate(ITEMS)};preferences[0x22]=0
            for i,(name,label,offset) in enumerate(ITEMS):
                frame=self.run_frame(name=name,preferences=preferences)
                self.assertEqual(frame['labels'],[f'{name} 40m'] if mask&(1<<i) else [])
                self.assertEqual(len(frame['reads']),bool(mask&(1<<i)))
        for name in ['M416','绷带','三 甲','三 头','二级包','空投弹药','主题箱子','盒子']:
            frame=self.run_frame(name=name,preferences={offset:1 for _,_,offset in ITEMS});self.assertEqual(frame['reads'],[])

    def test_airdrop_final_camera_range_and_vehicle_independence(self):
        prefs={offset:int(name=='空投') for name,label,offset in ITEMS};prefs[0x22]=0
        frame=self.run_frame(name='空投',preferences=prefs);self.assertEqual(frame['labels'],['空投 40m'])
        self.assertEqual(self.run_frame(name='空投',preferences=prefs,first_yaw=0,last_yaw=-90)['labels'],[])
        self.assertEqual(self.run_frame(name='空投',preferences=prefs,first_yaw=0,world=(60001,0,0))['labels'],[])
        self.assertEqual(self.run_frame(name='空投',preferences=prefs,first_yaw=0,world=(60000,0,0))['labels'],['空投 600m'])
        prefs={offset:0 for name,label,offset in ITEMS};prefs[0x22]=1
        self.assertEqual(self.run_frame(name='吉普车',vehicle=True,preferences=prefs)['labels'],['吉普车 40m'])

    def test_original_airdrop_constructor_uses_four_known_classes(self):
        mu=self.emulator();seen=[]
        def hook(uc,address,size,_):
            ins=struct.unpack('<I',uc.mem_read(address,4))[0]
            if ins&0xFC000000==0x94000000:
                pointer=uc.reg_read(r.UC_ARM64_REG_X1);seen.append(bytes(uc.mem_read(pointer,64)).split(b'\0')[0].decode());uc.reg_write(r.UC_ARM64_REG_PC,address+4)
        mu.hook_add(UC_HOOK_CODE,hook);mu.emu_start(BASE+0xF65D4,BASE+0xF6674,count=200)
        self.assertEqual(seen,['AirDropBox_C','空投','AirDropBox','空投','BP_AirDropBox_SuperPeople_C','空投','AirDropListWrapperActor','空投'])
