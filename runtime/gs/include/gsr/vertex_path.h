// Which path the vertex work of a VU1 run takes, and how often each path was taken.
//
// The game's vertex work can run three ways, and all three stay in the product:
//   Microprogram  the VU1 microprogram runs on the CPU and sends GS packets (the original path; nothing here acts)
//   Records       a recognised run is sent to the renderer as a record of its inputs (gsr/vertex_program.h) and the
//                 renderer evaluates the record on the CPU with the VU's arithmetic (the reference of the GPU path)
//   Gpu           the same record is drawn by a vertex shader of a backend that has an exact one; whatever a backend
//                 cannot draw exactly is evaluated on the CPU instead, per record
// The player's setting is `[video] vertex_processing = cpu | gpu | auto`; `resolve()` turns it into an evaluation mode
// from what the renderer reports. Finer switches for diagnosis override it (docs/reference/env-flags.md):
// DRAKAN_VERTEX_EVAL (microprogram | records | gpu), DRAKAN_VERTEX_STAGES (which stage families may leave the
// microprogram) and DRAKAN_VERTEX_TOGGLE (switch back and forth inside one run).
//
// One control word is shared by the VU1 worker (which decides per run whether to build a record) and the renderer
// (which decides per record whether to draw or evaluate it). It may change at any time: a run or record that started
// under one value finishes under it, so a change takes effect at the next object. Both paths produce the same GS
// input bit for bit, which is what makes the switch safe; saves and captures do not depend on it.
//
// Header only, no platform headers.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace gsr::vpath {

// The player's setting.
enum class Choice : uint8_t { Cpu = 0, Gpu = 1, Auto = 2 };

enum class Eval : uint8_t { Microprogram = 0, Records = 1, Gpu = 2 };

// Stage families of the microprogram that can leave it, one bit each.
enum Stage : uint32_t {
    kStrips = 1u << 0,         // triangle strips of an object inside the view (lit and constant colour)
    kDepthFade = 1u << 1,      // batch: alpha from depth
    kTextureMatrix = 1u << 2,  // batch: 2 x 3 matrix on the texture coordinates
    kGridLights = 1u << 3,     // batch: grid lights
    kPointLights = 1u << 4,    // batch: point lights
    kLightTransform = 1u << 5, // batch: directional lights, transform, fog
    kPrimitives = 1u << 6,     // batch: index lists to primitives
    kAllStages = (1u << 7) - 1u,
};
inline constexpr uint32_t kStageCount = 7;
inline constexpr const char* kStageNames[kStageCount] = {"strips", "fade",  "texmatrix", "grid",
                                                         "point",  "light", "prims"};

// Why a run or record did not take the GPU path. One counter each.
enum class Fallback : uint32_t {
    StageOff = 0,     // the run's stage family is switched off
    EnginePending,    // the VU1 engine was not in a state a run can be skipped from
    NotRecognised,    // the run's data does not have the layout the record assumes
    Clipper,          // the run uses clip codes or the clipper (cannot be matched by a rasteriser)
    BackendNoPath,    // the renderer backend has no exact vertex shader (records evaluated on the CPU)
    AdapterNoSupport, // the adapter lacks what the exact shader needs (records evaluated on the CPU)
    DrawState,        // a GS state the shader path does not cover (flat shading, UV coordinates, small target)
    SkipBit,          // the run's constants allow the "do not draw" bit of a vertex
    Feature,          // a renderer feature that reads vertices on the CPU is on
    DeviceLost,       // the graphics device was lost
    Internal,         // a failed allocation, shader or buffer: the path is off for the session
    Count
};
inline constexpr uint32_t kFallbackCount = static_cast<uint32_t>(Fallback::Count);
inline constexpr const char* kFallbackNames[kFallbackCount] = {
    "stage off",  "engine pending", "not recognised", "clipper",     "backend", "adapter",
    "draw state", "skip bit",       "feature",        "device lost", "internal"};

