// Drakan: FPS / frame time counter (drawn by the settings overlay's stats layer, see drakan_settings_host.cpp).
//   mode 0 = off, 1 = simple (completed world fps, present callback fps, frame time), 2 = detailed (+ 1% low, worst
//   frame, CPU load of the process / EE thread / renderer thread, GPU, backbuffer size). Hotkey F2 cycles the mode for
//   the session; drakan.ini [video] show_fps / fps_position set it at start and the F1 menu (Display) changes it live.
// Cost: onPresent() = one QPC read + a ring store per presented frame; the statistics are recomputed twice a second.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace drakan_fpsmeter {
enum Corner { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };

void configure(); // reads drakan.ini [video] show_fps / fps_position (environment DRAKAN_SHOW_FPS, DRAKAN_FPS_POSITION)
void setMode(int mode); // 0 off, 1 simple, 2 detailed (thread safe)
int mode();
int cycleMode(); // F2
void setCorner(int corner);
int corner();
void setCornerText(const char* text); // "top-left" | "top-right" | "bottom-left" | "bottom-right"
void setThreads(void* eeThreadHandle, void* rendererThreadHandle); // Win32 thread handles (may be null); not owned
void setInfo(const std::string& gpuName, unsigned backbufW, unsigned backbufH);

void setWorldFrames(uint64_t completed); // GS worker: completed positive world scenes with draws
uint64_t worldFrames();
void onPresent(); // every presented frame (presenting thread)
// Text lines for the overlay (recomputed twice a second); empty when mode == 0. World fps counts positive completed
// scenes with draws; Flip events is the legacy mixed DISPFB-write/vblank-key counter, not fps.
std::vector<std::string> lines();
} // namespace drakan_fpsmeter
