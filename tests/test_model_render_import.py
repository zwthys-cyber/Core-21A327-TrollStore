import struct
import unittest
import test_model_capture_import as capture_fixtures
from remote_imports import records,redirect_model_capture_import
from model_render_patch import redirect_model_render,DRAW_CALLS,DRAW_FUNCTION,DRAW_IMPORT_STUB
from minimal_features import call

class ModelRenderImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        capture_fixtures.ModelCaptureImportTests.setUpClass()
        cls.before=redirect_model_capture_import(capture_fixtures.ModelCaptureImportTests.before)
    def test_exact_draw_redirects_and_other_imports_unchanged(self):
        after=redirect_model_render(self.before);_,old,_=records(self.before);_,new,_=records(after)
        for a,b in zip(old,new):
            if a['name']==b'_getpid':self.assertEqual(b,{**a,'name':b'_cmdraw','ordinal':52})
            else:self.assertEqual(a,b)
        allowed=set()
        _,a,s=records(self.before)
        for entries,chain in ((a,True),(s,False)):
            p=next(x for x in entries if x['name']==b'_getpid');allowed.update(range(p['start'],p['start']+7));allowed.add(p['pos'] if chain else p['pos']+7)
        for offset in DRAW_CALLS:
            self.assertEqual(self.before[offset:offset+4],call(offset,DRAW_FUNCTION))
            self.assertEqual(after[offset:offset+4],call(offset,DRAW_IMPORT_STUB));allowed.update(range(offset,offset+4))
        self.assertEqual(len(after),len(self.before))
        self.assertTrue({i for i,(a,b) in enumerate(zip(self.before,after)) if a!=b}<=allowed)
    def test_bad_draw_site_and_duplicate_alias_rejected(self):
        broken=bytearray(self.before);struct.pack_into('<I',broken,DRAW_CALLS[0],0xD503201F)
        with self.assertRaisesRegex(ValueError,'draw call'):redirect_model_render(broken)
        with self.assertRaisesRegex(ValueError,'collision'):redirect_model_render(redirect_model_render(self.before))
    def test_native_pixel_batch_uses_original_scene_lock_handle_and_ray_abi(self):
        # Original E0BE8/E0CF0 wrappers agree on scene mutex +0x58,
        # handle +8 and 2AD2A0(handle, x19 RayHit*, NULL). These exact
        # source instructions pin the new fast path independently of its mock.
        for offset,word in {
            0xE0C14:'14600191',0xE0C24:'e00314aa',0xE0C78:'632200bd',
            0xE0C7C:'08008012',0xE0C80:'682600b9',0xE0C88:'684a00b9',
            0xE0C8C:'a00640f9',0xE0C94:'e10313aa',0xE0C98:'020080d2',
            0xE0D1C:'14600191',0xE0D94:'a00640f9',
            0x2AD2B8:'a20100b4',0x2AD2CC:'683e40f9',0x2AD2D8:'1f093fd6',
        }.items():
            self.assertEqual(self.before[offset:offset+4],bytes.fromhex(word),hex(offset))
        for site,target in ((0xE0C28,0x725000),(0xE0CA4,0x725010),(0xE0C9C,0x2AD2A0),(0xE0DA4,0x2AD2A0)):
            self.assertEqual(self.before[site:site+4],call(site,target))
