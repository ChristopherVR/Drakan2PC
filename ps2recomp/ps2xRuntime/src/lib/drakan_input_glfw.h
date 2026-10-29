// Drakan: drakan_input's host input on GLFW (Linux): keyboard (as DirectInput scancodes), mouse, wheel, cursor capture
// and the gamepad, for the shared input module (runtime/input). See drakan_window.h for the window.
#pragma once

namespace drakan_input_glfw {
// Makes HostInput use GLFW (call before drakan_input::InputModule::init with the GLFWwindow*).
void install();
// Window thread, after every glfwPollEvents (drakan_win::pump): applies capture / cursor requests made on other threads
// and samples the gamepad, focus and cursor for HostInput::poll.
void mainThreadTick();
} // namespace drakan_input_glfw
