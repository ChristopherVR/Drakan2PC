#pragma once

namespace drakan_ws {
// Private EE-thread query. True only inside 0x139010's world-render phase,
// before its EndWorld boundary, and only for the guest thread that owns it.
// Presentation aspect and camera pointer identity do not imply this scope.
bool renderingWorld(int thread);
} // namespace drakan_ws
