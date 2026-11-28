// Depth clear under the native 3D item overlays (PAL). See drakan_item_overlay.h.
#include "drakan_item_overlay.h"

#include "drakan_guest_layout.h"
#include "drakan_item_overlay_policy.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace drakan_item_overlay {
namespace {

constexpr uint32_t kDepthClear = 0x1342e0u;         // render object vtable +0x44 (a0 object, a1 int[4] rect, a2 0)
constexpr uint32_t kOverlayClearReturn = 0x3b0784u; // its call in the item overlay draw 0x3aff40
constexpr unsigned kItemRegister = 21;              // s5 = 0x3aff40's a0, the item handle, kept for the whole draw
// Handle fields (region independent).
constexpr uint32_t kHandleBox = 0xb8;      // Box: origin, three edge vectors (12 floats), kept by 0x17f938
constexpr uint32_t kHandleState = 0x104;   // bit 0x2000: the box is current
constexpr uint32_t kBoxCurrent = 0x2000u;  // 0x3b0868 clears it before each part draw; the draw sets it again
constexpr uint32_t kInstanceParts = 0x108; // instance data: {+0xc u16 refs[], +0x14 count} (0x3b0868, 0x346aa8)
constexpr uint32_t kPartRefs = 0xc, kPartCount = 0x14;
constexpr int32_t kMaxParts = 64;
// Render object fields read by 0x1336a8.
constexpr uint32_t kScaleX = 0x40, kScaleY = 0x44, kProjX = 0x48, kProjY = 0x4c, kOffsetX = 0x50, kOffsetY = 0x58;
constexpr uint32_t kFrameW = 0xf8, kFrameH = 0xfc, kViewportW = 0x118, kViewportH = 0x11c;
// World view struct rows (0x4dad58 + offset).
constexpr uint32_t kRowX = 0x0c, kRowY = 0x18, kRowW = 0x24;

std::atomic<bool> g_installed{false};
bool g_enabled = true;
bool g_log = false;
uint64_t g_widened = 0;
PS2Runtime::RecompiledFunction g_original = nullptr;

bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && a <= PS2_RAM_SIZE && bytes <= PS2_RAM_SIZE - a;
}
uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
Vec3 vec(const uint8_t* ram, uint32_t a) {
    return {f32(ram, a), f32(ram, a + 4), f32(ram, a + 8)};
}

bool readProjector(const uint8_t* ram, Projector& p, int32_t& frameW, int32_t& frameH) {
    const uint32_t view = drakan_layout::kWorldView;
    const uint32_t obj = u32(ram, drakan_layout::kRenderViewPtr) & 0x1fffffffu;
    if (!inRam(view, 0x80) || !inRam(obj, 0x120))
        return false;
    p.camera = vec(ram, view + drakan_layout::kWorldViewCameraPos);
    p.rowX = vec(ram, view + kRowX);
    p.rowY = vec(ram, view + kRowY);
    p.rowW = vec(ram, view + kRowW);
    const uint32_t w = u32(ram, obj + kFrameW), h = u32(ram, obj + kFrameH);
    p.offsetX = (f32(ram, obj + kOffsetX) - (2048.0f - float(w) * 0.5f)) + float(u32(ram, obj + kViewportW) >> 1);
    p.offsetY = (f32(ram, obj + kOffsetY) - (2048.0f - float(h) * 0.5f)) + float(u32(ram, obj + kViewportH) >> 1);
    p.scaleX = f32(ram, obj + kScaleX) * f32(ram, obj + kProjX);
    p.scaleY = f32(ram, obj + kScaleY) * f32(ram, obj + kProjY);
    frameW = int32_t(std::min<uint32_t>(w, 4096));
    frameH = int32_t(std::min<uint32_t>(h, 4096));
    return std::isfinite(p.offsetX) && std::isfinite(p.offsetY) && std::isfinite(p.scaleX) && std::isfinite(p.scaleY);
}

bool readBox(const uint8_t* ram, uint32_t handle, Box& box) {
    if (!inRam(handle, kHandleState + 4) || !(u32(ram, handle + kHandleState) & kBoxCurrent))
        return false;
    box.origin = vec(ram, handle + kHandleBox);
    box.a = vec(ram, handle + kHandleBox + 12);
    box.b = vec(ram, handle + kHandleBox + 24);
    box.c = vec(ram, handle + kHandleBox + 36);
    for (const Vec3* v : {&box.origin, &box.a, &box.b, &box.c})
        if (!std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z))
            return false;
    return true;
}

