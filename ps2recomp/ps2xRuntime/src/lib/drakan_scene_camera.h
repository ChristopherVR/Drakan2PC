#pragma once
#include "runtime/gs/scene_guide_camera.h"
#include <atomic>
class PS2Runtime;
namespace drakan_scene_camera {
// Sink is called on the EE executor, BEFORE the viewport builder emits this
// scene's commands. It MUST copy the packet into the ordered GS bridge, after
// draining earlier VIF work; never call the D3D backend directly from this hook.
using Sink = void (*)(PS2Runtime&, const gsr::SceneGuideCamera&, void*);
// Install once after widescreen/other game hooks and before runtime.run().
// Does not replace the widescreen hook at 0x1480e0.
bool install(PS2Runtime&, const std::atomic<uint64_t>* vsyncTick, Sink, void* opaque);
void setEnabled(bool enabled);
void setWaterCaptureEnabled(bool enabled);
// Private renderer protocol using the existing packet layout, not a camera:
// sourcePc=0x139010, flags=0 opens engineWorldOpen before ANY world draw.
// Handle this marker before ordinary camera/GUI updates; it must NOT begin a
// motion-history epoch or replace the last validated neural camera. The usual
// EndWorld packet closes the phase. An early return uses the same sourcePc
// with flags=EndWorld. Both helpers use the installed, enabled, ordered sink.
void beginWorldPhase(PS2Runtime&);
void endWorldPhase(PS2Runtime&); // early-return cleanup only
// A host screen's native item grid (437170): sourcePc=0x437170, reserved=1 before its black fill, 0 after its draws;
// viewportOffset = the grid's box in GUI frame units. The renderer keeps those draws in an off-screen copy for the
// host's icon crops and restores the frame under them (gsr::OverlayContext::gridSrv). False when nothing was sent.
bool gridCapture(PS2Runtime&, bool begin, float x0, float y0, float x1, float y1);
// Subtitle_Draw uses sourcePc=0x3afd18, reserved=1/0 for entry/return.
// Its last font DMA can follow the return; the renderer retains only cells
// inside the observed subtitle band through the display-frame tail.
} // namespace drakan_scene_camera
