#include "../src/hud_page.h"
#include <cstdio>
#include <initializer_list>

using namespace gsr::hud;
static_assert(!inventoryPage(true, 21, 512), "live world must stay native");
static_assert(!inventoryPage(false, -1, 512), "gameplay HUD is not inventory");
static_assert(!inventoryPage(false, 21, 128), "offscreen targets stay native");
static_assert(inventoryPage(false, 21, 512), "post-world inventory");
static_assert(inventoryMap(512, 0.75f).x(0) == 0);
static_assert(inventoryMap(512, 0.75f).x(512) == 384);
static_assert(inventoryMap(512, 1).x(73) == 73);
static_assert(gameplayCornerSubmission(false, false, 512, 512), "post-world rings may retain fog state");
static_assert(!gameplayCornerSubmission(true, false, 512, 512), "fogged world corners must not move");
static_assert(!gameplayCornerSubmission(false, true, 512, 512), "inventory retains its own map");

static_assert(menuForeground(128, 512, false), "saved thumbnail follows slot frame");
static_assert(menuForeground(170, 512, false), "slot panel follows labels");
static_assert(!menuForeground(512, 512, false), "full-page backdrop fills output");
static_assert(!menuForeground(128, 512, true), "captured world tiles fill output");

// Metadata fixture copied from gs_core.cpp's documented native background,
// including the seam sprite it emits. Exercise the production classifier.
static bool nativeMenuSourceChecks() {
    struct Column {
        float left, right;
    };
    const Column source[] = {{0, 170.625f}, {170, 340.625f}, {342, 512}, {340.625f, 342}};
    for (const auto& c : source) {
        if (!menuColumnBackdrop(c.left, 0, c.right, 512, 512, true))
            return false;
        if (menuForeground(c.right - c.left, 512, menuColumnBackdrop(c.left, 0, c.right, 512, 512, true)))
            return false;
        if (menuColumnBackdrop(c.left, 0, c.right, 512, 512, false))
            return false;
    }
    if (menuColumnBackdrop(72, 100, 242.625f, 180, 512, true))
        return false;
    if (menuColumnBackdrop(170, 100, 340.625f, 120, 512, true))
        return false;
    // FGE, ZTE and ZTST deliberately do not participate in menu admission.
    // Both gates precede the legacy HUD predicate in the backend.
    if (!menuSubmission(false, true, 512, 512, true))
        return false;
    if (menuSubmission(true, true, 512, 512, true))
        return false;
    if (menuSubmission(false, false, 512, 512, true))
        return false;
    if (menuSubmission(false, true, 128, 128, true))
        return false;
    if (menuSubmission(false, true, 512, 512, false))
        return false;
    // The pause page drawn while the host flag already (or still) says gameplay.
    if (!nativeMenuDraw(false, 0x17) || !nativeMenuDraw(false, 14) || !nativeMenuDraw(true, -1) ||
        nativeMenuDraw(false, -1) || nativeMenuDraw(false, 21))
        return false;
    // Pause (0x17), bounds from the pause drawlogs (native 512x512). Gameplay
    // HUD whatever the draw order: orbs at rest and sliding out, gauges, air
    // gauge, the small left sprite. Page: dimming panel, frame, text, highlights.
    {
        const float hud[][4] = {{48, 438, 108, 492},  {37, 470, 46, 487},      {28, 379, 54, 493},  {54, 431, 109, 494},
                                {417, 432, 481, 492}, {409.5f, 414, 488, 510}, {8, 478, 68, 512},   {0, 419, 14, 512},
                                {450, 454, 528, 550}, {470, 474, 548, 570},    {462, 32, 488, 142}, {471, 38, 480, 141},
                                {52, 167, 82, 208},   {490, 494, 568, 590}};
        const float page[][4] = {{154, 128, 358, 384}, {126, 118, 384, 395}, {224, 163, 291, 184},
                                 {209, 208, 305, 229}, {208, 192, 303, 228}, {241, 340, 274, 361},
                                 {126, 370, 150, 395}, {360, 110, 384, 130}};
        for (const auto& b : hud)
            if (!pauseHudDraw(true, b[0], b[1], b[2], b[3], 512, 512) ||
                pauseHudDraw(false, b[0], b[1], b[2], b[3], 512, 512))
                return false;
        for (const auto& b : page)
            if (pauseHudDraw(true, b[0], b[1], b[2], b[3], 512, 512))
                return false;
    }
    // Opening frame: the host already shows the pause menu while the camera
    // packet still says gameplay (the orbs must keep the gameplay maps); closing
    // frame: the reverse. Other menus are not the pause.
    if (!pauseScreen(-1, true, 0x17) || !pauseScreen(0x17, false, -1) || !pauseScreen(0x17, true, 0x17))
        return false;
    if (pauseScreen(-1, false, -1) || pauseScreen(-1, true, 9) || pauseScreen(8, true, 8) ||
        pauseScreen(-1, false, 0x17))
        return false;
    if (!frozenWorld(0x17, false, -1) || !frozenWorld(-1, true, 9) || !frozenWorld(5, false, -1))
        return false;
    if (frozenWorld(-1, false, -1) || frozenWorld(0x15, true, 0x15) || frozenWorld(-1, false, 0x17))
        return false;
    // The late perspective water drain after EndWorld under the pause menu.
    if (menuSubmission(false, true, 512, 512, true, true))
        return false;
    return true;
}

