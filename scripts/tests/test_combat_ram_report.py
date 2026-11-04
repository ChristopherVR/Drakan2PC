"""Synthetic native inventory cells; no original game data."""

import importlib.util
import pathlib
import struct
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "harness" / "combat_ram_report.py"
SPEC = importlib.util.spec_from_file_location("combat_ram_report", SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class InventoryEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.ram = bytearray(0x2000000)
        self.context, self.player, self.actor = 0x700000, 0x900000, 0x800000
        self.registry, self.records, self.objects = 0x600000, 0x610000, 0x620000
        self.put(0x5565D4, self.context)
        self.put(self.context + 0x4B0, self.actor)
        self.put(self.context + 0x7C, self.objects)
        self.put(self.context + 0x84, 2)
        self.put(self.context + 0x320, self.registry)
        self.put(self.registry + 8, self.records)
        self.put(self.registry + 0x10, 1)
        self.put(self.records + 8, 0x10009)
        self.put(self.objects, self.actor)
        self.put(self.actor + 0xFC, 0x810000)
        self.put(0x810010, self.player)
        self.cells = self.player + 0x584 + 0x30

    def put(self, address, value):
        struct.pack_into("<I", self.ram, address, value)

    def read(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "synthetic.ram"
            path.write_bytes(self.ram)
            return REPORT.report(path)

    def test_empty_carry_table_is_reported_without_opening_inventory(self):
        result = self.read()
        self.assertTrue(result["rynn_available"])
        self.assertEqual(result["inventory"]["screen"], "0x0")
        self.assertEqual(len(result["inventory_cells"]), 24)
        self.assertTrue(all(cell["item_word"] == "0x0" for cell in result["inventory_cells"]))
        self.assertTrue(all(cell["resource"] is None for cell in result["inventory_cells"]))

    def test_item_identity_and_continuation_are_distinct_from_ui_state(self):
        item = 0x820000
        # Carried item is deliberately absent from the active world actor table, as in native starting inventory.
        self.put(self.context + 0x84, 1)
        self.put(item, 0x51B8B0)
        self.put(item + 0xFC, 0x830000)
        self.put(0x830010, 0x840000)
        self.put(0x84000C, item)
        self.put(0x8400EC, item)
        struct.pack_into("<H", self.ram, item + 0x10, 1)
        self.put(self.records + 0x24, 0x344DB)
        self.put(self.records + 0x24 + 8, 0x1000E)
        self.put(self.cells, 1)
        self.put(self.cells + 8, item)
        self.put(self.cells + 8 * 16 + 8, 1)
        cells = self.read()["inventory_cells"]
        self.assertEqual(cells[0]["resource"], "0x344db")
        self.assertEqual(cells[0]["resource_class"], "0x1000e")
        self.assertEqual(cells[0]["equipped"], 1)
        self.assertEqual(cells[8]["item_word"], "0x1")
        self.assertIsNone(cells[8]["resource"])

    def test_unresolvable_cell_remains_visible_instead_of_becoming_empty(self):
        self.put(self.cells + 8, 0x03000000)
        cell = self.read()["inventory_cells"][0]
        self.assertEqual(cell["item_word"], "0x3000000")
        self.assertIsNone(cell["resource"])

    def test_untyped_heap_word_is_not_mistaken_for_an_item(self):
        self.put(self.cells + 8, 0x830000)
        cell = self.read()["inventory_cells"][0]
        self.assertEqual(cell["item_word"], "0x830000")
        self.assertIsNone(cell["resource_class"])


if __name__ == "__main__":
    unittest.main()
