// drakan_item_overlay_policy.h: the depth clear under a quick bar item and its parts (GitHub issue 22).
#include "../src/lib/drakan_item_overlay_policy.h"
#include <cstdio>
#include <cstdlib>

using namespace drakan_item_overlay;

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

static bool same(const Rect& r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    return r.x0 == x0 && r.y0 == y0 && r.x1 == x1 && r.y1 == y1;
}

// A camera at the origin looking down -z (w = -z), 640 x 512 frame centred, 100 pixels per unit at distance 1.
static Projector camera() {
    Projector p;
    p.rowX = {1, 0, 0};
    p.rowY = {0, 1, 0};
    p.rowW = {0, 0, 1};
    p.offsetX = 320;
    p.offsetY = 256;
    p.scaleX = 100;
    p.scaleY = 100;
    return p;
}

int main() {
    const Projector p = camera();
    float x = 0, y = 0;
    check(p.project({1, 1, -2}, x, y) && x == 370 && y == 206, "0x1336a8: x = offset + X/w * scale, y = offset - Y/w");
    check(!p.project({0, 0, 0}, x, y) && !p.project({0, 0, 3}, x, y), "a point on or behind the camera is refused");
    Projector moved = p;
    moved.camera = {10, 0, 0};
    check(moved.project({11, 1, -2}, x, y) && x == 370 && y == 206, "relative to the camera position");

    // The club as the bar draws it: the handle is the item handle's own box, the head a part above it.
    const Box handle{{-0.1f, -1.0f, -5.0f}, {0.2f, 0, 0}, {0, 1.0f, 0}, {0, 0, 0.2f}};
    Rect item;
    check(boxRect(p, handle, item), "the handle box projects");
    // Corners at z -5 and -4.8: x 320 +- 0.1 * 100 / 4.8, y 256 + 0 .. 1 * 100 / 4.8, truncated like cvt.w.s.
    check(same(item, 317, 256, 322, 276), "the native rectangle is the min / max of the eight truncated corners");
    const Box head{{-0.5f, 0.0f, -5.0f}, {1.0f, 0, 0}, {0, 0.6f, 0}, {0, 0, 0.2f}};
    Rect headRect;
    check(boxRect(p, head, headRect) && same(headRect, 309, 243, 330, 256), "the head projects above the handle");

    Coverage cover(item, 640, 512);
    check(!cover.widened() && same(cover.rect(), 317, 256, 322, 276), "nothing added: the native rectangle");
    check(cover.add(headRect), "a part touching the item is used");
    check(cover.widened() && same(cover.rect(), 309, 243, 330, 276), "the clear covers the handle and the head");

    // A stale part box (a part last drawn elsewhere) far from the item is not used.
    Coverage far(item, 640, 512);
    check(!far.add({20, 20, 60, 60}) && !far.widened(), "a box far from the item is ignored");
    // A huge box near the item is held to twice the item's size around it.
    Coverage huge(item, 640, 512);
    check(huge.add({-5000, -5000, 5000, 5000}), "a huge overlapping box is used");
    check(same(huge.rect(), 317 - 40, 256 - 40, 322 + 40, 276 + 40), "and clipped to the item grown by twice its size");
    // The frame limits the widening, never the native rectangle.
    const Rect edge{630, 500, 650, 520};
    Coverage clipped(edge, 640, 512);
    check(clipped.add({600, 470, 700, 600}), "a part at the frame edge is used");
    check(same(clipped.rect(), 600, 470, 650, 520), "widened inside the frame, the native part kept");
    // Degenerate input.
    Coverage none({10, 10, 10, 20}, 640, 512);
    check(!none.add(headRect) && !none.widened(), "an empty native rectangle is left alone");
    Coverage empty(item, 640, 512);
    check(!empty.add({300, 300, 300, 310}) && !empty.widened(), "an empty part rectangle is ignored");
    Rect behind;
    check(!boxRect(p, {{0, 0, -0.1f}, {0, 0, 0.5f}, {1, 0, 0}, {0, 1, 0}}, behind),
          "a box reaching behind the camera is not used");
    check(truncate(-3.7f) == -3 && truncate(3.7f) == 3 && truncate(1e20f) == INT32_MAX, "cvt.w.s truncation");

    std::printf("item_overlay_policy: %u checks passed\n", checks);
    return 0;
}
