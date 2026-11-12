// Which path the vertex work takes: the setting, the diagnosis overrides, the control word, the toggle and the
// counters (gsr/vertex_path.h).
#include "gsr/vertex_path.h"

#include <cstdio>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    using namespace gsr::vpath;
    const Capability none{};
    const Capability unproven{true, true, false};
    const Capability proven{true, true, true};
    const Capability noAdapter{true, false, false};

    // the setting: cpu never leaves the microprogram, gpu needs a backend path and an exact adapter, auto also needs
    // the combination to be proven
    expect(resolve(Choice::Cpu, proven) == Eval::Microprogram, "cpu stays on the microprogram");
    expect(resolve(Choice::Gpu, none) == Eval::Microprogram, "gpu without a backend path");
    expect(resolve(Choice::Gpu, noAdapter) == Eval::Microprogram, "gpu without an exact adapter");
    expect(resolve(Choice::Gpu, unproven) == Eval::Gpu, "gpu on an unproven combination");
    expect(resolve(Choice::Auto, unproven) == Eval::Microprogram, "auto on an unproven combination");
    expect(resolve(Choice::Auto, proven) == Eval::Gpu, "auto on a proven combination");

    Choice choice = Choice::Gpu;
    expect(parseChoice("cpu", choice) && choice == Choice::Cpu, "parse cpu");
    expect(parseChoice("gpu", choice) && choice == Choice::Gpu, "parse gpu");
    expect(parseChoice("auto", choice) && choice == Choice::Auto, "parse auto");
    expect(!parseChoice("fast", choice) && choice == Choice::Auto, "unknown choice leaves the value");
    expect(!parseChoice(nullptr, choice), "null choice");
    Eval eval = Eval::Gpu;
    expect(parseEval("microprogram", eval) && eval == Eval::Microprogram, "parse microprogram");
    expect(parseEval("records", eval) && eval == Eval::Records, "parse records");
    expect(parseEval("gpu", eval) && eval == Eval::Gpu, "parse gpu eval");
    expect(!parseEval("x", eval) && eval == Eval::Gpu, "unknown eval leaves the value");

    uint32_t stages = 0;
    expect(parseStages("all", stages) && stages == kAllStages, "all stages");
    expect(parseStages("none", stages) && stages == 0u, "no stages");
    expect(parseStages("strips", stages) && stages == kStrips, "one stage");
    expect(parseStages("strips,prims,light", stages) && stages == (kStrips | kPrimitives | kLightTransform), "a list");
    expect(parseStages("-texmatrix,-grid", stages) && stages == (kAllStages & ~(kTextureMatrix | kGridLights)),
           "everything but two");
    expect(parseStages("5", stages) && stages == 5u, "a mask");
    expect(parseStages("1023", stages) && stages == kAllStages, "a mask is cut to the known stages");
    stages = 77;
    expect(!parseStages("strips,bogus", stages) && stages == 77u, "an unknown name leaves the value");
    expect(!parseStages("", stages) && !parseStages(nullptr, stages), "empty list");
    for (uint32_t i = 0; i < kStageCount; ++i)
        expect(parseStages(kStageNames[i], stages) && stages == (1u << i), kStageNames[i]);

    // the control word
    const uint32_t word = pack(Eval::Gpu, kStrips | kPrimitives);
    expect(evalOf(word) == Eval::Gpu && stagesOf(word) == (kStrips | kPrimitives), "pack and unpack");
    expect(takes(word, kStrips) && takes(word, kPrimitives) && !takes(word, kDepthFade), "a stage that is off");
    expect(!takes(pack(Eval::Microprogram, kAllStages), kStrips), "the microprogram takes nothing");
    expect(takes(pack(Eval::Records, kAllStages), kStrips), "records leave the microprogram");

    // overrides: the evaluation mode is taken as given, the stage list replaces the full set
    expect(configuredWord(Choice::Cpu, none, nullptr, nullptr) == pack(Eval::Microprogram, kAllStages),
           "the default word");
    expect(configuredWord(Choice::Cpu, none, "records", nullptr) == pack(Eval::Records, kAllStages),
           "records override");
    expect(configuredWord(Choice::Gpu, proven, "microprogram", "strips") == pack(Eval::Microprogram, kStrips),
           "microprogram override");
    expect(configuredWord(Choice::Gpu, proven, "nonsense", "nonsense") == pack(Eval::Gpu, kAllStages),
           "unparsable overrides are ignored");

    // the toggle: even periods keep the configured word, odd ones run the microprogram
    expect(toggled(word, 0, 0) == word && toggled(word, 99, 0) == word, "no toggle");
    expect(toggled(word, 0, 4) == word && toggled(word, 3, 4) == word, "first period");
    expect(evalOf(toggled(word, 4, 4)) == Eval::Microprogram && stagesOf(toggled(word, 4, 4)) == stagesOf(word),
           "second period");
    expect(toggled(word, 8, 4) == word, "third period");

    // configuration, frame boundaries and counters
    State& s = state();
    const uint64_t switchesBefore = s.counters.switches.load();
    configure(word);
    expect(controlWord() == word, "configure applies the word");
    configure(word);
    expect(s.counters.switches.load() == switchesBefore + 1, "the same word again is not a switch");
    frameBoundary(1000);
    expect(controlWord() == word, "a frame boundary without a toggle changes nothing");
    configure(word, 2);
    expect(evalOf(controlWord()) == Eval::Microprogram, "a toggle starts on the microprogram until a frame ends");
    uint32_t changes = 0;
    uint32_t last = controlWord();
    for (uint64_t frame = 0; frame < 64; ++frame) {
        frameBoundary(frame);
        const uint32_t now = controlWord();
        expect(now == toggled(word, frame, 2), "the word of a toggled frame");
        if (now != last)
            ++changes;
        last = now;
    }
    expect(changes == 32, "64 frames at a period of 2 switch 32 times");
    configure(pack(Eval::Microprogram, kAllStages));

    countFallback(Fallback::Clipper);
    countFallback(Fallback::Clipper, 4);
    countFallback(Fallback::StageOff);
    expect(s.counters.fallback[static_cast<uint32_t>(Fallback::Clipper)].load() == 5, "fallback counter");
    expect(s.counters.fallback[static_cast<uint32_t>(Fallback::StageOff)].load() == 1, "second fallback counter");
    countTaken(kStrips, 120);
    countTaken(kPrimitives, 30);
    expect(s.counters.taken[0].load() == 1 && s.counters.taken[6].load() == 1, "taken per stage");
    expect(s.counters.takenVertices.load() == 150, "taken vertices");

    if (g_failures)
        std::printf("%d failure(s)\n", g_failures);
    else
        std::printf("vertex_path: ok\n");
    return g_failures ? 1 : 0;
}
