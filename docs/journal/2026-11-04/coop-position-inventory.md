# Co-op position replay and inventory-facing investigation

## Observed defects

Two instances launched directly from `dist/DrakanPC`, with separate private memory cards and UDP port 47983.
Evidence is private under `build/harness/coop-position-baseline/`; the temporary driver is
`build/coop-position-probe.py`. No original saves were changed.

* **Inventory preview leaks into replication [RAM, game].** A's gameplay yaw was -1.463415686 radians. Opening its
  modern inventory changed the live actor to +1.678176995 (a pi-radian turn). B's ghost of A then used
  +1.678176966. B's own player stayed at -1.463415686, and A's ghost of B also retained that gameplay heading.
  This isolates a sender-side preview leak, rather than every recipient actor being rotated by A's inventory draw.
  The native preview is the actual world actor, not a separate studio model. A's screenshots confirm the normal
  rear gameplay view and front inventory preview. The inventory also changes actor height slightly.
* **Replay depends on receiver cadence [code, synthetic regression].** The old playback cursor advanced by
  `1 + clamp(lag * .05, -.5, 3)` per receiving PadMgr callback. Its minimum advance of half a sender tick cannot
  follow a sender running at less than half the receiver's rate, and it can run beyond the latest received pose.
  PoseBuffer then extrapolated up to .25 sender-tick seconds using velocity measured in wall seconds. Those units
  differ whenever PadMgr does not run at exactly 60 Hz. The baseline ran uncapped at over 200 world updates/s.

## Corrections

* Keep the last gameplay position and full rotation for outgoing network poses while the native inventory-open
  stats flag is set. Preserve current animation, equipment, tick and level data. Zero outgoing velocity during
  the preview and on the first sample after closing. The native local preview remains free to turn normally.
  Reject an initial preview without a known gameplay transform; invalidate cached transforms on actor replacement,
  mount changes and lifecycle resets. Mounted dragon poses bypass the on-foot preview filter.
* Advance remote replay by elapsed time and an estimated sender tick rate, with an exponential phase correction.
  Sender ticks remain the interpolation coordinate. Bound playback by the latest received tick and disable
  network PoseBuffer extrapolation, avoiding invented movement and the velocity-unit mismatch. Re-anchor after
  a long receiver stall rather than slowly replaying an obsolete backlog.

## Verification

* `world_pose_test` passes: inventory entry/exit, preserved equipment/tick, unknown initial preview, actor replacement,
  mounted exclusion; sender and receiver rates of 30/60/240 Hz, no playback beyond the newest state, stationary
  convergence and recovery after a stall. Existing reader, interpolation and wire-bridge assertions also pass.
* CTest: `world_pose_test`, `net_pose_bridge_test`, `net_coop_test`, `net_loopback_e2e_test`: 4/4 pass.
* Repository guard passes: no prohibited tracked files.
* Canonical incremental builds compiled and linked successfully, but refreshing the main play folder encountered
  another running review session's lock on `dist/DrakanPC/avcodec-61.dll`. The same build entrypoint deployed an
  isolated playable folder at `dist/DrakanPC-coop-investigation` with HD textures and generated motion assets.
* First corrected Rynn/Rynn run: `build/harness/coop-position-fixed/`, both clean script exits. A's native inventory
  RAM yaw was +1.678176995 while B's ghost of A retained -1.463415687; A's ghost of B also kept its world heading.
  This confirmed open-inventory isolation but caught a **single close-boundary sample** still publishing +1.678.
  The inventory flag clears one PadMgr boundary before the saved transform is restored. Retain the cached transform
  for that first flag-cleared sample too; a focused test now explicitly covers that ordering. This first run is
  not the final close-boundary acceptance.

## Final live acceptance

`build/harness/coop-position-final-npcs/`, Male Villager host / Capn Serris client, executable SHA-256
`D2EC714402A27F06215FB1C980E57D01666AF81E1C92685B4DF52496525A1EE9`. Both instances exited through their scripts
with code 0. The same canonical build script refreshed the isolated deployed play folder; no manual binary copies.

* Every published local yaw on both instances stayed at -1.463 (log precision), including opening, the entire inventory
  interval, closing and subsequent gameplay. The earlier one-boundary pi-radian leak is absent.
* During A's inventory, its controller **and local cosmetic body** had the intended preview yaw +1.678176995. B's
  remote body of A retained world yaw -1.463415865. A's remote body of B retained -1.463415687. Local preview rendering
  is intact while network facing stays independent. Inspected A's native inventory image and B's gameplay capture.
* After movement settled, A's ghost on B had x/z **476379.28125 / 489122.0625**, matching A's gameplay x/z exactly.
  A's ghost of B had x/z **477163.28125 / 487078.90625**, matching B exactly. The compared samples were captured at
  different instants; their y differences were 0.1875 and 0.03125 game units during idle animation. Do not interpret
  those asynchronous measurements as a simultaneous-motion synchronization metric.
* `ram-summary.json` in the evidence folder contains the native and cosmetic transforms. B's final `ram_after` occurs
  after A leaves, so its old remote handle is freed/reused and is deliberately excluded from position assertions.

Normal network interpolation still introduces about 100 ms of presentation delay; this work removes preview-transform
leakage and update-rate-dependent overshoot. It does not claim zero-latency simultaneous rendering, nor WAN/loss or
mounted-dragon live certification for this change (the mount exclusion has synthetic coverage).

After the other review session released the main play folder, the canonical incremental build successfully refreshed
`dist/DrakanPC` too (`build/coop-position-main-deploy.log`, deployed SHA-256
`D9DCB5553B40A0278FEF6E592C6BC077C9D9A708D0219BE57F33662ABF9BCC07`). This later integration build includes other
concurrent workspace changes; the co-op live acceptance above identifies the exact isolated executable tested.
