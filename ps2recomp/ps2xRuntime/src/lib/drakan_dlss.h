// Drakan: optional DLSS Super Resolution (NVIDIA, Windows) - runtime glue. The module itself lives in decomp/runtime/gs
// (src/dlss_ngx.cpp, temporal.*); this file only reads drakan.ini [video] dlss / dlss_preset / dlss_sharpness, forwards
// the VU1 camera tap and offers the live setters to the settings overlay. See docs/reference/dlss.md. Nothing here is
// needed for the game to run: with dlss = off (default) the hook is not even installed.
#pragma once
#include <string>

namespace gsr {
class Renderer;
struct Config;
} // namespace gsr
namespace hle {
class IsoFs;
}
class GsrBridge;
class PS2Runtime;

namespace drakan_dlss {
void setAssetSource(hle::IsoFs* fs);
// Fills cfg.upscaleMode / upscalePreset / upscaleSharpness from drakan.ini (env DRAKAN_DLSS, DRAKAN_DLSS_PRESET
// override). Call before Renderer::create.
void configure(gsr::Config& cfg);
// Installs the VIF1 camera tap (only when the mode is not off, or later when it is switched on).
void attach(GsrBridge* bridge, gsr::Renderer* gs);
void detach();
void installSceneHooks(PS2Runtime& runtime);
void hostTick();                         // optional bounded developer toggle sequence
void commitDisplay(PS2Runtime& runtime); // ordered complete native display transaction

// Mode names: off | dlaa | quality | balanced | performance. Presets: auto | j | k | l | m.
void setMode(const std::string& mode, const std::string& preset,
             float sharpness); // live; also installs the tap when needed
// True when this build contains the DLSS module, the GPU is an NVIDIA one and nvngx_dlss.dll is in the configured
// search locations (preliminary probe, not confirmed feature support). `why` explains a false.
bool available(std::string* why = nullptr);
// "DLSS SR Quality 1024x1024 -> 1536x1536, preset K" or the reason it is off / unavailable.
std::string status();
void setNeural(bool on);
// [video] dlss_neural_resolution: "output" (default; NR on the SR output) | "render" (NR on the reduced raster
// before SR). Anything else reads as output. Live.
bool neuralResolutionIsRender(const std::string& value);
void setNeuralResolution(const std::string& value);
bool neuralRequested(); // host/GS presentation handoff; does not query the renderer
bool neuralAvailable(std::string* why = nullptr);
std::string neuralStatus();
} // namespace drakan_dlss
