# Ghost spike S2: the pose model (record and drive) - 2026-10-31

Spec: `actor-spawn.md` section 5 (kinematic drive) and section 8 S2. This note covers the platform-free half: reading the
local player's pose and replaying it. The guest-side writer belongs to the ghost agent (`drakan_ghost.cpp`).

## What was built

`runtime/world/include/world/Pose.h`, `src/Pose.cpp`: `readPlayerPose`, `interpolatePose`, `PoseBuffer`, duck-typed
`toPlayerState` / `fromPlayerState`. Tests: `world_pose_test`, `world_pose_ram_test`, `net_pose_bridge_test`.
Layout, offsets and confidence tags: `docs/reference/world-model.md` section 6.

## Findings that change the spike plan

1. **Two clips matter, not six.** Rynn's skeleton has 11 channels (13 to 19 with the bow or held items) but only two
   distinct clips are ever active: the channel of node 0 (root + legs, locomotion) and the channel of node 2 (spine, arms).
   They are equal while idle or walking and differ during attack / block / aim. A ghost needs `lower`, `upper` and a mask.
2. `sk = *(handle+0x50)`; the dual-wield "model" (`data+0x80`) is the handle itself; the instance data holds the attack
   flag (`+0x94 & 0x80`) and the weapon holders (`+0xeb4` right, `+0xe90` left).
3. Mounted = the controlled handle is class `0x1001c`; Arokh uses the same channel layout (7 channels, clip `0x40663`).
4. `net::PlayerState` has one `animId` (u16): enough for locomotion, **the upper-body clip does not fit**. The net layer needs
   a second animId / time (about 6 bytes) for attacks to show on a ghost. Until then the bridge copies lower to upper.
5. The weapon on the wire is a placeholder (low byte of the resource id); the net layer needs a real weapon table.

## Hooking it up (drakan_ghost)

* Local side, once per update (EE thread, the existing `beforePadMgr` hook, 20 Hz is enough):
  `readPlayerPose(mem, globals, st, nowSec, tick, p) == PoseStatus::Ok && !p.mounted` then `toPlayerState(p, state)` and send.
* Remote side: `buf.push(fromPlayerState(received, arrivalSec))` on each packet; each frame
  `PlayerPose g; if (buf.sample(nowSec, g)) { ... }` (`PoseBuffer` renders 100 ms behind).
* Drive: `g.position` / `g.rotation` -> `drakan_ghost::setTransform` (actor `+0x14` / `+0x20`, then the sector update
  `0x1494c8`). `g.lower` / `g.upper` -> `drakan_ghost::setAnim`: write the clip id at `channel+0x50` and the time at
  `channel+0x58` for every channel of the ghost's `sk+0xe8` array; channels whose bit is set in `g.lowerMask` get
  `g.lower`, the others `g.upper` (for a ghost of the same model; after `fromPlayerState` the mask is all ones, so
  everything gets `lower` until the wire carries the upper clip). Write before the pose update `0x103268`.
  Clip change snapping is already done by the buffer.
* Root motion still moves the ghost by the clip delta: hook `0x17ea70` for ghost skeletons, or accept the drift (the
  buffer rewrites the position every frame, so drift is bounded to one frame).

## Recording

`world_pose_ram_test --json build/pose/poses_from_dumps.json <dumps>` writes the poses of any RAM dumps as a JSON array
(`t`, `pos`, `yaw`, `vel`, `lower` / `upper` {clip, time}, weapons, flags). The file in `build/pose/` holds seven poses from
existing dumps (idle, strafe, attack frames, bow, Arokh) with synthetic 50 ms spacing; they are discrete frames, not a
continuous run, so the velocity is 0. **A continuous stream from a real run was not recorded** (it needs a game run and a
`readPlayerPose` call site on the EE thread, which belongs with drakan_ghost). Probe scripts: `build/pose/*.py`.

## Not verified

* The facing convention of `yaw` and the clip time unit in game. The clip start API is still unknown, so the ghost relies on
  writing the clip id and time every frame.
* The interpolation constants (100 ms delay, 250 ms extrapolation, 1500 unit teleport) are untuned.