// What the renderer can do with a record, reported once it has a device.
struct Capability {
    bool backendHasPath = false; // the backend implements vertex-program draws at all
    bool adapterExact = false;   // and this adapter passed the exactness requirements (arithmetic available)
    bool proven = false;         // and the combination is one the project has proven identical (what `auto` needs)
};

// The setting to an evaluation mode. `gpu` asks for the GPU path wherever the backend has one (anything it cannot
// draw exactly is evaluated on the CPU per record); `auto` only where the combination is proven.
inline Eval resolve(Choice choice, const Capability& cap) {
    if (choice == Choice::Cpu)
        return Eval::Microprogram;
    if (!cap.backendHasPath || !cap.adapterExact)
        return Eval::Microprogram;
    if (choice == Choice::Auto && !cap.proven)
        return Eval::Microprogram;
    return Eval::Gpu;
}

inline bool parseChoice(const char* text, Choice& out) {
    if (!text)
        return false;
    if (!std::strcmp(text, "cpu") || !std::strcmp(text, "0"))
        out = Choice::Cpu;
    else if (!std::strcmp(text, "gpu") || !std::strcmp(text, "1"))
        out = Choice::Gpu;
    else if (!std::strcmp(text, "auto"))
        out = Choice::Auto;
    else
        return false;
    return true;
}
inline const char* choiceName(Choice c) {
    return c == Choice::Gpu ? "gpu" : c == Choice::Auto ? "auto" : "cpu";
}
inline bool parseEval(const char* text, Eval& out) {
    if (!text)
        return false;
    if (!std::strcmp(text, "microprogram") || !std::strcmp(text, "micro"))
        out = Eval::Microprogram;
    else if (!std::strcmp(text, "records"))
        out = Eval::Records;
    else if (!std::strcmp(text, "gpu"))
        out = Eval::Gpu;
    else
        return false;
    return true;
}
inline const char* evalName(Eval e) {
    return e == Eval::Gpu ? "gpu" : e == Eval::Records ? "records" : "microprogram";
}

// A stage list: "all", "none", a number (bit mask), or names separated by commas, each optionally with a leading '-'
// to remove it from the full set ("-texmatrix,-grid" = everything but those two). False: a name is unknown.
inline bool parseStages(const char* text, uint32_t& out) {
    if (!text || !*text)
        return false;
    if (!std::strcmp(text, "all")) {
        out = kAllStages;
        return true;
    }
    if (!std::strcmp(text, "none")) {
        out = 0;
        return true;
    }
    if (text[0] >= '0' && text[0] <= '9') {
        uint32_t value = 0;
        for (const char* p = text; *p; ++p) {
            if (*p < '0' || *p > '9')
                return false;
            value = value * 10u + static_cast<uint32_t>(*p - '0');
        }
        out = value & kAllStages;
        return true;
    }
    uint32_t add = 0, remove = 0;
    const char* p = text;
    while (*p) {
        const bool minus = *p == '-';
        if (minus)
            ++p;
        const char* end = p;
        while (*end && *end != ',')
            ++end;
        uint32_t bit = 0;
        for (uint32_t i = 0; i < kStageCount; ++i)
            if (std::strlen(kStageNames[i]) == static_cast<size_t>(end - p) &&
                !std::strncmp(kStageNames[i], p, static_cast<size_t>(end - p)))
                bit = 1u << i;
        if (!bit)
            return false;
        (minus ? remove : add) |= bit;
        p = *end ? end + 1 : end;
    }
    out = (add ? add : static_cast<uint32_t>(kAllStages)) & ~remove;
    return true;
}

// The control word: evaluation mode in bits 0..1, enabled stage families from bit 8.
inline constexpr uint32_t pack(Eval eval, uint32_t stages) {
    return static_cast<uint32_t>(eval) | ((stages & kAllStages) << 8);
}
inline constexpr Eval evalOf(uint32_t word) {
    return static_cast<Eval>(word & 3u);
}
inline constexpr uint32_t stagesOf(uint32_t word) {
    return (word >> 8) & kAllStages;
}
// True when a run of `stage` may leave the microprogram under `word`.
inline constexpr bool takes(uint32_t word, Stage stage) {
    return evalOf(word) != Eval::Microprogram && (stagesOf(word) & stage) != 0u;
}

