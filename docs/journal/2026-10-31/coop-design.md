# Co-op design and a first prototype (host-authoritative enemies) - 2026-10-31

Sections 1 to 6 are the design and the phase 0 prototype. **Section 7 is phase 2** (kills, client hits, enemies that target
the partner, doors and pickups), verified in two game instances.

Goal: two players progress through the same level together. Desk research from the repo docs, then one cheap prototype:
the host replicates its creatures (position, yaw, HP) and the client's copies stop thinking and follow. Sources:
`multiplayer.md` (no lockstep: the game is not deterministic), `actor-spawn.md`, `ghost-spike*.md`, `netplay-spike.md`,
`docs/reference/netcode.md`, `world-model.md`, `level-format.md`, `ootf-reuse-study.md`. Evidence tags: **[run]** measured in
this note's prototype, **[RAM]** / **[dec]** as in `world-model.md` / `actor-spawn.md`, **[inf]** inference, **[?]**
unknown. No disassembly is reproduced.

## 1. World state that must agree

"Where it lives" is only what the docs and RAM dumps establish; every **[?]** is a research item (a RAM diff across a
trigger, or a decompile read of the class handler).

| State | Where it lives | Sync difficulty | Notes |
| --- | --- | --- | --- |
| Creature position / rotation | handle `+0x14` / `+0x20` (types 6 and 7 carry HP) [RAM] | easy | Written every tick on the client; the sector update `0x1494c8` is not yet called (see section 5). |
| Creature HP | instance data `q = *(*(h+0xfc)+0x10)`, plain int32 `q+0x24c` / max `q+0x250`, validity `*(q+0x23c) == h` [RAM] | easy | Not obfuscated, unlike the player's. 137 creatures in Surdana. |
| Creature death | HP <= 0, then the handle leaves the registry (kill list `ctx+0x440`, `0x1413c0`) [dec] | medium | The death sequence, loot drop and score are driven by the handler's per-tick code. A proxy that never ticks never dies on its own: the host's destroy must be replayed (`drakan_lifecycle::destroyActor`). Not done. |
| Creature AI target | AI reads the controlled actor `ctx+0x4b0` (135 functions) [dec]; per-creature target state inside the instance data [?] | hard | Host AI only sees the host's Rynn. See 2.4. |
| Creature animation | skeleton channels `sk+0xe8` (clip `+0x50`, time `+0x58`) [RAM] | medium | The ghost already drives these. With the tick swallowed the proxy may stop animating [?, not checked visually]. Replicate clip id + time like `PlayerState`. |
| Summoned / spawned creatures, projectiles | `0x181ad8` runtime spawns, resource index of the creature [dec] | hard | No stable id: not in the level list. Needs a spawn event (resource index, transform, owner key) and a key allocated by the host. Prototype ignores them. |
| Pickups, inventory drops | items are handles (types 10, 11) in the same registry; level-placed items by level object; drops are runtime spawns with `extra` (spawn context `ctx+0x4e0`) [dec] | medium | Event "item X taken by player N" keyed by object key; the taker's inventory is global to that player (`0x4fbac8` block, controller `data+0x57c`) and is not shared. Loot from a creature appears on the host only: replicate "drop spawned" or let both worlds drop from the same replicated death. Shared vs personal loot is a design choice. |
| Doors, levers, switches, gates | placed objects, classes `0x1009d` / `0x10031` / `0x10029` / `0x10025` (type 7) are the likely candidates [inf]; state fields of each class [?] | medium | Placed objects have stable `id`s (`level-format.md` LVL 0x0020, u32 ids ascending, identical on both sides). Replicate "object id -> new state". Needs the per-class state field or a message-based replay (below). |
| Triggers | placed objects with `radius` (flag 0x10) and `links` (ids of targets), parameters such as "Trigger Message", "Initial State" [disc]; message ids from OpenDrakan: 4097 On, 4098 Off, 4257 Play Sequence [doc, unverified on PS2] | medium | The brief names 9099 as the exit message; it is not found anywhere in the repo docs (grep), so treat it as **[?]**. Best sync point is the engine's message dispatch (send of message M to object id I): replay on the other side. The hook location is **[?]** (the send wrappers `0x283988` / `0x283a28` / `0x283b48` / `0x283be8` forward handler events 0x80 / 0x100 [dec], likely candidates). |
| Scripted sequences / cutscenes (STOMP) | SSD types 0x0310-0x0312 (xform / anim / sound / attach / message / music actions) [doc]; player is under script control while it runs; camera mode word `camera data +0x14c` [dec] | very hard | A cutscene takes over `ctx+0x4b0` / camera. Co-op policy: the host plays it, the client gets "sequence N started / ended" and either watches a fade or plays it locally with the same start event. Never replay the actions by position. |
| Level exits and level change | `Level_Load 0x1c94c8` via `0x13b7c0` (engine slot 0xd), `drakan_lifecycle` before / after events; an exit saves first (`0x169c28`) [run in `lifecycle.md`] | medium | Both players must be in the level; see 2.5. |
| Quest flags / progression | inside the `.rsg` save (the serialisers iterate the registry and global blocks) [dec]; individual flag storage [?] | hard | Not found as a named table. The pragmatic route is to replicate effects (doors, items, kills) and never the flags. |
| Player HP, mana, stats, inventory | globals: stats block `0x4fbac8`, key `0x4fe86c`, controller `data+0x57c` [RAM] | n/a | Per player and local. Never replicated except HP for the HUD of the partner. |
| Dragon (Arokh) | placed actor class `0x1001c` (200000.0 stored, ~202773 rest) [RAM]; mounting makes it the controlled actor (`ctx+0x4b0`); Rynn's init links to it via `0x452190(0x1001c)` / `0x27dfa0` [dec] | hard | Only one Arokh actor exists, and the engine thinks of "the dragon" as a single object. Either only one player mounts (the other ghost stays on foot) or Arokh is a host-owned shared object with the rider's pose replicated. Ghost mounted state is already hidden in netplay. |
| Saves / checkpoints | `.rsg` through `0x169c28` (write) and `0x16adb8` (restore); quick save drives the real save UI [dec][run] | medium | Policy in 2.6. |

