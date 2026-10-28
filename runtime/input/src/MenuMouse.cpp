#include "input/MenuMouse.h"
#include "input/FrameInput.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace drakan_input {

namespace {
constexpr uint32_t kRamEnd = 0x02000000u;
inline bool validPtr(uint32_t p) {
    return p >= 0x00100000u && p + 0x100u < kRamEnd && (p & 3u) == 0;
}

// GUI object layout (SCES_500.06, docs/reference/mouse-menus.md)
constexpr uint32_t kScrId = 0x18;    // screen id (8 title, 9 options, 0x17 pause ...)
constexpr uint32_t kScrFirst = 0x7c; // first child widget (+4 = next sibling)
constexpr uint32_t kScrLast = 0x80;  // last child widget (+0 = previous sibling)
constexpr uint32_t kScrFocus = 0x84; // focused widget (0 = none)
constexpr uint32_t kScrVtbl2 =
    0x98; // vtable pointer of the screen's event interface (+0x28 adj, +0x2c handler(widget, msg))
constexpr uint32_t kWNext = 0x04, kWPrev = 0x00;
constexpr uint32_t kWFlags = 0x10; // 0x40 = focused / highlighted, 0x80 = pressed, 0x100 = group member
constexpr uint32_t kWRect = 0x1c;  // s16 x, y, w, h
constexpr uint32_t kWType = 0x0c;  // s16: 3 = button, 9 = slider ...
constexpr uint32_t kWEnabledA = 0x3c, kWEnabledB = 0x40; // both must be non-zero for the widget to take part in input
constexpr uint32_t kWDirty = 0x50;
constexpr uint32_t kWVtbl = 0x54;
} // namespace

bool MenuMouse::mapToGui(const MenuMouseSample& s, const MenuView& v, int screenW, int screenH, int* gx, int* gy) {
    float vx = 0, vy = 0, vw = s.clientW, vh = s.clientH;
    if (v.valid) {
        vx = v.x;
        vy = v.y;
        vw = v.w;
        vh = v.h;
    }
    if (vw <= 0.0f || vh <= 0.0f || screenW <= 0 || screenH <= 0)
        return false;
    float u = (s.cx - vx) / vw;
    const float w = (s.cy - vy) / vh;
    if (v.valid && v.hudScaleX > 0.2f && v.hudScaleX < 0.999f)
        u = (u - v.hudAnchorX) / v.hudScaleX +
            v.hudAnchorX; // undo the sprite de-stretch of gsr (full-width rows reach the screen edges)
    if (u < 0.0f || u >= 1.0f || w < 0.0f || w >= 1.0f)
        return false;
    *gx = static_cast<int>(u * static_cast<float>(screenW));
    *gy = static_cast<int>(w * static_cast<float>(screenH));
    return true;
}

// ---- guest helpers
// --------------------------------------------------------------------------------------------------------------

namespace {
// Virtual call on an object whose vtable pointer sits at obj + vtOfs: records are {s16 this-adjust, fn} at vt + slot -
// 4 / vt + slot.
bool makeVcall(IGuest& g, GuestCall& c, uint32_t obj, uint32_t vtOfs, uint32_t slot, uint32_t a1, uint32_t a2,
               uint32_t a3, int nargs) {
    const uint32_t vt = g.read32(obj + vtOfs);
    if (!validPtr(vt))
        return false;
    c = GuestCall(g.read32(vt + slot));
    if (c.addr < 0x100000u || c.addr >= kRamEnd)
        return false;
    c.arg(obj + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + slot - 4))));
    if (nargs > 0)
        c.arg(a1);
    if (nargs > 1)
        c.arg(a2);
    if (nargs > 2)
        c.arg(a3);
    return true;
}

// Centre of a horizontal slider's thumb in GUI units, from the same numbers the game's own press handler
// (FUN_002c59e0) uses: widget +0x7c min, +0x80 max, +0x84 value, +0x88 page step (floats), rect x / width at +0x1c /
// +0x20, plus the skin's track margins (0x4fb408 left, 0x4fb438 right) and minimum thumb length (0x4fb3f8). The native
// press only pages left / right of the thumb; pressing on the thumb is what makes it follow the pointer.
bool sliderThumbCenter(IGuest& g, uint32_t w, int* center) {
    const float mn = g.readF(w + 0x7c), mx = g.readF(w + 0x80), cur = g.readF(w + 0x84), step = g.readF(w + 0x88);
    const float range = mx - mn;
    if (!(range > 0.0f))
        return false;
    const int marginL = static_cast<int32_t>(g.read32(0x4fb408u)), marginR = static_cast<int32_t>(g.read32(0x4fb438u));
    const int minThumb = static_cast<int32_t>(g.read32(0x4fb3f8u));
    const int track = g.readS16(w + 0x20) - marginL - marginR;
    if (track <= 0)
        return false;
    const int thumbLen = std::max(static_cast<int>(step / range * static_cast<float>(track)), minThumb);
    const int travel = track - thumbLen;
    const int left = g.readS16(w + 0x1c) + marginL + static_cast<int>((cur - mn) / range * static_cast<float>(travel));
    *center = left + thumbLen / 2;
    return true;
}

uint32_t vcall(IGuest& g, uint32_t obj, uint32_t vtOfs, uint32_t slot, uint32_t a1 = 0, uint32_t a2 = 0,
               uint32_t a3 = 0, int nargs = 0) {
    GuestCall c;
    return makeVcall(g, c, obj, vtOfs, slot, a1, a2, a3, nargs) ? g.call(c) : 0;
}

} // namespace

void MenuMouse::queueOp(const GuestCall& c, bool coalesce) {
    if (coalesce && !ops_.empty() && ops_.back().addr == c.addr)
        ops_.back() = c; // a newer drag position replaces a drag that has not started yet
    else
        ops_.push_back(c);
}

void MenuMouse::pumpOps(IGuest& g) {
    while (!ops_.empty() && !g.postBusy()) {
        const GuestCall c = ops_.front();
        ops_.pop_front();
        g.post(c);
    }
    if (clearCaptured_ && ops_.empty() && !g.postBusy()) {
        g.write32(a_.capturedWidgetVar, 0);
        clearCaptured_ = false;
    }
}

bool MenuMouse::isChild(IGuest& g, uint32_t screen, uint32_t widget) const {
    uint32_t w = g.read32(screen + kScrFirst);
    for (int guard = 0; w && guard < 512; ++guard) {
        if (w == widget)
            return true;
        if (!validPtr(w))
            return false;
        w = g.read32(w + kWNext);
    }
    return false;
}

bool MenuMouse::canFocus(IGuest& g, uint32_t widget) const {
    if (!validPtr(widget))
        return false;
    if (g.read32(widget + kWEnabledA) == 0 || g.read32(widget + kWEnabledB) == 0)
        return false;
    return vcall(g, widget, kWVtbl, 0x74) != 0;
}

// Menu rows are full-width widgets, but only the centred text (plus the 0x24 px icon the button draws left of it) is
// visible: restrict hover / click to that extent plus a margin. Button draw (FUN_002c0810 family): text pointer at
// +0x5c, flag 0x1 = centred, width via FUN_002885c8(str, 1, 0, len + 1). Other classes (sliders, check boxes,
// selectors) keep their whole rectangle (their visible control spans it).
bool MenuMouse::insideLabel(IGuest& g, uint32_t w, int gx, int x, int ww) const {
    if (g.readS16(w + kWType) != 3 || (g.read32(w + kWFlags) & 1u) == 0)
        return true;
    const uint32_t str = g.read32(w + 0x5c);
    if (!validPtr(str))
        return true;
    int len = 0;
    while (len < 120 && g.read8(str + static_cast<uint32_t>(len)) != 0)
        ++len;
    if (len == 0)
        return true;
    const int tw =
        static_cast<int>(g.call(GuestCall(0x2885c8).arg(str).arg(1).arg(0).arg(static_cast<uint32_t>(len + 1))));
    if (tw <= 0 || tw > ww)
        return true;
    const int half = (tw + 0x24 + 6) / 2 + 12; // text + icon, small margin
    const int cx = x + ww / 2;
    return gx >= cx - half && gx < cx + half;
}

uint32_t MenuMouse::hitTest(IGuest& g, uint32_t screen, int gx, int gy) const {
    uint32_t w = g.read32(screen + kScrLast); // topmost = last added, like the game's own hit test
    for (int guard = 0; w && guard < 512; ++guard) {
        if (!validPtr(w))
            return 0;
        const int x = g.readS16(w + kWRect), y = g.readS16(w + kWRect + 2), ww = g.readS16(w + kWRect + 4),
                  hh = g.readS16(w + kWRect + 6);
        // same comparisons as FUN_002bc618: x <= px < x + w, y < py < y + h
        if (gx >= x && gx < x + ww && gy > y && gy < y + hh && canFocus(g, w) && insideLabel(g, w, gx, x, ww))
            return w;
        w = g.read32(w + kWPrev);
    }
    return 0;
}

bool MenuMouse::hasFocusable(IGuest& g, uint32_t screen) const {
    uint32_t w = g.read32(screen + kScrFirst);
    for (int guard = 0; w && guard < 512; ++guard) {
        if (!validPtr(w))
            return false;
        if (canFocus(g, w))
            return true;
        w = g.read32(w + kWNext);
    }
    return false;
}