// DRAKAN_VERTEX_TOGGLE=<n>: the evaluation mode alternates every n frames between the configured one and the
// microprogram (n > 0), so matched pictures of a still scene can be compared inside one run.
inline constexpr uint32_t toggled(uint32_t word, uint64_t frame, uint32_t everyFrames) {
    if (everyFrames == 0u || ((frame / everyFrames) & 1u) == 0u)
        return word;
    return pack(Eval::Microprogram, stagesOf(word));
}

struct Counters {
    std::atomic<uint64_t> taken[kStageCount]{};       // runs that left the microprogram, per stage family
    std::atomic<uint64_t> takenVertices{0};           // their vertices
    std::atomic<uint64_t> gpuVertices{0};             // vertices drawn by a vertex shader
    std::atomic<uint64_t> cpuVertices{0};             // vertices of records evaluated on the CPU
    std::atomic<uint64_t> fallback[kFallbackCount]{}; // runs or records per reason
    std::atomic<uint64_t> switches{0};                // changes of the control word
};

struct State {
    std::atomic<uint32_t> word{pack(Eval::Microprogram, kAllStages)};     // what runs and records obey right now
    std::atomic<uint32_t> baseWord{pack(Eval::Microprogram, kAllStages)}; // the configured word (before the toggle)
    std::atomic<uint32_t> toggleFrames{0};                                // DRAKAN_VERTEX_TOGGLE
    Counters counters;
};
inline State& state() {
    static State s;
    return s;
}
inline uint32_t controlWord() {
    return state().word.load(std::memory_order_relaxed);
}
inline void setControlWord(uint32_t word) {
    State& s = state();
    if (s.word.exchange(word, std::memory_order_relaxed) != word)
        s.counters.switches.fetch_add(1, std::memory_order_relaxed);
}
// The configured word: the player's choice resolved against the renderer, then the diagnosis overrides. `evalOverride`
// and `stagesOverride` are the texts of DRAKAN_VERTEX_EVAL and DRAKAN_VERTEX_STAGES (null or unparsable: none).
// An override of the evaluation mode is taken as given: `gpu` on a backend without the path evaluates every record on
// the CPU (and counts it), which is still exact.
inline uint32_t configuredWord(Choice choice, const Capability& cap, const char* evalOverride,
                               const char* stagesOverride) {
    Eval eval = resolve(choice, cap);
    parseEval(evalOverride, eval);
    uint32_t stages = kAllStages;
    parseStages(stagesOverride, stages);
    return pack(eval, stages);
}
// Applies a configuration. Safe from any thread at any time.
inline void configure(uint32_t word, uint32_t toggleEveryFrames = 0) {
    State& s = state();
    s.baseWord.store(word, std::memory_order_relaxed);
    s.toggleFrames.store(toggleEveryFrames, std::memory_order_relaxed);
    setControlWord(toggleEveryFrames ? pack(Eval::Microprogram, stagesOf(word)) : word);
}
// Called by the renderer at the end of each frame: applies the toggle.
inline void frameBoundary(uint64_t frame) {
    State& s = state();
    const uint32_t every = s.toggleFrames.load(std::memory_order_relaxed);
    if (every)
        setControlWord(toggled(s.baseWord.load(std::memory_order_relaxed), frame, every));
}

inline void countFallback(Fallback why, uint64_t n = 1) {
    state().counters.fallback[static_cast<uint32_t>(why)].fetch_add(n, std::memory_order_relaxed);
}
inline void countTaken(Stage stage, uint64_t vertices) {
    Counters& c = state().counters;
    for (uint32_t i = 0; i < kStageCount; ++i)
        if (stage == (1u << i))
            c.taken[i].fetch_add(1, std::memory_order_relaxed);
    c.takenVertices.fetch_add(vertices, std::memory_order_relaxed);
}

} // namespace gsr::vpath
