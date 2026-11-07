# Issue 1: remote players stutter and rubberband

Tester report (RabeOhneNamen): other players look like they move faster than their real speed and are then pulled
back; teleports now only happen in the first seconds after spawning. Their log shows `snapshots 19.8/s, states sent
19.8/s` (tick rate 30 Hz is only advertised; states and snapshots both run at 20 Hz).

## Root cause

Two defects in the remote replay (`drakan_netplay.cpp` `beforePadMgr`, `world::PosePlaybackClock`):

1. **Timeline in sender frames.** `PlayerState.tick` was the sender's PadMgr counter. With an uncapped frame rate the
   frames are not evenly spaced (sender traces: frame time p10 8 ms, p90 16 ms, max 130..420 ms), so a 50 ms state
   interval holds anywhere from 3 to 7 frames. The receiver replayed ticks at one constant rate, so the body covered a
   whole interval's distance in a too short or too long time: faster than real, then slower.
2. **The playback clock chased a jumping target.** It aimed at `newestTick - 100 ms` with a proportional gain of 5/s
   and a cadence estimate updated from each pair of snapshots (EMA 0.2). The sender's 20 Hz sends and the server's
   20 Hz snapshots are not synchronised, so a snapshot sometimes carries two new states and sometimes none; the target
   and the cadence estimate jumped on every snapshot and the replay re-accelerated and decelerated each time.

Ruled out: the native ghost actor does not move itself on top of our writes (`[ghost] id .. pos` vs the replayed pose
differ by at most one frame of travel, 0.2..31 units at 2,300 units/s); the replay never moved backwards along the
track in the archived runs. "Corrected back" is the visible deceleration after a burst.

Evidence from archived paired runs (`build/harness/mp-verify-motion`, `issue1-walk-burst-before`,
`issue1-sender-yield-final`; Rynn running ~2,000 units/s): the replayed per-frame speed follows a sawtooth inside each
snapshot interval (for example 2,700, 2,300, 2,100, 2,000, 1,800, then 2,800 again) and changes by more than 5 % on 75..88 %
of consecutive moving frames, by more than 25 % on 7..12.5 %.

## Fix

- Protocol v9: the sender stamps `PlayerState.tick` with its pose time in ms (strictly increasing). No wire change; the
  version bump keeps v8 peers out. Receiver buffer time is `tick / 1000`.
- `PosePlaybackClock` rewritten as a jitter buffer: cadence measured over a ~2 s window (reset across gaps over 250 ms),
  playback at that cadence trimmed by at most 12 % so the low envelope of the buffered time left before each new sample
  settles on 40 ms (fast attack, slow release; faster release when clearly over-buffered). Never past the newest sample,
  never backwards, a jump only when more than 1 s behind, a receiver hitch advances at most 50 ms.
- `PoseBuffer::sampleAt` / `interpolatePose` take a clip rate so clip times that wrap between samples advance in sender
  seconds (they advanced in frame-tick seconds before, which ran the legs too fast across a wrap).

## Verification

- `world_pose_test` `testPlaybackAliasedStream`: a model of the live pipeline (uneven sender frames, 20 Hz sends and
  snapshots on their own polling loops, 2..10 ms jitter, 60..240 Hz senders, 60 / 144 Hz receivers). Speed over 50 ms
  windows: old clock with frame ticks p10 0.79..0.89, p90 1.12..1.23, max up to 1.85 times the real speed; new clock
  with ms ticks p10 0.96..0.98, p90 1.02..1.03, max 1.06, no stalls, no backward step, ~190 ms total delay (40 ms
  latency per hop included). A 600 ms outage: no frame faster than 1.21 times the real speed afterwards. Plus
  `testClipRate`. All `runtime/world` and `runtime/net` tests pass (protocol test asserts v9).
- Paired harness `remote_motion_check.py --name issue1-interp-after --char-a 0x0e88 --char-b 0x177d --delay-ms 65
  --jitter-ms 35 --loss .08` (same settings as `mp-verify-motion`): every check passes, no step over 500 units. The replayed
  speed is now constant within each sender interval (2,200 x 8 frames, 2,500 x 6, ...). Speed changes by more than 5 % on
  33 % of moving frame pairs (88 % before on the same settings), by more than 25 % on 6.9 % (12.5 %), frames
  accelerating over 1.5x: 3 (8). The harness summary now has `replay_smoothness` / `sender_smoothness`.
- Not verified: the reporter's real-internet conditions and the early-teleport report. With the new clock a timeline
  reset plays from 120 ms behind the first sample and the first cadence estimate replaces the default once 150 ms of
  samples exist, so the start no longer depends on an EMA converging from 60 Hz; the harness host stands still while the
  ghost spawns, so this was not exercised live.