// UI sound: the property table entry of the current UI object returns the sound id of property `property` (0x24 click,
// 0x2a / 0x2b focus tick down / up, ...); the sound manager (ctx + 0x32c) plays it. Same sequence as the inline code in
// FUN_002bc8c0 / FUN_002bf568.
void MenuMouse::playUiSound(IGuest& g, uint32_t property) {
    const uint32_t entry = g.call(GuestCall(a_.uiEntry));
    if (!validPtr(entry))
        return;
    const uint32_t obj = g.read32(entry + 0xc);
    if (!validPtr(obj))
        return;
    const uint32_t vt = g.read32(obj + 8);
    if (!validPtr(vt))
        return;
    GuestCall pc(g.read32(vt + 0x1c));
    if (pc.addr < 0x100000u || pc.addr >= kRamEnd)
        return;
    pc.arg(obj + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x18)))).arg(property).arg(0xffffffffu);
    const uint32_t idp = g.call(pc);
    if (!validPtr(idp))
        return;
    const uint32_t id = g.read32(idp);
    if (id == 0)
        return;
    const uint32_t ctx = g.read32(a_.ctxVar);
    if (!validPtr(ctx))
        return;
    const uint32_t snd = g.read32(ctx + 0x32c);
    if (!validPtr(snd))
        return;
    const uint32_t svt = g.read32(snd);
    if (!validPtr(svt))
        return;
    GuestCall sc(g.read32(svt + 0x114));
    if (sc.addr < 0x100000u || sc.addr >= kRamEnd)
        return;
    sc.argf(0.0f)
        .arg(snd + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(svt + 0x110))))
        .arg(id)
        .arg(a_.soundPos)
        .arg(4u);
    g.call(sc);
}

// The focus change of FUN_002bcc38 / FUN_002bc428: tell the screen handler (message 0xc, "focus leaves widget"), clear
// the highlight flag of the old widget, set it on the new one and mark both dirty.
void MenuMouse::setFocus(IGuest& g, uint32_t screen, uint32_t widget) {
    const uint32_t old = g.read32(screen + kScrFocus);
    if (old == widget)
        return;
    if (old)
        vcall(g, screen, kScrVtbl2, 0x2c, old, 0xc, 0, 2);
    if (validPtr(old)) {
        g.write32(old + kWFlags, g.read32(old + kWFlags) & ~0x40u);
        g.write32(old + kWDirty, 1);
    }
    g.write32(screen + kScrFocus, widget);
    g.write32(widget + kWFlags, g.read32(widget + kWFlags) | 0x40u);
    g.write32(widget + kWDirty, 1);
}

void MenuMouse::reset() {
    pressActive_ = false;
    pressedWidget_ = 0;
    inventoryTarget_ = -1;
    inventoryActivate_ = false;
    inventoryWait_ = 0;
    // Focus/overlay loss and inventory reopening invalidate the draw camera,
    // even if the guest reuses the same screen/controller addresses. A new
    // native grid draw must certify bounds before another absolute click.
    gridScreen_ = gridController_ = 0;
    gridAge_ = 100;
    categoryScreen_ = 0;
    categoryTarget_ = -1;
    categoryWait_ = 0;
    for (auto& category : categories_)
        category = CategoryRect{};
    dbg_.categoryHit = dbg_.categoryTarget = -1;
    mapRects_.clear();
    mapScreen_ = mapSection_ = mapTable_ = mapCount_ = mapRegion_ = 0;
    mapTarget_ = -1;
    mapAge_ = 100;
    mapWait_ = 0;
    dbg_.mapHit = dbg_.mapTarget = -1;
    dbg_.mapAge = 100;
    resetContent();
}

void MenuMouse::setHotspotHandler(HotspotHandler h) {
    std::lock_guard<std::mutex> lk(hotM_);
    hotFn_ = std::move(h);
}

void MenuMouse::setHotspots(uint32_t screen, const std::vector<Hotspot>& list) {
    std::lock_guard<std::mutex> lk(hotM_);
    hotScreen_ = screen;
    hot_ = list;
    hotFrame_ = frame_;
}

void MenuMouse::captureInventoryCategory(IGuest& g, uint32_t record) {
    const auto span = [](uint32_t p, uint32_t bytes) {
        return p >= 0x100000u && bytes <= kRamEnd && p <= kRamEnd - bytes && (p & 3u) == 0;
    };
    if (!span(record, 0x24))
        return;
    const uint32_t screen = g.read32(record + 0x10);
    if (!span(screen, 0x1e8) || g.read32(screen + kScrId) != kInventoryScreenId)
        return;
    const uint32_t base = g.read32(screen + 0x19c);
    if (!span(base, 5 * 0x24) || record < base || (record - base) % 0x24 || (record - base) / 0x24 >= 5)
        return;
    if (categoryScreen_ != screen) {
        for (auto& category : categories_)
            category = CategoryRect{};
        categoryScreen_ = screen;
        categoryTarget_ = -1;
    }
    const unsigned index = (record - base) / 0x24;
    auto& out = categories_[index];
    out.age = 100;
    // 42fd18 animates opening/closing. 42b308 is also used by unrelated UI;
    // only a settled inventory's actual category records may publish bounds.
    const uint32_t tabs = g.read32(screen + 0x1a4);
    if ((tabs != 5 && tabs != 3) || g.read32(screen + 0x3c) != 0 || g.readF(screen + 0x158) != 1.0f ||
        g.readF(screen + 0x15c) != 0.0f)
        return;
    const uint32_t player = g.read32(screen + 0x14c);
    if (!span(player, 0x594))
        return;
    // 340508 is exactly inventoryController+0xc, set by 33c660 when a shopkeeper opens the inventory: the shop draws
    // 3 tabs (Equipment, Spells, shop list).
    const unsigned count = g.read32(player + 0x590) ? 3u : 5u;
    if (index >= count)
        return;
    const uint32_t node = g.read32(record + 0x1c), sc = g.read32(a_.screenObjVar), ctx = g.read32(a_.ctxVar);
    if (!span(node, 0x120) || !span(sc, 0x120) || !span(ctx, 0x4b8) || g.read32(node) != 0x51b8b0u ||
        !(g.read32(node + 0x104) & 0x2000u))
        return;
    // 1801d8 returns node+b8 only when 17f938's cached-box bit is set. Do not
    // invoke the lazy getter (which can yield and uses a shared scratch box).
    const uint32_t cameraObject = g.read32(ctx + 0x4b4);
    if (!span(cameraObject, 0x50))
        return;
    const uint32_t camera = cameraObject + 0x14;
    // 430e90 installs identity. Reject a restored world/preview camera instead
    // of projecting old icon boxes through it. Native page placement is read.
    for (unsigned i = 0; i < 3; ++i) {
        if (g.readF(camera + i * 4) != 0.0f || g.readF(camera + 0x30 + i * 4) != 1.0f)
            return;
        for (unsigned k = 0; k < 3; ++k)
            if (g.readF(camera + 0xc + i * 4 + k * 12) != (i == k ? 1.0f : 0.0f))
                return;
    }
    float box[12];
    for (unsigned i = 0; i < 12; ++i) {
        box[i] = g.readF(node + 0xb8 + i * 4);
        if (!std::isfinite(box[i]))
            return;
    }
    // Native icon backgrounds have axis-aligned thin boxes. Intersect both
    // depth-face projections, conservatively avoiding the enclosing AABB's
    // perspective fringe. Untraced rotations/near clipping are ineligible.
    for (unsigned i : {4u, 5u, 6u, 8u, 9u, 10u})
        if (box[i] != 0.0f)
            return;
    if (!(box[3] > 0 && box[7] > 0 && box[11] >= 0 && box[2] + box[11] < -32.0f) || box[11] > -box[2] * 0.01f)
        return;
    const uint32_t width = g.read32(sc + 0xf8), height = g.read32(sc + 0xfc);
    if (!width || width > 4096 || !height || height > 4096)
        return;
    const float fx = g.readF(sc + 0x48) * g.readF(sc + 0x40), fy = g.readF(sc + 0x4c) * g.readF(sc + 0x44);
    const float ox = g.readF(sc + 0x100) - (2048.0f - float(width) * .5f);
    const float oy = g.readF(sc + 0x104) - (2048.0f - float(height) * .5f);
    if (!(fx > 0 && fy > 0) || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(ox) || !std::isfinite(oy))
        return;
    for (unsigned face = 0; face < 2; ++face) {
        const float depth = -(box[2] + float(face) * box[11]);
        const float left = ox + box[0] / depth * fx, right = ox + (box[0] + box[3]) / depth * fx;
        const float top = oy - (box[1] + box[7]) / depth * fy, bottom = oy - box[1] / depth * fy;
        if (!face) {
            out.left = left;
            out.right = right;
            out.top = top;
            out.bottom = bottom;
        } else {
            out.left = std::max(out.left, left);
            out.right = std::min(out.right, right);
            out.top = std::max(out.top, top);
            out.bottom = std::min(out.bottom, bottom);
        }
    }
    out.left += 1;
    out.right -= 1;
    out.top += 1;
    out.bottom -= 1;
    if (!std::isfinite(out.left) || !std::isfinite(out.right) || !std::isfinite(out.top) ||
        !std::isfinite(out.bottom) || !(out.right > out.left && out.bottom > out.top))
        return;
    out.record = record;
    out.node = node;
    out.age = 0;
    ++dbg_.categoryCaptures;
    dbg_.capturedCategory = int(index);
    dbg_.categoryLeft = out.left;
    dbg_.categoryTop = out.top;
    dbg_.categoryRight = out.right;
    dbg_.categoryBottom = out.bottom;
}

