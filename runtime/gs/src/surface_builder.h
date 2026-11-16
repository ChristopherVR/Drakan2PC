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
#include "thread_name.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <condition_variable>
#include <cstdint>
#include <cstring>
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
            thread_ = std::thread([this] {
                nameCurrentThread(L"SurfaceBuilder");
                run();
            });
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

// What the ray-surface submission of one draw reads, taken at the draw (gs_draw.inl). The draw's vertices are passed
// separately: a draw whose vertices only the GPU computed (a vertex-program batch, streamed out) is submitted once they
// are read back, and so is every later draw meanwhile, so the builder sees the draws in their order either way.
struct DrawInput {
    SurfaceList* list = nullptr;
    uint64_t epoch = 0;
    bool firstOfEpoch = false; // the target's first draw reaching the builder in this world: its job starts the list
    SceneGuideCamera camera;   // staged when the builder's camera key differs
    uint64_t cameraSerial = 0;
    GsDrawState s{};
    bool nativeWaterPass = false, haveTex = false, texIsRT = false, residentWater = false;
    uint8_t texMinimumAlpha = 0;
    uint32_t reflectionTexture = ~0u;
};

// The eligibility of one draw for the ray scene and its job (the former body of the surface block in Impl::draw).
inline void submitDraw(SurfaceBuilder& builder, const DrawInput& in, const DrawVertex* v, uint32_t count) {
    const GsDrawState& s = in.s;
    // Same camera cache key as before the builder: every draw reaching this point may reconfigure it.
    if (builder.needsConfigure(in.cameraSerial, s.xyoffset))
        builder.stageCamera(in.camera, s.xyoffset);
    const bool zmsk = (s.zbuf >> 32) & 1;
    // Eligibility is proved against native layer geometry, independent
    // of level, texture format, palette animation or vertex tint.
    const bool waterState =
        in.nativeWaterPass || (in.residentWater && zmsk && in.haveTex && !in.texIsRT && (s.flags & DF_ABE));
    bool constantAlphaPass = false;
    const uint32_t alphaTest = uint32_t(bitsOf(s.test, 1, 3)), alphaRef = uint32_t(bitsOf(s.test, 4, 8));
    float vertexAlpha = 255;
    for (uint32_t i = 0; i < count; ++i)
        vertexAlpha = std::min(vertexAlpha, v[i].a);
    const bool textureAlpha = in.haveTex && bitsOf(s.tex0, 34, 1) != 0;
    if ((!textureAlpha || (bitsOf(s.tex0, 35, 2) == 0 && vertexAlpha >= 128)) &&
        ((alphaTest == 5 && (textureAlpha ? in.texMinimumAlpha : vertexAlpha) >= alphaRef) ||
         (alphaTest == 6 && (textureAlpha ? in.texMinimumAlpha : vertexAlpha) > alphaRef)))
        constantAlphaPass = true;
    // A passing alpha test does not make a blended terrain/detail
    // layer opaque. Only a fully replacing GS blend may occlude rays.
    const bool opaqueBlend =
        !(s.flags & DF_ABE) || (s.alpha == 0x44 && vertexAlpha >= 128 &&
                                (!textureAlpha || (bitsOf(s.tex0, 35, 2) == 0 && in.texMinimumAlpha >= 128)));
    const bool opaqueEligible = !zmsk && opaqueBlend && (!(s.test & 1) || alphaTest == 1 || constantAlphaPass);
    const bool materialEligible =
        in.haveTex && !in.texIsRT && (!(s.flags & DF_ABE) || s.alpha == 0x44) && (!(s.test & 1) || alphaTest != 0);
    const bool eligible = opaqueEligible || materialEligible || waterState;
    if (!eligible && !in.firstOfEpoch)
        return;
    // Conversion to world space, the resident match/observe and the append run on the builder (in submission order);
    // readers of the results drain it first. A job without vertices only starts the target's list for this world,
    // like the clear the inline loop did at every draw reaching it.
    auto job = builder.makeJob(in.list, in.epoch);
    auto& d = job.params;
    d.waterState = waterState;
    d.nativeWaterPass = in.nativeWaterPass;
    d.opaqueEligible = opaqueEligible;
    d.materialEligible = materialEligible;
    d.transparent = (s.flags & DF_ABE) != 0 && !opaqueBlend;
    d.textureAlpha = textureAlpha;
    d.fst = (s.flags & DF_FST) != 0;
    d.alphaTest = (s.test & 1) ? alphaTest : 1;
    d.alphaReference = float(alphaRef);
    d.textureFunction = uint32_t(bitsOf(s.tex0, 35, 2));
    if (in.haveTex && !in.texIsRT)
        d.texture = in.reflectionTexture;
    d.texWidth = float(1u << bitsOf(s.tex0, 26, 4));
    d.texHeight = float(1u << bitsOf(s.tex0, 30, 4));
    builder.submit(std::move(job), v, eligible ? count : 0);
}

