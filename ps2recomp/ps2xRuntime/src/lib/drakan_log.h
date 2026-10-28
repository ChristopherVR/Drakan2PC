// Drakan: process logging for the GUI-subsystem exe (no console window).
//   * stdout / stderr are redirected into an in-process pipe; a reader thread moves the bytes into a bounded queue
//   (drops the
//     oldest data on overflow) and a writer thread writes them to drakan.log next to the exe (the previous run is kept
//     as drakan.1.log), to the original stderr when the process was started with stderr redirected to a file / pipe
//     (test scripts keep working) and to the console when [general] console = 1 / DRAKAN_CONSOLE=1.
//   * a log write therefore never blocks the emulation threads, whatever the destinations do (a console in QuickEdit
//     selection mode blocks every writer until the selection ends, which used to freeze the EE and loop the audio).
//   * console = 1: AllocConsole (when the process has none) with QuickEdit and insert mode disabled.
#pragma once

namespace drakan_log {
// Call first thing in main(). Safe to call once; no-op on non-Windows.
void init();
// Shutdown: waits (at most timeoutMs) until everything written so far has reached drakan.log / the sinks, then flushes
// the file to disk.
void flush(int timeoutMs = 1500);
} // namespace drakan_log
