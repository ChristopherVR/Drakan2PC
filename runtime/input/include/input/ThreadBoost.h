// Scheduling hints for the threads of the frame chain (game / EE thread, VU1 worker, GS worker, host + present loop,
// raw input). Header only, std + Win32. A no-op off Windows and when DRAKAN_THREAD_BOOST=0.
//
// Why: nothing set a priority, a power hint or a multimedia class, so on a busy desktop (browser, overlay, capture
// software) or a CPU with parked cores and two core complexes (Ryzen 3000) the threads that make a frame can be preempted
// for a scheduler quantum (several ms) in the middle of it: one late frame, seen as a stutter while moving the mouse.
//   * process: opts out of Windows 11 EcoQoS ("execution speed" throttling) and keeps the 1 ms timer resolution when the
//     window is covered, both of which otherwise slow a game that is not the foreground process of a Win32 message loop.
//   * frame threads: MMCSS class "Games" (the scheduler's own game class) and above-normal priority.
//   * input thread: highest priority, so a WM_INPUT is stamped when it arrives, not when a busy core frees up.
// Evidence and measurements: docs/journal/2026-10-31/mouse-microstutter.md.
#pragma once

namespace drakan_input {

enum class ThreadRole { Frame, Input };

// Call on the thread itself, first thing. No-op off Windows and with DRAKAN_THREAD_BOOST=0. The first call also applies
// the process-level hints.
void boostThread(ThreadRole role);

} // namespace drakan_input
