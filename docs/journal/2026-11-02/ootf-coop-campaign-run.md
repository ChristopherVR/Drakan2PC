# Co-op campaign runs with the new harness (2026-11-02)

Tool: `scripts/harness/ootf_coop_campaign.py`, described in [../../reference/ootf-coop-harness.md](../../reference/ootf-coop-harness.md).
Pack: all 13 levels converted with the formats at the time (`build/ootf-run/coopcamp/mods`), binary `build/final-game/bin/drakan.exe` of 05:40.

## 1. Every level, host + joiner (`--name all1`, exe of the morning)

All 13 sessions PASS: both instances load the expected level, get control, no crash, no `[stall]`, both ghosts spawn. Minimum / median
`completedFlips` per 2 s on the host: Intro 71/1283, Ruined Village 72/343, Book 72/365, Wartok Canyons 61/124, Mines 67/224, Grotto 81/122,
Islands 69/140, Alwarren 69/92, Rift Crystal 77/287, Islands Night 81/255, Succubus Lair 71/80, Volcanic 64/82, Rift World 76/119 (100 = about
50 fps; the lowest medians, Succubus / Volcanic / Alwarren, are about 40 fps). The Book session ends in Ruined Village (its timer exit).

## 2. Chain mode (`--chain`, probe `DRAKAN_LEVELCHANGE=<steps>@1500`)

* The probe string is `step,step,...@ticks`: the `@` belongs at the end (a first version wrote `0@1500,0,0` and the parser dropped everything
  after the `@`, so only one hop ran). Steps may be level titles since the OotF exits carry destination names.
* `--chain 12` with exit 0 each time: Intro -> Ruined Village -> Book -> Ruined Village -> Book ... 11 hops in 60 s on the host, the joiner
  follows every one (`level follow: arrived`, `follow exit ...: found`). RV exit 0 leads to the Book, so the chain now names the next level
  (the Book is skipped as a side level).
* **Joiner crash after following (open, intermittent):** twice the joiner (B) hit `missing-target` JALR in a Ruined Village that it had
  entered by following the host: `0x1043a0 -> 0x41c23ddc` 170 s after arrival (chain1, host idle in the same level) and `0x1946cc -> 0x3025a00`
  about 7 s after the 6th arrival (chain2, host already gone to the Book: `sameLevel=0 remote=00000000`). Both targets are float-looking data,
  i.e. an indirect call through a stale / freed object. Not seen in the 13 single-level sessions (the joiner stays in one level) and not
  explained yet; the co-op keying (`drakan_coop.cpp`, `keyed 79 creatures and 42 watched objects`) is a suspect because it keeps handles across
  the host's level changes. The host then reports `[stall]` too, as its partner vanished.

## 3. Runtime of 06:34 breaks Intro / Ruined Village loads (not from this work)

Re-runs after another session rebuilt `build/final-game/bin/drakan.exe` at 06:34 (uncommitted `drakan_texscale` hook, `drakan_world.cpp` /
`drakan_pvp.cpp` edits in the tree) fail: Ruined Village loads (`afterLevelLoad`) and the guest then goes silent (`[stall]` at frame about
2100, thread 1 on semaphore 2); the Intro joiner stalled once too. Solo sessions fail the same way, so it is not co-op. The 05:40 binary
passed all 13 levels. The harness `--exe` option pins a binary; verify the rebuilt runtime before trusting later runs.
