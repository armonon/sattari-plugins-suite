import copy
import json
from pathlib import Path
import sys
import tempfile
import plistlib
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from consolidate_catalog import consolidate
from gen_hub_manifest import deployment, validate
from check_hub_bundle_version import verify


class SuiteConsolidationTests(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((ROOT / "config/products.json").read_text())

    def test_consolidation_is_idempotent_and_preserves_legacy_identity(self):
        before = copy.deepcopy(self.catalog)
        after = consolidate(copy.deepcopy(before))
        self.assertEqual(before, after)
        for old, new in zip(before["products"], after["products"]):
            for field in ("id", "name", "lifecycle", "shippingProfile"):
                self.assertEqual(old[field], new[field])
        self.assertFalse(after["consolidation"]["publicReleaseApproved"])

    def test_current_catalog_covers_every_source_directory(self):
        products = validate(self.catalog)
        self.assertEqual(len(products), 51)
        variants = [p for p in products if p.get("sourceDirectory") == "modular"]
        self.assertEqual({deployment(p)[0] for p in variants}, {"SattariModularFX", "SattariModularInstrument"})
        self.assertNotEqual(deployment(variants[0])[2], deployment(variants[1])[2])

    def test_legacy_is_hidden_but_not_removed(self):
        by_id = {p["id"]: p for p in validate(self.catalog)}
        for pid in ("vocal", "create", "stack", "bus", "vocal-master"):
            self.assertEqual(by_id[pid]["portfolioStatus"], "legacy")
            self.assertFalse(by_id[pid]["webVisible"])
        self.assertEqual(by_id["harmony"]["portfolioStatus"], "migration-pending")
        self.assertTrue(by_id["maqam"]["webVisible"])
        current = {p["id"] for p in by_id.values() if p["shippingProfile"] == "alpha" and p["portfolioStatus"] in ("active", "specialist")}
        self.assertEqual(current, {"modular", "modular-instrument", "mix", "auto-pitch", "brain", "sattari-arp", "sub-conjurer"})

    def test_missing_variant_and_inconsistent_collections_are_rejected(self):
        broken = copy.deepcopy(self.catalog)
        broken["products"] = [p for p in broken["products"] if p["id"] != "modular-instrument"]
        with self.assertRaises(ValueError):
            validate(broken)

        broken = copy.deepcopy(self.catalog)
        broken["products"][0]["collections"].append("create")
        with self.assertRaises(ValueError):
            validate(broken)
        broken = copy.deepcopy(self.catalog)
        broken["collections"][0]["members"].append("clean")
        with self.assertRaises(ValueError):
            validate(broken)

    def test_modular_version_is_required_and_stale_payloads_are_rejected(self):
        product = next(p for p in self.catalog["products"] if p["id"] == "modular")
        bundle = deployment(product)[2][0]
        self.assertEqual(bundle["expectedVersion"], "0.2.0")
        manifest = {"products": [{"bundles": [bundle]}]}
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            plist = source / "Contents/Info.plist"
            plist.parent.mkdir()
            for version in (None, "0.1.1", "0.2.0"):
                metadata = {} if version is None else {"CFBundleShortVersionString": version}
                with plist.open("wb") as stream:
                    plistlib.dump(metadata, stream)
                if version == "0.2.0":
                    verify(manifest, bundle["relativeSourcePath"], source)
                else:
                    with self.assertRaises(ValueError):
                        verify(manifest, bundle["relativeSourcePath"], source)
        broken = copy.deepcopy(product)
        broken["binaryVersion"] = "0.1.1"
        with self.assertRaises(ValueError):
            deployment(broken)


if __name__ == "__main__":
    unittest.main()
