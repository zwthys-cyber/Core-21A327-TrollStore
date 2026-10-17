"""Validate the committed vehicle catalog; executed only in GitHub Actions."""
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VehicleCatalogTests(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads(
            (ROOT / "analysis/cosmetics/selected-vehicle-catalog.json").read_text()
        )

    def test_all_package_vehicle_remaps_are_resolved_once(self):
        presets = self.catalog["presets"]
        self.assertEqual(self.catalog["resolved_pair_count"], 35)
        self.assertEqual(self.catalog["unresolved_recipe_count"], 0)
        self.assertEqual(self.catalog["unresolved"], [])
        self.assertGreater(
            self.catalog["package_mapping_evidence"]["vehicle_resource_contract_rows"],
            100,
        )
        coverage = self.catalog["package_mapping_evidence"]["supplied_package_coverage"]
        self.assertEqual([row["vehicle_remaps"] for row in coverage], [0, 35])
        self.assertGreater(coverage[0]["vehicle_rows_checked"], 500)
        self.assertEqual(len({row["source_item_id"] for row in presets}), 35)
        self.assertTrue(all(row["source_item_id"] != row["target_item_id"] for row in presets))

    def test_known_ambiguous_labels_use_the_package_mapping(self):
        pairs = {
            row["source_item_id"]: (row["target_item_id"], row["target_resource_id"])
            for row in self.catalog["presets"]
        }
        self.assertEqual(pairs[3306002], (3306046, 330604600))
        self.assertEqual(pairs[3308002], (3308015, 330801500))
        self.assertEqual(pairs[3313001], (3336039, 333603900))
        self.assertEqual(pairs[3314001], (3314004, 331400400))
        self.assertEqual(pairs[3314002], (3314005, 331400500))
        self.assertEqual(pairs[3314003], (3314006, 331400600))
        self.assertEqual(pairs[3351001], (3351010, 335101000))
        self.assertEqual(pairs[3351003], (3351006, 335100600))
        self.assertEqual(pairs[3312001], (412009, 412009))

    def test_generated_header_matches_json(self):
        header = (ROOT / "patch/CoreCosmeticsVehicleCatalog.h").read_text()
        entries = [
            tuple(map(int, values))
            for values in re.findall(
                r"\{(\d+)u, (\d+)u, (\d+)u, (\d+)u, ", header
            )
        ]
        expected = [
            (
                row["source_item_id"],
                row["source_resource_id"],
                row["target_item_id"],
                row["target_resource_id"],
            )
            for row in self.catalog["presets"]
        ]
        self.assertEqual(entries, expected)


if __name__ == "__main__":
    unittest.main()
