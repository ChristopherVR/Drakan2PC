#pragma once
#include <cstdint>
#include <string>

class PS2Runtime;
namespace drakan_input {
struct FrameInput;
}
struct HostMouseFrame {
    float x = 0, y = 0, seconds = 1.0f / 30.0f;
    bool active = false;
    bool ownsView = false;
};
HostMouseFrame ps2HostMouseFrame();
void ps2BeginHostPadFrame(const uint8_t* memory, bool directMouse = true, bool captureMouse = true);
void drakanInstallPortableCamera(PS2Runtime& runtime);
void drakanPortablePublishInput(const drakan_input::FrameInput& frame);
drakan_input::FrameInput drakanPortableTakeUiInput();
void drakanPortableReloadControls(const std::string& path);
bool drakanPortablePadSample(int port, int slot, uint8_t out[32]);
