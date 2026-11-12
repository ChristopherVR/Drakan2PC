#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>

// Private host-thread policy; no dependency on generated code or renderer headers.
namespace drakan_ws::detail {
inline constexpr float nativeAspect = 4.0f / 3.0f;
inline float fieldOfViewScale(float degrees) {
    if (!std::isfinite(degrees))
        degrees = 80.0f;
    degrees = degrees < 60 ? 60 : degrees > 120 ? 120 : degrees;
    constexpr float radians = 0.017453292519943295f;
    return std::tan(40.0f * radians) / std::tan(degrees * .5f * radians);
}

// EE executor state, separate from the window thread's presentation policy.
// Camera/renderer addresses are reused by native inventory drawing, so neither
// pointer identity nor an active GUI ID can identify a world render by itself.
struct ProjectionScope {
    bool open = false;
    int owner = -1;
    uint32_t returnPc = 0;
    float aspect = nativeAspect;
    float fovScale = 1.0f;

    void begin(int thread, uint32_t caller, float requested, float fov = 1.0f) {
        open = true;
        owner = thread;
        returnPc = caller;
        aspect = requested;
        fovScale = fov;
    }
    bool world(int thread) const { return open && owner == thread; }
    float effectiveAspect(int thread) const { return world(thread) ? aspect : nativeAspect; }
    float effectiveFovScale(int thread) const { return world(thread) ? fovScale : 1.0f; }
    bool end(int thread) {
        if (!world(thread))
            return false;
        open = false;
        return true;
    }
};

inline float projectionX(float focal, float width, float aspect) {
    const float stock = -focal / width;
    return aspect == nativeAspect ? stock : stock * (nativeAspect / aspect);
}

// 0x199ff0's first call in 0x133498 consumes a caller-local {width,height,focal}
// vector. The second (extended frustum) call inherits its modified width.
inline bool correctFrustumWidth(const ProjectionScope& scope, int thread, uint32_t pc, uint32_t ra, uint32_t output,
                                uint32_t renderer, uint32_t input, uint32_t sp, float focal, float sx, float& width) {
    if (!scope.world(thread) || (scope.aspect == nativeAspect && scope.fovScale == 1.0f) || pc != 0x199ff0u ||
        ra != 0x133508u || output != renderer + 0x84u || input != sp || !std::isfinite(focal) || focal <= 0 ||
        !std::isfinite(sx) || sx >= -1e-6f || !std::isfinite(width) || width <= 0)
        return false;
    const float corrected = focal / -sx;
    if (!std::isfinite(corrected) || corrected <= 0)
        return false;
    width = corrected;
    return true;
}

// A mod pack's `level_aspect` entry, "Level title = 4:3" (also "16:9", or a number such as 1.333): the level title
// and the aspect its world is shown at. False when the entry has no '=', no title or no usable aspect (1.0 .. 4.0).
inline bool parseLevelAspect(const std::string& entry, std::string& level, float& aspect) {
    const size_t eq = entry.rfind('=');
    if (eq == std::string::npos)
        return false;
    const auto trim = [](std::string t) {
        const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    level = trim(entry.substr(0, eq));
    const std::string value = trim(entry.substr(eq + 1));
    float w = 0.0f, h = 0.0f;
    if (std::sscanf(value.c_str(), "%f:%f", &w, &h) == 2 && h > 0.0f)
        aspect = w / h;
    else if (std::sscanf(value.c_str(), "%f", &w) == 1)
        aspect = w;
    else
        return false;
    return !level.empty() && std::isfinite(aspect) && aspect >= 1.0f && aspect <= 4.0f;
}

// The aspect the world of the loaded level is rendered and presented at: the window's (or the fixed) aspect, but not
// wider than the level's own (`levelAspect`, 0 = the level has none). A level never widens the player's choice.
inline float levelWorldAspect(float target, float levelAspect) {
    return levelAspect > 0.0f && levelAspect < target ? levelAspect : target;
}

inline bool frontEndScreen(int id) {
    return id == 8 || id == 0xe || id == 0x13;
}

// Movie player screens: 0x1d = intro / outro book, 0x1e = logo (drakan_scene_camera.cpp guiAllowsWorld, the movie
// completion thunk in drakan_overrides.cpp). The engine keeps updating the 3D camera underneath them.
inline bool movieScreen(int id) {
    return id == 0x1d || id == 0x1e;
}

inline bool worldMenuScreen(int id) {
    // Native pause/inventory and their menu children/message boxes. These IDs
    // are also recognized by drakan_pause::menuScreen. They cannot establish a
    // world by themselves: options/load are accessible from the title too.
    switch (id) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 9:
        case 10:
        case 13:
        case 0x15:
        case 0x17:
        case 0x1b:
        case 0x1c:
            return true;
        default:
            return false;
    }
}

// The save-in-progress screen (0x1a) that follows the save browser (5) while the game renders the slot picture and
// writes the card. The camera stops for ~15 vblanks during the write; the screen keeps an established world (like
// the pause menus) but is not a native menu page: the game draws it like gameplay.
inline bool saveWriteScreen(int id) {
    return id == 0x1a;
}

// Native interactive menus use a wide backdrop and a single centered 4:3
// foreground canvas. Loading keeps that same canvas and presentation aspect.
// Credits, inventory and movies keep their own policy.
inline bool nativeMenuScreen(int id) {
    return id == 8 || id == 14 || (worldMenuScreen(id) && id != 21);
}

// The gameplay HUD size (hud_scale) applies while the world page is shown and under the native pause menu (0x17),
// which keeps drawing the gameplay HUD while it slides out of the screen corners; other menus are laid out at size 1.
inline bool gameplayHudSize(int screen, bool world) {
    return world && (!nativeMenuScreen(screen) || screen == 0x17);
}

// Full-screen baked 4:3 pictures with no widget layout to keep proportions for (credits): the presenter stretches
// the picture to the output aspect instead of pillarboxing it. Projection and foreground stay native.
inline bool stretchedImageScreen(int id) {
    return id == 0x13;
}

struct LayoutPolicy {
    bool menu, presentWide;
    float projection, foregroundScale;
    bool stretchImage = false;
};
inline LayoutPolicy layoutPolicy(int screen, bool world, bool enabled, float aspect, bool hudCorrect) {
    const bool menu = nativeMenuScreen(screen);
    const bool wide = enabled && aspect > 1.4f;
    return {menu, wide && (world || menu), wide && world ? aspect : nativeAspect,
            wide && (menu || (world && hudCorrect)) ? nativeAspect / aspect : 1.0f,
            wide && stretchedImageScreen(screen)};
}

struct WorldPolicy {
    bool wide = false;
    uint64_t lastWorldSeen = 0;