// Reproduce 42ab78 camera math, but return absolute GS framebuffer coordinates.
// 42ac68 returns local viewport coordinates and omits the page's 132f60 X offset.
// 1e93b8 uploads GS centres +100/+104; subtract the full framebuffer's GS origin.
// Capture with the DRAW camera, not the restored world/preview camera.
bool MenuMouse::projectPlane(IGuest& g, uint32_t transform, float x, float y, GridPoint& p) const {
    const uint32_t sc = g.read32(a_.screenObjVar), ctx = g.read32(a_.ctxVar);
    if (!validPtr(transform) || !validPtr(sc) || !validPtr(ctx))
        return false;
    const uint32_t camera = g.read32(ctx + 0x4b4) + 0x14;
    if (!validPtr(camera))
        return false;
    float basis[2][3]{};
    for (int axis = 0; axis < 2; ++axis) {
        float len = 0;
        for (int k = 0; k < 3; ++k) {
            basis[axis][k] = g.readF(transform + 0xc + axis * 4 + k * 12);
            len += basis[axis][k] * basis[axis][k];
        }
        if (!std::isfinite(len) || len < 0.000001f)
            return false;
        for (float& v : basis[axis])
            v /= std::sqrt(len);
    }
    float delta[3], local[3]{};
    for (int k = 0; k < 3; ++k)
        delta[k] = g.readF(transform + k * 4) + basis[0][k] * x + basis[1][k] * y - g.readF(camera + k * 4);
    for (int axis = 0; axis < 3; ++axis) {
        for (int k = 0; k < 3; ++k)
            local[axis] += delta[k] * g.readF(camera + 0xc + axis * 4 + k * 12);
        const float invScale = g.readF(camera + 0x30 + axis * 4);
        local[axis] *= invScale * invScale;
    }
    if (!std::isfinite(local[2]) || local[2] >= -.001f)
        return false;
    p.x = g.readF(sc + 0x100) - (2048.0f - float(g.read32(sc + 0xf8)) * .5f) +
          local[0] / -local[2] * g.readF(sc + 0x48) * g.readF(sc + 0x40);
    p.y = g.readF(sc + 0x104) - (2048.0f - float(g.read32(sc + 0xfc)) * .5f) -
          local[1] / -local[2] * g.readF(sc + 0x4c) * g.readF(sc + 0x44);
    return std::isfinite(p.x) && std::isfinite(p.y);
}

bool MenuMouse::projectInventoryPlane(IGuest& g, uint32_t transform, float x, float y, float& sx, float& sy) const {
    GridPoint p;
    if (!projectPlane(g, transform, x, y, p))
        return false;
    sx = p.x;
    sy = p.y;
    return true;
}

// FUN_437170 draws cell centres at (section+68 + col*60, section+6c - row*64).
void MenuMouse::captureInventoryGrid(IGuest& g, uint32_t section) {
    gridAge_ = 100;
    if (!validPtr(section))
        return;
    const uint32_t screen = g.read32(section + 0x10);
    const uint32_t controller = g.read32(section + 0x70);
    const uint32_t transform = g.read32(section + 0xc);
    if (!validPtr(screen) || !validPtr(controller) || !validPtr(transform))
        return;
    if (g.read32(screen + 0x154) != 0 || g.read32(controller + 0x24) != 0)
        return;
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    if (!(cw > 0 && ch > 0))
        return;
    for (int cell = 0; cell < 24; ++cell)
        for (int corner = 0; corner < 4; ++corner) {
            const float x = g.readF(section + 0x68) + (cell % 8 + ((corner == 1 || corner == 2) ? .5f : -.5f)) * cw;
            const float y = g.readF(section + 0x6c) - (cell / 8 + ((corner >= 2) ? .5f : -.5f)) * ch;
            if (!projectPlane(g, transform, x, y, inventoryCells_[cell][corner]))
                return;
        }
    gridScreen_ = screen;
    gridController_ = controller;
    gridAge_ = 0;
    ++dbg_.inventoryCaptures;
    dbg_.gridLeft = inventoryCells_[0][0].x;
    dbg_.gridTop = inventoryCells_[0][0].y;
    dbg_.gridRight = inventoryCells_[23][2].x;
    dbg_.gridBottom = inventoryCells_[23][2].y;
}

namespace {
// Section vtables of inventory screen 0x15 (builder 42c9a0). Shop mode puts the shop list at index 2 instead of Skills.
constexpr uint32_t kSpellsVtable = 0x54f3a8, kSkillsVtable = 0x54f1f0, kJournalVtable = 0x54e288,
                   kShopVtable = 0x54f488;
constexpr float kJournalGlyphW = 23, kJournalGlyphH = 22; // font 0 cell of the journal's arrow glyphs
// The inventory's current, settled section `index` is `section` (vtable at +0x18) on the active screen.
bool settledSection(IGuest& g, uint32_t activeScreenVar, uint32_t section, uint32_t vtable, int index,
                    uint32_t& screen) {
    if (!validPtr(section) || index < 0 || g.read32(section + 0x18) != vtable)
        return false;
    screen = g.read32(section + 0x10);
    if (!validPtr(screen) || g.read32(activeScreenVar) != screen || g.read32(screen + kScrId) != kInventoryScreenId ||
        g.read32(screen + 0x154) != uint32_t(index) || g.read32(screen + 0x3c) != 0 ||
        g.readF(screen + 0x158) != 1.0f || g.readF(screen + 0x15c) != 0.0f)
        return false;
    const uint32_t sections = g.read32(screen + 0x1a8);
    return validPtr(sections) && uint32_t(index) < g.read32(screen + 0x1b0) &&
           g.read32(sections + uint32_t(index) * 4) == section;
}
uint32_t contentVtable(int kind) {
    static const uint32_t table[] = {0, kSpellsVtable, kSkillsVtable, kJournalVtable, kShopVtable};
    return kind >= 0 && kind < 5 ? table[kind] : 0;
}
} // namespace

bool MenuMouse::insideQuad(const GridPoint (&quad)[4], float x, float y) {
    float sign = 0;
    for (int k = 0; k < 4; ++k) {
        const auto& a = quad[k];
        const auto& b = quad[(k + 1) % 4];
        const float cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (std::fabs(cross) < .001f)
            continue;
        if (sign * cross < 0)
            return false;
        sign = cross;
    }
    return sign != 0;
}

// 1c1e90 draws a 2D image at GS (sc+0x50 + x, sc+0x58 + y); in the absolute GUI units of captureInventoryGrid
// (GS minus the framebuffer's 2048 - W/2 origin) that is the same offset. 132f60's page placement lives in sc+0x50.
bool MenuMouse::localToGui(IGuest& g, float x, float y, GridPoint& out) const {
    const uint32_t sc = g.read32(a_.screenObjVar);
    if (!validPtr(sc))
        return false;
    const float w = float(g.read32(sc + 0xf8)), h = float(g.read32(sc + 0xfc));
    if (!(w > 0 && w <= 4096 && h > 0 && h <= 4096))
        return false;
    out.x = g.readF(sc + 0x50) + x - (2048.0f - w * .5f);
    out.y = g.readF(sc + 0x58) + y - (2048.0f - h * .5f);
    return std::isfinite(out.x) && std::isfinite(out.y);
}

void MenuMouse::resetContent() {
    contentKind_ = Content::None;
    content_.clear();
    contentScreen_ = contentSection_ = contentController_ = 0;
    contentAge_ = 100;
    contentSectionIndex_ = -1;
    contentTarget_ = -1;
    contentWait_ = 0;
    contentAction_ = 0;
    contentCount_ = 0;
    journalPage_ = false;
    dbg_.contentHit = dbg_.contentTarget = -1;
}

void MenuMouse::startContent(Content kind, uint32_t screen, uint32_t section, uint32_t controller, int index) {
    if (kind != contentKind_ || section != contentSection_ || screen != contentScreen_) {
        contentTarget_ = -1;
        contentAction_ = 0;
    }
    // A section can draw more than once per frame (44a678 runs again without icons): keep this frame's items until the
    // next input update has aged them.
    const bool sameFrame =
        kind == contentKind_ && section == contentSection_ && screen == contentScreen_ && contentAge_ == 0;
    contentKind_ = kind;
    contentScreen_ = screen;
    contentSection_ = section;
    contentController_ = controller;
    contentSectionIndex_ = index;
    if (sameFrame)
        return;
    content_.clear();
    contentCount_ = 0;
    journalPage_ = false;
    contentAge_ = 0;
    ++dbg_.contentCaptures;
    dbg_.contentKind = int(kind);
    dbg_.contentItems = 0;
}

