#pragma once
// Reconstruction of world-space surface triangles from GS draws for the ray scene, optionally on its own thread.
//
// The GS worker only classifies a draw and copies its vertices; the conversion to world space, the resident-scene
// match/observe and the append to the target's per-frame list run in submission order on a builder thread. Every
// reader of the results (the target's list, the resident scene's material projections) calls drain() first, so the
// lists and projections are exactly those of the synchronous path.
#include "gs_internal.h"
#include "resident_scene.h"
#include "surface_geometry.h"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
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
        tri.vertexLit = true;
        tri.shadowGeometry = false;
        tri.alphaTest = d.alphaTest;
        tri.alphaReference = d.alphaReference;
        tri.textureAlpha = d.textureAlpha;
        tri.textureFunction = d.textureFunction;
        tri.transparent = d.transparent;
        tri.texture = d.texture;
        for (unsigned j = 0; j < 3; ++j) {
            valid = valid && v[i + j].z > 0 && camera.point(v[i + j].x, v[i + j].y, v[i + j].z, tri.p[j]);
            tri.color[0] += v[i + j].r / (128.f * 3);
            tri.color[1] += v[i + j].g / (128.f * 3);
            tri.color[2] += v[i + j].b / (128.f * 3);
            tri.opacity[j] = std::clamp(v[i + j].a / 128.f, 0.f, 1.f);
            tri.lighting[j][0] = v[i + j].r / 128.f;
            tri.lighting[j][1] = v[i + j].g / 128.f;
            tri.lighting[j][2] = v[i + j].b / 128.f;
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
        if (!valid || length(sub(tri.p[1], tri.p[0])) > 2000000)
            continue;
        // A resident water match found here is also the water-layer match used below.
        const int waterMatch = d.waterState && !d.nativeWaterPass ? scene.match(tri, true) : -1;
        tri.water = d.waterState && (d.nativeWaterPass || waterMatch >= 0);
        const int resident = waterMatch >= 0 ? waterMatch : scene.match(tri, tri.water);
        if (resident >= 0 && (d.opaqueEligible || tri.water))
            scene.observe(tri, resident);
        if (tri.water || (resident < 0 && (d.opaqueEligible || d.materialEligible)))
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

    SurfaceBuilder(ResidentScene& scene, bool async) : scene_(scene), async_(async) {
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
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(job));
            ++submitted_;
        }
        wake_.notify_one();
    }
    // Runs a task on the builder thread after the jobs submitted before it (in place when synchronous).
    void submitTask(std::function<void()> task) {
        if (!async_) {
            task();
            return;
        }
        Job job;
        job.task = std::move(task);
        job.mathMode = currentMathMode();
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(job));
            ++submitted_;
        }
        wake_.notify_one();
    }
    // Waits until every submitted job has been applied.
    void drain() {
        if (!async_)
            return;
        std::unique_lock lock(mutex_);
        if (completed_ == submitted_)
            return;
        ++drainWaits_;
        done_.wait(lock, [this] { return completed_ == submitted_; });
    }
    uint64_t drainWaits() const { return drainWaits_; }

private:
    static unsigned currentMathMode() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
        return _mm_getcsr();
#else
        return 0;
#endif
    }
    static void setMathMode(unsigned mode) {
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
        if (_mm_getcsr() != mode)
            _mm_setcsr(mode);
#else
        (void)mode;
#endif
    }
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
    void run() {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty())
                return; // stop requested and nothing left
            Job job = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            // The worker's MXCSR (rounding, flush-to-zero) decides the float results; use the same mode.
            setMathMode(job.mathMode);
            if (job.task)
                job.task();
            else
                process(job, job.vertices.data(), uint32_t(job.vertices.size()));
            lock.lock();
            if (spare_.size() < 64)
                spare_.push_back(std::move(job.vertices));
            if (++completed_ == submitted_)
                done_.notify_all();
        }
    }

    ResidentScene& scene_;
    const bool async_;
    CachedRasterToWorld camera_; // builder side (or worker side in synchronous mode)
    uint64_t cameraSerial_ = ~0ull, cameraXY_ = 0;
    bool staged_ = false;
    SceneGuideCamera stagedCamera_;
    uint64_t stagedXY_ = 0;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::deque<Job> queue_;
    std::vector<std::vector<DrawVertex>> spare_;
    uint64_t submitted_ = 0, completed_ = 0, drainWaits_ = 0;
    bool stop_ = false;
    std::thread thread_;
};
} // namespace gsr::surface