int main() {
    {
        // The wide image's embedded 4:3 art must land exactly on the squeezed native canvas.
        const float set[] = {16.0f / 9, 21.0f / 9, 32.0f / 9};
        if (pickWideAspect(set, 3, 16.0f / 9) != 0 || pickWideAspect(set, 3, 2.0f) != 1 ||
            pickWideAspect(set, 3, 3.0f) != 2 || pickWideAspect(set, 3, 5.0f) != 2 ||
            pickWideAspect(set, 0, 2.0f) != -1)
            return 40;
        for (float target : {16.0f / 9, 2.0f, 21.0f / 9, 32.0f / 9})
            for (float image : set) {
                const float k = (4.0f / 3) / target;
                const auto p = widePlacement(image, k);
                const float artLeft = p.left + p.width * (0.5f - 0.5f * (4.0f / 3) / image);
                const float artWidth = p.width * (4.0f / 3) / image;
                if (std::fabs(artLeft - (0.5f - 0.5f * k)) > 1e-5f || std::fabs(artWidth - k) > 1e-5f)
                    return 41;
            }
    }
    SubtitleBounds subtitles;
    if (subtitles.glyph(256, 256, 480, 500))
        return 31;
    subtitles.set(428, 512); // captured 32x32 solid-tile subtitle bar
    if (!subtitles.glyph(256, 256, 480, 492) || !subtitles.glyph(256, 256, 493, 505))
        return 32;
    // The final font DMA may follow the guest return marker. Its map remains
    // valid, but native mana digits and 128x128 orb art must not follow it.
    if (subtitles.glyph(128, 128, 480, 500) || subtitles.glyph(256, 256, 410, 425))
        return 33;
    subtitles.reset();
    if (subtitles.glyph(256, 256, 480, 500))
        return 34;
    for (float aspect : {4.0f / 3, 16.0f / 9, 21.0f / 9, 32.0f / 9})
        for (float size : {.55f, 1.f, 1.5f}) {
            const auto map = subtitleMap(512, 512, (4.0f / 3) / aspect, size);
            // Both line ends and every intervening word have the same origin,
            // even where HUD corner and cluster policies disagree.
            for (float x : {80.f, 150.f, 256.f, 370.f, 430.f}) {
                if (std::fabs(map.x(x + 8) - map.x(x) - 8 * map.scaleX) > .0001f)
                    return 29;
                if (map.y(480) != 512 + (480 - 512) * size)
                    return 30;
            }
        }
    // Disconnected words must preserve their native spacing through the same
    // transform on the first frame, including across old cluster thresholds.
    for (float aspect : {4.0f / 3, 16.0f / 9, 21.0f / 9, 32.0f / 9})
        for (float size : {.55f, 1.0f, 1.5f}) {
            const auto line = tutorialMap(512, (4.0f / 3) / aspect, size);
            for (float x : {39.0f, 140.0f, 235.0f, 410.0f}) {
                const float gap = line.x(x + 8) - line.x(x);
                if (std::fabs(gap - 8 * line.scaleX) > 0.0001f)
                    return 23;
                if (line.y(20) != 20 * size)
                    return 24;
            }
        }
    if (!tutorialBand(12, 28, 512) || !tutorialBand(0, 42, 512) || tutorialBand(65, 80, 512))
        return 25;
    // Objective popup (Surdana start): its first line 65..354 sits in the top rows but is left aligned beside the
    // icon; tutorial text is centred or the full-width band itself.
    if (tutorialBandDraw(65, 354, 512) || !tutorialBandDraw(150, 362, 512) || !tutorialBandDraw(0, 512, 512))
        return 125;
    // Death hint, line 2: "key to" / "menu." are off-centre word draws on the full-width band and keep the band's
    // map; without a band (the popup) nothing is covered. The band may be drawn after its text (previous frame).
    {
        TutorialBandRows rows;
        if (rows.covers(40))
            return 128;
        rows.note(0, 512, 42, 512);
        rows.note(140, 300, 60, 512); // narrow draws never extend the band
        if (!rows.covers(40) || rows.covers(60))
            return 129;
        rows.endFrame();
        if (!rows.covers(40))
            return 130;
        rows.endFrame();
        if (rows.covers(40))
            return 131;
    }
    // The popup's fill is a triangle list with FGE, fog 255 and Q 1; fogged or perspective triangles stay world.
    if (!screenTextTriangles(true, false, 255) || screenTextTriangles(true, false, 241) ||
        screenTextTriangles(true, true, 255) || screenTextTriangles(false, false, 255))
        return 126;
    // Popup line 1 (65..354) anchors left like its icon; centred and right-side clusters keep their anchors.
    if (clusterAnchorX(65, 354, 0, 512) != 0 || clusterAnchorX(15, 57, 0, 512) != 0 ||
        clusterAnchorX(150, 362, 0, 512) != 256 || clusterAnchorX(417, 481, 0, 512) != 512 ||
        clusterAnchorX(120, 300, 0, 512) != 256)
        return 127;
    if (!splitCornerBatch(28, 379, 493, 495, 512, 512))
        return 26;
    if (splitCornerBatch(380, 228, 490, 495, 512, 512) || splitCornerBatch(380, 400, 490, 495, 512, 512) ||
        splitCornerBatch(20, 12, 490, 28, 512, 512))
        return 27;
    // Actual post-EndWorld weapon shine at f4500 uses Q around .0007,
    // TEST=0x5002d, unlike the Q=1 orb ring in the same frame. The world
    // phase is already closed for both; only the latter is a HUD candidate.
    if (nativeUiQ(.0007f) || nativeUiQ(.0008f) || !nativeUiQ(1.0f))
        return 28;
    // A rotating compass crosses the bottom edge near diagonal headings.
    // Its centre and map must remain stable throughout the full rotation.
    for (unsigned degrees = 0; degrees < 360; ++degrees) {
        const float a = degrees * 3.14159265f / 180;
        const float ex = std::fabs(42 * std::cos(a)) + std::fabs(31 * std::sin(a));
        const float ey = std::fabs(42 * std::sin(a)) + std::fabs(31 * std::cos(a));
        if (gameplayCorner(449 - ex, 462 - ey, 449 + ex, 462 + ey, 512, 512) != Corner::Right)
            return 35;
        if (gameplayCorner(63 - ex, 462 - ey, 63 + ex, 462 + ey, 512, 512) != Corner::Left)
            return 36;
    }
    if (gameplayCorner(450, 475, 550, 550, 512, 512) != Corner::None ||
        gameplayCorner(360, 350, 600, 510, 512, 512) != Corner::None)
        return 37;
    std::puts("PASS compass retains corner mapping through 360 headings and clipped quad edges; offscreen "
              "centres/oversized geometry excluded");
    std::puts("PASS tutorial shared-word spacing at 4:3/16:9/21:9/32:9 and three HUD sizes; rigid mesh batching; "
              "captured late weapon shine Q excluded");
    if (!nativeMenuSourceChecks())
        return 22;
    for (float aspect : {16.0f / 9, 21.0f / 9, 32.0f / 9}) {
        const auto menu = menuMap(512, (4.0f / 3) / aspect);
        for (float x : {0.0f, 72.0f, 128.0f, 256.0f, 384.0f, 512.0f}) {
            const float inverse = 256 + (menu.x(x) - 256) / menu.scaleX;
            if (std::fabs(inverse - x) > 0.0001f)
                return 20;
        }
        // Physical width divided by height retains native pixel proportions.
        if (std::fabs((menu.x(200) - menu.x(100)) * aspect - 100 * (4.0f / 3)) > 0.0001f)
            return 21;
    }
    std::puts("PASS native menu: documented three-column/seam metadata, world/color/target gates, centered inverse and "
              "proportions at 16:9/21:9/32:9");
    const auto map = inventoryMap(512, 0.75f);
    InventoryFrame context;
    context.observe(40, 21);
    if (context.inputAnchor() != 0.0f)
        return 30;
    if (context.applies(true, 512))
        return 3;
    if (!context.applies(false, 512))
        return 4;
    context.observe(40, -1); // preview metadata cannot split this page
    if (context.inputAnchor() != 0.0f)
        return 31;
    context.observe(40, 1); // the inventory tutorial uses the same page origin
    if (context.inputAnchor() != 0.0f)
        return 32;
    // Repeated presentations without new draws retain these same pixels.
    if (context.inputAnchor() != 0.0f)
        return 33;
    if (!context.applies(false, 512))
        return 5;
    if (context.applies(false, 128))
        return 6;
    context.observe(41, -1); // closed page does not stick across frames
    if (context.inputAnchor() != 0.5f)
        return 34;
    if (context.applies(false, 512))
        return 7;
    // An Order-of-the-Flame style view (full-screen map, strip) marks its frame: the page maps about the centre.
    {
        InventoryFrame view;
        view.observe(50, 21, true);
        view.observe(50, -1); // later preview packets keep the frame's decision
        if (!view.centred || view.inputAnchor() != 0.5f || !view.applies(false, 512))
            return 35;
        const auto centred = inventoryMap(512, .75f, view.centred);
        if (centred.x(256) != 256 || centred.x(0) != 64 || centred.x(512) != 448)
            return 36;
        if (!view.centredRecently(51) || !view.centredRecently(52) || view.centredRecently(53) ||
            view.centredRecently(49))
            return 39;        // the world of the following frames starts cleared, not later ones
        view.observe(51, 21); // the normal screen next frame: left-anchored again
        if (view.centred || view.inputAnchor() != 0.0f)
            return 37;
        InventoryFrame flagOnly;
        flagOnly.observe(60, -1, true); // the flag alone is not an inventory page
        if (flagOnly.centred || flagOnly.applies(false, 512))
            return 38;
        // The map view's world shade covers the whole output: a solid full-frame sprite of a centred frame only.
        if (!inventoryViewShade(true, true, 0, 0, 512, 512, 512, 512) ||
            inventoryViewShade(false, true, 0, 0, 512, 512, 512, 512) ||
            inventoryViewShade(true, false, 0, 0, 512, 512, 512, 512) ||
            inventoryViewShade(true, true, 64, 0, 448, 512, 512, 512) ||
            inventoryViewShade(true, true, 0, 0, 512, 400, 512, 512))
            return 40;
    }
    unsigned checks = 0;
    // All native positions have the same result independent of membership
    // in an RT fill, ring, tile, full page, depth-tested preview or glyph.
    for (int x = -64; x <= 576; ++x)
        for (unsigned component = 0; component != 6; ++component) {
            (void)component;
            if (map.x((float)x) != 0.75f * x)
                return 1;
            ++checks;
        }
    // A tile edge is shared exactly; a clipped fill and its ring retain
    // the same coordinate; scissor endpoints use this same affine map.
    if (map.x(128) != 96 || map.x(384) != 288 || map.x(512) - map.x(0) != 384)
        return 2;
    std::printf("PASS inventory page mapping: %u component-coordinate checks; stable subpass metadata, close, "
                "world/target gates and clip endpoints\n",
                checks);

    struct Point {
        float x, y;
    };
    // One native triangle-list batch spans both orbs. Whole-batch bounds
    // deliberately do not qualify; each triangle must choose its own corner.
    const Point batch[] = {{16, 400},  {112, 400}, {16, 500},  {112, 400}, {112, 500}, {16, 500},
                           {400, 400}, {496, 400}, {400, 500}, {496, 400}, {496, 500}, {400, 500}};
    if (gameplayCorner(16, 400, 496, 500, 512, 512) != Corner::None)
        return 8;
    unsigned corners = 0;
    for (unsigned b = 0; b < 12; b += 3) {
        float x0 = 512, y0 = 512, x1 = 0, y1 = 0;
        for (unsigned i = b; i < b + 3; ++i) {
            if (batch[i].x < x0)
                x0 = batch[i].x;
            if (batch[i].x > x1)
                x1 = batch[i].x;
            if (batch[i].y < y0)
                y0 = batch[i].y;
            if (batch[i].y > y1)
                y1 = batch[i].y;
        }
        const auto side = gameplayCorner(x0, y0, x1, y1, 512, 512);
        if (side != (b < 6 ? Corner::Left : Corner::Right))
            return 9;
        const auto m = cornerMap(side, 512, 512, .75f, .5f);
        for (unsigned i = b; i < b + 3; ++i) {
            const float expectedX = b < 6 ? batch[i].x * .375f : 512 + (batch[i].x - 512) * .375f;
            if (m.x(batch[i].x) != expectedX || m.y(batch[i].y) != 512 + (batch[i].y - 512) * .5f)
                return 10;
        }
        ++corners;
    }
    // Different native RT-fill/ring boxes must produce an identical map.
    const auto fill = cornerMap(gameplayCorner(420, 420, 470, 480, 512, 512), 512, 512, .75f, .5f);
    const auto ring = cornerMap(gameplayCorner(384, 380, 500, 510, 512, 512), 512, 512, .75f, .5f);
    if (fill.x(448) != ring.x(448) || fill.y(448) != ring.y(448))
        return 11;
    if (gameplayCorner(220, 400, 292, 500, 512, 512) != Corner::None ||
        gameplayCorner(16, 100, 112, 200, 512, 512) != Corner::None)
        return 12;
    const auto identity = cornerMap(Corner::Right, 512, 512, 1, 1);
    if (identity.x(448) != 448 || identity.y(400) != 400)
        return 13;
    // Both 035 (nonfogged) and 037 (fogged) UI submissions take the same
    // phase gate and native right-corner map, unlike the generic heuristic.
    for (unsigned flags : {0x035u, 0x037u}) {
        (void)flags; // no flag-dependent exclusion is allowed in this gate
        if (!gameplayCornerSubmission(false, false, 512, 512))
            return 14;
        if (gameplayCorner(384, 380, 500, 510, 512, 512) != Corner::Right)
            return 15;
    }
    std::printf("PASS gameplay HUD corners: %u triangles in one two-corner batch; RT fill/ring equality; center/top "
                "rejection; identity\n",
                corners);
    const auto bars = inventoryCanvasMargins(512, 512, 3, .75f, true, true);
    if (bars.count != 0)
        return 16; // left-anchored inventory has no unused left margin
    // No clear in native 4:3/identity, gameplay, saved/offscreen targets,
    // invalid dimensions or invalid scale. Interior remains untouched.
    if (inventoryCanvasMargins(512, 512, 3, 1, true, true).count ||
        inventoryCanvasMargins(512, 512, 3, .75f, false, true).count ||
        inventoryCanvasMargins(512, 512, 3, .75f, true, false).count ||
        inventoryCanvasMargins(0, 512, 3, .75f, true, true).count ||
        inventoryCanvasMargins(512, 512, 0, .75f, true, true).count ||
        inventoryCanvasMargins(512, 512, 3, 0, true, true).count)
        return 17;
    unsigned rectangleChecks = 0;
    for (unsigned width : {511u, 512u, 640u})
        for (unsigned scale : {1u, 2u, 3u, 8u})
            for (float aspect : {.75f, .6f, .999f}) {
                const auto m = inventoryCanvasMargins(width, 448, scale, aspect, true, true);
                const auto p = inventoryMap(width, aspect);
                if (m.count != 0 || p.x(0) != 0)
                    return 18;
                ++rectangleChecks;
                for (unsigned i = 0; i < m.count; ++i) {
                    const auto r = m.rect[i];
                    if (r.left >= r.right || r.top != 0 || r.bottom != 448 * scale || r.right > width * scale)
                        return 18;
                    if (r.left != 0 || r.right > p.x(0) * scale)
                        return 19;
                    ++rectangleChecks;
                }
            }
    CanvasFrame first, second;
    if (!first.begin(1900) || first.begin(1900) || !second.begin(1900) || !first.begin(1901))
        return 20;
    std::printf("PASS inventory canvas: %u fractional/scaled no-gutter checks; left-anchored page requires no black "
                "gutter; right world/preview preserved; identity/gameplay/saved target no-clear; per-RT/frame gate\n",
                rectangleChecks);
}
