#pragma once
// Reconstruction of world-space surface triangles from GS draws for the ray scene, optionally on its own thread.
//
// The GS worker classifies draws and submits their vertices. Reconstruction and resident material updates have
// one owner and follow submission order. At a reader's drain, the GS worker can take over the unstarted tail
// after the active batch finishes. Both execution paths produce the same lists and material projections.
#include "cpu_profile.h"
#include "gs_internal.h"
#include "resident_scene.h"
#include "surface_geometry.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && !defined(__EMSCRIPTEN__)
#include <xmmintrin.h>
#endif

namespace gsr::surface {
// Whether a draw may be reconstructed against `camera`. A world camera is accepted under any GUI screen: the host's
// publication gate (guiAllowsWorld) already withholds World from front-end, loading and movie screens, and the game
// keeps drawing the world behind its pause menu (0x17). Without a world camera (the window after EndWorld) only a
// gameplay camera is accepted.
inline bool surfaceCameraAccepted(const SceneGuideCamera& camera, bool pendingTemporalEnd) {
    return (camera.flags & SceneGuideCamera::World) || (pendingTemporalEnd && camera.guiScreen < 0);
}

// One target's reconstructed triangles of the current engine world (epoch).
struct SurfaceList {
    uint64_t epoch = 0;
    std::vector<Triangle> triangles;
};

// Per-draw values of the eligibility test, computed on the GS worker from the draw state.
struct DrawSurfaceParams {
    bool waterState = false, nativeWaterPass = false, opaqueEligible = false, materialEligible = false;
    bool transparent = false, textureAlpha = false, fst = false;
    uint32_t alphaTest = 1, textureFunction = 0;
    float alphaReference = 0;
    uint32_t texture = ~0u;
    float texWidth = 1, texHeight = 1; // FST texel -> normalized divisors
};

inline constexpr size_t kMaxSurfaceTriangles = 250000;

// Appends the eligible triangles of one draw to out (the former inline loop of Backend::Impl::draw).
inline void reconstructDraw(const DrawSurfaceParams& d, const DrawVertex* v, uint32_t count,
                            CachedRasterToWorld& camera, ResidentScene& scene, std::vector<Triangle>& out) {
    for (uint32_t i = 0; i < count && out.size() < kMaxSurfaceTriangles; i += 3) {
        Triangle tri;
        bool valid = true;
        for (unsigned j = 0; j < 3 && valid; ++j)
            valid = v[i + j].z > 0 && camera.point(v[i + j].x, v[i + j].y, v[i + j].z, tri.p[j]);
        if (!valid || length(sub(tri.p[1], tri.p[0])) > 2000000)
            continue;
        // A resident water match found here is also the water-layer match used below.
        const int waterMatch = d.waterState && !d.nativeWaterPass ? scene.match(tri, true) : -1;
        tri.water = d.waterState && (d.nativeWaterPass || waterMatch >= 0);
        const int resident = waterMatch >= 0 ? waterMatch : scene.match(tri, tri.water);
        const bool emit = tri.water || (resident < 0 && (d.opaqueEligible || d.materialEligible));
        const bool observe = resident >= 0 && (d.opaqueEligible || tri.water) && d.texture != ~0u;
        if (!emit && !observe)
            continue;
        tri.texture = d.texture;
        for (unsigned j = 0; j < 3; ++j) {
            const float q = d.fst ? 1.f : v[i + j].q;
            if (std::abs(q) > 1e-12) {
                tri.uv[j][0] = v[i + j].s / q;
                tri.uv[j][1] = v[i + j].t / q;
                if (d.fst) {
                    tri.uv[j][0] /= d.texWidth;
                    tri.uv[j][1] /= d.texHeight;
                }
            }
        }
        if (observe)
            scene.observe(tri, resident);
        if (!emit)
            continue;
        // A matched resident surface consumes its texture projection only. Build ray shading
        // attributes only for geometry that will actually be appended to the frame.
        tri.vertexLit = true;
        tri.shadowGeometry = false;
        tri.alphaTest = d.alphaTest;
        tri.alphaReference = d.alphaReference;
        tri.textureAlpha = d.textureAlpha;
        tri.textureFunction = d.textureFunction;
        tri.transparent = d.transparent;
        for (unsigned j = 0; j < 3; ++j) {
            tri.color[0] += v[i + j].r / (128.f * 3);
            tri.color[1] += v[i + j].g / (128.f * 3);
            tri.color[2] += v[i + j].b / (128.f * 3);
            tri.opacity[j] = std::clamp(v[i + j].a / 128.f, 0.f, 1.f);
            tri.lighting[j][0] = v[i + j].r / 128.f;
            tri.lighting[j][1] = v[i + j].g / 128.f;
            tri.lighting[j][2] = v[i + j].b / 128.f;
        }
        out.push_back(tri);
    }
}

class SurfaceBuilder {
public:
    struct Job {
        SurfaceList* list = nullptr;
        uint64_t epoch = 0;
        bool configure = false; // camera/xyoffset changed since the previous job
        SceneGuideCamera camera;
        uint64_t xyoffset = 0;
        unsigned mathMode = 0;
        DrawSurfaceParams params;
        std::vector<DrawVertex> vertices;
        std::function<void()> task; // instead of a draw: run this (in order with the draw jobs)
    };

