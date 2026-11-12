// Model detail options: characters keep their detailed model further out and objects are drawn further out than the
// PS2 budgets allowed ([video] lod_distance / draw_distance, env DRAKAN_LOD_DISTANCE / DRAKAN_DRAW_DISTANCE).
//
// How: four guest functions are wrapped and see changed numbers for the length of the call; no code and no model data
// is changed, and nothing outside those calls (saves, physics, collision, hit detection, the scene capture) sees a
// changed value.
//   * 0x102c90, the LOD selector of skinned models (a0 = skeleton controller, a1 = model resource, f12 = squared camera
//     distance), and 0x18b760, the near / far model switch of static objects (f12 = the same distance): f12 is divided
//     by factor^2. A character whose lower LOD draws from other texture sheets than its full model never reaches that
//     LOD while the option is on (the rule is read from the resident model: per LOD, the material indices of its
//     polygons).
//   * 0x18ba28, the draw-distance test of an object (a0 = object), and 0x189b38, its render method (the distance fade
//     of the draw call reads the same field): the object's draw distance at +0xf8 is multiplied by the factor for the
//     call and put back afterwards. The level's view distance still caps it inside the guest code.
// Both factors at 1 call the original functions with untouched arguments. The rules themselves are
// runtime/world/include/world/ModelDetail.h (unit tested in world_model_detail_test).
// PAL only (the addresses are PAL values; other disc profiles never install it).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_model_detail {

// Installs the hooks when the disc profile has PAL addresses and the functions exist. Safe to call more than once.
void install(PS2Runtime& runtime);
// True when the hooks are installed (the settings page greys the options out otherwise).
bool available();

// Live settings (any thread). Factors are clamped to 1 .. 8 (LOD) and 1 .. 4 (draw distance); 1 = original.
void configure(float lodFactor, float drawFactor);
float lodFactor();
float drawFactor();

} // namespace drakan_model_detail
