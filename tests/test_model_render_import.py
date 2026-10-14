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
