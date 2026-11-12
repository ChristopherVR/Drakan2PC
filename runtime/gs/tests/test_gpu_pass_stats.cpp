// GPU pass timing arithmetic (gpu_pass_stats.h): marks to milliseconds per pass, and a window's per-world report.
#include "gpu_pass_stats.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace gsr::gpupass;

static int g_fail = 0;
static void expect(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    g_fail += !ok;
}
static bool close(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

int main() {
    // 1000 ticks per millisecond
    const uint64_t frequency = 1000000;
    {
        const Mark marks[] = {{Pass::Frame, 1000},      {Pass::World, 2000, true}, {Pass::WorldEntry, 2500},
                              {Pass::World, 2700},      {Pass::Evaluate, 6000},    {Pass::Compose, 9000},
                              {Pass::AfterWorld, 9500}, {Pass::Display, 10000},    {Pass::Frame, 10250}};
        Frame f;
        expect(attribute(marks, std::size(marks), 11000, frequency, f), "a frame with a world is accepted");
        expect(f.worlds == 1, "one world begins in it: resuming the world pass after the entry is not a second one");
        expect(close(f.total, 10.0), "total is the first mark to the end");
        expect(close(f.ms[size_t(Pass::Frame)], 1.0 + 0.75), "a pass that occurs twice is summed");
        expect(close(f.ms[size_t(Pass::World)], 0.5 + 3.3), "the world resumes after the entry");
        expect(close(f.ms[size_t(Pass::WorldEntry)], 0.2), "world entry");
        expect(close(f.ms[size_t(Pass::Evaluate)], 3.0), "evaluate");
        expect(close(f.ms[size_t(Pass::Compose)], 0.5), "compose");
        expect(close(f.ms[size_t(Pass::Display)], 0.25), "display");
        double sum = 0;
        for (double v : f.ms)
            sum += v;
        expect(close(sum, f.total), "the passes add up to the total");
    }
    {
        Frame f;
        const Mark one[] = {{Pass::Frame, 500}};
        expect(attribute(one, 1, 700, frequency, f) && !f.worlds && close(f.total, 0.2), "a frame without a world");
        expect(!attribute(one, 0, 700, frequency, f), "no marks: refused");
        expect(!attribute(one, 1, 700, 0, f), "zero frequency: refused");
        expect(!attribute(one, 1, 400, frequency, f), "an end before the first mark: refused");
        const Mark back[] = {{Pass::Frame, 500}, {Pass::World, 400}};
        expect(!attribute(back, 2, 900, frequency, f), "ticks running backwards: refused");
    }
    {
        // Two worlds over five presented frames: one world's upscale falls into the frame after its raster, and two
        // frames only repeat the picture.
        Window w;
        Frame raster;
        raster.worlds = 1;
        raster.ms[size_t(Pass::World)] = 2.0;
        raster.total = 2.0;
        Frame upscale;
        upscale.ms[size_t(Pass::Evaluate)] = 4.0;
        upscale.ms[size_t(Pass::PresentBlit)] = 0.5;
        upscale.total = 4.5;
        Frame repeat;
        repeat.ms[size_t(Pass::PresentBlit)] = 0.5;
        repeat.total = 0.5;
        Frame whole;
        whole.worlds = 1;
        whole.ms[size_t(Pass::World)] = 3.0;
        whole.ms[size_t(Pass::Evaluate)] = 6.0;
        whole.ms[size_t(Pass::PresentBlit)] = 0.5;
        whole.total = 9.5;
        for (const Frame& f : {raster, upscale, repeat, whole, repeat})
            w.add(f);
        expect(w.size() == 5 && w.worlds() == 2, "the window counts frames and worlds apart");
        expect(close(w.perWorld(Pass::World), 2.5), "raster per world");
        expect(close(w.perWorld(Pass::Evaluate), 5.0), "a pass in the frame after its world still counts for it");
        expect(close(w.perWorld(Pass::PresentBlit), 1.0), "window blits of all frames, per world");
        expect(close(w.perWorld(Pass::Count), 8.5), "everything per world");
        const std::string line = w.report();
        std::printf("  %s\n", line.c_str());
        expect(line.find("frames=5 worlds=2 frameMedianMs=2.000 frameMeanMs=3.400 perWorld: total=8.500") == 0,
               "report head");
        expect(line.find(" evaluate=5.000") != std::string::npos, "report names a pass with time");
        expect(line.find("compose") == std::string::npos, "report leaves out a pass without time");
        Window menu;
        menu.add(repeat);
        expect(close(menu.perWorld(Pass::PresentBlit), 0.0), "no world: no per-world figure");
        expect(close(median({1.0, 3.0}), 2.0) && close(median({}), 0.0), "median of an even count and of nothing");
    }
    expect(std::strcmp(name(Pass::Overlay), "overlay") == 0 && std::strcmp(name(Pass::Frame), "frame") == 0,
           "every pass has its name");
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