// Section-plane rectangle (y up: top > bottom) through the draw camera.
bool MenuMouse::addContent(IGuest& g, uint32_t transform, int index, float left, float top, float right, float bottom) {
    if (contentDrawn(index))
        return true;
    ContentItem item;
    item.index = index;
    if (!projectPlane(g, transform, left, top, item.quad[0]) || !projectPlane(g, transform, right, top, item.quad[1]) ||
        !projectPlane(g, transform, right, bottom, item.quad[2]) ||
        !projectPlane(g, transform, left, bottom, item.quad[3]))
        return false;
    addContentItem(item);
    return true;
}

void MenuMouse::addContentItem(const ContentItem& item) {
    if (content_.size() >= 64)
        return;
    if (content_.empty()) {
        dbg_.contentLeft = dbg_.contentRight = item.quad[0].x;
        dbg_.contentTop = dbg_.contentBottom = item.quad[0].y;
    }
    for (const auto& p : item.quad) {
        dbg_.contentLeft = std::min(dbg_.contentLeft, p.x);
        dbg_.contentRight = std::max(dbg_.contentRight, p.x);
        dbg_.contentTop = std::min(dbg_.contentTop, p.y);
        dbg_.contentBottom = std::max(dbg_.contentBottom, p.y);
    }
    content_.push_back(item);
    dbg_.contentItems = unsigned(content_.size());
}

// Spells (4475b0 constants, 4481e8 layout): spell i sits in cell (i % 5, i / 5) of the section's cell frame; only known
// spells (player+0x1028 table, non-zero first word) are drawn and selectable.
void MenuMouse::captureInventorySpells(IGuest& g, uint32_t section) {
    uint32_t screen = 0;
    if (!settledSection(g, a_.activeScreenVar, section, kSpellsVtable, 1, screen))
        return;
    const uint32_t player = g.read32(screen + 0x14c), controller = g.read32(section + 0x70);
    if (!validPtr(player) || controller != player + 0x584 || g.read32(controller) != 0 ||
        g.read32(controller + 0x24) != 1 || g.read32(controller + 0x4a8) == 0)
        return;
    const uint32_t table = g.read32(player + 0x1028), transform = g.read32(section + 0xc);
    const unsigned count = std::min(g.read32(player + 0x1030), 10u);
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    const float ox = g.readF(section + 0x68), oy = g.readF(section + 0x6c);
    if (!validPtr(table) || !(cw > 0 && ch > 0) || !std::isfinite(ox) || !std::isfinite(oy))
        return;
    startContent(Content::Spells, screen, section, controller, 1);
    contentCount_ = count;
    for (unsigned i = 0; i < count; ++i) {
        if (!g.read32(table + i * 0x14))
            continue;
        const float x = ox + float(i % 5) * cw, y = oy - float(i / 5) * ch;
        addContent(g, transform, int(i), x - cw * .5f, y + ch * .5f, x + cw * .5f, y - ch * .5f);
    }
}

// Skills (section 0x54f1f0): three rows, each with its own frame (*(row+0x14)); the name is centred between the label
// corners at row+0x28 / +0x30 and the level number at the value point row+0x38. Rows navigate and highlight only while
// skill points are unspent (447358, 446f78), so without points nothing is selectable.
void MenuMouse::captureInventorySkills(IGuest& g, uint32_t section) {
    uint32_t screen = 0;
    if (!settledSection(g, a_.activeScreenVar, section, kSkillsVtable, 2, screen))
        return;
    const uint32_t player = g.read32(screen + 0x14c);
    if (!validPtr(player) || g.read32(player + 0x590) || g.read32(section + 0x100) != 3 || g.read32(0x4fbad0u) != 3 ||
        g.read32(screen + 0x1e4))
        return;
    startContent(Content::Skills, screen, section, 0, 2);
    // Unspent points, stored rotated against a key: rotr8(*0x4fbaf4 ^ *0x4fe86c).
    const uint32_t key = g.read32(0x4fbaf4u) ^ g.read32(0x4fe86cu);
    const int32_t points = static_cast<int32_t>((key >> 8) | (key << 24));
    const uint32_t rows = g.read32(section + 0xfc);
    if (points <= 0 || !validPtr(rows))
        return;
    contentCount_ = 3;
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t row = rows + i * 0x1b4;
        const float left = g.readF(row + 0x28), top = g.readF(row + 0x2c), bottom = g.readF(row + 0x34);
        const float right = std::max(g.readF(row + 0x30), g.readF(row + 0x38) + 40.0f);
        if (std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom))
            addContent(g, g.read32(row + 0x14), int(i), left, top, right, bottom);
    }
}

void MenuMouse::beginInventoryJournal(IGuest& g, uint32_t section) {
    uint32_t screen = 0;
    if (!settledSection(g, a_.activeScreenVar, section, kJournalVtable, 4, screen))
        return;
    const uint32_t player = g.read32(screen + 0x14c);
    if (!validPtr(player) || g.read32(player + 0x590))
        return;
    startContent(Content::Journal, screen, section, 0, 4);
}

// 4387b0's clip rectangle {s32 left, top, right, bottom} in viewport-local pixels.
void MenuMouse::captureJournalPage(IGuest& g, uint32_t rect) {
    if (contentKind_ != Content::Journal || contentAge_ != 0 || !validPtr(rect))
        return;
    GridPoint tl, br;
    if (!localToGui(g, float(int32_t(g.read32(rect))), float(int32_t(g.read32(rect + 4))), tl) ||
        !localToGui(g, float(int32_t(g.read32(rect + 8))), float(int32_t(g.read32(rect + 12))), br) || !(br.x > tl.x) ||
        !(br.y > tl.y))
        return;
    journalPageQuad_[0] = tl;
    journalPageQuad_[1] = {br.x, tl.y};
    journalPageQuad_[2] = br;
    journalPageQuad_[3] = {tl.x, br.y};
    journalPage_ = true;
    if (content_.empty()) { // verification aid: the page shows in the content bounds until an arrow is drawn
        dbg_.contentLeft = tl.x;
        dbg_.contentTop = tl.y;
        dbg_.contentRight = br.x;
        dbg_.contentBottom = br.y;
    }
}

// 286170 draws the glyph cell with its top-left at (x, y), viewport-local pixels.
void MenuMouse::captureJournalGlyph(IGuest& g, unsigned glyph, int x, int y) {
    if (contentKind_ != Content::Journal || contentAge_ != 0 || glyph > 3)
        return;
    GridPoint tl, br;
    if (!localToGui(g, float(x), float(y), tl) ||
        !localToGui(g, float(x) + kJournalGlyphW, float(y) + kJournalGlyphH, br))
        return;
    if (contentDrawn(int(glyph)))
        return;
    ContentItem item;
    item.index = int(glyph);
    item.quad[0] = tl;
    item.quad[1] = {br.x, tl.y};
    item.quad[2] = br;
    item.quad[3] = {tl.x, br.y};
    addContentItem(item);
}

void MenuMouse::beginInventoryShop(IGuest& g, uint32_t section) {
    uint32_t screen = 0;
    if (!settledSection(g, a_.activeScreenVar, section, kShopVtable, 2, screen))
        return;
    const uint32_t player = g.read32(screen + 0x14c), controller = g.read32(section + 0x70);
    if (!validPtr(player) || controller != player + 0x584 || g.read32(controller) != 0 ||
        g.read32(controller + 0xc) != 1 || g.read32(controller + 0x24) != 2)
        return;
    const uint32_t shop = g.read32(controller + 0x18);
    if (!validPtr(shop) || !g.read32(shop + 0x1c))
        return;
    startContent(Content::Shop, screen, section, controller, 2);
    contentCount_ = g.read32(shop + 0x1c);
}

// 434e80: item centre (ox + col*cw + (w-1)*cw/2, oy - row*ch - (h-1)*ch/2), size w*cw x h*ch.
void MenuMouse::captureShopItem(IGuest& g, uint32_t section, unsigned index, unsigned width, unsigned height,
                                float column, float row) {
    if (contentKind_ != Content::Shop || contentAge_ != 0 || section != contentSection_ || index >= contentCount_ ||
        !width || width > 8 || !height || height > 4 || !std::isfinite(column) || !std::isfinite(row))
        return;
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    const float ox = g.readF(section + 0x68), oy = g.readF(section + 0x6c);
    if (!(cw > 0 && ch > 0) || !std::isfinite(ox) || !std::isfinite(oy))
        return;
    addContent(g, g.read32(section + 0xc), int(index), ox + (column - .5f) * cw, oy - (row - .5f) * ch,
               ox + (column + float(width) - .5f) * cw, oy - (row + float(height) - .5f) * ch);
}

