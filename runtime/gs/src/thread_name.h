#pragma once
// Names the calling thread for debuggers and the PS2X_PERF line (`t<id>(<name>)`). Windows only; elsewhere a no-op
// (thread_name.cpp keeps the platform header out of the renderer's headers).

namespace gsr {
void nameCurrentThread(const wchar_t* name);
} // namespace gsr
