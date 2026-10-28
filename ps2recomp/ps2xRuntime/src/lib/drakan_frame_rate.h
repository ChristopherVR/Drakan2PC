#pragma once
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "drakan_config.h"
#include "drakan_frame_policy.h"
#include "drakan_software_frame_clock.h"
#include "drakan_dlss.h"
#include "drakan_present_clock.h"
#include "runtime/ee_scheduler.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

// SCES-500.06 cadence experiment. Keep the guest vblank and elapsed clock
// unchanged; the graphics worker still performs its normal hardware checks.
namespace drakan_frame_rate {
inline PS2Runtime::RecompiledFunction sleepOriginal = nullptr, timerOriginal = nullptr;
inline bool highRate = false;
inline bool uncapped = false;
inline PS2Runtime::RecompiledFunction waitOriginal = nullptr, flipOriginal = nullptr;
inline uint64_t retryGeneration = 0;
inline uint64_t wakeRequests = 0, completedFlips = 0, retryRequests = 0;
inline uint32_t readWord(const uint8_t* ram, uint32_t address) {
    uint32_t value = 0;
    std::memcpy(&value, ram + address, 4);
    return value;
}
inline bool validWorker(uint8_t* ram, uint32_t object, int thread) {
    return object >= 0x100000u && object <= PS2_RAM_SIZE - 16 && readWord(ram, 0x4e8ee0u) == object &&
           readWord(ram, object + 12) == static_cast<uint32_t>(thread);
}
inline void wakePending(uint8_t* ram, PS2Runtime* rt, uint32_t object, int thread) {
    if (!validWorker(ram, object, thread) || readWord(ram, object + 4) != 0)
        return;
    auto* worker = rt->eeScheduler().thread(thread);
    // Never accumulate wake credits: a ready/running worker already has work.
    if (worker && worker->entry == 0x1d78f0u && worker->status == EeThreadStatus::Waiting &&
        worker->wait.reason == EeWaitReason::Sleep) {
        const uint32_t ready = 4;
        std::memcpy(ram + 0x4e8e98u, &ready, 4);
        rt->eeScheduler().wakeupThread(thread, true);
        ++wakeRequests;
    }
}
inline void waitHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc == 0x1d8660u) {
        const uint32_t object = GPR_U32(ctx, 4);
        if (object >= 0x100000u && object <= PS2_RAM_SIZE - 16)
            wakePending(ram, rt, object, static_cast<int>(readWord(ram, object + 12)));
    }
    waitOriginal(ram, ctx, rt);
}
inline void flipHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const bool worker = ctx->pc == 0x1e0048u && GPR_U32(ctx, 31) == 0x1d7a04u &&
                        validWorker(ram, GPR_U32(ctx, 16), rt->eeScheduler().currentThreadId());
    flipOriginal(ram, ctx, rt);
    // This leaf routine has completed every privileged display-register write.
    if (worker && ctx->pc == 0x1d7a04u) {
#if defined(PS2X_HAVE_GSR)
        drakan_dlss::commitDisplay(*rt);
#endif
        ++completedFlips;
        static const bool trace = [] {
            const char* v = std::getenv("DRAKAN_FRAME_AUDIT");
            return v && std::atoi(v) >= 2;
        }();
        if (trace)
            std::fprintf(stderr, "[ft] F %lld\n",
                         (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
        static auto begin = std::chrono::steady_clock::now();
#if !defined(PS2X_HAVE_GSR)
        std::unique_lock frameLatch(drakan_presentation::g_frameLatchMutex, std::defer_lock);
        if (uncapped) {
            frameLatch.lock();
            // Capture at the original flip, before subsequent guest draws can
            // queue ahead of presentation. Publish only once the image is ready.
            rt->gs().latchHostPresentationFrame();
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        drakan_presentation::g_frameClock.commit(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
#if !defined(PS2X_HAVE_GSR)
        if (frameLatch.owns_lock())
            frameLatch.unlock();
#endif
        const double elapsed = std::chrono::duration<double>(now - begin).count();
        if (elapsed >= 2) {
            std::fprintf(stderr, "[frame-uncapped] seconds=%.6f completedFlips=%llu wakes=%llu retries=%llu\n", elapsed,
                         (unsigned long long)completedFlips, (unsigned long long)wakeRequests,
                         (unsigned long long)retryRequests);
            begin = now;
            completedFlips = wakeRequests = retryRequests = 0;
        }
    }
}
inline void sleepHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if ((highRate || uncapped) && ctx->pc == 0x4bc080u && GPR_U32(ctx, 31) == 0x1d7a24u) {
        uint32_t object = 0, thread = 0;
        std::memcpy(&object, ram + 0x4e8ee0u, 4);
        if (object < 0x100000u || object > PS2_RAM_SIZE - 16 || object != GPR_U32(ctx, 16)) {
            sleepOriginal(ram, ctx, rt);
            return;
        }
        std::memcpy(&thread, ram + object + 12, 4);
        if (thread != static_cast<uint32_t>(rt->eeScheduler().currentThreadId())) {
            sleepOriginal(ram, ctx, rt);
            return;
        }
        uint32_t count = 0;
        std::memcpy(&count, ram + 0x4e8e98u, 4);
        // The worker increments before sleeping. Reaching two wake cycles
        // satisfies the original four-count gate on its next iteration.
        count = cadenceCount(count, highRate, ctx->pc, GPR_U32(ctx, 31));
        std::memcpy(ram + 0x4e8e98u, &count, 4);
        if (uncapped) {
            // Yield to the executor while DMA/VIF readiness is pending. This
            // retry has no wall-clock deadline and is independent of vblank.
            // Native readiness tests and completion flag writes stay intact.
            rt->eeScheduler().cancelWakeup(static_cast<int>(thread));
            const auto generation = ++retryGeneration;
            if (readWord(ram, object + 4) == 0) {
                ++retryRequests;
                rt->eeScheduler().postAfterGuestCycles(65536, [ram, rt, object, thread, generation] {
                    if (generation == retryGeneration)
                        wakePending(ram, rt, object, static_cast<int>(thread));
                });
            }
        }
    }
    sleepOriginal(ram, ctx, rt);
}
inline PS2Runtime::RecompiledFunction velocityOriginal = nullptr;
inline float readFloat(const uint8_t* ram, uint32_t address) {
    float value = 0;
    std::memcpy(&value, ram + address, 4);
    return value;
}
// 325d30 turns a tick's displacement into units per second as
// displacement * min(30, 1 / (engine+2c tick seconds * time scale 512db4)).
// At a faster world cadence the 30 cap under-reports speed by 30 / rate, so
// 2f90c8's running-jump test (player+8 > 1000) failed and jumps went straight
// up. Rescale to the real rate; paused (fixed 30 by 142688) stays native.
inline void velocityHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t out = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
    const uint32_t engine = readWord(ram, 0x5565d4u);
    velocityOriginal(ram, ctx, rt);
    if (ctx->pc != ra || out < 0x100000u || out > PS2_RAM_SIZE - 12 || engine < 0x100000u ||
        engine > PS2_RAM_SIZE - 0x4c8)
        return;
    if (readWord(ram, engine + 0x4c4) & 2u)
        return;
    const float tick = readFloat(ram, engine + 0x2c) * readFloat(ram, 0x512db4u);
    if (!(tick > 0.001f)) // also rejects NaN; caps the rate at 1000 Hz
        return;
    const float rate = 1.0f / tick;
    if (rate <= 30.0f)
        return;
    const float scale = rate / 30.0f;
    for (uint32_t i = 0; i < 3; ++i) {
        const float v = readFloat(ram, out + 4 * i) * scale;
        std::memcpy(ram + out + 4 * i, &v, 4);
    }
}
inline PS2Runtime::RecompiledFunction layerSpeedOriginal = nullptr;
// 450cf0(speed, player, layer) sets an animation layer's speed. The in-air update 2fa328 calls it with (0, Rynn, 0)
// at 2fb8b0 when the takeoff animation is within 0.03 s of its end, just before it starts the in-air loop on layer 0
// (directly, or through 3253a0 while an attack plays on layer player+0xaec, ab0 bit 2). A 30 Hz tick (0.033 s) steps
// over that window, so the takeoff animation has already ended (speed 0) and the call changes nothing. At 60 Hz and
// above it lands inside the window, writes speed 0 to every bone record, the attack layer's upper body included, and
// 3253a0 re-applies that speed to the attack layer: Rynn froze in the jump pose after attacking and jumping, until
// the attack ended much later. Skip the call while an attack layer is active, as at 30 Hz.
inline void layerSpeedHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t player = GPR_U32(ctx, 4) & 0x1fffffffu, ra = GPR_U32(ctx, 31);
    if (ra == 0x2fb8b8u && GPR_U32(ctx, 5) == 0 && ctx->f[12] == 0.0f && player >= 0x100000u &&
        player <= PS2_RAM_SIZE - 0xab4 && (readWord(ram, player + 0xab0) & 2u)) {
        ctx->pc = ra;
        return;
    }
    layerSpeedOriginal(ram, ctx, rt);
}
inline PS2Runtime::RecompiledFunction tickTimeOriginal = nullptr;
inline double tickTimeCarry = 0.0;
// 1c3028(timer, out), called once per engine tick from 1433c0 with out = engine+24: out = clock delta (ms) *
// timer+10 (s per ms) * timer+28 (time scale). See snapTickToRefresh in drakan_frame_policy.h.
inline void tickTimeHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t timer = GPR_U32(ctx, 4), out = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
    tickTimeOriginal(ram, ctx, rt);
    if (ctx->pc != ra || ra != 0x1433e8u || out < 0x100000u || out > PS2_RAM_SIZE - 4 || timer < 0x100000u ||
        timer > PS2_RAM_SIZE - 0x2c)
        return;
    const double refresh = drakan_present_clock::lockedRefreshSeconds();
    const float scale = readFloat(ram, timer + 0x28);
    if (!(refresh > 0.0) || !(scale > 0.0f)) {
        tickTimeCarry = 0.0;
        return;
    }
    const float measured = readFloat(ram, out);
    const float given = static_cast<float>(snapTickToRefresh(measured / scale, refresh, tickTimeCarry) * scale);
    std::memcpy(ram + out, &given, 4);
    static const bool trace = [] {
        const char* v = std::getenv("DRAKAN_FRAME_AUDIT");
        return v && std::atoi(v) >= 2;
    }();
    if (trace)
        std::fprintf(stderr, "[ft] S %lld %.4f %.4f\n",
                     (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count(),
                     measured * 1000.0f, given * 1000.0f);
}
inline PS2Runtime::RecompiledFunction windOriginal = nullptr;
// 141d80 (engine wind gusts) eases the direction +40c..414 towards the gust
// target +418..420 by a fixed 25% per update: x' = 0.75 x + 0.25 t. Pre-shift x
// so the native step yields t + (x - t) * 0.75^(dt * 30), the 30 Hz decay.
inline void windHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t engine = GPR_U32(ctx, 4);
    if (engine >= 0x100000u && engine <= PS2_RAM_SIZE - 0x430) {
        const float dt = readFloat(ram, engine + 0x24);
        if (dt > 0.0f && dt < 1.0f / 30.0f) {
            const float keep = std::pow(0.75f, dt * 30.0f) / 0.75f;
            for (uint32_t i = 0; i < 3; ++i) {
                const float x = readFloat(ram, engine + 0x40c + 4 * i), t = readFloat(ram, engine + 0x418 + 4 * i);
                const float v = t + (x - t) * keep;
                std::memcpy(ram + engine + 0x40c + 4 * i, &v, 4);
            }
        }
    }
    windOriginal(ram, ctx, rt);
}
inline void timerHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    // This engine boundary follows timestep completion, including calculations
    // that yield and resume. Do not infer completion from a wrapper return.
    if (ctx->pc != 0x143448u || GPR_U32(ctx, 31) != 0x135d3cu) {
        timerOriginal(ram, ctx, rt);
        return;
    }
    static auto start = std::chrono::steady_clock::now();
    static double total = 0, localTotal = 0;
    static unsigned frames = 0, capped = 0;
    float dt = 0;
    std::memcpy(&dt, ram + 0x4de604u, 4);
    drakan_presentation::g_frameMetrics.gameDelta(dt);
    static const int audit = [] {
        const char* value = std::getenv("DRAKAN_FRAME_AUDIT");
        return value ? std::atoi(value) : 0;
    }();
    const bool trace = audit >= 2;
    if (audit == 0) {
        timerOriginal(ram, ctx, rt);
        return;
    }
    if (trace)
        std::fprintf(stderr, "[ft] U %lld %.4f\n",
                     (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count(),
                     dt * 1000.0f);
    total += dt;
    localTotal += dt > 0.030f ? 0.030f : dt;
    capped += dt > 0.030f;
    ++frames;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - start).count();
    if (audit != 0 && seconds >= 2) {
        std::fprintf(stderr,
                     "[frame-time] seconds=%.6f updates=%u dtSum=%.6f dtMean=%.6f localCapProxy=%.6f over30ms=%u\n",
                     seconds, frames, total, total / frames, localTotal, capped);
        total = 0;
        localTotal = 0;
        frames = 0;
        capped = 0;
        start = now;
    }
    timerOriginal(ram, ctx, rt);
}
inline PS2Runtime::RecompiledFunction lightingReleaseOriginal = nullptr;
inline uint64_t keptLighting = 0;
// vtable +11c (17e478): see keepUnrebuildableLighting in drakan_frame_policy.h.
inline void lightingReleaseHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t object = GPR_U32(ctx, 4) & 0x1fffffffu, ra = GPR_U32(ctx, 31);
    if (ctx->pc == 0x17e478u && object >= 0x100000u && object <= PS2_RAM_SIZE - 0x120) {
        const uint32_t link = readWord(ram, object + 0xa0) & 0x1fffffffu;
        const ObjectLighting lighting{readWord(ram, object), readWord(ram, object + 0x7c),
                                      link >= 0x100000u && link <= PS2_RAM_SIZE - 8 ? readWord(ram, link + 4) : 0,
                                      readWord(ram, object + 0x78)};
        if (keepUnrebuildableLighting(ra, lighting)) {
            if (++keptLighting <= 4)
                std::fprintf(stderr, "[frame-rate] kept the lighting of far object %x (nothing could rebuild it)\n",
                             object);
            ctx->pc = ra;
            return;
        }
    }
    lightingReleaseOriginal(ram, ctx, rt);
}
inline void install(PS2Runtime& rt) {
    const char* metricsValue = std::getenv("DRAKAN_FRAME_METRICS");
    const bool metrics = metricsValue && std::atoi(metricsValue) != 0;
    drakan_presentation::g_frameMetrics.enable(metrics);
    drakan_presentation::g_frameClock.reset(false);
    auto rate = drakan_cfg::getString("video", "world_rate", "DRAKAN_WORLD_RATE", nullptr);
    std::transform(rate.begin(), rate.end(), rate.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    highRate = rate == "60";
    uncapped = rate == "uncapped";
    if (uncapped && (!rt.hasFunction(0x1d78f0u) || !rt.hasFunction(0x4bc080u) || !rt.hasFunction(0x1d8660u) ||
                     !rt.hasFunction(0x1e0048u))) {
        uncapped = false;
        std::fprintf(stderr, "[frame-rate] required native boundaries unavailable; uncapped disabled\n");
    }
    retryGeneration = 0;
    if (rate != "30" && rate != "60" && !uncapped)
        std::fprintf(stderr, "[frame-rate] unsupported world_rate; using original 30 FPS cadence\n");
    if ((highRate || uncapped) && rt.hasFunction(0x1d78f0u) && rt.hasFunction(0x4bc080u)) {
        sleepOriginal = rt.lookupFunction(0x4bc080u);
        rt.replaceFunction(0x4bc080u, sleepHook);
        std::fprintf(stderr, "[frame-rate] %s world cadence; guest clock unchanged\n",
                     uncapped ? "uncapped" : "60 FPS");
    }
    if ((highRate || uncapped) && rt.hasFunction(0x325d30u)) {
        velocityOriginal = rt.lookupFunction(0x325d30u);
        rt.replaceFunction(0x325d30u, velocityHook);
    }
    if ((highRate || uncapped) && rt.hasFunction(0x450cf0u)) {
        layerSpeedOriginal = rt.lookupFunction(0x450cf0u);
        rt.replaceFunction(0x450cf0u, layerSpeedHook);
    }
    // Smooth tick time unless DRAKAN_TICK_SNAP=0 (any cadence: snapping only acts while presents are vsync-locked).
    if (drakan_cfg::getBool("video", "tick_snap", "DRAKAN_TICK_SNAP", true) && rt.hasFunction(0x1c3028u)) {
        tickTimeOriginal = rt.lookupFunction(0x1c3028u);
        rt.replaceFunction(0x1c3028u, tickTimeHook);
    }
    if ((highRate || uncapped) && rt.hasFunction(0x141d80u)) {
        windOriginal = rt.lookupFunction(0x141d80u);
        rt.replaceFunction(0x141d80u, windHook);
    }
    if ((highRate || uncapped) && rt.hasFunction(0x17e478u)) {
        lightingReleaseOriginal = rt.lookupFunction(0x17e478u);
        rt.replaceFunction(0x17e478u, lightingReleaseHook);
    }
    if (uncapped && rt.hasFunction(0x1d8660u) && rt.hasFunction(0x1e0048u)) {
        drakan_presentation::g_frameClock.reset(true);
        waitOriginal = rt.lookupFunction(0x1d8660u);
        rt.replaceFunction(0x1d8660u, waitHook);
        flipOriginal = rt.lookupFunction(0x1e0048u);
        rt.replaceFunction(0x1e0048u, flipHook);
    }
    const char* audit = std::getenv("DRAKAN_FRAME_AUDIT");
    if (((audit && std::atoi(audit) != 0) || metrics) && rt.hasFunction(0x143448u)) {
        timerOriginal = rt.lookupFunction(0x143448u);
        rt.replaceFunction(0x143448u, timerHook);
    }
}
} // namespace drakan_frame_rate