    // movieTick: vblank of the newest IPU movie frame (0 = none). The movie screens themselves (movieScreen) never
    // establish the world; the grace only covers a movie frame still on screen after its screen was left. Gating the
    // movie screens on IPU recency alone widened the picture whenever the EE stalled for more than kMovieGrace vblanks
    // between two frames (disc refill, HD-pack seek, slow host) and for the last ~50 vblanks after the final frame.
    static constexpr uint64_t kMovieGrace = 15;

    // A level change draws the save-in-progress screen (0x1a) between two stretches of the loading screen (0xe):
    // 0xe -> 0x1a -> 0xe -> gameplay. Alone, 0x1a is neither a menu nor a world, so those ~10 vblanks were presented
    // as a 4:3 pillarbox in the middle of a wide loading screen. The screen that decides the layout therefore stays
    // the loading screen while 0x1a follows it (and ends with the first screen that is neither).
    bool loadingLatch = false;
    int layoutScreen(int screen) {
        if (screen == 0xe)
            loadingLatch = true;
        else if (!saveWriteScreen(screen))
            loadingLatch = false;
        return loadingLatch ? 0xe : screen;
    }

    void update(uint64_t now, uint64_t cameraTick, int screen, bool settingsOpen, uint64_t movieTick = 0) {
        // F1 holds the EE. Refresh the same grace period used on native menu
        // close so the first resumed host frame cannot expire the held world.
        if (settingsOpen) {
            if (wide)
                lastWorldSeen = now;
            return;
        }
        if (frontEndScreen(screen) || movieScreen(screen) || (movieTick != 0 && now - movieTick <= kMovieGrace)) {
            wide = false;
            return;
        }
        const bool recent = cameraTick != 0 && now - cameraTick <= 6;
        // -1 is the live gameplay page. A slow render or streaming stall can
        // outlast camera recency without leaving that page; preserve the world
        // until an actual page transition, as we already do for pause menus.
        if (recent || (wide && (screen == -1 || worldMenuScreen(screen) || saveWriteScreen(screen)))) {
            wide = true;
            lastWorldSeen = now;
        } else if (wide && now - lastWorldSeen > 6)
            wide = false;
    }
};
} // namespace drakan_ws::detail
