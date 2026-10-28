// A pthread mixes into a shared ring. The Web Audio worklet consumes it without
// calling C++ or waiting on a mutex on the browser's UI/audio threads.
#include "spu2/AudioOutput.h"
#include <emscripten.h>
#include <array>
#include <chrono>
#include <thread>

namespace spu2 {
namespace {
constexpr uint32_t kRingFrames = 8192;
struct Ring {
    std::array<std::atomic<uint32_t>, 4> control{}; // read, write, connected, dropped blocks
    std::array<float, kRingFrames * 2> samples{};
};
static_assert(sizeof(std::atomic<uint32_t>) == 4 && std::atomic<uint32_t>::is_always_lock_free);
Ring g_ring;
} // namespace
std::vector<OutputDevice> AudioOutput::enumerateDevices() {
    return {{"default", "Browser audio", true}};
}
void AudioOutput::threadMain() {
    using Clock = std::chrono::steady_clock;
    for (auto& value : g_ring.control)
        value.store(0);
    if (opt_.useDevice) {
        MAIN_THREAD_EM_ASM(
            {
                if (globalThis.drakanStartAudio)
                    globalThis.drakanStartAudio(HEAPU8.buffer, $0, $1, $2);
            },
            g_ring.control.data(), g_ring.samples.data(), kRingFrames);
    }
    std::array<float, 480 * 2> samples{};
    rate_ = 48000;
    while (nameLock_.test_and_set(std::memory_order_acquire)) {
    }
    name_ = "Browser audio";
    nameLock_.clear(std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(devM_);
        curId_ = "default";
    }
    const auto started = Clock::now();
    auto next = started;
    while (!stop_) {
        renderBlock(samples.data(), 480);
        const uint32_t read = g_ring.control[0].load(std::memory_order_acquire);
        const uint32_t write = g_ring.control[1].load(std::memory_order_relaxed);
        if (kRingFrames - (write - read) >= 480) {
            for (uint32_t i = 0; i < 480; ++i) {
                const auto slot = (write + i) % kRingFrames;
                g_ring.samples[2 * slot] = samples[2 * i];
                g_ring.samples[2 * slot + 1] = samples[2 * i + 1];
            }
            g_ring.control[1].store(write + 480, std::memory_order_release);
        } else {
            ++g_ring.control[3]; // Suspended/denied audio must not freeze guest voices.
        }
        running_ = g_ring.control[2].load() != 0;
        if (!ready_ && (!opt_.useDevice || running_ || Clock::now() - started >= std::chrono::seconds(3))) {
            if (opt_.useDevice && !running_)
                err_ = "Browser audio is suspended or unavailable";
            ready_ = true;
        }
        next += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next);
        if (Clock::now() > next + std::chrono::milliseconds(100))
            next = Clock::now();
    }
    running_ = false;
    MAIN_THREAD_EM_ASM({
        if (globalThis.drakanAudioNode)
            globalThis.drakanAudioNode.disconnect();
    });
}
} // namespace spu2
