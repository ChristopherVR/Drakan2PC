// Drakan (browser): drakan_input's host input from the page's DOM events (drakan_web_events.h): keys as DirectInput
// scancodes, pointer-lock mouse motion, buttons, wheel, focus and the Gamepad API.
#pragma once

namespace drakan_input_web {
// Makes HostInput use the browser events (call before drakan_input::InputModule::init).
void install();
// Host thread, once per host frame (drakan_win::pump): moves queued DOM events into the input module.
void pumpTick();
} // namespace drakan_input_web
