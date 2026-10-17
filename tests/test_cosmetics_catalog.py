"""Check the cloud-generated menu against its reviewed metadata, in Actions."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CosmeticsCatalogTests(unittest.TestCase):
    def test_compiled_catalog_matches_exact_metadata(self):
        catalog = json.loads((ROOT / 'analysis/cosmetics/selected-avatar-catalog.json').read_text())
        header = (ROOT / 'patch/CoreCosmeticsCatalog.h').read_text()
        rows = re.findall(r'\{(\d+)u, (\d+)u, ("(?:[^"\\]|\\.)*"), ("(?:[^"\\]|\\.)*")\}', header)
        expected = [(p['slot'], p['target_id'], p['category'], p['label']) for p in catalog['presets']]
        self.assertEqual([(int(slot), int(target), json.loads(category), json.loads(label))
                          for slot, target, category, label in rows], expected)
        self.assertEqual(len(expected), 143)
        self.assertEqual(len({p[1] for p in expected}), len(expected))
        self.assertEqual(expected[0], (5, 403188, '外套', '至尊金龙外套'))
        self.assertEqual(catalog['category_counts'], {'外套': 60, '头发': 53, '裤子': 14, '鞋子': 5, '降落伞': 11})
        for preset in catalog['presets']:
            self.assertTrue(preset['exact_label_and_item_row_confirmed'])
            self.assertEqual(preset['device_visual_verified'], preset['target_id'] == 403188)
            self.assertNotIn('\ufffd', preset['label'])
            self.assertFalse(any(ord(c) < 32 for c in preset['label']))


if __name__ == '__main__':
    unittest.main()
