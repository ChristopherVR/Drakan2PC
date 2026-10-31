# Co-op dragons, stand-in combat and chest loot keying - 2026-10-31

Follow-up to [coop-characters.md](coop-characters.md) (design: the host plays Rynn, joiners pick another character, every
player has their own dragon). Code: `drakan_netplay.cpp` (mounted partners), `drakan_ghost.{h,cpp}`, `drakan_coop.cpp`,
`runtime/net` protocol v8, `runtime/world` pose. Harnesses `scripts/harness/{coop_dragon,coop_standin}.py`.

## 1. Dragons in co-op

**What a mounted sender publishes.** While the local player rides Arokh the controlled actor is the dragon, so the existing
pose is Arokh's (position, 7 skeleton channels, Arokh's own clips); Rynn is the passenger and is not posed. The wire gets
what a flying dragon needs, **protocol v8**: `PlayerState.pitch / roll` (the pose is `Ry(yaw) * Rx(pitch) * Rz(roll)`,
quantised to 1e-4 rad) and `clipHi` (high half of the clip ids when it is not the usual 0x2; 0 = 0x2). `Snapshot` with 16
players no longer fits 1100 bytes, so `kMaxPacketPayload` is 1280. The server's coordinate sanity limit was 1e6 and silently
dropped every state of **Ravenshold (x = 1.3e6)**: now 4e6 (any level above that was never co-op capable).

