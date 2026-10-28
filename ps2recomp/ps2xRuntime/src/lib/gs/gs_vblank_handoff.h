#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <gsr/scene_guide_camera.h>

namespace drakan_vblank {
// Track only fields actually changed by live UI setters. A flicker-only
// change must not erase a startup/environment deinterlace override.
class FieldMode {
    std::atomic<uint32_t> bits{7}; // unknown is always field-sensitive
public:
    void update(uint32_t mode, bool flicker, uint32_t mask = 7) {
        const uint32_t values = (mode <= 2 ? mode : 3u) | (flicker ? 4u : 0u);
        mask &= 7u;
        auto old = bits.load(std::memory_order_relaxed);
        while (!bits.compare_exchange_weak(old, (old & ~mask) | (values & mask), std::memory_order_release,
                                           std::memory_order_relaxed)) {
        }
    }
    bool safe() const { return bits.load(std::memory_order_acquire) == 0; }
};
// Context-lock owned. This changes only host handoffs, never GS execution,
// guest vblank/field progression, draws or transfers.
class Handoff {
public:
    struct Sample {
        std::array<uint64_t, 17> display{};
        uint64_t draws = 0, hostToLocal = 0, localToLocal = 0, uploads = 0, readbacks = 0;
        uint64_t cpuWrites = 0, liveApplied = 0;
        bool operator==(const Sample&) const = default;
    };
    enum Reason : unsigned {
        Native,
        FieldSensitive,
        SettingsChanged,
        AwaitFreshWorld,
        OpenWorld,
        NoCompletedWorld,
        SceneChanged,
        DrawChanged,
        TransferChanged,
        DisplayChanged,
        Baseline,
        Duplicate,
        Rearmed,
        PendingBegin,
        PendingContentChanged,
        ReasonCount
    };
    static constexpr std::array<const char*, ReasonCount> reasonNames = {
        "native",           "fieldSensitive", "settingsChanged", "awaitFreshWorld", "openWorld",
        "noCompletedWorld", "sceneChanged",   "drawChanged",     "transferChanged", "displayChanged",
        "baseline",         "duplicate",      "rearmed",         "pendingBegin",    "pendingContentChanged"};
    uint32_t reasons() const { return lastReasons; }
    void configure(bool weave) {
        *this = Handoff{};
        initialFieldSafe = weave;
    }
    struct SceneClear {
        bool cleared = false;
        uint32_t sourcePc = 0, flags = 0, closedBy = 0;
        int32_t gui = -1;
        int phaseBefore = -1, phaseAfter = -1; // -1 unknown, 0 closed, 1 open
    };
    const SceneClear& sceneClear() const { return lastSceneClear; }
    void scene(const gsr::SceneGuideCamera& c, const Sample* content = nullptr) {
        using Camera = gsr::SceneGuideCamera;
        const bool wasClosed = complete && !open;
        const int phaseBefore = enginePhase;
        lastSceneClear = {};
        // Snapshot AFTER the phase marker flushed older GS work. Compare to
        // that point, not the previous vblank (which may predate the completed
        // world we are retaining). Never move this anchor on repeated begins.
        if (pendingBegin && (!content || !sameContent(*content, pendingContent))) {
            pendingBegin = false;
            complete = false;
            ++sceneRevision;
        }
        if (c.sourcePc == 0x139010u && c.flags == 0)
            enginePhase = 1;
        if ((c.sourcePc == 0x147690u || c.sourcePc == 0x139010u) && (c.flags & Camera::EndWorld))
            enginePhase = 0;
        constexpr uint32_t required = Camera::Valid | Camera::World | Camera::HaveViewport;
        if (c.sourcePc == 0x139010u && c.flags == 0 && c.guiScreen < 0 && complete && !open && content) {
            if (!pendingBegin)
                pendingContent = *content;
            pendingBegin = true; // next engine entry precedes its native wait/camera
        } else if (c.flags & Camera::EndWorld) {
            pendingBegin = false;
            if (open) {
                open = false;
                complete = true;
                ++completedWorlds;
                closedBy = c.sourcePc;
            }
        } else if ((c.flags & required) == required && c.guiScreen < 0 && c.frameWidth && c.frameHeight) {
            pendingBegin = false;
            open = true;
            complete = false;
            ++sceneRevision;
        } else if (c.sourcePc == 0x1e97a0u && c.guiScreen < 0 && complete && !open) {
            // Only the existing known native preview exception is retained.
        } else {
            pendingBegin = false;
            open = false;
            complete = false;
            ++sceneRevision;
        }
        if (wasClosed && !complete)
            lastSceneClear = {true, c.sourcePc, c.flags, closedBy, c.guiScreen, phaseBefore, enginePhase};
    }
    bool advance(bool neural, const Sample& now) { return advance(neural, now, initialFieldSafe); }
    // fieldSafe is the verified requested weave/flicker-off mode, sampled AFTER
    // vsync applies queued settings. Unknown/field-dependent modes never skip.
    bool advance(bool neural, const Sample& now, bool fieldSafe) {
        lastReasons = 0;
        if (pendingBegin) {
            note(PendingBegin);
            if (!sameContent(now, pendingContent)) {
                pendingBegin = false;
                complete = false;
                ++sceneRevision;
                note(PendingContentChanged);
            }
        }
        const bool settingsChanged = haveSample && now.liveApplied != previous.liveApplied;
        const bool fieldChanged = haveSample && fieldSafe != previousFieldSafe;
        if (settingsChanged || fieldChanged || !neural || !fieldSafe) {
            needsFreshWorld = true;
            worldAtChange = completedWorlds;
            previousQualified = false;
        }
        if (settingsChanged || fieldChanged)
            note(SettingsChanged);
        if (!neural)
            note(Native);
        if (!fieldSafe)
            note(FieldSensitive);
        if (open)
            note(OpenWorld);
        if (!complete)
            note(NoCompletedWorld);
        const bool closed = complete && !open;
        if (neural && fieldSafe && closed && needsFreshWorld && completedWorlds > worldAtChange && !settingsChanged &&
            !fieldChanged) {
            needsFreshWorld = false;
            note(Rearmed);
        }
        if (needsFreshWorld)
            note(AwaitFreshWorld);
        const bool qualified = neural && fieldSafe && closed && !needsFreshWorld;
        if (haveSample) {
            if (previousScene != sceneRevision)
                note(SceneChanged);
            if (now.draws != previous.draws)
                note(DrawChanged);
            if (now.hostToLocal != previous.hostToLocal || now.localToLocal != previous.localToLocal ||
                now.uploads != previous.uploads || now.readbacks != previous.readbacks ||
                now.cpuWrites != previous.cpuWrites)
                note(TransferChanged);
            if (now.display != previous.display)
                note(DisplayChanged);
        }
        // The first qualified sample after a change is always handed off.
        // Only a subsequent identical interval in the same closed world skips.
        const bool duplicate =
            qualified && previousQualified && haveSample && previousScene == sceneRevision && now == previous;
        if (duplicate)
            note(Duplicate);
        else if (qualified && !previousQualified)
            note(Baseline);
        previous = now;
        previousScene = sceneRevision;
        haveSample = true;
        previousQualified = qualified;
        previousFieldSafe = fieldSafe;
        return !duplicate;
    }

private:
    static bool sameContent(const Sample& a, const Sample& b) {
        return a.draws == b.draws && a.hostToLocal == b.hostToLocal && a.localToLocal == b.localToLocal &&
               a.uploads == b.uploads && a.readbacks == b.readbacks && a.cpuWrites == b.cpuWrites;
    }
    void note(Reason reason) { lastReasons |= uint32_t{1} << reason; }
    Sample previous{}, pendingContent{};
    uint64_t sceneRevision = 0, previousScene = 0, completedWorlds = 0, worldAtChange = 0;
    uint32_t lastReasons = 0, closedBy = 0;
    int enginePhase = -1;
    SceneClear lastSceneClear{};
    bool initialFieldSafe = false, previousFieldSafe = false;
    bool open = false, complete = false, needsFreshWorld = false;
    bool previousQualified = false, haveSample = false, pendingBegin = false;
};
} // namespace drakan_vblank
