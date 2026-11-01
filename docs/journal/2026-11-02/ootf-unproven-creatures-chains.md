# Unproven creatures and puzzle chains, second pass (2026-11-02)

Method: converter test aids (`DRAKAN_DBG_PULL` / `PULLIDS` / `PULLDIST`, see `ootfpipeline.ts`) put the objects next to Rynn, the harness
recipes `scripts/harness/recipes/ootf_creature_ring.txt` and `ootf_switch_use.txt` take screenshots; world dumps (`DRAKAN_WORLD_DUMP_FRAME=2200`)
list live actors. Runs `build/ootf-run/tc_*` (private).

| Item | Result |
| --- | --- |
| Succubus (Succubus Lair, rfl-30 clone with the PC model) | **proven**: winged woman draws and flaps beside Rynn (`tc_tk`) |
| Blade Dragon (Grotto, Mines) | **alive and hostile**: fire effects hit Rynn and her orb drops (`tc_bd`, `tc_bt`); the body itself is not clearly in frame (flies off) |
| Queen + Navy Hueges (Succubus Lair) | **alive** in the world dump (hp 300 / 250 at 700 units either side of Rynn); no body in the shots (level is dark, Queen hovers) |
| Ebon Dragon, Drone (Islands) | pulled into the lit cave: nothing in frame at 700 units, no dump taken (the dump frame was missed on those runs); still unproven by picture |
| Bats | in the Mines shots only as dark specks |
| Switch -> Portcullis (Wartok, `Switch#2986 -> Portcullis#2989`) | **inconclusive**: portcullis shown, E pressed; Rynn also moved (camera pans), so the door state change cannot be read from `tc_sw` |

## Pack conflict (RESOURES / WORLDCON / PROCTEES)

The two release packs collide in 67 paths: 5 whole files (`RESOURES.MOD/.TXD`, `WORLDCON.MOD/.TXD`, `PROCTEES.TXD`), 56 `new/` entries (class ids
16944-16986 of `RESOURES.ODB`, `WORLDCON.ODB` 2366/2367, XDB ids, SDB entries) and the `overrides/*.ODB` class-count records. Each conversion allocates
new class ids from the same first free id, so a runtime merge cannot be done safely: the ids mean different classes in each pack, and a whole-file
`.MOD` replacement drops the other pack's models. The robust fix is one conversion for both sets (arenas are detected per `Multiplayer/*` level
already) with the campaign list kept apart from the arena levels in `mod.toml`; this needs a `campaign_levels` subset the engine honours
(`drakan_game::applyBase`, title menu) and was not started. Until then the later pack wins (documented in `ootf-port.md`).

## Not done (needs engine work)

* Dragon breath flame / particle and the dying-dragon fall: the PvP breath is a host claim only (`drakan_pvp.cpp` `scanBreath`); drawing a flame
  needs an engine effect object to be spawned, and a fall needs a ground height query, neither exists in the module.
* Real PC triggers (Islands Detector 4131, Volcanic Detector 2318, Succubus ship2) and the Dr. Evil fight were not walked in this pass.
