#!/usr/bin/env python3
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FamilyContractTest(unittest.TestCase):
    def test_command_catalog(self):
        catalog = json.loads((ROOT / "docs/FoxicMPFamilyCommands.json").read_text(encoding="utf-8"))
        self.assertEqual(catalog["schemaVersion"], 1)
        commands = catalog["commands"]
        self.assertGreaterEqual(len(commands), 40)
        self.assertEqual(len({item["id"] for item in commands}), len(commands))
        self.assertEqual(len({item["classicId"] for item in commands}), len(commands))
        self.assertTrue(any(item["id"] == "frameNext" for item in commands))

    def test_capability_matrix(self):
        matrix = json.loads((ROOT / "docs/FoxicMPFamilyCapabilities.json").read_text(encoding="utf-8"))
        self.assertEqual(set(matrix["editions"]), {"windows-classic", "windows-qt", "linux-qt"})
        for row in matrix["capabilities"]:
            self.assertTrue(row["id"])
            for edition in matrix["editions"]:
                self.assertIn(edition, row)
                self.assertTrue(row[edition])


if __name__ == "__main__":
    unittest.main()