## 2. Architecture options

### 2.1 Event replication only

Each instance runs its full simulation; discrete state changes (door opened, item picked, trigger fired, exit taken,
creature killed) are sent keyed by level object id and replayed. Cheap and robust to latency, but enemies are the problem:
two instances run two AIs from different random seeds and timing (`multiplayer.md` section 2), so the same goblin is in two
places and one world sees kills the other does not. Replaying "creature X died" patches the visible contradiction but
never the moving positions. **Prototype result [run], mark mode:** with AI left running on both sides and both players in
the same level, the average distance between a creature and its host counterpart grew from 13 units at the start to 128
after 40 s, maximum 4383, only 12 of 137 creatures even moving. Event replication alone is therefore not enough for
enemies, but it is the right tool for everything that is discrete.

### 2.2 Host-authoritative enemies (recommended for enemies)

The host runs AI, combat and death. The client's copies are **kinematic proxies**: their per-tick behaviour event is
swallowed, and position, rotation, animation and HP are written from the host's 10 Hz state. The client sends **hit
claims** (creature key, weapon, damage, own position) which the host validates (the same sanity ladder as `MatchServer`:
range, rate) and applies to the real creature. This is the ghost technique inverted: instead of spawning a Rynn-class
actor with a swapped handler, the existing actor of each placed creature gets the swapped handler.

### 2.3 Hybrid (the plan)

Host-authoritative creatures (2.2) plus event replication of discrete world changes (2.1) keyed by level object ids. The
host is also authoritative for the order of exits and for the mission state. Both worlds are loaded identically, so
placed objects are matched by identity, not by guessing.

### 2.4 How a second Rynn affects enemy targeting

- All perception goes through `ctx+0x4b0` (135 readers) [dec], so on the host the partner's ghost is invisible to
  every AI (`actor-spawn.md` section 7); enemies only ever chase the host's Rynn. If the client wanders away from the host,
  enemies near the client do nothing, and the client sees proxies standing still or walking toward the host.
