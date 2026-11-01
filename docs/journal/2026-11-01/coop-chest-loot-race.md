# Co-op chest loot: the instant-grab race and the harness fixes

## The race (found with the new `chestloot` scenario, fixed)

A chest's spew creates its loot items; the engine reuses freed actor handles for them. If a player takes an
item in the same moment as the spew, that item's handle is freed and handed to a new loot child before
`watchObjects` has seen the pickup leave. `g_obj` then still held the old pickup under that handle, so
`keyChestChildren` skipped the child (`g_obj.count(child)`), and `scanNewObjects` later keyed it as a "new pickup"
by order of appearance on the client only. Result: host key `ef47790c` (chest key, child 3) against client key
`34d282eb` for the same item, so a later pickup of it was never matched.

Fix (`keyChestChildren`): a `g_obj` entry whose resource/ref no longer matches the child is stale. The old pickup's
removal is reported to the peer right there (it was taken), the entry is dropped, and the child is keyed as
(chest key, child index).

## Harness (`scripts/harness/coop_two_instances.py`)

- `chestloot` scenario: both players start beside a chest that holds gold (`CHESTLOOT_SPOT`, budget 103, four to
  five gold items). The loot check requires a non-empty, equal key set. The old `chest`/`chesthost` chest has one
  potion that is already a level pickup, so their loot comparison is vacuous. `--chest-key` / `DRAKAN_COOP_TEST_TO_KEY`
  pick a chest by key; chest keys differ between runs, so the default is "nearest to the start spot".
- Roles: A is now always the host. Under load A booted slower than the fixed 4 s head start, B joined first and
  won the election (player 1 = creature world owner), swapping every host/client check. The harness now waits for A's
  "co-op authority: player 1 (us)" before starting B. MatchServer's joinSeq election itself is correct (first joiner).
- Use `--folder <own prefix>`: the default `coop2-a/b` play folders are shared between agents, and a running game
  locks the copy (`PermissionError`) or gets its exe replaced.
- Far teleports (`was 100000 away`) from the default spawn kill the player (level reloads); start next to the chest.
