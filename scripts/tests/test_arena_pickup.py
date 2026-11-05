"""Native pickup acceptance must distinguish a late initial loadout from a collected item."""

import copy
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import arena_pickup


class ArenaPickupTest(unittest.TestCase):
    def receipt(self, blade=False, fault=False):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            names = ("before", "pickup", "inventory", "equipped", "moved", "late", "late_inventory", "final")
            for name in names:
                (folder / f"ram_{name}.ram").touch()
            log = (
                "[menus] native options prepared for the game startup loader\n"
                "multiplayer launch: New Game with City\n"
                "arena world mode on: City of the Damned\n"
                "test: going to pickup 0\n"
                "gui screen -1 -> 21\n"
                "pickup 0 (Lastat's Blade) taken here: claim\n"
                "[in] script quit at frame 10000\n"
            )
            if fault:
                log += "[guest-branch:missing-target] tid=1 target=0x0\n"
            (folder / "stdout_1.log").write_text(log)
            dagger = dict(resource="0x33cbc", type=10, props="0x123000")
            weapon = dict(resource="0x3373c", type=10, props="0x124000")

            def fake_inventory(path):
                cells = [] if path.stem == "ram_before" else [copy.copy(dagger)]
                if blade and path.stem != "ram_before":
                    cells.append(copy.copy(weapon))
                return dict(
                    cells=cells, hands=[copy.copy(dagger)], position=[1000 if path.stem == "ram_moved" else 0, 0, 0]
                )

            with patch.object(arena_pickup, "inventory", side_effect=fake_inventory):
                return arena_pickup.report(folder)

    def test_local_claim_without_server_acceptance_fails_peer_checks(self):
        self.assertFalse(all(arena_pickup.peer_pickup_checks("pickup 0 taken here: claim", "").values()))

    def test_bidirectional_accepted_pickups_and_removals(self):
        host = (
            "server: pickup 0 (Blade) taken by player 1\n"
            "server: pickup 0 (Blade) taken by player 2\n"
            "pickup 0 (Blade) taken by player 2: removed here\n"
        )
        client = "pickup 0 (Blade) taken by player 1: removed here\n"
        self.assertTrue(all(arena_pickup.peer_pickup_checks(host, client).values()))

    def test_distinct_native_pickups_require_matching_peer_removals(self):
        host = (
            "server: pickup 0 (Blade) taken by player 1\n"
            "server: pickup 1 (Blade) taken by player 2\n"
            "pickup 1 (Blade) taken by player 2: removed here\n"
        )
        client = "pickup 0 (Blade) taken by player 1: removed here\n"
        self.assertTrue(all(arena_pickup.peer_pickup_checks(host, client, 1).values()))
        self.assertFalse(all(arena_pickup.peer_pickup_checks(host, client, 0).values()))

    def test_native_inventory_tags_are_not_actor_pointers(self):
        data = bytearray(0x600000)

        def write(address, value):
            struct.pack_into("<I", data, address, value)

        context, actor, inner, player = 0x100000, 0x110000, 0x115000, 0x120000
        resource, records, registry, blade = 0x130000, 0x140000, 0x150000, 0x160000
        for address, value in (
            (0x5565D4, context),
            (context + 0x4B0, actor),
            (actor + 0xFC, inner),
            (inner + 0x10, player),
            (context + 0x320, resource),
            (resource + 8, records),
            (context + 0x7C, registry),
            (records, 0x3373C),
            (records + 8, 0x1000E),
            (blade + 0xC, 10),
            (blade + 0x54, 0x170000),
            (player + 0x584 + 0x30 + 16 + 8, blade),
            (player + 0x584 + 0x30 + 9 * 16 + 8, 1),
            (player + 0x584 + 0x30 + 17 * 16 + 8, 2),
        ):
            write(address, value)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "synthetic.ram"
            path.write_bytes(data)
            result = arena_pickup.inventory(path)
        self.assertEqual([cell["resource"] for cell in result["cells"]], ["0x3373c"])
        self.assertEqual(result["sentinels"], [dict(index=9, value=1), dict(index=17, value=2)])

    def test_partial_native_options_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "native.ini"
            source.write_text("[options]\nsensitivity=1\ninvert_y=0\ncontroller_scheme=0\n")
            with self.assertRaisesRegex(ValueError, "native options incomplete"):
                arena_pickup.private_native_options(source)

    def test_complete_native_options_are_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "native.ini"
            text = "[options]\n" + "".join(f"{key}=1\n" for key in sorted(arena_pickup.NATIVE_OPTION_KEYS))
            source.write_text(text)
            self.assertEqual(arena_pickup.private_native_options(source), text)
            self.assertEqual(source.read_text(), text)

    def test_native_use_fixture_has_bounded_post_collection_actions(self):
        recipe = arena_pickup.pickup_host_recipe(1, 0, True)
        self.assertIn("10000 key tap E", recipe)
        self.assertIn("35000 dump ram_final", recipe)
        self.assertTrue(recipe.endswith("38000 quit\n"))

    def test_initial_loadout_is_not_a_pickup(self):
        result = self.receipt()
        self.assertFalse(result["checks"]["blade_in_native_inventory"])
        self.assertFalse(result["passed"])

    def test_authored_sites_keep_all_city_pickup_definitions(self):
        source = (arena_pickup.ROOT / "configs/mp_maps.ini").read_text()
        city = source.split("[arena city-skeleton-square]", 1)[1].split("[arena ", 1)[0]
        stock = [line for line in city.splitlines() if line.startswith(("pickup =", "bounds ="))]
        for sites in arena_pickup.CITY_SITES:
            fixture = arena_pickup.city_site_map(sites).splitlines()
            self.assertEqual([line for line in fixture if line.startswith(("pickup =", "bounds ="))], stock)
            self.assertEqual(sum(line.startswith("spawn =") for line in fixture), 2)

    def test_real_blade_and_native_completion_pass(self):
        self.assertTrue(self.receipt(blade=True)["passed"])

    def test_fault_cannot_be_hidden_by_later_items_or_quit(self):
        self.assertFalse(self.receipt(blade=True, fault=True)["passed"])


if __name__ == "__main__":
    unittest.main()
