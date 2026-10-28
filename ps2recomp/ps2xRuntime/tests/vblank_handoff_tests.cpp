#include "../src/lib/gs/gs_vblank_handoff.h"
#include <cstdio>
#include <cstdlib>
using drakan_vblank::Handoff;
using Camera = gsr::SceneGuideCamera;
static unsigned checks = 0;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}
static Camera world() {
    Camera c;
    c.flags = Camera::Valid | Camera::World | Camera::HaveViewport;
    c.frameWidth = c.frameHeight = 512;
    return c;
}
static void complete(Handoff& p) {
    p.scene(world());
    Camera end;
    end.flags = Camera::EndWorld;
    p.scene(end);
}
static Handoff armed(Handoff::Sample s = {}) {
    Handoff p;
    p.configure(true);
    complete(p);
    check(p.advance(true, s), "first completed world always advances");
    return p;
}
int main() {
    Handoff::Sample s;
    {
        auto p = armed();
        check(!p.advance(true, s), "unchanged completed world suppresses duplicate handoff");
        check(!p.advance(true, s), "repeated unchanged world remains duplicate");
        ++s.draws;
        check(p.advance(true, s), "any new draws preserve HUD, particles and fades");
        check(!p.advance(true, s), "subsequent no-draw blank is duplicate");
        complete(p);
        check(p.advance(true, s), "new world/camera metadata forces handoff even without draw");
    }
    for (unsigned i = 0; i < 17; ++i) {
        auto p = armed();
        auto changed = Handoff::Sample{};
        changed.display[i] = 1;
        check(p.advance(true, changed), "every privileged snapshot change forces handoff");
    }
    using Member = uint64_t Handoff::Sample::*;
    for (Member member : {&Handoff::Sample::draws, &Handoff::Sample::hostToLocal, &Handoff::Sample::localToLocal,
                          &Handoff::Sample::uploads, &Handoff::Sample::readbacks, &Handoff::Sample::cpuWrites,
                          &Handoff::Sample::liveApplied}) {
        auto p = armed();
        auto changed = Handoff::Sample{};
        changed.*member = 1;
        check(p.advance(true, changed), "render/VRAM/settings mutations force handoff");
    }
    {
        Handoff p;
        p.configure(true);
        check(p.advance(true, {}), "unknown scene always advances");
        check(p.advance(true, {}), "no positive world metadata never deduplicates");
        Camera end;
        end.flags = Camera::EndWorld;
        p.scene(end);
        check(p.advance(true, {}), "EndWorld alone cannot establish world provenance");
        p.scene(world());
        check(p.advance(true, {}), "unfinished world is never suppressed");
        check(p.advance(true, {}), "second unfinished blank still advances");
    }
    {
        auto p = armed();
        Camera menu;
        menu.guiScreen = 14;
        p.scene(menu);
        check(p.advance(true, {}), "menu transition advances");
        check(p.advance(true, {}), "unchanged menu is not deduplicated");
        Camera movie;
        p.scene(movie);
        check(p.advance(true, {}), "unknown/movie scene advances");
    }
    {
        auto p = armed();
        Camera preview;
        preview.sourcePc = 0x1e97a0u;
        p.scene(preview);
        check(!p.advance(true, {}), "known post-world HUD preview retains positive provenance");
        preview.guiScreen = 8;
        p.scene(preview);
        check(p.advance(true, {}), "known GUI overrides HUD preview exception");
    }
    {
        auto p = armed();
        Camera unknown;
        unknown.sourcePc = 0x123456;
        p.scene(unknown);
        check(p.advance(true, {}), "unrecognized preview cannot retain world provenance");
        Handoff absent;
        absent.configure(true);
        Camera preview;
        preview.sourcePc = 0x1e97a0;
        absent.scene(preview);
        check(absent.advance(true, {}), "HUD preview cannot establish a missing world");
    }
    {
        auto p = armed();
        for (unsigned i = 0; i < 5; ++i)
            check(p.advance(false, {}), "native/SR-only retains every blank");
        check(p.advance(true, {}), "NR re-enable forces initial handoff");
        check(!p.advance(true, {}), "NR re-enabled duplicate may then suppress");
        Handoff bob;
        bob.configure(false);
        complete(bob);
        check(bob.advance(true, {}) && bob.advance(true, {}), "bob/blend never suppresses field presentation");
    }
    {
        auto p = armed();
        Handoff::Sample changed;
        changed.liveApplied = 1;
        check(p.advance(true, changed), "live setting change advances");
        check(p.advance(true, changed), "unknown effective field mode disarms suppression");
        complete(p);
        check(p.advance(true, changed), "new camera cannot accidentally rearm unknown field mode");
    }
    {
        Handoff p;
        p.configure(true);
        Handoff::Sample frame;
        unsigned latches = 0, waits = 0, vsyncs = 0;
        for (unsigned i = 0; i < 60; ++i) {
            if (i % 2 == 0) {
                complete(p);
                frame.draws += 155;
            }
            ++vsyncs;
            const bool advance = p.advance(true, frame);
            if (advance) {
                ++latches;
                ++waits;
            }
        }
        check(vsyncs == 60, "all 60 guest vsyncs retained");
        check(latches == 30 && waits == 30, "30 worlds plus 30 empty blanks need exactly 30 handoffs");
    }
    std::printf("PASS %u CPU vblank handoff checks; no GPU or game\n", checks);
}
