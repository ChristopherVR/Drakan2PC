#pragma once
#include <cstdint>
#include <cmath>

namespace gsr::hud {

constexpr bool nativeUiQ(float q) {
    return q >= 0.99999f && q <= 1.00001f;
}

// Page coordinates must not depend on each piece's texture, bounds, depth
// state, or the previous gameplay HUD clusters. World submission wins over
// GUI metadata: inventory can keep a live world behind its page.
constexpr bool inventoryPage(bool worldOpen, int guiScreen, unsigned targetWidth) {
    return !worldOpen && guiScreen == 21 && targetWidth >= 480;
}

// A post-world native menu is authoritative even when a UI triangle retains
// FGE or a depth-test state rejected by the gameplay HUD heuristic. Textured
// perspective triangles (Q != 1) are not page UI: the world's late water drain
// follows EndWorld under the pause menu too and must keep world coordinates.
constexpr bool menuSubmission(bool worldOpen, bool menu, unsigned width, unsigned height, bool colorWrite,
                              bool perspective = false) {
    return !worldOpen && menu && width >= 480 && height >= 320 && colorWrite && !perspective;
}

constexpr int kPauseScreen = 0x17;

// The native page map applies while the host reports a native menu, and also
// while the draw's own camera packet names the pause menu: the host polls the
// active screen once per presented frame, so on resume (and on open) the game
// still draws (or already draws) its pause page while the host's flag says
// gameplay. Those page draws would otherwise take the gameplay HUD cluster map.
constexpr bool nativeMenuDraw(bool hostMenu, int guiScreen) {
    return hostMenu || guiScreen == 14 || guiScreen == kPauseScreen;
}

// Either side may name the pause menu first: on open the host's flag (and its
// screen) can already say pause while the frame's camera packet still says
// gameplay; on resume it is the other way round.
constexpr bool pauseScreen(int cameraScreen, bool hostMenu, int hostMenuScreen) {
    return cameraScreen == kPauseScreen || (hostMenu && hostMenuScreen == kPauseScreen);
}

// The game holds the world still behind its pause menu and the pages opened
// from it (Load 4, Save 5, Options 9, Controller 10, Audio 13) but submits it
// again every frame. A moving sub-pixel jitter then makes the temporal
// upscaler resample one static image, and thin alpha-tested edges (grass,
// leaves) crawl. Either side may name the screen first, as for pauseScreen.
constexpr bool frozenWorldScreen(int screen) {
    return screen == kPauseScreen || screen == 4 || screen == 5 || screen == 9 || screen == 10 || screen == 13;
}
constexpr bool frozenWorld(int cameraScreen, bool hostMenu, int hostMenuScreen) {
    return frozenWorldScreen(cameraScreen) || (hostMenu && frozenWorldScreen(hostMenuScreen));
}

// Under the native pause menu (0x17) the game keeps drawing the gameplay HUD
// after EndWorld, while it slides the orbs out of the screen corners (and back
// in on resume). Its draw order against the page varies on the opening frame,
// so the decision is geometric: every pause page piece (dimming panel
// 154..358 x 128..384, frame and corner diamonds 126..384 x 118..395, text
// and highlights) is centred inside the native page box 120..390 x 110..400
// of the 512x512 frame; the orbs, gauges and other gameplay HUD are centred
// outside it. Those keep the gameplay corner / cluster maps.
constexpr bool pauseHudDraw(bool pause, float x0, float y0, float x1, float y1, float width, float height) {
    const float cx = 0.5f * (x0 + x1) * 512.0f / width, cy = 0.5f * (y0 + y1) * 512.0f / height;
    return pause && (cx < 120.0f || cx > 390.0f || cy < 110.0f || cy > 400.0f);
}

// Explicit guard for the textured sprite columns documented by gs_core.cpp's
// fixSpriteSeam: [0,170.625], [170,340.625], [342,512]. Also preserve its
// synthetic seam filler [340.625,342]. This is a known-pattern guard, not a
// general assertion that arbitrary narrow menu pieces are foreground.
inline bool menuColumnBackdrop(float x0, float y0, float x1, float y1, float targetWidth, bool texturedSprite) {
    if (!texturedSprite || targetWidth < 480 || y1 - y0 < 32)
        return false;
    const float k = 512.0f / targetWidth;
    x0 *= k;
    x1 *= k;
    const auto almostEqual = [](float a, float b) { return std::fabs(a - b) <= 1.0f / 32; };
    return (almostEqual(x0, 0) && almostEqual(x1, 170.625f)) || (almostEqual(x0, 170) && almostEqual(x1, 340.625f)) ||
           (almostEqual(x0, 342) && almostEqual(x1, 512)) || (almostEqual(x0, 340.625f) && almostEqual(x1, 342));
}

constexpr bool menuForeground(float width, float targetWidth, bool backdrop) {
    return !backdrop && width < 0.85f * targetWidth;
}

struct PageMap {
    float centerX, scaleX;
    constexpr float x(float nativeX) const { return centerX + (nativeX - centerX) * scaleX; }
};

constexpr PageMap menuMap(unsigned targetWidth, float aspectScale) {
    return {0.5f * static_cast<float>(targetWidth), aspectScale};
}

// Wide menu backdrops: pack images that contain the native 4:3 art centered
// with outpainted side content. An image is chosen per output aspect and
// placed so its embedded 4:3 art lines up with the squeezed native canvas.
// Returns the index of the narrowest image at least as wide as the target,
// else the widest one; -1 when there are none.
inline int pickWideAspect(const float* aspects, unsigned count, float target) {
    int best = -1;
    for (unsigned i = 0; i < count; ++i) {
        if (!(aspects[i] > 0))
            continue;
        const bool covers = aspects[i] >= target * 0.995f;
        const bool bestCovers = best >= 0 && aspects[best] >= target * 0.995f;
        if (best < 0 || (covers && !bestCovers) ||
            (covers == bestCovers && (covers ? aspects[i] < aspects[best] : aspects[i] > aspects[best])))
            best = (int)i;
    }
    return best;
}

// Horizontal extent of an image of aspect imageAspect, as a fraction of the
// target width, centered on the target. aspectScale = (4/3) / target aspect.
struct WidePlacement {
    float left, width;
};
constexpr WidePlacement widePlacement(float imageAspect, float aspectScale) {
    const float width = imageAspect * aspectScale * (3.0f / 4.0f);
    return {0.5f - 0.5f * width, width};
}

// Keep the existing inventory composition together and align it to the left
// edge. Wider output reveals more of the world/preview on the right instead
// of inserting an unused black gutter before the inventory.
// An Order-of-the-Flame style view of the inventory (full-screen map, bottom strip; the runtime marks its frames
// with SceneGuideCamera::CentredPage) lays its page out in the native window and is centred on the screen instead.
constexpr PageMap inventoryMap(unsigned targetWidth, float aspectScale, bool centred = false) {
    return {centred ? 0.5f * static_cast<float>(targetWidth) : 0.0f, aspectScale};
}

// A view's world shade (the map view dims the world behind the map): a solid sprite (no texture or the native 32x32
// white tile) covering the whole native frame in a centred view frame. It is not page-mapped, so it covers the whole
// output width instead of the centred 4:3 window.
constexpr bool inventoryViewShade(bool centredView, bool solid, float minX, float minY, float maxX, float maxY,
                                  float frameW, float frameH) {
    return centredView && solid && minX <= 0.5f && minY <= 0.5f && maxX >= frameW - 0.5f && maxY >= frameH - 0.5f;
}

struct CanvasRect {
    unsigned left, top, right, bottom;
};
struct CanvasMargins {
    CanvasRect rect[2]{};
    unsigned count = 0;
};

// Only the unused LEFT canvas margin is black. The right side contains the
// live world/character preview and must never be cleared by the UI canvas.
// Round away from the page interior to preserve its edge pixels.
inline CanvasMargins inventoryCanvasMargins(unsigned width, unsigned height, unsigned scale, float aspectScale,
                                            bool inventory, bool displayTarget) {
    CanvasMargins result;
    if (!inventory || !displayTarget || !width || !height || !scale || !(aspectScale > 0 && aspectScale < 1))
        return result;
    const auto map = inventoryMap(width, aspectScale);
    const unsigned physicalHeight = height * scale;
    const unsigned leftEnd = (unsigned)std::floor(map.x(0) * scale);
    if (leftEnd)
        result.rect[result.count++] = {0, 0, leftEnd, physicalHeight};
    return result;
}

struct CanvasFrame {
    std::uint64_t last = ~std::uint64_t{0};
    bool begin(std::uint64_t frame) {
        if (last == frame)
            return false;
        last = frame;
        return true;
    }
};

enum class Corner { None, Left, Right };
// Fog is deliberately absent from this submission gate: GS UI rings can keep
// FGE enabled. Engine phase and native corner bounds decide this special path;
// generic (noncorner) UI classification still rejects fogged triangles.
constexpr bool gameplayCornerSubmission(bool worldOpen, bool inventory, unsigned width, unsigned height) {
    return !worldOpen && !inventory && width >= 480 && height >= 320;
}
constexpr Corner gameplayCorner(float x0, float y0, float x1, float y1, float width, float height) {
    // The compass rotates with mouse look. Its quad can cross a screen edge
    // while its centre remains inside the corner. Requiring every vertex to
    // be on-screen makes it suddenly lose the HUD map (and change size).
    // Bound the mesh size and centre instead; world/perspective rejection is
    // still enforced by the caller before this geometric classification.
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    if (width < 480 || height < 320 || x1 < x0 || y1 < y0 || x1 - x0 > 0.3f * width || y1 - y0 > 0.35f * height ||
        cx < 0 || cx > width || cy > height || y0 < 0.65f * height)
        return Corner::None;
    return x1 < 0.3f * width ? Corner::Left : x0 > 0.7f * width ? Corner::Right : Corner::None;
}

struct CornerMap {
    float anchorX, bottom, scaleX, scaleY;
    constexpr float x(float nativeX) const { return anchorX + (nativeX - anchorX) * scaleX; }
    constexpr float y(float nativeY) const { return bottom + (nativeY - bottom) * scaleY; }
};
struct SubtitleBounds {
    float top = 1e30f, bottom = -1e30f;
    void reset() {
        top = 1e30f;
        bottom = -1e30f;
    }
    void set(float y0, float y1) {
        top = y0;
        bottom = y1;
    }
    bool glyph(unsigned textureWidth, unsigned textureHeight, float y0, float y1) const {
        return textureWidth == 256 && textureHeight == 256 && y0 >= top - 1 && y1 <= bottom + 1;
    }
};
// Tutorial glyphs are submitted in separate words/sprites. They share the
// top band's center/top origin, regardless of gaps between words.
constexpr bool tutorialBand(float y0, float y1, float height) {
    return y0 >= 0 && y1 <= height * 0.125f;
}
// Horizontal anchor of a HUD cluster: the nearest screen edge or the centre. A cluster that starts at the left
// margin and is clearly left of centre is left-aligned content (the objective popup's first line beside its icon,
// centre at 0.41 of the width), not a centred element; the right margin likewise.
constexpr float clusterAnchorX(float lo, float hi, float f0, float width) {
    const float t = (0.5f * (lo + hi) - f0) / width;
    if (t < 0.4f || (t < 0.45f && lo - f0 < 0.15f * width))
        return f0;
    if (t > 0.6f || (t > 0.55f && f0 + width - hi < 0.15f * width))
        return f0 + width;
    return f0 + 0.5f * width;
}
// Tutorial hints are centred on a full-width band. Left-aligned text in the same rows (the objective popup's first
// line beside its icon) keeps the cluster map shared with its icon and second line. x0/x1: the whole draw.
constexpr bool tutorialBandDraw(float x0, float x1, float width) {
    const float c = (x0 + x1) * 0.5f - width * 0.5f;
    return x1 - x0 >= 0.85f * width || (c < 0.05f * width && c > -0.05f * width);
}
// Rows covered by the full-width tutorial band this frame or the last (the band may follow its text). A wrapped
// line's words are separate draws, so an off-centre word ("key to" ... "menu.") is still band text when it sits on
// the band; the objective popup has no band behind it.
struct TutorialBandRows {
    float current = -1e30f, previous = -1e30f;
    void note(float x0, float x1, float y1, float width) {
        if (x1 - x0 >= 0.85f * width)
            current = current > y1 ? current : y1;
    }
    void endFrame() {
        previous = current;
        current = -1e30f;
    }
    bool covers(float y1) const { return y1 <= (current > previous ? current : previous) + 1; }
};
// Screen-space text drawn as a textured triangle list (the popup text's fill; its shadow is a sprite batch) keeps
// the FGE bit with fog 255 on every vertex. Late world draws after EndWorld are perspective or fogged.
constexpr bool screenTextTriangles(bool textured, bool perspective, float minFog) {
    return textured && !perspective && minFog >= 255.0f;
}
// Only a batch spanning BOTH bottom corners needs triangle partitioning.
// A single 3D weapon preview must retain one map even when its triangles
// cross the corner classifier's bounds or the legacy anchor thresholds.
constexpr bool splitCornerBatch(float x0, float y0, float x1, float y1, float width, float height) {
    return x0 >= -1 && x1 <= width + 1 && y0 >= .65f * height && y1 <= height + 1 && x0 < .3f * width &&
           x1 > .7f * width;
}
constexpr CornerMap tutorialMap(float width, float aspectScale, float size) {
    return {width * 0.5f, 0, aspectScale * size, size};
}
constexpr CornerMap subtitleMap(float width, float height, float aspectScale, float size) {
    return {width * 0.5f, height, aspectScale * size, size};
}
constexpr CornerMap cornerMap(Corner side, float width, float height, float aspectScale, float size) {
    return {side == Corner::Right ? width : 0, height, aspectScale * size, size};
}

// One display-frame decision: a later preview packet may replace the camera
// metadata, but cannot switch half of an inventory page to gameplay anchors.
// The next GS frame must observe inventory again; closing it cannot leave a
// sticky page mode. This state contains no renderer or resource pointers.
struct InventoryFrame {
    std::uint64_t frame = ~std::uint64_t{0};
    bool page = false, centred = false;
    std::uint64_t centredFrame = ~std::uint64_t{0}; // last frame that drew a centred view
    void observe(std::uint64_t nextFrame, int guiScreen, bool centredPage = false) {
        if (nextFrame != frame) {
            frame = nextFrame;
            page = centred = false;
        }
        page = page || guiScreen == 21;
        centred = centred || (guiScreen == 21 && centredPage);
        if (centred)
            centredFrame = nextFrame;
    }
    // A view shows the whole world, which the game does not fully cover behind its inventory (no sky there): the
    // world phase of the frame after a centred view starts from a cleared target instead of stale gameplay pixels.
    bool centredRecently(std::uint64_t currentFrame) const {
        return centredFrame != ~std::uint64_t{0} && currentFrame >= centredFrame && currentFrame - centredFrame <= 2;
    }
    bool applies(bool worldOpen, unsigned targetWidth) const {
        return page && inventoryPage(worldOpen, 21, targetWidth);
    }
    float inputAnchor() const {
        // Vblank can latch/present the same pixels after endFrame advances the
        // counter. Keep their origin until a new draw observes another page.
        return page && !centred ? 0.0f : 0.5f;
    }
};

} // namespace gsr::hud
