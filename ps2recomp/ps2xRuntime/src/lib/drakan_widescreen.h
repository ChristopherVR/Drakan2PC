// Drakan: widescreen (anamorphic Hor+) and the related video options, see docs/reference/widescreen.md.
//   * 3D world: the game's horizontal projection scale is squeezed by (4/3)/aspect (hooks on the camera update 0x1480e0
//   and SetViewport 0x132d68),
//     the 512x512 frame (a 4:3 picture on the PS2) is presented stretched to the wide aspect by gsr.
//   * Native interactive menus fill the selected aspect with a centered, aspect-correct foreground.
//     FMV/loading keep native presentation. UI projection stays native in every case.
//   * HUD / 2D de-stretch (hud_aspect_correct) is done inside gsr (Renderer::setHudScaleX); this module just tells gsr
//   the factor.
// Setting values: drakan.ini [video] aspect = 4:3 | 16:9 | 21:9 | auto (= window aspect) | native | stretch (+ any
// "w:h" / decimal), env DRAKAN_WS_ASPECT / PS2X_ASPECT.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

class PS2Runtime;
namespace gsr {
class Renderer;
}

namespace drakan_ws {
enum class Mode : int { Fixed, Auto, Native, Stretch };

// Reads drakan.ini ([video] aspect, hud_aspect_correct, smooth_menu_highlight). Call once at start-up (before the
// renderer is created).
void configure();
// Installs the projection hooks on the recompiled functions and remembers the guest memory / vblank counter used for
// the state detection.
void install(PS2Runtime& rt, uint8_t* rdram, const std::atomic<uint64_t>* vsyncTick);
// Live changes (settings overlay, any thread). `text` uses the ini spelling.
void setAspectText(const std::string& text);
void setFieldOfView(float degrees); // 60..120 horizontal degrees at 4:3, original 80
void setHudCorrect(bool on);
// hud_scale: 0 = auto (by output size), else 0.5 .. 1.5.
void setHudScale(float v);
void setSmoothHighlight(bool on);
// The renderer the decisions are pushed to (null = detach).
void attachRenderer(gsr::Renderer* gs, void* hwnd);
// Called every presented frame by the window thread: decides wide / 4:3 and pushes aspect + HUD scale to gsr when
// something changed.
void hostTick();

// Initial values for gsr::Config (before the renderer exists).
void initialPresenter(float& forcedAspect, bool& stretch, float windowAspect);
bool smoothHighlightInitial();
// True while the 3D world is presented with the widened projection.
bool wideActive();
// Called on the EE thread whenever the game rendered a 3D camera update.
void noteWorldFrame();
// Horizontal projection of the last 3D world camera update: frame pixels per unit of view x / depth (renderer
// sc+0x40 * sc+0x48 after the aspect / FOV correction; the 132f60 crop scale cancels out). 0 = none yet. EE thread.
float worldProjectionX();
} // namespace drakan_ws