- Per-AI retargeting is too broad (135 functions). Two cheaper levers, in increasing cost:
  1. **Aggro by proxy**: the client sends its position; the host periodically teleports its own *invisible* perception
     anchor, or biases specific creatures by writing their target field [?] when the client is closer than the host. Needs
     the target field of the AI instance data, not yet located (one RAM diff of an enemy switching between Rynn and a
     decoy would find it).
  2. **Controlled-actor swap on the host**: for the duration of one creature's tick, put the partner's ghost into
     `ctx+0x4b0` (as `0x243620` does for its camera attach), so that creature's perception sees the nearer player. A
     class-`0x10009` ghost with a swapped handler is not the controlled player and has no HP of its own, so damage must be
     routed (ghost-vs-creature hits are a net event, `actor-spawn.md` section 7).
  Phase 3 option 1; option 2 only if 1 is not good enough.
- Damage to the partner: creatures hitting the ghost on the host are swallowed (inert ghost). The host sends "partner took
  N damage" to the client, which applies it to its own global HP block through the game's damage entry `0x314ab0` so
  the existing hit flash / death logic runs.

### 2.5 Shared exits

An exit is `Level_Load` plus a level-change save. Rule: **exit = ready-up**. When a player steps on an exit trigger, the
instance does not load at once; it sends `ExitRequested(levelId)` to the host and shows "waiting for partner". When both are
at the exit (or the other has agreed within a timeout) the host sends `ExitGo(level)` and both call `Level_Load` together.
That needs a hook before the exit's `Level_Load` to defer it (`lifecycle` has `beforeLevelLoad` only as a notification, so
the deferral hook has to be at the exit trigger's message, **[?]** 9099 per the brief). First cut: do not defer, let any
player's exit pull the other one along (`ExitGo` on the exiting player's `beforeLevelLoad`), which needs nothing new in the
guest. A player who is mounted or in a cutscene postpones the pull until control returns.

### 2.6 Death, revive, saves

