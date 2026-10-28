// Layout and state rules of the Order-of-the-Flame style inventory views (drakan_inventory_views_policy.h).
#include "../src/lib/drakan_inventory_views_policy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace drakan_views;
static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
static bool near(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Section jump: preset the next section, one native "previous" step lands on the target.
    check(jumpPreset(kMapSection, kSectionCount) == 4, "map: preset Journal, step back to Map");
    check((jumpPreset(kMapSection, kSectionCount) + kSectionCount - 1) % kSectionCount == kMapSection,
          "one previous step reaches the target");
    check(targetSection(View::Map) == 3 && targetSection(View::Strip) == 0, "view sections");

    // Fit: largest uniform scale, centred, inside the box.
    {
        const Fit f = fitCentred(256, 192, 12, 12, 500, 472);
        check(near(f.scale, std::fmin(488.0f / 256, 460.0f / 192)), "uniform fit scale");
        check(f.x >= 12 && f.x + 256 * f.scale <= 500.01f && f.y >= 12 && f.y + 192 * f.scale <= 472.01f,
              "fit inside the box");
        check(near(f.x + 128 * f.scale, 256.0f), "fit centred horizontally");
    }
    {
        const MapLayout m = mapLayout(80, 100, 240, 256, 512, 512);
        check(m.valid && m.fit.scale > 1.6f && m.fit.scale < 1.9f, "map scaled up to the window");
        check(near(m.x(80), m.fit.x) && near(m.y(100), m.fit.y), "map origin maps to the fitted top-left");
        check(near(m.x(80 + 240), m.fit.x + 240 * m.fit.scale), "map right edge scales");
        check(m.fit.y + 256 * m.fit.scale <= 512 - MapBox{}.captionReserve + 0.01f, "room left for the caption");
        // The caption keeps its native distance below the image and its x from the image's left edge.
        const float nativeCaptionY = 100 + 279.0f;
        check(near(m.captionY(nativeCaptionY), m.fit.y + 256 * m.fit.scale + (279.0f - 256)), "caption below image");
        check(near(m.captionX(80 + 12), m.fit.x + 12), "caption x follows the image");
        check(m.iconScale >= 1 && m.iconScale <= m.fit.scale, "icons grow less than the map");
        // Marker centred at map point (180, 200), drawn 16 x 16, becomes 16k x 16k centred on its scaled point.
        float nx = 0, ny = 0;
        const float k = m.iconScale;
        placeCentred(m, 172, 192, 16, 16, 16 * k, 16 * k, nx, ny);
        check(near(nx + 8 * k, m.x(180)) && near(ny + 8 * k, m.y(200)), "marker centre follows the map");
    }
    {
        const MapLayout big = mapLayout(0, 0, 700, 700, 512, 512);
        check(!big.valid && big.fit.scale == 1.0f, "never shrinks a map larger than the window");
        check(!mapLayout(0, 0, 0, 10, 512, 512).valid, "empty image ignored");
    }
    check(markerScale(4.0f) == 2.0f && markerScale(0.5f) == 1.0f && near(markerScale(2.25f), 1.5f),
          "marker scale sqrt, clamped to 1..2");

    // "You are here": pixel <-> object units round-trip (native 439b28 formula).
    for (float px : {0.0f, 123.0f, 511.0f}) {
        const float kx = -256.0f * 0.8f, ky = 256.0f * 1.1f;
        check(near(objectToPixelX(pixelToObjectX(px, kx, 512), kx, 512), px, 0.001f), "object x round trip");
        check(near(objectToPixelY(pixelToObjectY(px, ky, 512), ky, 512), px, 0.001f), "object y round trip");
    }

    // Strip: grid centred at the bottom of the window, details text above it.
    {
        const StripTarget t = stripTarget(280, 105, 512, 512);
        check(near(t.gridCentreX, 256.0f), "grid centred in the window");
        check(near(t.gridCentreY + 52.5f, 512 - StripBox{}.bottomMargin), "grid above the bottom margin");
        check(t.textTop + 6 * kDetailsLineHeight < t.gridCentreY - 52.5f, "text block above the grid");
        check(near(t.textLeft, t.gridCentreX - 140 - StripBox{}.barPad + StripBox{}.panelPad),
              "text aligned with the bar's left edge");
        // Bar and its brass frame stay inside the window; cells tile the grid.
        const Rect bar = stripBar(t);
        check(bar.y1 + kFrameOut <= 512 && bar.x0 - kFrameOut >= 0 && bar.x1 + kFrameOut <= 512, "bar frame on screen");
        const Rect c0 = stripCell(t, 0, 0), c23 = stripCell(t, 7, 2);
        check(near(c0.x0, 116) && near(c0.w(), 35) && near(c0.h(), 35), "first cell");
        check(near(c23.x1, 396) && near(c23.y1, t.gridCentreY + 52.5f), "last cell ends at the grid corner");
        // Panel: stands on the bar's left end, sized to the text, clamped to the bar.
        const StripPanel p = stripPanel(t, 5, 9, 19);
        check(near(p.fill.x0, bar.x0) && near(p.fill.y1, bar.y0 - StripBox{}.panelGap), "panel on the bar");
        check(near(p.fill.w(), 19 * kStatCharWidth + 2 * StripBox{}.panelPad), "panel width from the text");
        check(near(p.fill.h(), 6 + 5 * kDetailsLineHeight + 2 * StripBox{}.panelPad), "panel height from the lines");
        check(near(p.textTop, p.fill.y0 + StripBox{}.panelPad) && near(p.textLeft, t.textLeft), "text in the panel");
        check(near(stripPanel(t, 2, 3, 3).fill.w(), kPanelMinWidth), "panel minimum width");
        check(near(stripPanel(t, 2, 200, 3).fill.w(), bar.w()), "panel never wider than the bar");
        check(stripPanel(t, 0, 0, 0).fill.empty(), "no item: no panel");
    }
    {
        // Details lines: name, +0x2c4 only when >= 2 characters (else +0x450), trailing empty lines dropped.
        check(detailsLines({9, 0, 15, 9, 0, 19, 0}) == 5, "an empty 0x450 line still takes its row");
        check(detailsLines({9, 0, 15, 9, 6, 19, 0}) == 5, "0x450 line when 0x2c4 is short");
        check(detailsLines({9, 0, 15, 9, 6, 0, 0}) == 4, "trailing empty lines dropped");
        check(detailsLines({9, 12, 15, 9, 6, 19, 4}) == 6, "0x2c4 replaces 0x450");
        check(detailsLines({0, 0, 0, 0, 0, 0, 0}) == 0, "nothing");
    }
    check(gridCentrePlaneX(-210, 60) == 0 && gridCentrePlaneY(64, 64) == 0, "grid centre in plane units");
    {
        // A linear projection: sx = 300 + 2u, sy = 200 - 3v. Solve for screen (400, 50).
        float u = 0, v = 0;
        check(solvePlane(10, 10, 320, 170, 2, 0, 0, -3, 400, 50, u, v), "solvable");
        check(near(300 + 2 * u, 400) && near(200 - 3 * v, 50), "Newton step exact for a linear map");
        volatile float two = 2; // runtime value: the compiler must not fold the refused division
        check(!solvePlane(0, 0, 0, 0, 1, two, two, 4, 1, 1, u, v), "degenerate Jacobian refused");
    }

    // Keys: open only from free gameplay; any view key closes an open view.
    check(viewKeyAction(true, true, View::None) == KeyAction::Open, "open from gameplay");
    check(viewKeyAction(true, false, View::None) == KeyAction::None, "no open from a menu or the Tab screen");
    check(viewKeyAction(true, false, View::Map) == KeyAction::Close, "the key closes the view");
    check(viewKeyAction(false, true, View::None) == KeyAction::None, "no key, no action");

    // The view stays bound while its screen or a box stacked on it is active.
    check(viewScreenUp(0x700000, 0x700000, 21, 0), "screen active");
    check(viewScreenUp(0x700000, 0x710000, 1, 0x700000), "help box over the view");
    check(!viewScreenUp(0x700000, 0x710000, 23, 0x700000), "another screen ends the view");
    check(!viewScreenUp(0x700000, 0, -1, 0), "no screen ends the view");
    check(!viewScreenUp(0, 0x700000, 21, 0), "nothing bound");

    {
        // Host targets kept inside the native window (letterboxed picture: the grid above the window's top).
        const Box g = keepInWindow({0, -53.1f, 576, 231.3f}, 512, 512, 8);
        check(g.x0 >= 8 - 0.01f && g.x1 <= 504.01f && g.y0 >= 8 - 0.01f && g.y1 <= 504.01f, "grid inside the window");
        check(near(g.w() / g.h(), 576 / 284.4f), "grid aspect kept");
        check(near(0.5f * (g.x0 + g.x1), 256), "grid centre kept where it fits");
        const Box same = keepInWindow({128, 101.5f, 416, 243.7f}, 512, 512, 8);
        check(near(same.x0, 128) && near(same.y0, 101.5f) && near(same.x1, 416) && near(same.y1, 243.7f),
              "a box that fits is unchanged");
        const Box low = keepInWindow({100, 450, 400, 560}, 512, 512, 8);
        check(near(low.y1, 504) && near(low.h(), 110) && near(low.w(), 300), "moved up, not shrunk");
        const Box m = clipToWindow({-61, 7.5f, 470, 520}, 512, 512, 8);
        check(near(m.x0, 8) && near(m.y0, 8) && near(m.x1, 470) && near(m.y1, 504), "map area clipped");
        check(clipToWindow({600, 0, 700, 100}, 512, 512, 8).w() <= 0, "outside: empty");
    }
    std::printf("inventory views policy: %u checks passed\n", checks);
    return 0;
}
