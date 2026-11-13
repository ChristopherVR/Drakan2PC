#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "runtime_flags.h"

namespace gsr {
// Renderer calls are serialized by its existing context lock. This profiler
// adds no synchronization, GPU queries, per-draw logging or dynamic allocation.
class CpuProfile {
    using Clock = std::chrono::steady_clock;

public:
    enum Id {
        Draw,
        ResolveTexture,
        SyncRT,
        ReadbackRT,
        ReadbackMap,
        ReadbackPack,
        TemporalResolve,
        PresentLatched,
        PresentCall,
        PumpUp,
        Snapshot,
        WindowScreenshot,
        LatchDisplay,
        EndFrame,
        TextureCoherenceFlush,
        TextureStampCheck,
        TextureDecode,
        TextureCallback,
        TextureUpscale,
        TextureCreate,
        GuidePrepare,
        RayScene,
        ModelGuides,
        SuperResolution,
        NeuralEvaluation,
        TemporalCompose,
        RayPack,
        RayProduce,
        RayBuild,
        RayWait,
        RayConsume,
        RayEnqueue,
        RayTriangles,
        RayModelAttributes,
        RayUpload,
        DrawSurface,
        DrawObjectMotion,
        DrawVertexUpload,
        DrawSubmit,
        ModelPrepare,
        SurfaceDrain,
        SurfaceWait,
        SurfaceAssist,
        ProgramRecord,   // vertex-program records in the core (parse, checks, copies), by the core's own profile
        ProgramEvaluate, // of those, records evaluated on the CPU in the core (declined, or the strip tail)
        ProgramFlush,    // a vertex-program batch handed to the backend (core)
        ProgramBounds,   // its exact bounds (backend)
        ProgramUpload,   // its buffers and bindings (backend)
        ProgramMotion,   // its previous state for the velocity pass (backend)
        DrawDest,        // of drawSubmit: the destination or feedback copy of the draw
        DrawBind,        // of drawSubmit: targets, programs, textures, viewport and scissor (bindDraw)
        DrawPass,        // of drawSubmit: the colour passes (constants, blend and depth state, the draw call)
        DrawVelocity,    // of drawSubmit: the motion-vector passes (temporal upscaling only)
        DrawEnd,         // of drawSubmit: releasing the draw's bindings (endDraw)
        DrawConstants,   // the constants upload of a pass (inside DrawPass or DrawVelocity)
        Count
    };
    struct Scope;

private:
    struct Sample {
        unsigned long long calls = 0;
        double inclusive = 0, exclusive = 0, maximum = 0;
    };
    std::array<Sample, Count> samples{};
    Scope* top = nullptr;
    Clock::time_point window{};
    bool enabled = false;
    double seconds = 2;
    double rootMs = 0;
    unsigned long long readbackBytes = 0, uploadBytes = 0;
    void report(Clock::time_point now) {
        const double elapsed = std::chrono::duration<double>(now - window).count();
        if (elapsed < seconds)
            return;
        static constexpr const char* names[] = {"draw",
                                                "resolveTexture",
                                                "syncRT",
                                                "readbackRT",
                                                "readbackMap",
                                                "readbackPackUnmap",
                                                "temporalResolve",
                                                "presentLatched",
                                                "presentCall",
                                                "pumpUp",
                                                "snapshot",
                                                "windowScreenshot",
                                                "latchDisplay",
                                                "endFrame",
                                                "textureCoherenceFlush",
                                                "textureStampCheck",
                                                "textureDecode",
                                                "textureCallback",
                                                "textureUpscale",
                                                "textureCreate",
                                                "guidePrepare",
                                                "rayScene",
                                                "modelGuides",
                                                "superResolution",
                                                "neuralEvaluation",
                                                "temporalCompose",
                                                "rayPack",
                                                "rayProduce",
                                                "rayBuild",
                                                "rayWait",
                                                "rayConsume",
                                                "rayEnqueue",
                                                "rayTriangles",
                                                "rayModelAttributes",
                                                "rayUpload",
                                                "drawSurface",
                                                "drawObjectMotion",
                                                "drawVertexUpload",
                                                "drawSubmit",
                                                "modelPrepare",
                                                "surfaceDrain",
                                                "surfaceWait",
                                                "surfaceAssist",
                                                "programRecord",
                                                "programEvaluate",
                                                "programFlush",
                                                "programBounds",
                                                "programUpload",
                                                "programMotion",
                                                "drawDest",
                                                "drawBind",
                                                "drawPass",
                                                "drawVelocity",
                                                "drawEnd",
                                                "drawConstants"};
        static_assert(sizeof(names) / sizeof(names[0]) == Count, "Profiler bucket names must match Id");
        std::fprintf(stderr,
                     "[gsr-profile] %.3fs root=%.2fms readbackCopy=%.2fMiB syncUpload=%.2fMiB; "
                     "calls/inclusive_ms/exclusive_ms/max_ms:",
                     elapsed, rootMs, readbackBytes / 1048576.0, uploadBytes / 1048576.0);
        for (unsigned i = 0; i < Count; ++i) {
            const auto& s = samples[i];
            if (s.calls)
                std::fprintf(stderr, " %s=%llu/%.2f/%.2f/%.2f", names[i], s.calls, s.inclusive, s.exclusive, s.maximum);
        }
        std::fputc('\n', stderr);
        samples = {};
        rootMs = 0;
        readbackBytes = uploadBytes = 0;
        window = Clock::now();
    }

public:
    CpuProfile() {
        if (runtimeFlags().profileSeconds > 0) {
            enabled = true;
            seconds = runtimeFlags().profileSeconds;
        }
        if (enabled)
            window = Clock::now();
    }
    void copied(unsigned long long bytes) {
        if (enabled)
            readbackBytes += bytes;
    }
    void uploaded(unsigned long long bytes) {
        if (enabled)
            uploadBytes += bytes;
    }
    struct Scope {
        CpuProfile* owner = nullptr;
        Scope* parent = nullptr;
        Id id;
        Clock::time_point start{};
        double childMs = 0;
        Scope(CpuProfile& p, Id bucket) : Scope(&p, bucket) {}
        Scope(CpuProfile* p, Id bucket) : id(bucket) {
            if (!p || !p->enabled)
                return;
            owner = p;
            parent = p->top;
            p->top = this;
            start = Clock::now();
        }
        ~Scope() {
            if (!owner)
                return;
            const auto now = Clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - start).count();
            auto& s = owner->samples[id];
            ++s.calls;
            s.inclusive += ms;
            s.exclusive += ms > childMs ? ms - childMs : 0;
            if (ms > s.maximum)
                s.maximum = ms;
            owner->top = parent;
            if (parent)
                parent->childMs += ms;
            else {
                owner->rootMs += ms;
                owner->report(now);
            }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
};
} // namespace gsr
