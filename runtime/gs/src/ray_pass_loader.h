// Builds a ray tracing pass (the DXR device and pipelines, or the WebGPU ray programs) on a background thread and
// hands it to the render thread without ever waiting for it. With a cold driver cache the DXR pipelines take about
// 18 s to compile: world frames meanwhile draw the plain raster (the ray effects off, the settings say "Ray tracing is
// starting") and the effects switch on with the first frame after the build finished. Pure (std::thread only), shared
// by the D3D11 and WebGPU backends, unit tested with a fake pass (tests/test_ray_pass_loader.cpp).
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace gsr::raystatus {

inline constexpr const char* kStarting = "Ray tracing is starting";

// GSR_RAY_PREWARM_DELAY_MS: a test aid that holds the build this long before it starts (a slow, cold-cache compile).
inline std::chrono::milliseconds prewarmDelay() {
    const char* v = std::getenv("GSR_RAY_PREWARM_DELAY_MS");
    const long ms = v ? std::atol(v) : 0;
    return std::chrono::milliseconds(ms > 0 ? ms : 0);
}

template <class T>
class PassLoader {
public:
    using Build = std::function<std::unique_ptr<T>()>;
    PassLoader() = default;
    PassLoader(const PassLoader&) = delete;
    PassLoader& operator=(const PassLoader&) = delete;
    ~PassLoader() { join(); }

    // Starts the build once per loader (later calls do nothing). `delay` holds it first (cut short by join()).
    void start(Build build, std::chrono::milliseconds delay = {}) {
        std::lock_guard lock(mutex_);
        if (started_)
            return;
        started_ = true;
        pending_ = true;
        thread_ = std::thread([this, build = std::move(build), delay] {
            if (delay.count() > 0) {
                std::unique_lock lock(mutex_);
                cv_.wait_for(lock, delay, [&] { return stopping_; });
            }
            std::unique_ptr<T> made;
            bool stop;
            {
                std::lock_guard lock(mutex_);
                stop = stopping_;
            }
            if (!stop)
                made = build();
            std::lock_guard lock(mutex_);
            result_ = std::move(made);
            pending_ = false;
        });
    }
    bool started() const {
        std::lock_guard lock(mutex_);
        return started_;
    }
    // Started and not finished yet.
    bool pending() const {
        std::lock_guard lock(mutex_);
        return pending_;
    }
    // Never blocks: the finished build, once. Null before start(), while it is still building, after it was taken,
    // and when the build gave nothing.
    std::unique_ptr<T> take() {
        std::lock_guard lock(mutex_);
        if (pending_)
            return {};
        return std::move(result_);
    }
    // Waits for the thread (shutdown: the device the build uses must outlive it).
    void join() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool started_ = false, pending_ = false, stopping_ = false;
    std::unique_ptr<T> result_;
};

// The render thread's step in each frame that wants a ray effect: starts the build when nothing started it yet and
// adopts a finished build into `pass` (`adopt` turns the built pieces into the pass, on the render thread). Never
// blocks. Null: draw this frame without the ray effects.
template <class Pass, class Built, class Start, class Adopt>
Pass* acquirePass(std::unique_ptr<Pass>& pass, PassLoader<Built>& loader, Start&& start, Adopt&& adopt) {
    if (pass)
        return pass.get();
    if (!loader.started())
        start();
    if (auto built = loader.take())
        pass = adopt(std::move(built));
    return pass.get();
}

} // namespace gsr::raystatus