// The parts' boxes as 0x3b0868 finds the parts: instance = *(*(item + 0xfc) + 0x10), refs through the registry.
void addParts(const uint8_t* ram, uint32_t item, const Projector& projector, Coverage& coverage, int& used, int& seen) {
    if (!inRam(item, 0x100))
        return;
    const uint32_t inner = u32(ram, item + world::handle::kInner) & 0x1fffffffu;
    if (!inRam(inner, 0x14))
        return;
    const uint32_t instance = u32(ram, inner + world::handle::kInnerData) & 0x1fffffffu;
    if (!inRam(instance, kInstanceParts + 4))
        return;
    const uint32_t parts = u32(ram, instance + kInstanceParts) & 0x1fffffffu;
    if (!inRam(parts, kPartCount + 4))
        return;
    const int32_t count = std::min<int32_t>(int32_t(u32(ram, parts + kPartCount)), kMaxParts);
    const uint32_t refs = u32(ram, parts + kPartRefs) & 0x1fffffffu;
    const uint32_t ctx = u32(ram, drakan_layout::kEngineContextPtr) & 0x1fffffffu;
    if (count <= 0 || !inRam(refs, uint32_t(count) * 2) || !inRam(ctx, world::ctx::kRegistryHighWater + 4))
        return;
    const uint32_t table = u32(ram, ctx + world::ctx::kRegistryTable) & 0x1fffffffu;
    const int64_t highWater = int32_t(u32(ram, ctx + world::ctx::kRegistryHighWater));
    for (int32_t i = 0; i < count; ++i) {
        uint16_t ref;
        std::memcpy(&ref, ram + refs + uint32_t(i) * 2, 2);
        if (ref == 0 || int64_t(ref) > highWater + 1 || !inRam(table, uint32_t(ref) * 8))
            continue;
        const uint32_t part = u32(ram, table + (uint32_t(ref) - 1) * 8) & 0x1fffffffu;
        Box box;
        Rect rect;
        if (part == 0 || part == item || !readBox(ram, part, box) || !boxRect(projector, box, rect))
            continue;
        ++seen;
        used += coverage.add(rect) ? 1 : 0;
    }
}

void widen(uint8_t* ram, uint32_t rectAddress, uint32_t item) {
    if (!inRam(rectAddress, 16))
        return;
    Rect native;
    std::memcpy(&native, ram + rectAddress, sizeof native);
    Projector projector;
    int32_t frameW = 0, frameH = 0;
    if (!native.valid() || !readProjector(ram, projector, frameW, frameH))
        return;
    Coverage coverage(native, frameW, frameH);
    int used = 0, seen = 0;
    addParts(ram, item, projector, coverage, used, seen);
    if (!coverage.widened())
        return;
    const Rect& r = coverage.rect();
    // 0x1342e0 copies the rectangle into its own locals on entry; 0x3aff40 does not read it again.
    std::memcpy(ram + rectAddress, &r, sizeof r);
    ++g_widened;
    if (g_log && (g_widened <= 20 || g_widened % 600 == 0))
        std::fprintf(stderr,
                     "[item-overlay] #%llu item %x: depth clear %d,%d..%d,%d -> %d,%d..%d,%d (%d of %d part boxes)\n",
                     (unsigned long long)g_widened, item, native.x0, native.y0, native.x1, native.y1, r.x0, r.y0, r.x1,
                     r.y1, used, seen);
}

void depthClear(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (g_enabled && c->pc == kDepthClear && GPR_U32(c, 31) == kOverlayClearReturn)
        widen(ram, GPR_U32(c, 5) & 0x1fffffffu, GPR_U32(c, kItemRegister) & 0x1fffffffu);
    g_original(ram, c, rt);
}

} // namespace

void install(PS2Runtime& rt) {
    if constexpr (!drakan_region::build().identityAddresses)
        return;
    if (g_installed.exchange(true))
        return;
    const char* off = std::getenv("DRAKAN_ITEM_OVERLAY_DEPTH");
    g_enabled = !(off && off[0] == '0');
    const char* log = std::getenv("DRAKAN_ITEM_OVERLAY_DEPTH_LOG");
    g_log = log && log[0] == '1';
    if (!g_enabled || !rt.hasFunction(kDepthClear))
        return;
    g_original = rt.lookupFunction(kDepthClear);
    rt.replaceFunction(kDepthClear, &depthClear);
}

} // namespace drakan_item_overlay