// GSR_SURFACE_HASH: one value for a world's reconstructed triangles, every field in list order (no padding bytes), so
// two vertex paths can be compared on the same paused scene.
inline uint64_t listHash(const std::vector<Triangle>& list) {
    uint64_t h = 1469598103934665603ull;
    const auto word = [&h](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
    const auto real = [&word](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        word(v);
    };
    for (const Triangle& t : list) {
        for (const Point& q : t.p) {
            real(q.x);
            real(q.y);
            real(q.z);
        }
        for (float f : t.color)
            real(f);
        for (const auto& row : t.lighting)
            for (float f : row)
                real(f);
        for (const auto& row : t.uv)
            for (float f : row)
                real(f);
        for (float f : t.opacity)
            real(f);
        word(t.texture);
        word(t.alphaTest);
        word(t.textureFunction);
        real(t.alphaReference);
        real(t.roughness);
        real(t.reflectance);
        word(uint32_t(t.water) | uint32_t(t.vertexLit) << 1 | uint32_t(t.textureAlpha) << 2 |
             uint32_t(t.transparent) << 3 | uint32_t(t.shadowGeometry) << 4);
    }
    return h;
}

// One vertex of a vertex-program draw as the exact shader computes it (VSProgramRaw, VP_RAW_ONLY), streamed out.
struct RawVertex {
    uint32_t place[4];  // x, y (12.4 fixed point, low 16 bits), z, fog lane (bits 4..11 fog)
    uint32_t colour[4]; // r g b a, 0..255
    uint32_t stq[4];    // float bits of s, t, q; the fourth word is unused
};
static_assert(sizeof(RawVertex) == 48, "the stream-out layout");

// One triangle list vertex as GsCore::kick and toDV make it from the same values (ST/Q, no FST: program_draw::
// stateTakes declines FST records).
inline DrawVertex drawVertexOf(uint32_t x, uint32_t y, uint32_t z, uint32_t fog, const uint32_t colour[4],
                               const uint32_t stq[3], uint64_t xyoffset) {
    const int ofx = (int)(xyoffset & 0xFFFF), ofy = (int)((xyoffset >> 32) & 0xFFFF);
    DrawVertex d;
    d.x = ((int)(x & 0xFFFF) - ofx) * (1.0f / 16.0f);
    d.y = ((int)(y & 0xFFFF) - ofy) * (1.0f / 16.0f);
    d.z = (float)z;
    d.fog = (float)(fog & 0xFFu);
    d.r = (float)(colour[0] & 0xFFu);
    d.g = (float)(colour[1] & 0xFFu);
    d.b = (float)(colour[2] & 0xFFu);
    d.a = (float)(colour[3] & 0xFFu);
    std::memcpy(&d.s, &stq[0], 4);
    std::memcpy(&d.t, &stq[1], 4);
    std::memcpy(&d.q, &stq[2], 4);
    return d;
}
inline DrawVertex drawVertexOf(const RawVertex& r, uint64_t xyoffset) {
    return drawVertexOf(r.place[0], r.place[1], r.place[2], r.place[3] >> 4, r.colour, r.stq, xyoffset);
}
// emitTri without IIP: every vertex of a triangle takes the colour of its last one.
inline void flatShade(DrawVertex* v, uint32_t count) {
    for (uint32_t i = 0; i + 2 < count; i += 3)
        for (uint32_t j = 0; j < 2; ++j) {
            v[i + j].r = v[i + 2].r;
            v[i + j].g = v[i + 2].g;
            v[i + j].b = v[i + 2].b;
            v[i + j].a = v[i + 2].a;
        }
}

// The triangle list vertices of a strip batch (not the batch path) evaluated on the CPU with the VU's arithmetic, in
// index order: what the core's kick would have queued for the same records. Appended to `out`.
inline void evaluateBatch(const VertexProgramBatch& batch, const GsDrawState& s, std::vector<DrawVertex>& out) {
    const size_t first = out.size();
    out.reserve(first + batch.indexCount);
    for (uint32_t k = 0; k < batch.indexCount; ++k) {
        const VertexProgramVertex& pv = batch.vertices[batch.indices[k]];
        const vp::Output o = vp::evaluate(pv.program, batch.constants[pv.constants], pv.in);
        const uint32_t colour[4] = {o.r, o.g, o.b, o.a};
        const uint32_t stq[3] = {o.s, o.t, o.q};
        out.push_back(drawVertexOf(o.x, o.y, o.z, o.fog, colour, stq, s.xyoffset));
    }
    if (!(s.flags & DF_IIP))
        flatShade(out.data() + first, batch.indexCount);
}

// Surface submissions held back behind a draw whose vertices are still on the GPU (gs_draw.inl, flushSurfaces).
struct PendingDraws {
    struct Draw {
        DrawInput in;
        uint32_t first = 0, count = 0; // in `vertices` (CPU draws), or in the backend's stream (gpu)
        bool gpu = false;
        uint32_t check = 0; // GSR_VERTEX_SURFACE_VERIFY: first of the CPU evaluation in `checks` (gpu draws)
    };
    std::vector<Draw> draws;
    std::vector<DrawVertex> vertices, checks, scratch;
    uint64_t streamed = 0, compared = 0, differing = 0, flushes = 0;
    bool empty() const { return draws.empty(); }
    void clear() {
        draws.clear();
        vertices.clear();
        checks.clear();
    }
};
} // namespace gsr::surface
