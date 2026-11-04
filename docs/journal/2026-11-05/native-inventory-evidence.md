# Native carried-inventory evidence

`scripts/harness/combat_ram_report.py` now reports all 24 native carry cells separately from its existing inventory-screen observations. A closed or empty-looking screen cannot establish that the actual carry table is empty. Each cell includes its raw item word, equipped/hot flags, icon word and, when resolved, resource and native class.

Carried items can be absent from the active world actor table. Resource resolution therefore validates the PAL actor vtable, custom-data bounds and both base-item self pointers, then reads the resource registry. Small continuation markers and unresolved pointers remain visible as raw words with null resource identity; they are not silently turned into empty cells. This remains a read-only observation tool with no inferred pass verdict.

Four synthetic tests cover an empty table with the UI closed, a carried item outside the active actor table with its continuation cell, an out-of-range item word, and an untyped heap word. They pass alongside Ruff and formatting checks. The CLI still accepts individual RAM files or directories and emits the same batch JSON structure with the additional `inventory_cells` field.

Independent native comparisons reproduce the earlier manual audit. The genuine CD09 normal-opening `ram_sustained.ram` has 24 zero item words. The diagnostic direct-RV `ram_initial_player.ram` has Atimar resource `0x344db` / native class `0x1000e` in cell 0 and continuation marker 1 in cell 8. Private combined output is `build/scratch/ootf-normal-route/carried-evidence.json`. The direct-RV control isolates initialization behavior; it is not campaign progression acceptance.