bool MenuMouse::contentCurrent(IGuest& g, uint32_t scr) const {
    uint32_t screen = 0;
    if (contentKind_ == Content::None || scr != contentScreen_ || contentAge_ > 4 ||
        !settledSection(g, a_.activeScreenVar, contentSection_, contentVtable(int(contentKind_)), contentSectionIndex_,
                        screen) ||
        screen != scr)
        return false;
    switch (contentKind_) {
        case Content::Spells: {
            // 3422c8 refuses while a cast is under way (player+0x1040).
            const uint32_t casting = g.read32(g.read32(scr + 0x14c) + 0x1040);
            return g.read32(contentController_ + 0x24) == 1 && !(casting >= 1 && casting <= 4);
        }
        case Content::Shop:
            return g.read32(contentController_ + 0xc) == 1 && g.read32(contentController_ + 0x24) == 2;
        case Content::Skills:
            return g.read32(scr + 0x1e4) == 0;
        default:
            return true;
    }
}

int MenuMouse::contentSelected(IGuest& g) const {
    switch (contentKind_) {
        case Content::Spells:
            return int(g.read32(contentController_ + 0x4b4));
        case Content::Skills:
            return int(g.read32(contentSection_ + 0x58));
        case Content::Shop:
            return int(g.read32(contentController_ + 0x1c));
        default:
            return -1;
    }
}

bool MenuMouse::contentDrawn(int index) const {
    for (const auto& item : content_)
        if (item.index == index)
            return true;
    return false;
}

int MenuMouse::takeSpellSelection(IGuest& g, uint32_t controller, uint32_t commit) {
    if (commit || contentKind_ != Content::Spells || contentTarget_ < 0 || controller != contentController_ ||
        !contentCurrent(g, contentScreen_) || !contentDrawn(contentTarget_))
        return -1;
    return contentTarget_;
}

int MenuMouse::takeShopSelection(IGuest& g, uint32_t controller, uint32_t buy) {
    if (buy || contentKind_ != Content::Shop || contentTarget_ < 0 || controller != contentController_ ||
        !contentCurrent(g, contentScreen_) || !contentDrawn(contentTarget_) ||
        uint32_t(contentTarget_) >= g.read32(g.read32(controller + 0x18) + 0x1c))
        return -1;
    return contentTarget_;
}

// Spells, Skills, shop list: hover selects through the game's own navigation (the selector hooks substitute the
// requested destination; Skills steps its three rows), a click selects and then presses the section's action once the
// native selection acknowledges it. Journal: the arrow glyphs are clicked, the wheel scrolls over the page.
MenuMouseResult MenuMouse::updateContent(IGuest& g, uint32_t scr, const MenuMouseSample& s, bool inGui, int gx, int gy,
                                         bool moved) {
    MenuMouseResult res;
    const bool current = contentCurrent(g, scr);
    int hit = -1;
    if (current && inGui)
        for (const auto& item : content_)
            if (insideQuad(item.quad, float(gx), float(gy))) {
                hit = item.index;
                break;
            }
    dbg_.contentHit = hit;
    if (!current || (s.pressed & MBTN_RIGHT))
        contentTarget_ = -1;
    if (contentKind_ == Content::Journal) {
        static const uint16_t kArrow[4] = {Pad::LEFT, Pad::RIGHT, Pad::UP, Pad::DOWN};
        contentTarget_ = -1;
        if ((s.pressed & MBTN_LEFT) && hit >= 0 && hit < 4) {
            res.padTap = kArrow[hit];
            ++dbg_.presses;
        }
        if (s.wheel && current && inGui && journalPage_ && insideQuad(journalPageQuad_, float(gx), float(gy)))
            res.padTap = s.wheel > 0 ? Pad::UP : Pad::DOWN;
        else if (s.wheel && inGui)
            res.padTap = s.wheel > 0 ? Pad::L1 : Pad::R1;
        if ((s.pressed & MBTN_RIGHT) && inGui)
            res.padTap = Pad::SELECT;
        dbg_.contentTarget = -1;
        return res;
    }
    // A shop strip scrolls to keep its selection visible: hover selection would slide the stock under the cursor, so
    // it is used only when the whole stock is drawn.
    const bool hoverSelects = contentKind_ != Content::Shop || content_.size() >= contentCount_;
    if (hit >= 0 && moved && hoverSelects && !(s.pressed & MBTN_LEFT) && contentTarget_ != hit) {
        contentTarget_ = hit;
        contentAction_ = 0;
        contentWait_ = 0;
    }
    if ((s.pressed & MBTN_LEFT) && inGui) {
        contentTarget_ = hit;
        contentAction_ = hit < 0 ? 0 : contentKind_ == Content::Skills ? Pad::RIGHT : Pad::SQUARE;
        contentWait_ = 0;
    }
    if (s.wheel && inGui) {
        if (contentKind_ == Content::Skills && hit >= 0) {
            // Like the wheel over a slider: up spends a point on the row, down takes one back.
            contentTarget_ = hit;
            contentAction_ = s.wheel > 0 ? Pad::RIGHT : Pad::LEFT;
            contentWait_ = 0;
        } else {
            contentTarget_ = -1;
            res.padTap = s.wheel > 0 ? Pad::L1 : Pad::R1;
        }
    }
    if ((s.pressed & MBTN_RIGHT) && inGui)
        res.padTap = Pad::SELECT;
    if (contentTarget_ >= 0 && !res.padTap) {
        const int selected = contentSelected(g);
        if (!current || !contentDrawn(contentTarget_) || ++contentWait_ > 60) {
            contentTarget_ = -1;
        } else if (contentController_ && g.read32(contentController_) != 0) {
            // The inventory controller is still busy with the last selection change (345d08 drops keys while
            // controller+0 is set): wait instead of losing the tap.
        } else if (selected == contentTarget_) {
            // Native acknowledgement: the section's own selection now is the target.
            if (contentAction_) {
                res.padTap = contentAction_;
                ++dbg_.presses;
            }
            contentTarget_ = -1;
        } else if (contentWait_ % 3 == 1) {
            if (contentKind_ == Content::Skills)
                res.padTap = (contentTarget_ - selected + 3) % 3 == 1 ? Pad::DOWN : Pad::UP;
            else // any arrow that reaches the native selector, which substitutes the target (edges refuse one way)
                res.padTap = (contentWait_ / 3) % 2 ? Pad::LEFT : Pad::RIGHT;
            ++dbg_.taps;
        }
    }
    if (contentTarget_ < 0)
        contentAction_ = 0;
    dbg_.contentTarget = contentTarget_;
    return res;
}

bool MenuMouse::inventoryCategoryHovered(IGuest& g, uint32_t record) const {
    const int hit = dbg_.categoryHit;
    if (hit < 0 || hit >= 5 || !validPtr(categoryScreen_) || g.read32(a_.activeScreenVar) != categoryScreen_ ||
        g.read32(categoryScreen_ + kScrId) != kInventoryScreenId ||
        g.read32(categoryScreen_ + 0x154) == uint32_t(hit) || g.readF(categoryScreen_ + 0x158) != 1.0f ||
        g.readF(categoryScreen_ + 0x15c) != 0.0f || g.read32(categoryScreen_ + 0x3c) != 0)
        return false;
    const auto& r = categories_[hit];
    return r.age <= 4 && r.record == record && validPtr(record) && g.read32(record + 0x1c) == r.node &&
           validPtr(r.node) && (g.read32(r.node + 0x104) & 0x2000u) != 0;
}

int MenuMouse::inventoryHit(int x, int y) const {
    for (int cell = 0; cell < 24; ++cell)
        if (insideQuad(inventoryCells_[cell], float(x), float(y)))
            return cell;
    return -1;
}

int MenuMouse::inventoryOrigin(IGuest& g, int cell) const {
    if (cell < 0 || cell >= 24)
        return -1;
    const uint32_t value = g.read32(gridController_ + 0x38 + cell * 16);
    if (!value)
        return -1;
    // Native 340510: continuation marker encodes column*10 + row offset.
    const int origin = value <= 0x53 ? cell - int(value / 10) - int(value % 10) * 8 : cell;
    if (origin < 0 || origin >= 24 || g.read32(gridController_ + 0x38 + origin * 16) <= 0x53)
        return -1;
    return origin;
}

int MenuMouse::takeInventorySelection(IGuest& g, uint32_t controller) {
    if (!opt_.inventoryMouse || inventoryTarget_ < 0 || controller != gridController_ || gridAge_ > 4 ||
        g.read32(a_.activeScreenVar) != gridScreen_ || g.read32(gridScreen_ + 0x154) != 0 ||
        g.read32(controller + 0x24) != 0 || inventoryOrigin(g, inventoryTarget_) != inventoryTarget_ ||
        g.read32(controller + 0x38 + inventoryTarget_ * 16) != inventoryItem_)
        return -1;
    return inventoryTarget_;
}

uint64_t MenuMouse::mapIdentity(IGuest& g, unsigned index) const {
    if (index >= mapCount_)
        return 0;
    uint64_t hash = 1469598103934665603ull;
    // +8 is a native animation phase, not stable identity.
    for (unsigned offset : {0u, 4u, 0x10u, 0x14u, 0x18u, 0x1cu})
        hash = (hash ^ g.read32(mapTable_ + index * 0x20 + offset)) * 1099511628211ull;
    return hash;
}

