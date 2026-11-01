# Regression triage (co-op claims, ghost removal, arena_ranged B freeze)

Working tree vs HEAD diff of drakan_coop / drakan_netplay / drakan_ghost / drakan_pvp / drakan_overrides was read: nothing
touches the claim path (`kMsgDamage` handler, `g_keyed`, `isLocalAttacker`). Findings below; runs used base port 28200
with a heavily loaded machine (3-4 other game instances).

1. Co-op partner melee claims: not a code regression. `fight` passed 9/9 twice (claims sent 1, applied). `death` and the
   3-player `fight`/`death` missed only the claim checks, the known flake documented in coop-dragons.md section 4 (B's
   swings meet A's ghost instead of an orc; with several guests the swings are timing dependent). `--swing-hold 250`
   helps on a slow machine.
2. netplay_two_instances "b: ghost removed when A left the level": real harness weakness plus a missing log line. B's
   own level change goes through `forgetGhosts()` (silent), so the only matching line was the final "player left" at
   shutdown, which exists only if B still sees A leave. Fixed: `forgetGhosts()` now logs "ghost of player N removed
   (local level change or save)".
3. arena_ranged B "freeze at ~900 frames": REAL and reproducible with single runs (rt_ar, rt_ar2, rt_ar5; A runs 11k frames, B
   stops at 860-930). It is not boot/title and not the heal / resumeMultiplayer / preview / Vitals code: B reaches the arena,
   A takes the bow, B logs "pickup 0 (Short Wood Bow) taken by player 1: removed here" (killActor on actor b03e80) and
   a few frames later `[guest-branch:missing-target] JALR source=0x341548 target=0 s0=0xb03e80` (a call on the dead
   pickup actor, its record pointer already freed): the guest thread ends there, the net thread keeps printing stats.
   With `DRAKAN_ARENA_NOFILTER=1` the same run passes (B 6981 frames, no null call, 10/14), so it depends on the campaign
   removal (350 actors) having run. Tried: skip ghost-held weapon items in the loose-item sweep (isGhostItem), which did
   not change the outcome. Open: find who still holds b03e80 (B's player stands 300 units from the bow, spawn 1 is on the
   pickup; suspect the player's use/focus target or a linked story object that the sweep killed).

## Pass table (working tree, build of 09:48 and 10:45, machine loaded)

| Scenario | Result |
| --- | --- |
| coop fight 2p | 9/9 (twice) |
| coop death 2p | 11/13 (claim checks: the known swing flake) |
| coop latejoin 2p, door, pickup, gold | 8/8, 4/4, 4/4, 4/4 |
| coop chest 2p | B never booted (7 log lines), load; not re-proven |
| coop chesthost 2p | 5/8 (B did not match creatures in two runs); not re-proven |
| coop fight 3p | 13/19 (claims from player 3, ghost hit, load) |
| coop death 3p | 23/28 and 19/28 (claim checks) |
| coop latejoin 3p | C starved by load, 7/13 (earlier attempt exe missing) |
| netplay_two_instances | 13/13 |
| arena_two_instances (surdana-meadow) | 17/19 (server accepted pickup / B removed its copy) |
| arena_ranged | 3/14, B guest dies after the pickup removal (above) |
