# Wartok Canyons: the taken poison crystal stayed in place

Report: "with the save the crystal doesn't disappear after we've entered and consumed it".

## What happened

The owner's `Slot33` (boss dead, crystal released, journal `wartok-boss-wake.md`) holds two Dragon Power-ups (runtime class
`0x10039`) at the shrine (289792, 34000, 281613): the placed 2538 (inert, no model, ref 1233) and the copy released by
Object Generator 3233 (class `0x1003f`, ref 1792, record without a level id). Flying into the copy works as on the PC:
Arokh's breath count (`+0x6b0`) goes from 1 to 2, Timer 3079 plays the Stomp Player scene (about 30 s, input locked) and
the wheel then selects the gas breath. But the copy never goes: at the take it clears its visible bit (`+4` bit 0,
`0x18501048`) and sets it again on its next update, so Arokh sits inside it during and after the scene
(`crystal_take_before_fix_scene.png`). The disc has no power-up a dragon takes by touch (its two hidden ones are taken by
a Dragon link at load), so the PS2 class has no removal for it; the PC engine deletes a taken power-up.

Killing only the copy is not enough: generator 3233 (Limit 1) releases a new one as soon as its part is gone (ref 1821
appeared at the same spot in `crystal-takefix6`). Sinking the copy in the save instead removes the only takeable crystal
(`wartok-gone4`).

## Fix (runtime, `drakan_dragon_breath.cpp`)

Each frame the controlled dragon's breath count is compared with the last frame's. When it grows (Order of the Flame
only, not in the arena), every Dragon Power-up within 4000 units of him is killed (`kKillActor`, the queued kill), and so
is every Object Generator within 64 units of one of them (the generator that released it; the gas lightning generators
around the shrine are thousands of units away and keep running). This covers the other converted Quickening crystals
(Grotto 777, Alwarren 2569, Volcano 2332) with no pack change, and old saves need no upgrade rule.

A wrapper around the dragon's message handler `0x27d0e0` (AddBreath `0x1028`, payload at `0x27d6f0`) was tried first; it
was never called for the take, so the take does not reach the handler through the function table.

## Evidence (exe of `dist/DrakanPC` after `Build-LocalGame.ps1 -Incremental`, the owner's settings, copy of `mc0_ootf`)

| Run (`build/harness/`) | What | Result |
| --- | --- | --- |
| `crystal-takefix5` | place Arokh in the crystal, wait, wheel -1, fire | both power-ups removed, breath count 2, gas burst (`crystal_take_fixed_gas.png`) |
| `crystal-takefix6` | same, before the generator rule | generator released a new copy (ref 1821) |
| `crystal-takefix7`, `-9` | with the generator rule | power-ups and generator 3233 removed, no power-up left in the level, breath count 2 |
| `crystal-takefix10` | fly into the crystal, shots through the scene | scene plays with Arokh and no crystal around him (`crystal_take_fixed_scene.png`) |

Images are in `build/review/wartok-crystal/`. The owner's `Slot33` was not changed (backup of it before this work:
`dist/DrakanPC/save-backups/manual/2026-11-28-before-crystal-take-fix/`).

## Not verified

* The other three Quickening crystals (Grotto, Alwarren, Volcano) were not run.
* A save made after the take and reloaded was not checked (the killed objects should be absent from it).
* Any other power-up the dragon touches within 4000 units at the moment he gains a breath is removed too; the OotF
  levels place no two power-ups that close apart from the inert shrine copies.