    SurfaceBuilder(ResidentScene& scene, bool async, bool assist = true)
        : scene_(scene), async_(async), assist_(assist) {
        if (async_)
            thread_ = std::thread([this] { run(); });
    }
    ~SurfaceBuilder() {
        if (thread_.joinable()) {
            {
                std::lock_guard lock(mutex_);
                stop_ = true;
            }
            wake_.notify_one();
            thread_.join();
        }
    }
    SurfaceBuilder(const SurfaceBuilder&) = delete;
    SurfaceBuilder& operator=(const SurfaceBuilder&) = delete;

    bool async() const { return async_; }
    // Mirrors the camera cache of the synchronous path: configure only when the camera serial or xyoffset changed.
    bool needsConfigure(uint64_t serial, uint64_t xyoffset) {
        if (serial == cameraSerial_ && xyoffset == cameraXY_)
            return false;
        cameraSerial_ = serial;
        cameraXY_ = xyoffset;
        return true;
    }
    // The camera the next job converts with (the latest staged one wins, as the latest configure did).
    void stageCamera(const SceneGuideCamera& camera, uint64_t xyoffset) {
        staged_ = true;
        stagedCamera_ = camera;
        stagedXY_ = xyoffset;
    }

    // A job for the list's world epoch with an empty vertex vector whose capacity is reused.
    Job makeJob(SurfaceList* list, uint64_t epoch) {
        Job job;
        job.list = list;
        job.epoch = epoch;
        if (staged_) {
            job.configure = true;
            job.camera = stagedCamera_;
            job.xyoffset = stagedXY_;
            staged_ = false;
        }
        if (async_) {
            std::lock_guard lock(mutex_);
            if (!spare_.empty()) {
                job.vertices = std::move(spare_.back());
                spare_.pop_back();
            }
        }
        job.vertices.clear();
        job.mathMode = currentMathMode();
        return job;
    }
    // Synchronous mode: processes in place without copying vertices (vertices may then stay empty).
    void submit(Job&& job, const DrawVertex* vertices, uint32_t count) {
        if (!async_) {
            process(job, vertices, count);
            return;
        }
        job.vertices.assign(vertices, vertices + count);
        enqueue(std::move(job));
    }
    // Submission and drains are serialized by one producer. Normally keep model work overlapped
    // with rendering on the worker; a real draw backlog at the preceding join gives it priority.
    bool preferIndependentPreparation() const { return deferNextPreparation_ || bool(independentTask_); }
    void stageIndependentTask(std::function<void()> task) {
        deferNextPreparation_ = false;
        independentMathMode_ = currentMathMode();
        independentTask_ = std::move(task); // a newer immutable snapshot supersedes unused work
    }
    void submitTask(std::function<void()> task) {
        if (!async_) {
            task();
            return;
        }
        Job job;
        job.task = std::move(task);
        job.mathMode = currentMathMode();
        enqueue(std::move(job));
    }
    bool hasIndependentTask() const { return bool(independentTask_); }
    void finishIndependentTask() {
        if (!independentTask_)
            return;
        auto task = std::exchange(independentTask_, {});
        RestoreMathMode restore;
        setMathMode(independentMathMode_);
        task();
    }
    // Finish independent producer work before waiting for the ordered geometry jobs.
    void drain(CpuProfile* profile = nullptr) {
        finishIndependentTask();
        if (!async_)
            return;
        std::unique_lock lock(mutex_);
        if (completed_ == submitted_)
            return;
        ++drainWaits_;
        if (!assist_ || queue_.empty() || (workerBusy_ && queue_.size() < kBatchSize)) {
            CpuProfile::Scope wait(profile, CpuProfile::SurfaceWait);
            done_.wait(lock, [this] { return completed_ == submitted_; });
            return;
        }

        // The producer has reached a reader and can run the remaining FIFO itself. Wait only
        // for the already claimed batch. Exactly one thread owns the camera, lists and resident
        // material updates at a time; the worker cannot start another batch during this handoff.
        producerJoining_ = true;
        if (workerBusy_) {
            CpuProfile::Scope wait(profile, CpuProfile::SurfaceWait);
            done_.wait(lock, [this] { return !workerBusy_; });
        }
        std::deque<Job> tail;
        tail.swap(queue_);
        if (tail.size() >= kBatchSize)
            deferNextPreparation_ = true;
        producerJoining_ = false; // the sole producer owns the detached tail and submits nothing until drain returns
        lock.unlock();
        {
            CpuProfile::Scope assist(profile, CpuProfile::SurfaceAssist);
            RestoreMathMode restore;
            for (auto& job : tail)
                execute(job);
        }
        lock.lock();
        for (auto& job : tail)
            recycle(job);
        completed_ += tail.size();
        lock.unlock(); // discard any surplus vertex storage outside the queue lock
    }
    uint64_t drainWaits() const { return drainWaits_; }

private:
    static unsigned currentMathMode() {
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && !defined(__EMSCRIPTEN__)
        return _mm_getcsr();
#else
        return static_cast<unsigned>(std::fegetround());
#endif
    }
    static void setMathMode(unsigned mode) {
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && !defined(__EMSCRIPTEN__)
        if (_mm_getcsr() != mode)
            _mm_setcsr(mode);
#else
        if (mode != static_cast<unsigned>(-1) && static_cast<unsigned>(std::fegetround()) != mode)
            std::fesetround(static_cast<int>(mode));
#endif
    }
    struct RestoreMathMode {
        unsigned saved = currentMathMode();
        ~RestoreMathMode() { setMathMode(saved); }
    };
    void process(const Job& job, const DrawVertex* vertices, uint32_t count) {
        auto& list = *job.list;
        if (list.epoch != job.epoch) {
            list.triangles.clear();
            list.epoch = job.epoch;
        }
        if (job.configure)
            camera_.configure(job.camera, job.xyoffset);
        reconstructDraw(job.params, vertices, count, camera_, scene_, list.triangles);
    }
    void execute(Job& job) {
        setMathMode(job.mathMode);
        if (job.task) {
            job.task();
            job.task = {}; // release captured snapshots outside the queue lock
        } else
            process(job, job.vertices.data(), uint32_t(job.vertices.size()));
    }
    void recycle(Job& job) {
        if (spare_.size() < 64 && job.vertices.capacity())
            spare_.push_back(std::move(job.vertices));
    }
    void enqueue(Job&& job) {
        bool wake;
        {
            std::lock_guard lock(mutex_);
            wake = queue_.empty();
            queue_.push_back(std::move(job));
            ++submitted_;
        }
        if (wake)
            wake_.notify_one();
    }
    void run() {
        // Under load there can be hundreds of queued draws. Take a small FIFO batch per lock
        // acquisition, returning vertex storage regularly so the producer can reuse it.
        std::array<Job, kBatchSize> batch;
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || (!producerJoining_ && !queue_.empty()); });
            if (queue_.empty())
                return;
            const size_t count = std::min(batch.size(), queue_.size());
            for (size_t i = 0; i < count; ++i) {
                batch[i] = std::move(queue_.front());
                queue_.pop_front();
            }
            workerBusy_ = true;
            lock.unlock();
            for (size_t i = 0; i < count; ++i)
                execute(batch[i]);
            lock.lock();
            workerBusy_ = false;
            for (size_t i = 0; i < count; ++i)
                recycle(batch[i]);
            completed_ += count;
            if (producerJoining_ || completed_ == submitted_)
                done_.notify_all();
        }
    }

    static constexpr size_t kBatchSize = 8;
    ResidentScene& scene_;
    const bool async_, assist_;
    CachedRasterToWorld camera_; // builder side (or worker side in synchronous mode)
    uint64_t cameraSerial_ = ~0ull, cameraXY_ = 0;
    bool staged_ = false;
    SceneGuideCamera stagedCamera_;
    uint64_t stagedXY_ = 0;
    std::function<void()> independentTask_; // producer-owned, never accessed by the worker
    unsigned independentMathMode_ = 0;
    bool deferNextPreparation_ = false;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::deque<Job> queue_;
    std::vector<std::vector<DrawVertex>> spare_;
    uint64_t submitted_ = 0, completed_ = 0, drainWaits_ = 0;
    bool stop_ = false, workerBusy_ = false, producerJoining_ = false;
    std::thread thread_;
};
} // namespace gsr::surface
