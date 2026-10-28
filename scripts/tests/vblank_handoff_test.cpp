// CPU-only host-handoff policy checks: no renderer or game instantiation.
#include "gs_vblank_handoff.h"
#include <cassert>
#include <cstdio>

using H = drakan_vblank::Handoff;
using C = gsr::SceneGuideCamera;
unsigned checks = 0;
void check(bool value) {
    ++checks;
    assert(value);
}
void world(H& h) {
    C c;
    c.flags = C::Valid | C::World | C::HaveViewport;
    c.frameWidth = c.frameHeight = 512;
    h.scene(c);
    c.flags = C::EndWorld;
    h.scene(c);
}
bool reason(const H& h, H::Reason r) {
    return (h.reasons() & (uint32_t{1} << r)) != 0;
}
H armed(H::Sample& s) {
    H h;
    h.configure(true);
    world(h);
    check(h.advance(true, s, true));
    check(!h.advance(true, s, true));
    return h;
}
int main() {
    drakan_vblank::FieldMode mode;
    check(!mode.safe());
    mode.update(1, false);
    check(!mode.safe());
    mode.update(0, false, 4);
    check(!mode.safe()); // flicker-only preserves bob override
    mode.update(0, false, 3);
    check(mode.safe());
    mode.update(0, true, 4);
    check(!mode.safe());
    mode.update(0, false, 3);
    check(!mode.safe()); // mode-only preserves flicker
    mode.update(0, false, 4);
    check(mode.safe());
    mode.update(2, false);
    check(!mode.safe());
    mode.update(99, false);
    check(!mode.safe());
    mode.update(0, false);
    check(mode.safe());
    H::Sample s;
    auto h = armed(s);
    // Settings changes may have applied after this world was drawn. Neither
    // that world nor a stable stale interval may rearm suppression.
    ++s.liveApplied;
    check(h.advance(true, s, true));
    check(reason(h, H::SettingsChanged));
    check(h.advance(true, s, true));
    check(reason(h, H::AwaitFreshWorld));
    C camera;
    camera.flags = C::Valid | C::World | C::HaveViewport;
    camera.frameWidth = camera.frameHeight = 512;
    h.scene(camera);
    check(h.advance(true, s, true));
    check(reason(h, H::OpenWorld));
    C end;
    end.flags = C::EndWorld;
    h.scene(end);
    check(h.advance(true, s, true));
    check(reason(h, H::Rearmed));
    check(reason(h, H::Baseline));
    check(!h.advance(true, s, true));
    check(reason(h, H::Duplicate));
    // A second setting change invalidates the first recovery attempt.
    ++s.liveApplied;
    check(h.advance(true, s, true));
    world(h);
    ++s.liveApplied;
    check(h.advance(true, s, true));
    check(h.advance(true, s, true));
    world(h);
    check(h.advance(true, s, true));
    check(!h.advance(true, s, true));

    // Every sampled display word independently forces a visible handoff.
    for (unsigned i = 0; i < s.display.size(); ++i) {
        H::Sample a;
        auto d = armed(a);
        ++a.display[i];
        check(d.advance(true, a, true));
        check(reason(d, H::DisplayChanged));
        check(!d.advance(true, a, true));
    }
    // Draws, both transfer directions, uploads, readbacks and direct CPU VRAM
    // writes must never be hidden by an otherwise identical world marker.
    uint64_t H::Sample::* counters[] = {&H::Sample::draws,   &H::Sample::hostToLocal, &H::Sample::localToLocal,
                                        &H::Sample::uploads, &H::Sample::readbacks,   &H::Sample::cpuWrites};
    for (auto counter : counters) {
        H::Sample a;
        auto d = armed(a);
        ++(a.*counter);
        check(d.advance(true, a, true));
        check(reason(d, counter == &H::Sample::draws ? H::DrawChanged : H::TransferChanged));
        check(!d.advance(true, a, true));
    }

    // Unsafe modes (bob, blend, weave with flicker, unknown) always hand off,
    // even with fresh completed worlds and identical content. Returning to
    // known weave must wait for a world completed after the observed switch.
    H::Sample a;
    auto d = armed(a);
    for (int i = 0; i < 4; ++i) {
        world(d);
        check(d.advance(true, a, false));
        check(reason(d, H::FieldSensitive));
    }
    check(d.advance(true, a, true));
    check(d.advance(true, a, true));
    world(d);
    check(d.advance(true, a, true));
    check(!d.advance(true, a, true));
    check(d.advance(false, a, true));
    check(reason(d, H::Native));
    check(d.advance(true, a, true));
    world(d);
    check(d.advance(true, a, true));
    check(!d.advance(true, a, true));

    // Phase/UI/movie/menu/open-world transitions cannot inherit a closed-world
    // duplicate classification. A known non-GUI preview may retain it.
    C gui;
    gui.guiScreen = 21;
    d.scene(gui);
    check(d.advance(true, a, true));
    check(d.advance(true, a, true));
    world(d);
    check(d.advance(true, a, true));
    C preview;
    preview.sourcePc = 0x1e97a0;
    preview.guiScreen = -1;
    d.scene(preview);
    check(!d.advance(true, a, true));
    C phase;
    phase.sourcePc = 0x139010;
    phase.flags = 0;
    d.scene(phase);
    check(d.advance(true, a, true));
    d.scene(end);
    check(d.advance(true, a, true));
    // Live-confirmed order: completed world, next entry's phase marker, native
    // wait/vblank, then validated camera. Entry alone has no new pixels.
    H::Sample pendingSample;
    auto pending = armed(pendingSample);
    pending.scene(phase, &pendingSample);
    check(!pending.sceneClear().cleared);
    check(!pending.advance(true, pendingSample, true));
    check(reason(pending, H::PendingBegin));
    pending.scene(phase, &pendingSample); // repeated begin does not move anchor
    check(!pending.advance(true, pendingSample, true));
    pending.scene(camera, &pendingSample);
    check(pending.advance(true, pendingSample, true));
    check(reason(pending, H::OpenWorld));
    pending.scene(end, &pendingSample);
    check(pending.advance(true, pendingSample, true));
    check(!pending.advance(true, pendingSample, true));

    // A newer completed world may not yet have reached its first vblank.
    // Pending comparisons must anchor at entry, not the prior vblank.
    world(pending);
    pendingSample.draws += 10;
    pending.scene(phase, &pendingSample);
    check(pending.advance(true, pendingSample, true));
    check(!pending.advance(true, pendingSample, true));

    // Every kind of new content before camera validation discards closure;
    // even changedSample subsequent stable partial-world interval must continue handing off.
    for (auto counter : counters) {
        H::Sample changedSample;
        auto changedPolicy = armed(changedSample);
        changedPolicy.scene(phase, &changedSample);
        ++(changedSample.*counter);
        check(changedPolicy.advance(true, changedSample, true));
        check(reason(changedPolicy, H::PendingContentChanged));
        check(changedPolicy.advance(true, changedSample, true));
        check(reason(changedPolicy, H::NoCompletedWorld));
        changedPolicy.scene(camera, &changedSample);
        check(changedPolicy.advance(true, changedSample, true));
        changedPolicy.scene(end, &changedSample);
        check(changedPolicy.advance(true, changedSample, true));
        check(!changedPolicy.advance(true, changedSample, true));
    }
    for (unsigned i = 0; i < pendingSample.display.size(); ++i) {
        H::Sample changedSample;
        auto changedPolicy = armed(changedSample);
        changedPolicy.scene(phase, &changedSample);
        ++changedSample.display[i];
        check(changedPolicy.advance(true, changedSample, true));
        check(reason(changedPolicy, H::DisplayChanged));
    }
    // A repeated begin after drawing may not reanchor and hide that drawing.
    H::Sample b;
    auto twice = armed(b);
    twice.scene(phase, &b);
    ++b.draws;
    twice.scene(phase, &b);
    check(twice.advance(true, b, true));
    check(twice.advance(true, b, true));

    // Early end without camera/drawing retains only the OLD completion. It
    // cannot manufacture a fresh world to satisfy settings rearming.
    C earlyEnd;
    earlyEnd.sourcePc = 0x139010;
    earlyEnd.flags = C::EndWorld;
    H::Sample e;
    auto early = armed(e);
    early.scene(phase, &e);
    early.scene(earlyEnd, &e);
    check(!early.advance(true, e, true));
    ++e.liveApplied;
    check(early.advance(true, e, true));
    early.scene(phase, &e);
    early.scene(earlyEnd, &e);
    check(early.advance(true, e, true));
    check(reason(early, H::AwaitFreshWorld));
    world(early);
    check(early.advance(true, e, true));
    check(!early.advance(true, e, true));
    auto partial = armed(e);
    partial.scene(phase, &e);
    ++e.draws;
    partial.scene(earlyEnd, &e);
    check(partial.advance(true, e, true));
    check(partial.advance(true, e, true));
    H::Sample g;
    auto guiPending = armed(g);
    guiPending.scene(phase, &g);
    guiPending.scene(gui, &g);
    check(guiPending.advance(true, g, true));
    check(guiPending.advance(true, g, true));
    auto unknownPending = armed(g);
    unknownPending.scene(phase, &g);
    C unknown;
    unknown.sourcePc = 0x123456;
    unknownPending.scene(unknown, &g);
    check(unknownPending.advance(true, g, true));
    check(unknownPending.advance(true, g, true));
    auto unsafePending = armed(g);
    unsafePending.scene(phase, &g);
    check(unsafePending.advance(true, g, false));
    check(unsafePending.advance(false, g, true));
    H noCamera;
    noCamera.configure(true);
    noCamera.scene(end);
    check(noCamera.advance(true, a, true));
    check(noCamera.advance(true, a, true));
    std::printf("PASS: %u handoff checks; fresh-world rearm, all display/content fields, unsafe field modes, native/UI "
                "guards\n",
                checks);
}
