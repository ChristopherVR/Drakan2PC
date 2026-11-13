#include "drakan_vertex_path.h"

#include "drakan_config.h"
#include "gsr/gsr.h"
#include "gsr/vertex_path.h"
#include "vu/ps2_vu1_check.h"
#include "vu/ps2_vu1_gpu_live.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace drakan_vertex_path {
namespace {
namespace vpath = gsr::vpath;

std::mutex g_mutex;
gsr::Renderer* g_renderer = nullptr;
vpath::Choice g_choice = vpath::Choice::Cpu;
std::atomic<bool> g_gpuAvailable{false};
uint32_t g_appliedStages = 0; // Capability::shaderStages the word was last made from

void applyLocked() {
    const vpath::Capability cap = g_renderer ? g_renderer->vertexPathCapability() : vpath::Capability{};
    g_appliedStages = cap.shaderStages;
    g_gpuAvailable.store(cap.backendHasPath && cap.adapterExact, std::memory_order_relaxed);
    const char* toggleText = std::getenv("DRAKAN_VERTEX_TOGGLE");
    const uint32_t toggle = toggleText ? static_cast<uint32_t>(std::strtoul(toggleText, nullptr, 10)) : 0u;
    const uint32_t word =
        vpath::configuredWord(g_choice, cap, std::getenv("DRAKAN_VERTEX_EVAL"), std::getenv("DRAKAN_VERTEX_STAGES"));
    const uint32_t before = vpath::state().baseWord.load(std::memory_order_relaxed);
    // DRAKAN_VERTEX_TOGGLE_STAGES: the toggled frames keep the mode and run another stage set
    const char* toggleStages = std::getenv("DRAKAN_VERTEX_TOGGLE_STAGES");
    const uint32_t other = vpath::toggleWordOf(word, toggleStages);
    vpath::configure(word, toggle, other);
    if (vu1check::shadowRequested()) // DRAKAN_VERTEX_SHADOW: the VU1 worker compares records beside the microprogram
        vpath::requestVuAttention();
    if (word != before || toggle)
        std::fprintf(stderr,
                     "[vertex-path] vertex_processing = %s: %s (renderer path %d, adapter exact %d, proven %d), "
                     "stages 0x%02x%s%s (stages 0x%02x)\n",
                     vpath::choiceName(g_choice), vpath::evalName(vpath::evalOf(word)), int(cap.backendHasPath),
                     int(cap.adapterExact), int(cap.proven), vpath::stagesOf(word), toggle ? ", toggling with " : "",
                     toggle ? vpath::evalName(vpath::evalOf(other)) : "", toggle ? vpath::stagesOf(other) : 0u);
}
} // namespace

void attach(gsr::Renderer* renderer) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_renderer = renderer;
    const std::string text = drakan_cfg::getString("video", "vertex_processing", "DRAKAN_VERTEX_PROCESSING", "cpu");
    if (!vpath::parseChoice(text.c_str(), g_choice)) {
        std::fprintf(stderr, "[vertex-path] unknown vertex_processing \"%s\", using cpu\n", text.c_str());
        g_choice = vpath::Choice::Cpu;
    }
    applyLocked();
}

void configure(const std::string& choice) {
    std::lock_guard<std::mutex> lock(g_mutex);
    vpath::Choice parsed = vpath::Choice::Cpu;
    vpath::parseChoice(choice.c_str(), parsed);
    g_choice = parsed;
    applyLocked();
}

const char* activePath() {
    const vpath::Eval eval = vpath::evalOf(vpath::controlWord());
    return eval == vpath::Eval::Gpu ? "gpu" : eval == vpath::Eval::Records ? "records" : "cpu";
}

bool gpuAvailable() {
    return g_gpuAvailable.load(std::memory_order_relaxed);
}

std::string countersLine() {
    const vpath::Counters& c = vpath::state().counters;
    std::string line;
    char text[96];
    uint64_t any = 0;
    for (uint32_t i = 0; i < vpath::kStageCount; ++i) {
        const uint64_t n = c.taken[i].load(std::memory_order_relaxed);
        any += n;
        if (n) {
            std::snprintf(text, sizeof(text), " %s %llu", vpath::kStageNames[i], static_cast<unsigned long long>(n));
            line += text;
        }
    }
    std::string fallbacks;
    for (uint32_t i = 0; i < vpath::kFallbackCount; ++i) {
        const uint64_t n = c.fallback[i].load(std::memory_order_relaxed);
        any += n;
        if (n) {
            std::snprintf(text, sizeof(text), " %s %llu", vpath::kFallbackNames[i], static_cast<unsigned long long>(n));
            fallbacks += text;
        }
    }
    if (!any)
        return {};
    std::string batchFallbacks;
    for (uint32_t i = 0; i < vpath::kFallbackCount; ++i)
        if (const uint64_t n = c.batchFallback[i].load(std::memory_order_relaxed)) {
            std::snprintf(text, sizeof(text), " %s %llu", vpath::kFallbackNames[i], static_cast<unsigned long long>(n));
            batchFallbacks += text;
        }
    std::snprintf(text, sizeof(text), "; vertices: shader %llu (batch path %llu), cpu %llu",
                  static_cast<unsigned long long>(c.gpuVertices.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(c.gpuBatchVertices.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(c.cpuVertices.load(std::memory_order_relaxed)));
    std::string out = std::string("path ") + activePath() + "; runs handed over:" + (line.empty() ? " none" : line) +
                      text + "; fallbacks:" + (fallbacks.empty() ? " none" : fallbacks) +
                      " (of them batch-path records:" + (batchFallbacks.empty() ? " none" : batchFallbacks) + ")";
    if (const uint64_t stale = c.staleDraws.load(std::memory_order_relaxed)) {
        std::snprintf(text, sizeof(text), "; RUNS THAT DREW FROM UNKNOWN VU MEMORY: %llu",
                      static_cast<unsigned long long>(stale));
        out += text;
    }
    return out;
}

// Under `gpu` the word hands over only the stage families whose shader the renderer has (or has not started to
// compile): while a shader compiles in the background its runs stay in the microprogram instead of being evaluated
// on the CPU, and the word follows when it is ready (a quarter of a second later at most).
void pollCapability() {
    static const bool overridden = std::getenv("DRAKAN_VERTEX_EVAL") != nullptr;
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last{};
    const Clock::time_point now = Clock::now();
    if (now - last < std::chrono::milliseconds(250))
        return;
    last = now;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_renderer || (g_choice == vpath::Choice::Cpu && !overridden))
        return; // the player's `cpu`: nothing to follow
    if (g_renderer->vertexPathCapability().shaderStages != g_appliedStages)
        applyLocked();
}

void frameTick() {
    pollCapability();
    static const bool log = std::getenv("DRAKAN_VERTEX_LOG") != nullptr;
    if (!log)
        return;
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last = Clock::now();
    const Clock::time_point now = Clock::now();
    if (now - last < std::chrono::seconds(5))
        return;
    last = now;
    const std::string line = countersLine();
    if (!line.empty())
        std::fprintf(stderr, "[vertex-path] %s\n", line.c_str());
    char tracker[1280]; // read without a lock from the VU1 worker's counters: a diagnosis line, not a measurement
    vu1gpu::live::countersText(tracker, sizeof(tracker));
    if (tracker[0])
        std::fprintf(stderr, "[vertex-path] %s\n", tracker);
}

} // namespace drakan_vertex_path