- **Death**: single-player death reloads the last save, which would drag the other player's world with it. In co-op, the
  dead player becomes a spectator ghost of the partner (camera follows the ghost) until the partner reaches a checkpoint,
  uses an elixir on them, or both die (then reload on both). Revive = host message "revive N at pose P with HP H" ->
  the client writes its HP block. The level reload only happens when both are dead. **[?]** how to suppress the engine's
  own death handler on the client (Rynn's damage path `0x314ab0` ends in the death logic).
- **Saves**: each player keeps their own save slot (their inventory and stats are global and personal). The host owns the
  level state: a client never writes a level-change save into the host's world, and loading a save during co-op is a
  host-only action followed by `ExitGo`-style reload of both. Ghosts and proxies are not serialised: `drakan_lifecycle` already
  destroys ghosts before a save write; proxies are real creatures, so the client's save contains their (host-written)
  positions. Joining mid-level needs a catch-up snapshot of all discrete state (open doors, taken items, dead creatures),
  built from the event log, because there is no whole-machine snapshot (`multiplayer.md` option (a)).

## 3. Phased plan

| Phase | Content | Needs |
| --- | --- | --- |
| 0 (done here) | Creature proxies: identity keys, position / yaw / HP replication, tick swallowed on the client | `drakan_coop` |
| 1 | Animation (clip id + time per creature, as for ghosts); sector update after the write; replay of host deaths on the client (`destroyActor` with flush at a PadMgr boundary) and of "creature destroyed" in general; keep state across the 150-tick settle window | `drakan_coop`; `drakan_lifecycle` listener context for destroy |
| 2 | Hit claims: client weapon hits on a proxy become a `CreatureHit` claim; the host applies HP damage (writes `q+0x24c`, or calls the creature's own damage entry [?]) and broadcasts the result. Partner damage and revive messages | hook at the hit application `0x498020` (victim = a proxy) -> net event; `MatchServer` hit validation reused |
| 3 | Event layer: object-id keyed state changes (doors, levers, pickups, trigger messages, STOMP start / end), exits as ready-up, catch-up snapshot for late joiners | message dispatch hook [?], per-class state fields [?], `Protocol` messages (reliable) |
| 4 | Targeting (2.4), dragon policy, death / revive policy, saves policy, a coop mode in `MatchServer` (`MatchMode::Coop`, no frag limit, shared health display) | decompile work on AI target fields |

Priority is what makes the first multi-player session fun without cheating: phases 1 and 2 (kills count for both), then 3.

## 4. Prototype: `drakan_coop` and its results

Files: `ps2recomp/ps2xRuntime/src/lib/drakan_coop.{h,cpp}` (new); registration is `#include`, `install()` and
`beforePadMgr()` lines in `drakan_input_glue.cpp` and the source in `ps2xRuntime/CMakeLists.txt`. Harness:
`scripts/harness/coop_two_instances.py` (private folders `build/coop-a` / `build/coop-b`, own memory cards; reuses
`netplay_two_instances.py` helpers). Nothing in `drakan_ghost.*`, `drakan_netplay.*` or `runtime/net` was edited; `drakan_coop`
uses `net::UdpTransport` as a plain consumer on its own port.

Environment: `DRAKAN_COOP_HOST=<port>` (host), `DRAKAN_COOP_CONNECT=<host:port>` (client), `DRAKAN_COOP_MODE=mark|follow`
(client; mark writes nothing to the guest and only measures), `DRAKAN_COOP_SWALLOW=<hex>` (event mask a proxy ignores,
default 4 = per-tick).

How it works:
- **Identity.** A level-placed creature has no id on its handle. Registry order is allocation order, which is the level
  list order, so the k-th creature of a resource id on the client is taken to be the k-th on the host
  (`key = hash(resourceId, k)`), computed once, 150 PadMgr ticks after a level load, from the registry snapshot
  (`world::readSnapshot`, types 6 / 7 with HP). Creatures that appear later (summons) have no key and are not synced.
  Handle recycling is guarded by re-checking the resource id (host) and the registry slot (client, `isLiveHandle`).
- **Wire.** One unreliable datagram type: `u8 type, u32 levelHash, u32 seq, u16 count, count x {u32 key, f32 pos[3], f32 yaw,
  i32 hp}` (24 bytes per creature, 45 per datagram), every 100 ms; 137 creatures are 4 datagrams = about 14 KB/s. The client
  drops a packet whose level hash is not its own and clears its inbox when the host changes level.
- **AI suppression.** The creature's behaviour handle (`*(actor+0xfc)+0x14`) is replaced with a host handler registered in a
  free function slot (the ghost technique). It swallows event 4 (per tick) and forwards every other event (init, destroy,
  messages) to the original handler, which is remembered per actor, so objects are still built and torn down normally.
- **Follow.** Each tick the client eases position toward the host's (exponential, factor 12/s, snap above 3000 units),
  writes a pure yaw rotation and the host's HP. A hit on the proxy therefore changes its HP for at most one tick.
- Everything runs on the EE thread at the `PadMgr_Update` boundary except the socket (own thread, never touches guest
  memory). Level load / save restore clears all state (`drakan_lifecycle` before events); the intro cut scene is ignored.

Results (`python scripts/harness/coop_two_instances.py [--mode mark]`, A hosts, B connects, both New Game into Surdana,
A walks forward; logs in `build/harness/coop-{follow,mark}/`):

| Check | Result |
| --- | --- |
| Identity keys | host and client both key **137 creatures**; 137 / 137 match **[run]** |
| Key quality (client vs host position at first match, before any write) | mean 13-25 units; 130-133 within 100 units, 4-7 within 1000, none far in follow runs, one at 2333 in an earlier run (a creature that wandered before the key tick on one side) **[run]** |
| Traffic | about 38 datagrams/s, 1736 datagrams in 45 s, no loss visible on loopback **[run]** |
| AI divergence without suppression (mark) | mean error to the host: 13 units at start, 40 after 10 s, **128** after 40 s, max 4383, 6 creatures over 200 units. Confirms that two worlds drift, and that proxies are needed **[run]** |
| Follow | 137 / 137 proxies swapped, **no crash**, clean exits on both instances; steady-state error mean **0.1-0.6 units**, max 17-29, none over 200 **[run]** |
| Host side | 12 of 137 creatures moved more than 50 units in 45 s with the host player walking; the rest idle |

Not verified: whether proxies still animate (the screenshots were not inspected for this; the per-tick event may also drive
the skeleton update, in which case clips must be written like ghosts), whether the sector structure goes stale after
position writes (collision of the client's own sword with a proxy depends on it), what a proxy does when hit, and anything
about a creature's death (the host's death never reaches the client: a dead proxy's HP is written but it is not
destroyed). Use mark mode first when changing identity keys.

## 5. API the other agents' files would need

- **`drakan_lifecycle`**: a way to run a callback with a valid `Event` context at the next `PadMgr` boundary (not only in a
  lifecycle callback) so `destroyActor(e, h, flush)` can replay deaths and exits; today `destroyActor` needs an `Event`.
  Also a `beforeExit(target)` notification distinct from `beforeLevelLoad` for the ready-up exit.
- **`drakan_ghost`**: `spawnAt` is enough for the partner; add `setMounted(id, bool)` / a dragon ghost class so a partner on
  Arokh is visible; and a `isGhost(handle)` query so hit hooks can tell a ghost from a creature.
- **`runtime/net` protocol**: reliable `CoopEvent` messages (`u8 kind`, `u32 objectKey`, `i32 value`, bounded payload) for
  door / pickup / trigger / exit / creature-death events; `CreatureHit` (C>S claim, `u32 key`, `u16 damage`, `u8 weapon`) with
  host validation identical to `Hit`; `Revive`; `MatchMode::Coop` and an `ExitRequested` / `ExitGo` pair. Creature
  snapshots would then move from the private `drakan_coop` datagram to a `CreatureSnapshot` message on the same transport
  (one socket, one session, the same `levelHash` rule).
- **`world`**: a per-creature `animation` read (like `readPlayerPose` for any handle) and the placed-object `id`
  per handle if it can be recovered (it would replace the ordinal key with the real level object id of
  `level-format.md`; the id is not on the handle, so a record of "id order = registry order" must be verified per level).

## 6. Risks and open questions

- Ordinal keying relies on identical level instantiation order on both sides. It held on Surdana; test other levels,
  and a save-restore (which recreates actors in saved, not level, order: keying would then have to use saved order or the
  level object id).
- Creatures that streamed in or were spawned by trigger scripts after the key tick are unsynced until the event layer exists.
- Swallowing every per-tick event may also stop timers that the creature's own scripts use (burning, stun); phase 1 should
  swallow only the AI part once located.
- Latency is not simulated on loopback; the 100 ms send interval plus smoothing gives about 150 ms visual delay.

## 7. Phase 2: two players fight through a level (2026-10-31, later the same day)

Code: `ps2recomp/ps2xRuntime/src/lib/drakan_coop.{h,cpp}` (rewritten around the prototype), one changed call in
`drakan_input_glue.cpp` (`beforePadMgr` takes the guest interface now). Harness: `python scripts/harness/coop_two_instances.py
[--scenario fight|door|pickup]`, private folders `build/coop2-{a,b}`, output `build/harness/<name>/` (logs and screenshots).
Both instances run `drakan_netplay` (the partner's ghost; `DRAKAN_PVP=0`) **and** `drakan_coop` on a second UDP port. Tags:
**[game]** seen in the two-instance harness, **[dec]** Ghidra decompile read, **[inf]** inference.

### 7.1 What was found

* **One message carries all damage.** Every hit goes through the engine's message send `0x171150` (vtable `+0x14` of the object
  at `ctx+0x33c`): `(this, sender, target, msg*, 0x34, 1)`; a damage message has type `0x10db` at `+0` and the damage at `+0x10`
  [dec]. Hooking that one function (`replaceFunction`) sees player hits on creatures, creature hits on the ghost (the ghost is
  filed in the collision cells, so creature sweeps find it) and anything else, with no need for the blade hit hook. **The sender is
  the attacker's weapon actor**, not the attacker: the local player's are the actors behind the hand holders (`data+0xeb4` /
  `+0xe90`, actor ref at `+0x1c`); a creature's is its own weapon actor [game: `damage msg: sender 00a7beb0 ... dmg 4` for
  Rynn's dagger, `sender 00da94a0 ... dmg 10` for an orc's].
* **Creature death is in the damage path**, not in the tick. A proxy whose HP is simply set to 0 stays alive. A real damage
  message makes the creature's own handler run its death (collapse animation, blood, loot) [game, B's screenshots: the proxy
  collapses and a dropped weapon appears]. Corpses stay in the registry (the host's too), so the client does not remove them
  (`DRAKAN_COOP_DESTROY=1` does, after 5 s).
* **Proxies must be refiled.** A host write of `actor+0x14` does not move the actor in the collision cells (same as the
  ghosts, `pvp.md`): the local blade found nothing until every proxy that moved is filed again (`drakan_ghost::refileActor`,
  at most 24 per tick) [game: no claims before, claims after].
* **How creature AI picks its target.** Every perception read goes through `ctx+0x4b0` (135 readers, `actor-spawn.md`). Swapping
  `ctx+0x4b0` to the ghost for one creature tick **crashes the guest** (`guest-branch:missing-target`, JALR to 0 at `0x33f6c8`, in
  the command code the AI shares with the player's input; the ghost's data object is never initialised because its handler
  swallows the init event) [game]. What works is moving the *local player's body* (`+0x14`, 0x30 bytes of position and rotation)
  onto the ghost for the length of that creature's tick and putting it back: perception, chase and attack range all see "the
  player" at the ghost, the attack sweep hits the ghost's collision filing, and the damage message arrives at the ghost [game].
  Side effect: the same sweep also reaches the host's real body (it stands there for the tick), so a damage message to the
  controlled actor during such a tick is swallowed (`hurtSwallowed`) and only the ghost's copy counts.
* **Per-creature target fields were not looked for**: the position trick makes them unnecessary. Rule: the nearest of {local
  player, ghosts} wins with 20 percent hysteresis (a ghost is picked when it is under 0.8 times the player's distance), and only
  ghosts within 6000 units count (forcing a far ghost on all 127 creatures hung the guest in an earlier run).
* **Doors.** Class `0x10013` (Town Door, Desert Gate Door, GF Wood Door; 41 in Surdana, `type 8`) **opens by itself** when the
  controlled actor comes near: no message is sent for it (probe: nothing on walking up or on the use key). `handle+0x9c` goes 1
  (closed) -> 2 (awake, within about 7500 units) -> 0x1e / 0x18 (opening, within about 3900 units); `data+0x10` 0 -> 1 and
  `data+0x8` -1 -> 1 (opened), `data+0x1c` is the swing angle and the handle matrix (+0x20) follows it;
  `data+0xec/0xf4/0x108/0x10c` track the player's position (noise). The door's tick reads `ctx+0x4b0` like the AI does, so it gets
  the same wrapper (position trick): with the partner's ghost at the door and the local player far away, the door opens.
  **Verified: A walked to the door, B stood still about 7500 units away and B's door went through `handle+9c 1 -> 0x18` and
  `data+10 0 -> 1` [game, `--scenario door`].** Message replay (`DRAKAN_COOP_MSGS=<hex types>`, reliable `ObjMsg`) is implemented
  for levers / triggers but unused: no class needing it was found yet.
* **Pickups** (class `0x1006d` treasure, 47 in Surdana, and the loose type 11 items: elixirs `0x1000a`, potions `0x10059`,
  `0x10075`, `0x10021`, `0x1002d`, `0x10090`; the inventory objects are type 11 too but all sit at one parked position and
  are excluded): a taken pickup leaves the registry. Either side reports it (`ObjGone`) and the other removes its copy
  through the kill list (`0x1413c0`, flushed by the engine's frame tail). Verified with a stand-in for walking over the
  treasure (test aid `DRAKAN_COOP_TEST_TAKE` destroys the first live treasure on A through the kill list): B logs
  `pickup ... taken by the partner: removed` [game, `--scenario pickup`]. A real walk-over was not staged. Loot spawned by a
  death exists on both sides (each runs the same death); those drops get keys by order of appearance per (class, resource)
  (`new pickup` lines), a heuristic that holds only if both worlds drop in the same order. Not verified.
* **Chests, levers, triggers, STOMP, exits**: not done. Chests (class `0x100c5`, 22) probably open like doors (proximity) or by
  the use key; no lever or switch class appeared in Surdana's class list; triggers (`0x1003d`, `0x1003e`, 346 timers and
  detectors) read the controlled actor as well and would follow the position trick if wrapped.

### 7.2 What works (checks of `--scenario fight`)

| Step | Evidence |
| --- | --- |
| B's dagger hits a proxy -> claim | `client: claim on creature ... damage 4` (damage message hook, local weapon actor) |
| Host validates and applies | `host: claim applied to creature 00aa7b80 ...: damage 4, hp was 40` (posted `0x10db` from the ghost as sender, x8 test scale: 40 -> 12 -> dead) |
| Kill replicates | `host: creature ... died (hp <= 0) -> client`, then `client: creature ... died on the host: own handler restored, lethal hit follows`; the proxy collapses and drops loot on B |
| Enemies pick the ghost | `host: creature ... now targets the partner's ghost (ghost 789, player 991 units)`, 500 to 6500 ghost ticks per 5 s |
| Client takes damage from host enemies | `host: ghost ... hit by 00da94a0 for 10 -> client`, `client: took 10 damage from a host enemy (hp 84)`, down to hp 4 (never lethal: clamped to 1) |
| Both attack the same creature | A (host, real hits) and B (claims) wore down the same orc; the host's death event reached B (screenshots `build/harness/coop2-fight1/b/b_f2x.png`) |
| Clean exits | both instances quit through their recipes; no crash |

Runs: `coop2-fight1` and `coop2-fight2` passed 9 of 9 checks; `coop2-fight` (same code) passed 7 of 9 (no claims: B's lock-on
picked A's ghost as its target). The fight is not scripted, so expect that variance. Test aids (env, harness only):
`DRAKAN_COOP_TEST_GOD` (host HP never drops), `_FORCE_GHOST=all` (host creatures in range always pick the ghost; makes the fight
reproducible because both players start together), `_SUMMON=<res>,<n>@<file>` (move creatures to the players; off by default,
the creatures moved this way did not engage), `_GOTO=x,y,z@<file>` (teleport the player: **does not work across the map**, the
engine puts the player back when no floor is resident), `_TAKE=<file>`, `_CLAIM=<ms>`. The fight harness builds two
Rynn-beside-the-Yellow-Orcs saves from the dagger card with `combat_save_fixture.py` (A 970 units from the nearest orc, B 570).

### 7.3 Known problems and not done

* **Item 5 (move onto `drakan::net` MatchServer/MatchClient) was not done.** The reliable channel of `UdpTransport` already
  carries every event on `drakan_coop`'s own socket and the ghosts come from `drakan_netplay` on another; moving the creature
  stream into a `CoopSnapshot` message plus a co-op `MatchMode` means a `kProtocolVersion` bump (it is 5) while another agent
  edits the net and PvP code. Nothing under `runtime/net` was touched, no net tests changed. Cost: two sessions must be started
  (two ports; the multiplayer menu only hosts the netplay one).
* **Events sent before the peer has keyed the level are lost** (the inbox is cleared on a load, and events are only applied
  once the level is keyed). Fine when both load together; a late joiner needs the catch-up snapshot of section 2.6 (dead
  creatures are covered by the HP stream: `hp <= 0` makes a proxy die; open doors and taken pickups are not).
* The client never dies from host enemies (damage is clamped to HP - 1: the engine's own death reloads the last save, and a
  co-op death policy is phase 4). The host player dies normally.
* Hits claimed by the client use the damage the local engine computed (cap 400, claimant's ghost within 3000 units); arrows and
  spells were not tried. One claim is applied per boundary (posted, never nested, see `pvp.md`).
* A locked door that the partner could not open locally: whether the position trick would open it was not tested (the Town
  Door used has no lock).
* The position trick runs creature ticks with the player body moved: if a creature tick ever yields to another guest thread,
  that thread would see the body at the ghost for a moment (not seen in these runs).

### 7.4 Phase table update

| Phase | Status |
| --- | --- |
| 1 (animation, deaths) | deaths and loot replay done (7.1); proxies animate on their own; events wait for the level to be keyed |
| 2 (hit claims) | done (damage-message hook instead of `0x498020`) |
| 3 (events) | doors (proximity wrapper) and pickups (registry loss) done; levers, chests, triggers, exits, catch-up snapshot not |
| 4 (targeting, policies) | targeting done (position trick); dragon, death / revive, saves, `MatchMode::Coop` not |
