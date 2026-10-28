# Options > Audio: clicking a slider or check box froze the game

Report: clicking the Master volume slider on the game's own Options > Audio screen (GUI screen 13), and later the Show
Subtitles / Stereo Sound boxes, froze the game. The SPU kept streaming music; frames stopped.

## Cause

Not the volume code. The mouse click is a **host-injected guest call**: `MenuMouse::update` runs inside
`hookPadMgrUpdate` and calls the widget's press/release handler through `RecompGuest::call` (a copy of the context,
preemption off). The slider/box handlers play a UI sound; when its descriptor is not loaded yet the game requests it
(`INTERFCE.SDB` type 0x302 id 15, plus a 0x305 lip-sync entry) and **waits on a semaphore** for the answer.

A wait inside such a nested call cannot be continued: `blockCurrent` unwinds the host stack and nobody resumes the
injected call. It is abandoned half way, **with the sound manager lock (semaphore 28, `f_0x1e6460`) still held**. The
next frame's `WaitSema(28)` blocks forever, and the 0x1301 resource-delivery handler (`0x1f0110`) blocks on the queue lock
(semaphore 3, `0x4a0208`) the abandoned call also owned. `RpcPoster::post` then logs
`guest handler 0x1f0110 did not return within 20 s`.

How it was found: a ring buffer of `WaitSema/SignalSema` events, a backtrace captured at each acquire of semaphore 28
(the owner's chain was `hookPadMgrUpdate -> MenuMouse::update -> RecompGuest::call -> f_0x2c59e0 (slider) -> f_0x441440
(Audio screen) -> f_0x1e6460`), and the scheduler snapshot at the stall. Raising `DRAKAN_RES_LATENCY_US` to 200 ms hid it
(the answer arrived before the wait), which pointed at timing rather than a bad lock.

Same family as the title "fire" flicker (`title-fire-flicker.md`): host code that runs guest code nested, and the
scheduler unwinding in the middle of it. Radio widgets already avoided it by turning the click into a pad tap.

## Fix

* `IGuest::post()` / `postBusy()`: queue a guest call as its own dispatcher thread (`EeScheduler::postGuestCall`, priority
  6, like the game's RPC servers), which may block. The default implementation is synchronous so test guests behave as
  before.
* `MenuMouse` sends press / drag / release of every widget **except buttons (class 3)** through a small in-order queue
  (`queueOp` / `pumpOps`): one posted call at a time, newest drag replaces a drag that has not started, and the
  captured-widget variable `*0x4fb19c` is cleared only after the release has run (the handlers read it).
* Slider click position. The game's press handler (`FUN_002c59e0`) only **pages**: left of the thumb = value minus the page
  step, right = plus; it follows the pointer only when the press lands on the thumb. `MenuMouse` now computes the thumb
  with the handler's own formula (min/max/value/step at widget +0x7c/+0x80/+0x84/+0x88, rect +0x1c/+0x20, skin margins
  at 0x4fb408 / 0x4fb438 and minimum thumb length 0x4fb3f8), presses on the thumb centre and posts a drag to the click,
  so the value lands where the cursor is. Only for horizontal sliders (flags `0x4000`).
* Test: `input_test` `testMenuMouse` (deferred-post fake guest).

## Not verified / caveats

* Checked by repeated scripted clicks on the slider (no stall, `did not return` never logged) before the queue and
  position change; the final queue + jump build was verified by unit test only until `dist/DrakanPC` could be redeployed.
* Buttons stay synchronous. A button whose sound is not cached could still hit the same wait; the title and Options
  buttons have not.