bool MenuMouse::mapCurrent(IGuest& g, uint32_t scr) const {
    if (!validPtr(scr) || scr != mapScreen_ || !validPtr(mapSection_) || mapAge_ > 2 ||
        g.read32(a_.activeScreenVar) != scr || g.read32(scr + 0x18) != 21 || g.read32(scr + 0x154) != 3 ||
        g.read32(scr + 0x3c) != 0 || g.readF(scr + 0x158) != 1 || g.readF(scr + 0x15c) != 0 ||
        g.read32(0x4fbb70) != mapTable_ || g.read32(0x4fbb78) != mapCount_ || g.read32(mapSection_ + 0x10) != scr ||
        g.read32(mapSection_ + 0x70) != mapRegion_)
        return false;
    const uint32_t sections = g.read32(scr + 0x1a8);
    return validPtr(sections) && g.read32(scr + 0x1b0) == 5 && g.read32(sections + 3 * 4) == mapSection_;
}

void MenuMouse::beginInventoryMap(IGuest& g, uint32_t section) {
    mapRects_.clear();
    mapAge_ = 100;
    if (!validPtr(section))
        return;
    const uint32_t scr = g.read32(section + 0x10), base = g.read32(0x4fbb70), count = g.read32(0x4fbb78);
    if (!validPtr(scr) || !validPtr(base) || !count || count > 256 || base > kRamEnd - count * 0x20)
        return;
    const uint32_t region = g.read32(section + 0x70);
    if (scr != mapScreen_ || section != mapSection_ || base != mapTable_ || count != mapCount_ || region != mapRegion_)
        mapTarget_ = -1;
    mapScreen_ = scr;
    mapSection_ = section;
    mapTable_ = base;
    mapCount_ = count;
    mapRegion_ = region;
    mapAge_ = 0;
    if (!mapCurrent(g, scr)) {
        mapAge_ = 100;
        mapTarget_ = -1;
    }
}

void MenuMouse::captureInventoryMapMarker(IGuest& g, uint32_t section, unsigned index, uint32_t image,
                                          uint32_t vertices, unsigned count) {
    if (section != mapSection_ || !mapCurrent(g, mapScreen_) || index >= mapCount_ || !validPtr(image) ||
        (g.read32(image + 0x20) & 1) == 0 || (count != 2 && count != 4) || vertices < 0x100000 ||
        vertices > kRamEnd - count * 32)
        return;
    const uint32_t record = mapTable_ + index * 32, renderer = g.read32(a_.screenObjVar);
    if (!validPtr(renderer) || !g.read32(record + 0x10) || g.read32(record + 0x14) != mapRegion_)
        return;
    const float w = float(g.read32(renderer + 0xf8)), h = float(g.read32(renderer + 0xfc));
    if (!(w > 0 && w <= 2048 && h > 0 && h <= 2048))
        return;
    MapRect r;
    r.left = r.top = 1e9f;
    r.right = r.bottom = -1e9f;
    for (unsigned i = 0; i < count; ++i) {
        const float x = g.readF(vertices + i * 32) - (2048 - w * .5f);
        const float y = g.readF(vertices + i * 32 + 4) - (2048 - h * .5f);
        if (!std::isfinite(x) || !std::isfinite(y))
            return;
        r.left = std::min(r.left, x);
        r.top = std::min(r.top, y);
        r.right = std::max(r.right, x);
        r.bottom = std::max(r.bottom, y);
    }
    r.left = std::max(0.f, r.left);
    r.top = std::max(0.f, r.top);
    r.right = std::min(w, r.right);
    r.bottom = std::min(h, r.bottom);
    if (r.left >= r.right || r.top >= r.bottom)
        return;
    r.index = index;
    r.image = image;
    r.identity = mapIdentity(g, index);
    r.imageVtable = g.read32(image + 0x18);
    r.imageWidth = g.read32(image + 0x24);
    r.imageHeight = g.read32(image + 0x28);
    // Repeated submissions for one record are ambiguous too: never union them
    // into a larger clickable area that includes undrawn space.
    if (mapRects_.size() < 256) {
        mapRects_.push_back(r);
        ++dbg_.mapCaptures;
    }
}

MenuMouseResult MenuMouse::updateMap(IGuest& g, uint32_t scr, const MenuMouseSample& s, bool inGui, int gx, int gy) {
    MenuMouseResult res;
    dbg_.mapHit = -1;
    dbg_.mapAge = mapAge_;
    int hit = -1;
    uint64_t identity = 0;
    bool ambiguous = false;
    if (inGui && mapCurrent(g, scr))
        for (const auto& r : mapRects_) {
            if (gx < r.left || gx >= r.right || gy < r.top || gy >= r.bottom)
                continue;
            if (r.identity != mapIdentity(g, r.index) || (g.read32(r.image + 0x20) & 1) == 0 ||
                g.read32(r.image + 0x18) != r.imageVtable || g.read32(r.image + 0x24) != r.imageWidth ||
                g.read32(r.image + 0x28) != r.imageHeight)
                continue;
            if (hit >= 0) {
                ambiguous = true;
                break;
            }
            hit = int(r.index);
            identity = r.identity;
        }
    if (ambiguous)
        hit = -1;
    // Native dynamic state is outside record identity. Query only one unique
    // candidate, using the checked host adapter; failed/incomplete calls deny.
    if (hit >= 0) {
        int state = -1;
        if (!mapStateQuery_ || !mapStateQuery_(g, mapTable_ + unsigned(hit) * 32, state) || (state != 0 && state != 2))
            hit = -1;
    }
    dbg_.mapHit = hit;
    if (s.pressed & MBTN_LEFT) {
        mapTarget_ = hit;
        mapTargetIdentity_ = identity;
        mapWait_ = 0;
        mapRequestX_ = s.cx;
        mapRequestY_ = s.cy;
        mapStall_ = 0;
        mapLastSelected_ = g.read32(mapSection_ ? mapSection_ + 0x6c : 0);
    }
    if (!inGui || !mapCurrent(g, scr) || s.wheel || (s.pressed & MBTN_RIGHT))
        mapTarget_ = -1;
    if (mapTarget_ >= 0) {
        // Requests require the same freshly drawn marker on every update.
        const uint32_t selected = g.read32(mapSection_ + 0x6c);
        mapStall_ = selected == mapLastSelected_ ? mapStall_ + 1 : 0;
        mapLastSelected_ = selected;
        if (hit != mapTarget_ || identity != mapTargetIdentity_ || std::fabs(s.cx - mapRequestX_) >= .5f ||
            std::fabs(s.cy - mapRequestY_) >= .5f || selected >= mapCount_ || ++mapWait_ > 3 * (mapCount_ + 1) ||
            mapStall_ > 12)
            mapTarget_ = -1;
        else if (g.read32(mapSection_ + 0x6c) == unsigned(mapTarget_))
            mapTarget_ = -1; // Native acknowledgement. Map has no Cross action.
        else if (mapWait_ % 3 == 1) {
            res.padTap = Pad::RIGHT;
            ++dbg_.taps;
        }
    }
    if (s.wheel && inGui)
        res.padTap = s.wheel > 0 ? Pad::L1 : Pad::R1;
    if ((s.pressed & MBTN_RIGHT) && inGui)
        res.padTap = Pad::SELECT;
    dbg_.mapTarget = mapTarget_;
    return res;
}

