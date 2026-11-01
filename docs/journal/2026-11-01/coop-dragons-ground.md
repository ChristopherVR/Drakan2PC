# Co-op dragons: ground-level mount / dismount and three players - 2026-11-01

Follow-up to [coop-dragons](../2026-10-31/coop-dragons.md). Code: `drakan_netplay.cpp` (receiver), `drakan_ghost.cpp`.
Harness: `scripts/harness/coop_dragon_ground.py` (base port 28000; scenarios `mount`, `three-ground`, `three-ravens`).

## Changes
* **Dismount keeps the dragon.** When a partner's pose flips from mounted to on foot the dragon ghost is no longer destroyed:
  it is *parked* at its last mounted pose (log `dragon of player N parked (dismounted)`), the rider body is already the
  partner on foot (so the rider detaches), and a re-mount reuses the body (`re-mounted`). A partner leaving the level or a
  local load still destroys it (`hide`). "Flies off" is not modelled: the dragon stays where the partner left it, which
  matches the partner's own world.
* **Surdana clip bug fixed.** The dragon ghost rewrote every clip id with prefix 0x2 to the Ravenshold guess 0x4xxxx, which
  is wrong in a level whose SYSTEM database index is 2 (Surdana: Arokh's clips really are `0x2xxxx`). The ghost now takes the
  level's own SYSTEM prefix (`g_levelSysHi`, set at each spawn); `DRAKAN_DRAGON_CLIP_HI` still forces a value.
* **Test aids** (env, receiver unaffected): `DRAKAN_COOP_FAKE_MOUNT=on1,off1[,on2,off2]` (seconds after the first pose) makes the
  local instance *publish* a mount on a dragon at `DRAKAN_COOP_FAKE_MOUNT_OFFSET=x,y,z`; `DRAKAN_DRAGON_TEST_SPREAD=1` draws
  partner N at N times `DRAKAN_DRAGON_TEST_OFFSET`.
* **Own dragon for joiners.** Every machine loads the level itself, so every player already has the level's parked Arokh locally
  (Surdana, Kragmor, Catacombs, City, Andrellian Isles, Ravenshold, Tundra, Shiverbane, Valley, Stratos); nothing is shared, so
  no extra spawn is needed. Levels without one (Inquisitor's Lair, Shadowmire, Crystal Mine) have no dragon for anybody.

## Verified (build with the changes, runs in `build/harness/dg_*`)
* `mount` (Surdana, 2 players): B logs dragon spawned once (clips 20663), parked twice, re-mounted once; screenshots show Rynn on
  foot, then the dragon with the partner as rider. All PASS.
* `three-ground` (Surdana) and `three-ravens` (Ravenshold, real mounts): every one of the 3 instances spawns two partner dragons and
  two rider bodies, local rider/dragon link `intact`; screenshots show two partner dragons with riders on every screen.

## Not proven
* A real mount / dismount in a ground level: Arokh is parked at y 200000 and no script teleport exists, so the sender side
  (`mounted` flipping because the player really walked to Arokh and mounted) is only exercised through the fake-mount aid. The
  receiver path is identical (it only sees `mounted` flip).
* Parked dragon keeps its last frozen flap pose (no landed/idle clip chosen). Fake-mount offsets put dragons very close to the camera.