**What a receiver builds.** Per mounted partner two bodies: a **dragon ghost** (`drakan_ghost::spawnDragon`, record = the
level's SYSTEM database prefix | 0xa60) replaying the dragon pose and clips, and the partner's **body ghost as rider**
(Rynn's own body or the chosen stand-in), placed at the saddle offset in the dragon's frame and playing Rynn's "Ride" clip
(a stand-in maps it to its idle). The ids of Rynn's / Arokh's records and of every SYSTEM clip carry the level's database
index: `0x2xxxxx` in Surdana, `0x4xxxxx` in Ravenshold (the DRAKAN_DRAGON_CLIP_HI guess is only a fallback for the mirror test
aid). `drakan_netplay` no longer hides a mounted remote; when the partner dismounts the dragon ghost is destroyed (Arokh then
stays behind in the partner's own world only; a parked standing copy is not replayed).

**The local link survives.** Every ghost spawn logs `[ghost] link check`: the controlled actor's `data+0x4d8` (rider /
dragon ref), the actor at the other end and its own `+0x4d8` are read before and after the factory call; in all runs the
result is `intact` (body ghost and dragon ghost, mounted on both machines).

**Saddle offset** (`DRAKAN_DRAGON_PROBE=1`, local rider in the dragon frame): about (-8, 330 +/- 55, 0), the y wobble is the
dragon's flap; rider heading equals the dragon's. `DRAKAN_DRAGON_RIDER_OFFSET=x,y,z` / `DRAKAN_DRAGON_RIDER_YAW` override,
`DRAKAN_DRAGON_TEST_OFFSET=x,y,z` draws a mounted partner that far off (test aid: two dragons that start on the same spot).

**One controlled actor per machine.** A second *flyable* dragon is not possible (`ctx+0x4b0` / `+0x4b4` is the single
controller pair): every player flies their own Arokh locally and sees the others as visual copies. Mount and dismount
happen on the partner's machine; the receiver only sees `mounted` flip (a dragon ghost appears / disappears).

**Verified** (Ravenshold, both start mounted; `coop_dragon.py`; runs `build/harness/drag5..drag9`, git-ignored): both logs show
`dragon of player N spawned`, link check `intact` for body and dragon ghosts, screenshots on both screens show the partner's
dragon beside the local one with its wing clips running in step (`drag9` with `DRAKAN_DRAGON_TEST_OFFSET=1100,150,0`, `--hover-a`).
The rider is the grey figure at the neck base of both dragons (the engine's own Rynn is just as small from the flight camera).
Not tested: mount / dismount transitions in a ground level (Surdana: the parked Arokh sits at y 200000), three players.

## 2. Stand-in combat

`coop_standin.py` (one instance, `DRAKAN_GHOST_MIRROR=1`, stand-in beside the player, `DRAKAN_GHOST_ACTS=idle,run,attack,hit,
death,block` cycles the mapped clip of each activity for 3 s): the stand-in (Capn Serris, class 0x1798) plays its own clips
for each activity: idle 0x50df7, run 0x50dfa, attack 0x50df5, hit 0x50df6, block 0x50df9, death 0x50df4 (log lines
`[ghost] id N channels ...` list each change). Close-up screenshots (`build/harness/sin1/sheet.png`) show a clean attack
swing, the hit recoil, the block, and the death collapse (kneeling, held at 99.9 percent of the clip) with legs and arms moving.
Over the network the same mapping runs on Rynn's attack / hit / death clips from the partner's pose.

**Weapons: not done.** The weapon record resolves (`33cbc` -> index 0x4b9) but an NPC model's property block is laid out
differently from Rynn's: the "hand bone" read through `*(h+0x54)+4` returns garbage (0x3f8ccccd), so the item is not
attached (guarded, logged `weapon record ... unusable`). The NPC models have a `weapon` node (Villager: 30, found in the
0x0208 node names) but the node index per model is not in the roster table yet; a generator extension (node index per class)
plus calling `0x13b878(model, item, node, 0, 0)` with it is the way.

## 3. Chest loot keyed at spawn

Loot keys used to be handed out by `scanNewObjects` in order of appearance, so an item taken the moment it appeared (a player
standing at the chest) was never keyed and the partner's copy stayed (coopv-chest2f 7/8). Now `hookSpew` keys the chest's
children (`actor+0x90` refs / `+0x98` count) right after each spew call as **(chest key, index in the child list)**
(`keyChestChildren`; also run before every scan and from `pollChests`), and the spew runs inside a no-preempt section
(`DRAKAN_COOP_SPEW_PREEMPT=1` turns that off).

Found on the way: the host's **gold generator hands the loot of every chest in its table to whichever chest spews first**, and
two adjacent chests spew in the same frame in an order that differs per machine, so the same children were listed under
different chests on the two sides (one chest's list looked empty). `ObjMsg` spew messages now carry a host spew sequence
number (`w[3]`) and the client spews in the host's order (waits for earlier spews it has been told about, 10 s limit).

Result (`--spot 614272,150000,505400`, the two-chest room; harness `loot keys:` line now counts the keyed-at-spawn lines):
`chesthost` 8/8 with 9 identical keys on both sides, including the loot the host's own player took the instant it appeared
(`build/harness/rh-chesthost`). `chest` (the client's player opens it and stands next to it) is 7/8: the client's spew yielded
mid-way in that run (`spew call` line missing: the unwound call skips the hook's post-processing) and the children had left
the chest's child list (they are removed from it once thrown) before the next keying, so the client never keyed them (3 of 7
keys). The remaining fix is keying at creation (a hook on the actor factory 0x181ad8 attributing items to the nearest
spewing chest was tried: it works when the factory call returns through the hook but misses calls that yield; not kept).

## 4. Regressions (this batch, `build/harness/rh-*`, port 47912, private folders `build/cx*`)

2 players: `chesthost` 8/8, `fight` 9/9, `latejoin` 8/8, `door`, `pickup`, `gold` all checks pass (4/4 each); `death` 12/13
(`a: claims applied to the real creature` missed: B's swings met A's ghost instead of an orc, the known flake of this scenario);
`chest` 7/8 (above). 3 players: `fight` 16/19 and `death` 27/28 (only B's / player 2's claim checks missed, C's pass),
`latejoin` 13/13. A first batch with protocol v8 failed every claim check: `kEntriesPerPacket` in `drakan_coop.cpp` was derived
from `kMaxPacketPayload`, so raising it to 1280 made the host send 52-creature datagrams that the 45-entry decoder rejects
(the guest saw 23 of 127 creatures); now capped at `kMaxCoopCreatures`. The pre-change exe passed `fight` 9/9 as the baseline.