MenuMouseResult MenuMouse::updateInventory(IGuest& g, uint32_t scr, const MenuMouseSample& sample, const MenuView& view,
                                           bool moved) {
    MenuMouseResult res;
    // A view (inventoryView_) has no tabs and keeps its section: the wheel is not a section switch there.
    MenuMouseSample s = sample;
    if (inventoryView_)
        s.wheel = 0;
    const int section = static_cast<int>(g.read32(scr + 0x154));
    invSection_.store(section);
    if (mapAge_ < 100)
        ++mapAge_;
    if (contentAge_ < 100)
        ++contentAge_;
    if (section != 3) {
        mapAge_ = 100;
        mapTarget_ = -1;
        mapRects_.clear();
    }
    const uint32_t sc = g.read32(a_.screenObjVar);
    int W = 512, H = 512;
    if (validPtr(sc)) {
        W = static_cast<int>(g.read32(sc + 0xf8));
        H = static_cast<int>(g.read32(sc + 0xfc));
    }
    int gx = 0, gy = 0;
    MenuView gridView = view;
    gridView.hudAnchorX = 0.0f;
    gridView.hudScaleX = view.inventoryPageScaleX; // undo only the actual whole-page transform
    // A view's page is centred on the screen by the renderer (hud_page.h inventoryMap), not left-anchored.
    if (inventoryView_)
        gridView.hudAnchorX = 0.5f;
    const bool inGui = s.inside && mapToGui(s, gridView, W, H, &gx, &gy);
    ++gridAge_; // Category-row early returns must not freeze old grid bounds.
    // Categories are native 3D page geometry on every section, independent of
    // the legacy relative adapter's HUD sprite scale.
    MenuView categoryView = view;
    categoryView.hudScaleX = view.inventoryPageScaleX;
    categoryView.hudAnchorX = 0.0f;
    int tx = 0, ty = 0;
    const bool inCategoryGui = s.inside && mapToGui(s, categoryView, W, H, &tx, &ty);
    const uint32_t player = g.read32(scr + 0x14c), base = g.read32(scr + 0x19c);
    const uint32_t tabs = g.read32(scr + 0x1a4);
    // A view hides the tabs: no category hits, and their last boxes must not swallow clicks either.
    const unsigned categoryCount =
        !inventoryView_ && validPtr(player) && player < kRamEnd - 0x594 && (tabs == 5 || tabs == 3)
            ? (g.read32(player + 0x590) ? 3u : 5u)
            : 0u;
    const auto currentCategory = [&](unsigned i) {
        const auto& r = categories_[i];
        return categoryScreen_ == scr && r.age <= 4 && validPtr(base) && g.readF(scr + 0x158) == 1.0f &&
               g.readF(scr + 0x15c) == 0.0f && g.read32(scr + 0x3c) == 0 && r.record == base + i * 0x24 &&
               g.read32(r.record + 0x1c) == r.node && validPtr(r.node) && (g.read32(r.node + 0x104) & 0x2000u) != 0;
    };
    int categoryHit = -1;
    bool categoryBand = false;
    for (unsigned i = 0; i < 5; ++i) {
        auto& r = categories_[i];
        if (r.age < 100)
            ++r.age;
        // Old bounds may only suppress a blind legacy activation, never
        // authorize navigation. Fresh capture remains required for a hit.
        if (!inventoryView_ && categoryScreen_ == scr && r.record && inCategoryGui && ty >= r.top && ty < r.bottom)
            categoryBand = true;
        if (i >= categoryCount || !currentCategory(i) || !inCategoryGui)
            continue;
        if (ty >= r.top && ty < r.bottom)
            categoryBand = true;
        if (tx >= r.left && tx < r.right && ty >= r.top && ty < r.bottom)
            categoryHit = int(i);
    }
    dbg_.categoryHit = categoryHit;
    if (categoryBand) {
        mapTarget_ = -1;
        contentTarget_ = -1;
    }
    if (s.wheel || (s.pressed & MBTN_RIGHT) || !s.inside || section < 0 || unsigned(section) >= categoryCount)
        categoryTarget_ = -1;
    if (s.pressed & MBTN_LEFT) {
        categoryTarget_ = categoryHit;
        categoryWait_ = 0;
        if (categoryHit >= 0 || categoryBand) {
            inventoryTarget_ = -1;
            inventoryActivate_ = false;
            contentTarget_ = -1;
            // A click between proven icon boxes must not fall through to the
            // relative section's Cross activation of the current item.
            if (categoryHit < 0 || categoryHit == section) {
                categoryTarget_ = -1;
                dbg_.categoryTarget = -1;
                return res;
            }
        }
    }
    if (categoryTarget_ >= 0) {
        mapTarget_ = -1;
        inventoryTarget_ = -1;
        inventoryActivate_ = false;
        contentTarget_ = -1;
        if (unsigned(categoryTarget_) >= categoryCount || !currentCategory(unsigned(categoryTarget_)) ||
            ++categoryWait_ > 24 || section == categoryTarget_)
            categoryTarget_ = -1;
        else if (categoryWait_ % 3 == 1) {
            const unsigned forward = (unsigned(categoryTarget_) + categoryCount - unsigned(section)) % categoryCount;
            res.padTap = forward <= categoryCount - forward ? Pad::R1 : Pad::L1;
            ++dbg_.taps;
        }
        dbg_.categoryTarget = categoryTarget_;
        return res; // Native acknowledgement, never a direct section write/activation.
    }
    dbg_.categoryTarget = -1;
    if (categoryBand && !s.wheel && !(s.pressed & MBTN_RIGHT)) {
        inventoryTarget_ = -1;
        inventoryActivate_ = false;
        return res;
    }
    if (section == 3)
        return updateMap(g, scr, s, inGui, gx, gy);
    if (contentKind_ != Content::None && contentScreen_ == scr && contentSectionIndex_ == section)
        return updateContent(g, scr, s, inGui, gx, gy, moved);
    const bool absoluteGrid = section == 0 && gridScreen_ == scr && gridAge_ <= 4;
    if (s.pressed & MBTN_LEFT) {
        ++dbg_.inventoryClickSerial;
        dbg_.clickSample = s;
        dbg_.clickView = view;
        dbg_.clickGridAge = gridAge_;
        dbg_.clickW = W;
        dbg_.clickH = H;
        dbg_.clickInGui = inGui;
        dbg_.clickAbsoluteGrid = absoluteGrid;
        dbg_.gx = inGui ? gx : -1;
        dbg_.gy = inGui ? gy : -1;
        dbg_.clickRawCell = absoluteGrid && inGui ? inventoryHit(gx, gy) : -1;
        dbg_.clickItem = absoluteGrid ? inventoryOrigin(g, dbg_.clickRawCell) : -1;
        dbg_.clickSelected = absoluteGrid ? static_cast<int>(g.read32(gridController_ + 0x20)) : -1;
    }
    if (absoluteGrid) {
        const int hit = inGui ? inventoryOrigin(g, inventoryHit(gx, gy)) : -1;
        dbg_.inventoryCell = hit;
        if (s.wheel || (s.pressed & MBTN_RIGHT)) {
            inventoryTarget_ = -1;
            inventoryActivate_ = false;
        } else if ((moved || (s.pressed & MBTN_LEFT)) && hit >= 0) {
            inventoryTarget_ = hit;
            inventoryItem_ = g.read32(gridController_ + 0x38 + hit * 16);
            inventoryActivate_ = (s.pressed & MBTN_LEFT) != 0;
            inventoryWait_ = 0;
        } else if ((s.pressed & MBTN_LEFT) || !inGui) {
            inventoryTarget_ = -1;
            inventoryActivate_ = false;
        }
        if (inventoryTarget_ >= 0) {
            if (inventoryOrigin(g, inventoryTarget_) != inventoryTarget_ ||
                g.read32(gridController_ + 0x38 + inventoryTarget_ * 16) != inventoryItem_ || ++inventoryWait_ > 60) {
                inventoryTarget_ = -1;
                inventoryActivate_ = false;
            } else if (g.read32(gridController_) != 0) {
                // The controller is still busy with the selection change (345d08 drops keys while controller+0 is
                // set): an action tapped now would be lost.
            } else if (g.read32(gridController_ + 0x20) == uint32_t(inventoryTarget_)) {
                if (inventoryActivate_) {
                    // In a shop (controller+0xc) Equipment is the sell pane: Circle opens "Sell Item To Store?".
                    res.padTap = g.read32(gridController_ + 0xc) == 1 ? Pad::CIRCLE
                                 : opt_.inventoryClickSquare          ? Pad::SQUARE
                                                                      : Pad::CROSS;
                    ++dbg_.presses;
                }
                inventoryTarget_ = -1;
                inventoryActivate_ = false;
            } else if (inventoryWait_ % 3 == 1) {
                // A normal native selection event consumes the requested destination in 33eaf8.
                // Alternate axes in case a row contains only the current multi-cell item.
                res.padTap = ((inventoryWait_ / 3) % 2) ? Pad::DOWN : Pad::RIGHT;
                ++dbg_.taps;
            }
        }
        if (s.wheel && inGui)
            res.padTap = s.wheel > 0 ? Pad::L1 : Pad::R1;
        if ((s.pressed & MBTN_RIGHT) && inGui)
            res.padTap = Pad::SELECT;
        dbg_.inventoryTarget = inventoryTarget_;
        dbg_.gx = inGui ? gx : -1;
        dbg_.gy = inGui ? gy : -1;
        return res;
    }
    inventoryTarget_ = -1;
    inventoryActivate_ = false;
    dbg_.inventoryTarget = -1;
    // No certified content in this section right now (stale or missing draw capture, an empty Spells page, Skills
    // without points): pointer motion and clicks do nothing rather than steer an item the cursor is not on. Wheel and
    // right click keep their section-wide meaning.
    if (s.wheel && inGui) {
        res.padTap = s.wheel > 0 ? Pad::L1 : Pad::R1;
        ++dbg_.taps;
    }
    if ((s.pressed & MBTN_RIGHT) && inGui) {
        res.padTap = Pad::SELECT;
        ++dbg_.presses;
    }
    dbg_.gx = inGui ? gx : -1;
    dbg_.gy = inGui ? gy : -1;
    return res;
}

MenuMouseResult MenuMouse::update(IGuest& g, const MenuMouseSample& s, const MenuView& view, float idleHideMs) {
    MenuMouseResult res;
    const uint32_t scr = g.read32(a_.activeScreenVar);
    if (!validPtr(scr)) {
        reset();
        screen_ = 0;
        screenId_.store(-1);
        visible_.store(false);
        havePrev_ = false;
        return res;
    }
    screenId_.store(static_cast<int>(g.read32(scr + kScrId)));
    if (scr != screen_) {
        reset(); // the screen changed: a press on the old one can no longer be released
        screen_ = scr;
    }
    pumpOps(g);
    const auto now = std::chrono::steady_clock::now();
    const bool moved =
        havePrev_ && s.inside && (std::fabs(s.cx - prevCx_) >= 0.5f || std::fabs(s.cy - prevCy_) >= 0.5f);
    if (s.inside) {
        prevCx_ = s.cx;
        prevCy_ = s.cy;
        havePrev_ = true;
    } else
        havePrev_ = false;
    if (moved || s.pressed || s.wheel) {
        lastMove_ = now;
        everMoved_ = true;
    }
    visible_.store(
        s.focused && s.inside && everMoved_ &&
        (idleHideMs <= 0.0f || std::chrono::duration<float, std::milli>(now - lastMove_).count() < idleHideMs));
    if (!s.focused) {
        reset();
        return res;
    }
    // Pages without widgets that the game closes on a key: the full-screen text page (0x10) and the movies (0x1d /
    // 0x1e) take START (key 1), the credits (0x13) any key, the memory-card warning (0x1c) Cross. A left click
    // continues them.
    if (s.pressed & MBTN_LEFT) {
        const uint32_t id = g.read32(scr + kScrId);
        const uint16_t key = id == 0x10 || id == 0x1d || id == 0x1e ? Pad::START
                             : id == 0x13 || id == 0x1c             ? Pad::CROSS
                                                                    : 0;
        if (key) {
            res.padTap = key;
            ++dbg_.presses;
            return res;
        }
    }

    const uint32_t sc = g.read32(a_.screenObjVar);
    if (!validPtr(sc))
        return res;
    const int W = static_cast<int>(g.read32(sc + 0xf8)), H = static_cast<int>(g.read32(sc + 0xfc));
    int gx = 0, gy = 0;
    const bool inGui = mapToGui(s, view, W, H, &gx, &gy);
    dbg_.gx = inGui ? gx : -1;
    dbg_.gy = inGui ? gy : -1;
    const bool leftDown = (s.down & MBTN_LEFT) != 0;
    if (static_cast<int>(g.read32(scr + kScrId)) == kInventoryScreenId) {
        if (!opt_.inventoryMouse) {
            reset();
            return res;
        }
        return updateInventory(g, scr, s, view, moved);
    }
    invSection_.store(-1);

    // ---- hotspots published by the host for this screen (load / save slot grid and page buttons): they replace the
    // widget logic
    ++frame_;
    {
        HotspotHandler fn;
        int hotId = -1;
        bool hotMode = false;
        {
            std::lock_guard<std::mutex> lk(hotM_);
            if (!hot_.empty() && hotScreen_ == scr && frame_ - hotFrame_ <= 4u) {
                hotMode = true;
                fn = hotFn_;
                if (inGui)
                    for (auto it = hot_.rbegin(); it != hot_.rend(); ++it)
                        if (gx >= it->x && gx < it->x + it->w && gy >= it->y && gy < it->y + it->h) {
                            hotId = it->id;
                            break;
                        }
            }
        }
        hotHover_.store(hotMode ? hotId : -1);
        if (hotMode && (s.pressed & MBTN_LEFT) && std::getenv("DRAKAN_MENU_MOUSE_LOG"))
            std::fprintf(stderr, "[menu-mouse] screen=%d cursor=%.1f,%.1f gui=%d,%d inside=%d hotspot=%d\n",
                         screenId_.load(), s.cx, s.cy, gx, gy, int(inGui), hotId);
        if (hotMode) {
            if (fn && hotId >= 0) {
                if (moved && hotId != lastHotHover_)
                    fn(g, hotId, HotspotEvent::Hover);
                if (s.pressed & MBTN_LEFT) {
                    fn(g, hotId, HotspotEvent::Press);
                    ++dbg_.presses;
                }
            }
            lastHotHover_ = hotId;
            if (s.wheel != 0 && inGui) {
                res.padTap |= s.wheel > 0 ? Pad::UP : Pad::DOWN;
                ++dbg_.taps;
            }
            return res;
        }
        lastHotHover_ = -1;
    }

    uint32_t hit = inGui ? hitTest(g, scr, gx, gy) : 0;
    if ((s.pressed || s.released) && std::getenv("DRAKAN_MENU_MOUSE_LOG"))
        std::fprintf(stderr,
                     "[menu-mouse] screen=%d cursor=%.1f,%.1f view=%.1f,%.1f,%.1f,%.1f scale=%g anchor=%g gui=%d,%d "
                     "inside=%d hit=%x pressed=%u released=%u\n",
                     screenId_.load(), s.cx, s.cy, view.x, view.y, view.w, view.h, view.hudScaleX, view.hudAnchorX, gx,
                     gy, int(inGui), hit, unsigned(s.pressed), unsigned(s.released));
    dbg_.hover = hit;
    dbg_.focus = g.read32(scr + kScrFocus);

    // ---- hover: move the highlight like the D-pad does (same focus change, same tick sound)
    if (moved && hit && !leftDown && !pressActive_) {
        const uint32_t focus = g.read32(scr + kScrFocus);
        if (hit != focus && (g.read32(hit + kWFlags) & 0x100u) == 0) {
            setFocus(g, scr, hit);
            playUiSound(g, 0x2a);
            ++dbg_.focusMoves;
        }
    }

    // ---- left button: press on the widget under the cursor (focus + the widget's own press handler: click sound,
    // pressed flag,
    //      slider thumb grab), release = activation (message 1 to the screen handler) or end of a slider drag
    if ((s.pressed & MBTN_LEFT) && inGui) {
        if (hit) {
            setFocus(g, scr, hit);
            // Native radio activation can wait for its sound resource. Let the
            // ordinary pad dispatcher run it on the live guest context instead
            // of entering that wait from a synchronous, non-preemptible call.
            if (g.readS16(hit + kWType) == 5) {
                res.padTap |= Pad::CROSS;
                ++dbg_.presses;
                return res;
            }
            g.write32(a_.capturedWidgetVar, hit);
            pressedWidget_ = hit;
            pressActive_ = true;
            // Sliders, check boxes and selectors may have to load a sound resource before they return. A wait inside a
            // nested call cannot be continued (the call would be abandoned with the sound manager lock held), so their
            // handlers run as their own guest calls, in order (queueOp / pumpOps). Buttons stay synchronous.
            GuestCall pc;
            int pressX = gx, thumbCenter = 0;
            const bool jump = g.readS16(hit + kWType) == 9 && (g.read32(hit + kWFlags) & 0x4000u) != 0 &&
                              sliderThumbCenter(g, hit, &thumbCenter);
            if (jump)
                pressX = thumbCenter; // grab the thumb, then drag it to the click: the value follows the pointer
            if (g.readS16(hit + kWType) != 3 &&
                makeVcall(g, pc, hit, kWVtbl, 0x2c, 3, static_cast<uint32_t>(pressX), static_cast<uint32_t>(gy), 3)) {
                queueOp(pc, false);
                GuestCall dc;
                if (jump &&
                    makeVcall(g, dc, hit, kWVtbl, 0x34, 3, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 3))
                    queueOp(dc, false);
                pumpOps(g);
            } else
                vcall(g, hit, kWVtbl, 0x2c, 3, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 3);
            ++dbg_.presses;
        }
        // a click that is not over a widget does nothing (it used to be a Cross tap on pages without widgets; a click
        // next to a menu row must never activate the highlighted row)
    }
    if (pressActive_ && moved && leftDown && isChild(g, scr, pressedWidget_)) {
        // drag (slider)
        GuestCall dc;
        if (g.readS16(pressedWidget_ + kWType) != 3 &&
            makeVcall(g, dc, pressedWidget_, kWVtbl, 0x34, 3, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy),
                      3)) {
            queueOp(dc, true);
            pumpOps(g);
        } else
            vcall(g, pressedWidget_, kWVtbl, 0x34, 3, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 3);
    }
    if (pressActive_ && ((s.released & MBTN_LEFT) || !leftDown)) {
        const uint32_t w = pressedWidget_;
        const bool still = isChild(g, scr, w) && g.read32(a_.capturedWidgetVar) == w;
        pressActive_ = false;
        pressedWidget_ = 0;
        if (still) {
            const bool over = inGui && hit == w;
            const bool isButton = g.readS16(w + kWType) == 3;
            const bool async = g.readS16(w + kWType) != 3;
            if (async)
                clearCaptured_ = true; // cleared by pumpOps once the release has run
            else
                g.write32(a_.capturedWidgetVar, 0);
            if (over || !isButton) {
                // release = activate
                GuestCall rc;
                if (async &&
                    makeVcall(g, rc, w, kWVtbl, 0x2c, 2, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 3)) {
                    queueOp(rc, false);
                    pumpOps(g);
                } else
                    vcall(g, w, kWVtbl, 0x2c, 2, static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 3);
                ++dbg_.releases;
            } else {
                g.write32(w + kWFlags, g.read32(w + kWFlags) & ~0x80u); // released outside a button: cancel
                g.write32(w + kWDirty, 1);
            }
        }
    }

    // ---- wheel: up / down moves the highlight (D-pad), over a slider it changes the value (left / right)
    if (s.wheel != 0 && inGui) {
        const bool slider = hit && g.readS16(hit + kWType) == 9;
        const bool up = s.wheel > 0;
        res.padTap |= slider ? (up ? Pad::RIGHT : Pad::LEFT) : (up ? Pad::UP : Pad::DOWN);
        ++dbg_.taps;
    }
    return res;
}

} // namespace drakan_input
