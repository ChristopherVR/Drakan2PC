#include "ray_water.h"
#ifdef GSR_HAVE_DXR_WATER
#include "ray_water_bytecode.h"
#include "ray_sky_bytecode.h"
#include "nr_interop.h"
#include "cpu_profile.h"
#include "runtime_flags.h"
#include "ray_world_cache.h"
#include "ray_normals.h"
#include <optional>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <set>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
namespace gsr::surface {
namespace {
void check(HRESULT h, const char* message) {
    if (FAILED(h)) {
        char b[256];
        std::snprintf(b, sizeof(b), "%s (0x%08lx)", message, (unsigned long)h);
        throw std::runtime_error(b);
    }
}
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER v{};
    v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
    return v;
}
struct alignas(256) Parameters {
    float projection[16], inverse[16], right[4], up[4], forward[4], sizeTime[4], jitter[4], limits[4];
    uint32_t scene[4];
    float ambient[4], lightVector[4][4], lightColor[4][4];
    uint32_t effects[4];
};
} // namespace
// Upload-heap buffer that stays mapped for its whole life (Map/Unmap per upload costs kernel calls).
struct UploadBuffer {
    ComPtr<ID3D12Resource> resource;
    uint8_t* data = nullptr;
    ID3D12Resource* operator->() const { return resource.Get(); }
    ID3D12Resource* Get() const { return resource.Get(); }
};

// Frames overlap on the GPU. Each submitted frame waits on the GPU for the D3D11 producer fence, and D3D11 waits
// for the previous frame's consumer fence before it produces again, so GPU-only resources (BLAS, TLAS, scratch,
// materials, sky) are never used by two frames at once. Only CPU-written memory (allocator, descriptors, upload
// buffers) needs a slot per frame in flight; the CPU waits only before reusing a slot.
//
// Threads: render() runs on the renderer thread (validation, texture change detection, the D3D11 copies and fence
// operations) and queues a Job. The pass thread packs and uploads the geometry, records and submits the D3D12 work
// and signals the consumer fence, one job at a time in queue order. Everything below the "pass thread" marker is
// used only by that thread, except while it is idle (finish()).
struct RayWater::Impl {
    static constexpr unsigned kFrames = 3;
    static constexpr unsigned kQueuedJobs = 2;        // render() blocks while this many passes are waiting or running
    static constexpr float kRebaseDistance = 16384.f; // static BLAS coordinates stay this close to the camera
    static constexpr uint32_t kTriangleBudget = 500000;
    static constexpr uint32_t kTextureBudget = 768;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12DescriptorHeap> heap;
        UploadBuffer vertices, instances, constants, staging, models;
        std::vector<ComPtr<ID3D12Resource>> retired; // released once this slot's fence has passed
        uint64_t fence = 0;
    };
    struct TextureUpload {
        uint32_t slot = 0;
        std::vector<uint32_t> pixels;
    };
    // Everything a pass reads, owned by the job: the renderer thread reuses its own buffers for the next frame.
    struct Job {
        std::vector<Triangle> triangles;
        std::shared_ptr<const std::vector<Triangle>> staticTriangles;
        uint64_t staticGeometry = 0, staticAttributes = 0;
        std::shared_ptr<const SceneModels> models;
        uint32_t textureCount = 0, nativeTextureBase = 0;
        std::vector<TextureUpload> textureUploads; // texture slots whose texels changed since the previous job
        nr::guides::FrameConstants camera{};
        float seconds = 0;
        bool reflections = false;
        RayOptions options;
        std::string diagnosticDump;
        uint32_t width = 0, height = 0;
        UINT64 produced = 0, consumed = 0;
        bool consumedSignaled = false;
        bool cpuCache = false, checkCache = false, worldCache = false, packedTextures = false;
        uint32_t coverageDebug = 0;
    };
    CpuProfile cpuProfile;    // renderer thread
    CpuProfile threadProfile; // pass thread
    mutable std::mutex statusMutex;
    std::string status = "DXR water uninitialized";
    bool initialized = false;
    std::atomic<bool> faulted{false};
    // Job queue. The front job is the one running; it stays queued until it has been submitted.
    std::mutex queueMutex;
    std::condition_variable wake, idle;
    std::deque<std::unique_ptr<Job>> jobs;
    std::vector<std::vector<Triangle>> spareTriangles; // recycled per-frame triangle storage (keeps capacity)
    bool stopping = false;
    std::thread worker;
    // Renderer thread state.
    std::vector<uint64_t> textureFingerprints;
    std::shared_ptr<const std::vector<Triangle>> staticPublished;
    uint64_t staticPublishedGeometry = 0, staticPublishedAttributes = 0;
    uint32_t staticPublishedWater = 0;
    bool staticPublishedValid = false;
    void setStatus(std::string text) {
        std::lock_guard lock(statusMutex);
        status = std::move(text);
    }
    void fault(const char* what) {
        faulted = true;
        setStatus(what);
    }
    void finish() {
        std::unique_lock lock(queueMutex);
        idle.wait(lock, [&] { return jobs.empty(); });
    }
    void stop() {
        {
            std::lock_guard lock(queueMutex);
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }
    void enqueue(std::unique_ptr<Job> job, std::vector<Triangle>& recycled) {
        if (!worker.joinable())
            worker = std::thread([this] { loop(); });
        std::unique_lock lock(queueMutex);
        idle.wait(lock, [&] { return jobs.size() < kQueuedJobs; });
        recycled.clear();
        if (!spareTriangles.empty()) {
            recycled.swap(spareTriangles.back());
            spareTriangles.pop_back();
        }
        jobs.push_back(std::move(job));
        lock.unlock();
        wake.notify_one();
    }
    void loop() {
        std::unique_lock lock(queueMutex);
        for (;;) {
            wake.wait(lock, [&] { return stopping || !jobs.empty(); });
            if (jobs.empty())
                return;
            Job& job = *jobs.front();
            lock.unlock();
            run(job);
            lock.lock();
            job.triangles.clear();
            if (job.triangles.capacity() && spareTriangles.size() < kQueuedJobs + 1)
                spareTriangles.push_back(std::move(job.triangles));
            jobs.pop_front();
            idle.notify_all();
        }
    }
    void run(Job& job) {
        if (!faulted) {
            try {
                execute(job);
            } catch (const std::exception& e) {
                fault(e.what());
            }
        }
        // The renderer's D3D11 context waits for this value on the GPU; it must be signaled even after a failure.
        if (!job.consumedSignaled && interop && queue)
            try {
                interop->signal12(queue.Get(), job.consumed);
            } catch (...) {
            }
    }
    void execute(Job& job);
    // ---- pass thread
    RayWorldCache worldCache;
    ComPtr<ID3D11Device> d11;
    ComPtr<ID3D12Device5> device;
    ComPtr<ID3D12InfoQueue> debug;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12GraphicsCommandList4> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t serial = 0;
    std::array<Frame, kFrames> frames;
    unsigned frameIndex = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline, skyPipeline;
    ComPtr<ID3D12Resource> materials, scratch, blas[3], tlas, sky;
    uint64_t skyLevel = UINT64_MAX;
    std::unique_ptr<NrInterop> interop; // replaced by the renderer thread only while the pass thread is idle
    struct NativeMesh {
        ComPtr<ID3D12Resource> blas;
        bool built = false;
        std::shared_ptr<const SceneModelMesh> attributeSource;
        std::vector<Attribute> attributes;
        uint64_t attributeGeneration = 0;
        uint32_t texturedTriangles = 0;
    };
    std::unordered_map<uint64_t, NativeMesh> nativeMeshes;
    // Vertex-normal welds by mesh topology: each pose of an animated model is a new mesh with the same welds.
    std::unordered_map<uint64_t, CornerWelds> modelWelds;
    // Animated models get a new mesh identity for every pose but keep their topology. Each pose reuses a BLAS of the
    // same topology that no current mesh uses and refits it in place (passes run in order on one queue), instead of
    // allocating and building a new one; a full rebuild every kRefits updates bounds the loss of trace quality.
    struct Deformable {
        ComPtr<ID3D12Resource> blas;
        uint32_t vertexCount = 0, updates = 0;
        uint64_t identity = 0, used = 0;
    };
    static constexpr uint32_t kRefits = 60;
    static constexpr uint64_t kDeformableIdle = 120; // passes before an unused one is released
    std::unordered_map<uint64_t, std::deque<Deformable>> deformables;
    std::vector<Point> modelPositions;
    uint64_t modelPass = 0, modelRefits = 0;
    std::vector<Point> cpuPositions;
    std::vector<Attribute> cpuMaterials;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> cpuInstances;
    std::vector<uint32_t> materialFlags;
    uint32_t materialTextureBase = ~0u;
    uint64_t materialGeneration = 0, checkedFrames = 0;
    uint32_t width = 0, height = 0; // interop extent (renderer thread)
    // Static world the GPU currently holds.
    uint64_t staticGeometry = ~0ull, staticAttributes = ~0ull;
    size_t staticTextureCount = ~size_t(0);
    bool staticUploaded = false; // staticAttributeData has been copied into the materials buffer
    Point staticBase{};
    uint32_t staticCounts[3]{}, staticTextured = 0;
    std::vector<Attribute> staticAttributeData;
    UploadBuffer staticVertices;
    ComPtr<ID3D12Resource> staticBlas[3];
    ~Impl() {
        stop();
        try {
            drain();
        } catch (...) {
        }
        if (event)
            CloseHandle(event);
    }
    void waitFor(uint64_t value) {
        if (!fence || fence->GetCompletedValue() >= value)
            return;
        check(fence->SetEventOnCompletion(value, event), "DXR completion event");
        if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("DXR GPU completion timeout");
    }
    void drain() {
        if (!queue || !fence)
            return;
        check(queue->Signal(fence.Get(), ++serial), "DXR drain signal");
        waitFor(serial);
        for (auto& f : frames)
            f.retired.clear();
    }
    Frame& acquire() {
        Frame& f = frames[frameIndex];
        waitFor(f.fence);
        f.retired.clear();
        return f;
    }
    void submitted(Frame& f) {
        check(queue->Signal(fence.Get(), ++serial), "DXR completion signal");
        f.fence = serial;
        frameIndex = (frameIndex + 1) % kFrames;
        check(device->GetDeviceRemovedReason(), "DXR device removed");
    }
    // Grows a buffer; the replaced one stays alive until the current frame's work has completed.
    void buffer(ComPtr<ID3D12Resource>& out, uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                std::vector<ComPtr<ID3D12Resource>>& retired, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        bytes = (bytes + 255) & ~255ull;
        if (out && out->GetDesc().Width >= bytes)
            return;
        if (out)
            retired.push_back(out);
        out.Reset();
        D3D12_HEAP_PROPERTIES h{};
        h.Type = type;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = std::max<uint64_t>(bytes, 256);
        d.Height = 1;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = flags;
        check(device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&out)),
              "DXR buffer allocation");
    }
    void ensureUpload(UploadBuffer& out, uint64_t bytes, std::vector<ComPtr<ID3D12Resource>>& retired) {
        ID3D12Resource* before = out.resource.Get();
        buffer(out.resource, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, retired);
        if (out.resource.Get() != before || !out.data) {
            void* mapped = nullptr;
            const D3D12_RANGE read{0, 0};
            check(out.resource->Map(0, &read, &mapped), "DXR upload map");
            out.data = static_cast<uint8_t*>(mapped);
        }
    }
    void upload(UploadBuffer& out, const void* data, size_t bytes, std::vector<ComPtr<ID3D12Resource>>& retired) {
        ensureUpload(out, bytes, retired);
        if (bytes)
            std::memcpy(out.data, data, bytes);
    }
    void upload(ComPtr<ID3D12Resource>& out, const void* data, size_t bytes,
                std::vector<ComPtr<ID3D12Resource>>& retired) {
        buffer(out, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, retired);
        void* mapped = nullptr;
        const D3D12_RANGE read{0, 0};
        check(out->Map(0, &read, &mapped), "DXR upload map");
        if (bytes)
            std::memcpy(mapped, data, bytes);
        out->Unmap(0, nullptr);
    }
    void barrier(ID3D12Resource* resource) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = resource;
        list->ResourceBarrier(1, &b);
    }
    void buildBlas(ComPtr<ID3D12Resource>& target, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t vertexCount,
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags,
                   std::vector<ComPtr<ID3D12Resource>>& retired, bool replace, bool update = false) {
        D3D12_RAYTRACING_GEOMETRY_DESC geo{};
        geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geo.Triangles.VertexBuffer = {vertices, sizeof(Point)};
        geo.Triangles.VertexCount = vertexCount;
        geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        in.NumDescs = 1;
        in.pGeometryDescs = &geo;
        in.Flags = flags;
        if (update)
            in.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &size);
        if (update) {
            // Same topology and flags as the structure being refitted, so it already has the right size.
            buffer(scratch, size.UpdateScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, retired, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
            build.Inputs = in;
            build.DestAccelerationStructureData = build.SourceAccelerationStructureData =
                target->GetGPUVirtualAddress();
            build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
            list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
            barrier(target.Get());
            barrier(scratch.Get());
            return;
        }
        if (replace && target) {
            // Earlier frames may still trace the old structure; never rebuild it in place.
            retired.push_back(target);
            target.Reset();
        }
        buffer(target, size.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, retired,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        buffer(scratch, size.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               retired, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = in;
        build.DestAccelerationStructureData = target->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        barrier(target.Get());
        barrier(scratch.Get());
    }
};
RayWater::RayWater(ID3D11Device* d11, ID3D11DeviceContext*) : impl(std::make_unique<Impl>()) {
    auto& s = *impl;
    s.d11 = d11;
    try {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        check(d11->QueryInterface(IID_PPV_ARGS(&dxgi)), "DXR adapter query");
        check(dxgi->GetAdapter(&adapter), "DXR adapter");
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&s.device)), "DXR device creation");
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        check(s.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)),
              "DXR capability query");
        if (options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
            throw std::runtime_error("DXR 1.1 inline ray tracing is unavailable");
        D3D12_COMMAND_QUEUE_DESC q{};
        q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(s.device->CreateCommandQueue(&q, IID_PPV_ARGS(&s.queue)), "DXR queue");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.NumDescriptors = 3;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        for (auto& f : s.frames) {
            check(s.device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&f.allocator)), "DXR allocator");
            check(s.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&f.heap)), "DXR water descriptors");
        }
        check(s.device->CreateCommandList(0, q.Type, s.frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&s.list)),
              "DXR command list");
        check(s.list->Close(), "DXR initial close");
        check(s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence)), "DXR fence");
        s.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!s.event)
            throw std::runtime_error("DXR event creation failed");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2};
        D3D12_ROOT_PARAMETER p[5]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].Descriptor.ShaderRegister = 0;
        p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[1].Descriptor.ShaderRegister = 0;
        p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[2].Descriptor.ShaderRegister = 1;
        p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[3].DescriptorTable = {2, ranges};
        p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[4].Descriptor.ShaderRegister = 1;
        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 5;
        rd.pParameters = p;
        ComPtr<ID3DBlob> code, error;
        check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &code, &error),
              "DXR root signature serialization");
        check(s.device->CreateRootSignature(0, code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&s.root)),
              "DXR root signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = s.root.Get();
        pd.CS = {g_rayWater, sizeof(g_rayWater)};
        check(s.device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&s.pipeline)), "DXR water pipeline");
        pd.CS = {g_raySky, sizeof(g_raySky)};
        check(s.device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&s.skyPipeline)), "DXR sky reduction pipeline");
        s.device.As(&s.debug);
        if (s.debug)
            s.debug->ClearStoredMessages();
        s.initialized = true;
        s.status = "DXR 1.1 water/reflection pipeline ready";
    } catch (const std::exception& e) {
        s.status = e.what();
    }
}
RayWater::~RayWater() = default;
bool RayWater::ready() const {
    return impl->initialized && !impl->faulted;
}
std::string RayWater::status() const {
    std::lock_guard lock(impl->statusMutex);
    return impl->status;
}
void RayWater::finish() {
    impl->finish();
}
void RayWater::setStaticWorld(std::span<const Triangle> triangles, uint64_t geometryVersion,
                              uint64_t attributeVersion) {
    auto& s = *impl;
    if (s.staticPublishedValid && s.staticPublishedGeometry == geometryVersion &&
        s.staticPublishedAttributes == attributeVersion)
        return;
    shareStaticWorld(std::make_shared<const std::vector<Triangle>>(triangles.begin(), triangles.end()), geometryVersion,
                     attributeVersion);
}
void RayWater::shareStaticWorld(std::shared_ptr<const std::vector<Triangle>> triangles, uint64_t geometryVersion,
                                uint64_t attributeVersion) {
    auto& s = *impl;
    if (!triangles)
        triangles = std::make_shared<const std::vector<Triangle>>();
    if (s.staticPublished != triangles)
        s.staticPublishedWater =
            uint32_t(std::count_if(triangles->begin(), triangles->end(), [](const Triangle& t) { return t.water; }));
    s.staticPublished = std::move(triangles);
    s.staticPublishedGeometry = geometryVersion;
    s.staticPublishedAttributes = attributeVersion;
    s.staticPublishedValid = true;
}
bool RayWater::render(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* output,
                      const nr::guides::FrameConstants& camera, std::span<const Triangle> triangles, float seconds,
                      bool reflections, std::span<const Texture> textures, const SceneModels* models,
                      const RayOptions& options) {
    std::vector<Triangle> copy(triangles.begin(), triangles.end());
    // The caller keeps ownership of `models`: wait for the pass before returning.
    const bool queued = submit(color, depth, output, camera, copy, seconds, reflections, textures,
                               std::shared_ptr<const SceneModels>(models, [](const SceneModels*) {}), options);
    impl->finish();
    return queued;
}
bool RayWater::submit(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* output,
                      const nr::guides::FrameConstants& camera, std::vector<Triangle>& triangles, float seconds,
                      bool reflections, std::span<const Texture> textures, std::shared_ptr<const SceneModels> models,
                      const RayOptions& options) {
    auto& s = *impl;
    if (!ready())
        return false;
    try {
        std::optional<CpuProfile::Scope> stage;
        stage.emplace(s.cpuProfile, CpuProfile::RayProduce);
        if (camera.orthographic) {
            s.setStatus("DXR water bypassed for orthographic camera");
            return false;
        }
        if (!color || !depth || !output || !std::isfinite(seconds))
            throw std::runtime_error("DXR requires complete finite frame inputs");
        D3D11_TEXTURE2D_DESC d{};
        color->GetDesc(&d);
        for (unsigned i = 0; i < 16; ++i)
            if (!std::isfinite(camera.currentViewToClip[i]) || !std::isfinite(camera.clipToCameraView[i]))
                throw std::runtime_error("DXR requires finite camera projection");
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(camera.cameraPosition[i]) || !std::isfinite(camera.cameraRight[i]) ||
                !std::isfinite(camera.cameraUp[i]) || !std::isfinite(camera.cameraForward[i]))
                throw std::runtime_error("DXR requires finite camera basis");
        if (!std::isfinite(camera.nearPlane) || camera.nearPlane <= 0)
            throw std::runtime_error("DXR requires a positive near plane");
        auto validate = [&](ID3D11Texture2D* texture, DXGI_FORMAT format) {
            D3D11_TEXTURE2D_DESC t{};
            texture->GetDesc(&t);
            ComPtr<ID3D11Device> owner;
            texture->GetDevice(&owner);
            if (owner.Get() != s.d11.Get() || t.Width != d.Width || t.Height != d.Height || t.Format != format ||
                t.SampleDesc.Count != 1 || t.ArraySize != 1 || t.MipLevels != 1)
                throw std::runtime_error("DXR texture device/extent/format mismatch");
        };
        validate(color, DXGI_FORMAT_R16G16B16A16_FLOAT);
        validate(output, DXGI_FORMAT_R16G16B16A16_FLOAT);
        validate(depth, DXGI_FORMAT_R32_FLOAT);
        if (!s.staticPublishedValid)
            RayWater::shareStaticWorld(std::shared_ptr<const std::vector<Triangle>>(), 0, 0);
        const auto& staticTriangles = *s.staticPublished;
        if (!output || !depth || triangles.size() + staticTriangles.size() > Impl::kTriangleBudget)
            throw std::runtime_error("DXR world input is incomplete or exceeds triangle budget");
        struct MaterialTexture {
            uint64_t identity;
            std::span<const uint32_t> pixels;
        };
        std::vector<MaterialTexture> allTextures;
        allTextures.reserve(textures.size() + (models ? models->textures.size() : 0));
        for (const auto& t : textures)
            allTextures.push_back({t.identity, t.pixels});
        const uint32_t nativeTextureBase = uint32_t(allTextures.size());
        if (models)
            for (const auto& t : models->textures)
                allTextures.push_back({t->identity, t->pixels});
        size_t totalTriangles = triangles.size() + staticTriangles.size();
        if (models)
            for (const auto& m : models->meshes)
                totalTriangles += m->triangles.size();
        if (totalTriangles > Impl::kTriangleBudget || allTextures.size() > Impl::kTextureBudget) {
            s.setStatus("DXR scene exceeds geometry/material budget");
            return false;
        }
        if (models)
            for (const auto& source : models->instances) {
                if (source.mesh >= models->meshes.size())
                    throw std::runtime_error("DXR invalid native model instance");
                if (!std::isfinite(source.opacity))
                    throw std::runtime_error("DXR invalid native model opacity");
            }
        const bool frameWater =
            std::any_of(triangles.begin(), triangles.end(), [](const Triangle& t) { return t.water; });
        if (!frameWater && !s.staticPublishedWater && !options.shadows) {
            s.setStatus("DXR water bypassed: no verified water world geometry");
            return false;
        }
        for (const auto& texture : allTextures)
            if (texture.pixels.size() != Texture::size * Texture::size)
                throw std::runtime_error("DXR invalid material texture");
        if (!s.interop || d.Width != s.width || d.Height != s.height) {
            s.finish();
            s.drain(); // in-flight frames still use the old shared textures
            s.interop = std::make_unique<NrInterop>(s.device.Get(), d.Width, d.Height, s.d11.Get());
            s.width = d.Width;
            s.height = d.Height;
        }
        auto job = std::make_unique<Impl::Job>();
        // Texel uploads only for texture slots whose content changed since the previous pass.
        s.textureFingerprints.resize(allTextures.size());
        for (size_t i = 0; i < allTextures.size(); ++i) {
            const auto& texture = allTextures[i];
            uint64_t fingerprint = texture.identity;
            if (!fingerprint) {
                fingerprint = 14695981039346656037ull;
                for (auto pixel : texture.pixels) {
                    fingerprint ^= pixel;
                    fingerprint *= 1099511628211ull;
                }
            }
            if (s.textureFingerprints[i] == fingerprint)
                continue;
            s.textureFingerprints[i] = fingerprint;
            job->textureUploads.push_back({uint32_t(i), {texture.pixels.begin(), texture.pixels.end()}});
        }
        job->staticTriangles = s.staticPublished;
        job->staticGeometry = s.staticPublishedGeometry;
        job->staticAttributes = s.staticPublishedAttributes;
        job->models = std::move(models);
        job->textureCount = uint32_t(allTextures.size());
        job->nativeTextureBase = nativeTextureBase;
        job->camera = camera;
        job->seconds = seconds;
        job->reflections = reflections;
        job->options = options;
        if (options.diagnosticDump)
            job->diagnosticDump = options.diagnosticDump;
        job->options.diagnosticDump = nullptr;
        job->width = d.Width;
        job->height = d.Height;
        job->cpuCache = runtimeFlags().rayCpuCache;
        job->checkCache = runtimeFlags().rayCpuCacheCheck;
        job->worldCache = runtimeFlags().rayWorldCache;
        job->packedTextures = runtimeFlags().rayPackedTextures;
        job->coverageDebug = runtimeFlags().waterCoverageDebug;
        // D3D11 side, in this frame's order: the inputs are copied before the D3D12 queue reads them, and later
        // D3D11 work waits on the GPU for the pass result before the output is copied back.
        job->produced = s.interop->produce11({color, nullptr, nullptr, depth});
        stage.reset();
        stage.emplace(s.cpuProfile, CpuProfile::RayConsume);
        job->consumed = s.interop->consume11(output);
        job->triangles.swap(triangles);
        stage.reset();
        s.enqueue(std::move(job), triangles);
        return true;
    } catch (const std::exception& e) {
        s.fault(e.what());
        return false;
    }
}
void RayWater::Impl::execute(Job& job) {
    auto& s = *this;
    const auto& camera = job.camera;
    const auto& triangles = job.triangles;
    const SceneModels* models = job.models.get();
    const auto& options = job.options;
    const float seconds = job.seconds;
    const bool reflections = job.reflections;
    const std::span<const Triangle> staticTriangles =
        job.staticTriangles ? std::span<const Triangle>(*job.staticTriangles) : std::span<const Triangle>();
    const uint64_t staticGeometryInput = job.staticGeometry, staticAttributeInput = job.staticAttributes;
    const size_t textureCount = job.textureCount;
    const uint32_t nativeTextureBase = job.nativeTextureBase;
    const bool cpuCache = job.cpuCache, checkCache = job.checkCache;
    const struct {
        UINT Width, Height;
    } d{job.width, job.height};
    size_t totalTriangles = triangles.size() + staticTriangles.size();
    if (models)
        for (const auto& m : models->meshes)
            totalTriangles += m->triangles.size();
    std::optional<CpuProfile::Scope> stage;
    stage.emplace(s.threadProfile, CpuProfile::RayPack);
    {
        const Point origin{camera.cameraPosition[0], camera.cameraPosition[1], camera.cameraPosition[2]};
        // Static world: rebuilt on a new level/geometry, or when the camera has moved far from the BLAS base
        // (positions are stored relative to that base to keep float precision near the camera).
        std::optional<CpuProfile::Scope> packing;
        packing.emplace(s.threadProfile, CpuProfile::RayTriangles);
        const auto offset = sub(origin, s.staticBase);
        const bool rebuildStatic =
            !staticTriangles.empty() &&
            (s.staticGeometry != staticGeometryInput || (!s.staticBlas[0] && !s.staticBlas[1] && !s.staticBlas[2]) ||
             length(offset) > Impl::kRebaseDistance);
        std::vector<Point> staticPositions;
        if (staticTriangles.empty()) {
            s.staticCounts[0] = s.staticCounts[1] = s.staticCounts[2] = 0;
            s.staticTextured = 0;
            s.staticGeometry = s.staticAttributes = ~0ull;
            s.staticTextureCount = ~size_t(0);
            s.staticAttributeData.clear();
        } else if (rebuildStatic) {
            s.staticBase = origin;
            staticPositions.reserve(staticTriangles.size() * 3);
            for (unsigned g = 0; g < 3; ++g) {
                s.staticCounts[g] = 0;
                for (const auto& tri : staticTriangles)
                    if (group(tri) == g) {
                        for (const auto& p : tri.p)
                            staticPositions.push_back(sub(p, origin));
                        ++s.staticCounts[g];
                    }
            }
            s.staticGeometry = staticGeometryInput;
            s.staticAttributes = ~0ull; // group order may have changed
        }
        if (!staticTriangles.empty() &&
            (s.staticAttributes != staticAttributeInput || s.staticTextureCount != textureCount)) {
            s.staticAttributeData.clear();
            s.staticAttributeData.reserve(staticTriangles.size() * kTriangleAttributes);
            s.staticTextured = 0;
            // The resident terrain grid shares exact corner positions, so welding recovers its smooth shading.
            std::vector<Point> normals;
            smoothNormals(
                staticTriangles.size(),
                [&](size_t i, Point(&p)[3]) { std::copy(staticTriangles[i].p, staticTriangles[i].p + 3, p); }, normals);
            for (unsigned g = 0; g < 3; ++g)
                for (size_t i = 0; i < staticTriangles.size(); ++i)
                    if (group(staticTriangles[i]) == g)
                        s.staticTextured +=
                            packAttributes(staticTriangles[i], textureCount, s.staticAttributeData, &normals[i * 3]);
            s.staticAttributes = staticAttributeInput;
            s.staticTextureCount = textureCount;
            s.staticUploaded = false;
        }
        const uint32_t staticTotal = s.staticCounts[0] + s.staticCounts[1] + s.staticCounts[2];
        // Per-frame (dynamic) triangles, camera relative.
        std::vector<Point> temporaryPositions;
        std::vector<Attribute> temporaryColors;
        auto& positions = cpuCache ? s.cpuPositions : temporaryPositions;
        auto& colors = cpuCache ? s.cpuMaterials : temporaryColors;
        positions.clear();
        colors.clear();
        positions.reserve(triangles.size() * 3);
        colors.reserve((totalTriangles - staticTriangles.size()) * kTriangleAttributes);
        if (cpuCache) {
            // Material attributes depend on flags, texture-table indices and
            // immutable mesh data, not on instance transforms or lighting.
            std::vector<uint32_t> flags;
            if (models) {
                flags.reserve(models->textures.size());
                for (const auto& t : models->textures)
                    flags.push_back(t->flags);
            }
            if (s.materialTextureBase != nativeTextureBase || flags != s.materialFlags) {
                s.materialFlags = std::move(flags);
                s.materialTextureBase = nativeTextureBase;
                ++s.materialGeneration;
            }
        }
        uint32_t counts[3]{}, texturedTriangles = 0;
        const auto originalPack = [&](auto& positions, auto& colors, auto& counts) {
            uint32_t texturedTriangles = 0;
            for (unsigned g = 0; g < 3; ++g)
                for (const auto& tri : triangles)
                    if (group(tri) == g) {
                        for (const auto& p : tri.p)
                            positions.push_back(sub(p, origin));
                        texturedTriangles += packAttributes(tri, textureCount, colors);
                        ++counts[g];
                    }
            return texturedTriangles;
        };
        const bool worldCache = job.worldCache;
        if (worldCache) {
            texturedTriangles = s.worldCache.append(triangles, origin, textureCount, positions, colors, counts);
            if (checkCache) {
                std::vector<Point> referencePositions;
                std::vector<Attribute> referenceColors;
                uint32_t referenceCounts[3]{};
                const auto referenceTextured = originalPack(referencePositions, referenceColors, referenceCounts);
                if (referenceTextured != texturedTriangles || std::memcmp(counts, referenceCounts, sizeof(counts)) ||
                    referencePositions.size() != positions.size() || referenceColors.size() != colors.size() ||
                    std::memcmp(referencePositions.data(), positions.data(), positions.size() * sizeof(Point)) ||
                    std::memcmp(referenceColors.data(), colors.data(), colors.size() * sizeof(colors[0])))
                    throw std::runtime_error("DXR world cache differs from original packing");
            }
        } else
            texturedTriangles = originalPack(positions, colors, counts);
        packing.reset();
        packing.emplace(s.threadProfile, CpuProfile::RayModelAttributes);
        std::vector<uint32_t> modelOffsets;
        std::set<uint64_t> usedMeshes, usedTopologies;
        if (models)
            modelOffsets.reserve(models->meshes.size());
        if (models)
            for (const auto& model : models->meshes) {
                modelOffsets.push_back(staticTotal + uint32_t(colors.size() / kTriangleAttributes));
                usedMeshes.insert(model->identity);
                usedTopologies.insert(model->topology);
                const auto buildAttributes = [&](std::vector<Attribute>& target) {
                    const auto corners = [&](size_t i, Point(&p)[3]) {
                        for (unsigned j = 0; j < 3; ++j) {
                            const auto& v = model->triangles[i].position[j];
                            p[j] = {v[0], v[1], v[2]};
                        }
                    };
                    CornerWelds unshared;
                    auto& welds = model->topology ? s.modelWelds[model->topology] : unshared;
                    if (welds.corners.size() != model->triangles.size() * 3)
                        weldCorners(model->triangles.size(), corners, welds);
                    std::vector<Point> normals;
                    smoothNormals(model->triangles.size(), corners, welds, normals);
                    return packModelAttributes(*model, *models, nativeTextureBase, normals, target);
                };
                if (cpuCache) {
                    auto& cached = s.nativeMeshes[model->identity];
                    if (cached.attributeSource != model || cached.attributeGeneration != s.materialGeneration) {
                        cached.attributes.clear();
                        cached.attributes.reserve(model->triangles.size() * kTriangleAttributes);
                        cached.texturedTriangles = buildAttributes(cached.attributes);
                        cached.attributeSource = model;
                        cached.attributeGeneration = s.materialGeneration;
                    }
                    if (checkCache) {
                        std::vector<Attribute> reference;
                        reference.reserve(model->triangles.size() * kTriangleAttributes);
                        const auto count = buildAttributes(reference);
                        if (count != cached.texturedTriangles || reference.size() != cached.attributes.size() ||
                            (!reference.empty() && std::memcmp(reference.data(), cached.attributes.data(),
                                                               reference.size() * sizeof(reference[0]))))
                            throw std::runtime_error("ray CPU material cache differs from uncached attributes");
                    }
                    colors.insert(colors.end(), cached.attributes.begin(), cached.attributes.end());
                    texturedTriangles += cached.texturedTriangles;
                } else
                    texturedTriangles += buildAttributes(colors);
            }
        if (cpuCache && checkCache && (++s.checkedFrames == 1 || s.checkedFrames % 60 == 0))
            std::fprintf(stderr,
                         "[ray-cpu-cache] frames=%llu world/materials compared byte-for-byte, zero mismatches; world "
                         "blocks rebuilt=%zu/%zu\n",
                         (unsigned long long)s.checkedFrames, s.worldCache.rebuiltBlocks(), s.worldCache.blockCount());
        texturedTriangles += s.staticTextured;
        uint32_t totalCounts[3];
        for (unsigned g = 0; g < 3; ++g)
            totalCounts[g] = counts[g] + s.staticCounts[g];
        packing.reset();
        // render() bypassed passes without water or shadows before queuing them.
        stage.reset();
        stage.emplace(s.threadProfile, CpuProfile::RayWait);
        auto& frame = s.acquire();
        stage.reset();
        stage.emplace(s.threadProfile, CpuProfile::RayUpload);
        // Materials live in GPU memory. Layout: [static world][frame triangles][native models] attributes, then
        // texture texels at a fixed offset. Only changed ranges are copied from this frame's upload buffer.
        constexpr uint32_t texelOffset = Impl::kTriangleBudget * kTriangleAttributes;
        const bool packedTextures = job.packedTextures;
        const size_t textureBytes =
            Texture::size * Texture::size * (packedTextures ? sizeof(uint32_t) : sizeof(Attribute));
        const uint64_t materialBytes = uint64_t(texelOffset) * sizeof(Attribute) + Impl::kTextureBudget * textureBytes;
        if (!s.materials) {
            // Allocated once: render() sends the texels of every texture slot with the first pass.
            s.buffer(s.materials, materialBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, frame.retired);
            s.staticUploaded = false;
        } else if (s.materials->GetDesc().Width < materialBytes)
            throw std::runtime_error("DXR material buffer layout changed");
        struct Copy {
            uint64_t destination, source, bytes;
        };
        std::vector<Copy> copies;
        uint64_t stagingBytes = 0;
        const auto addCopy = [&](uint64_t destination, uint64_t bytes) {
            if (bytes)
                copies.push_back({destination, stagingBytes, bytes});
            stagingBytes += bytes;
        };
        const bool uploadStatic = !s.staticUploaded && !s.staticAttributeData.empty();
        if (uploadStatic)
            addCopy(0, s.staticAttributeData.size() * sizeof(Attribute));
        addCopy(uint64_t(staticTotal) * kTriangleAttributes * sizeof(Attribute), colors.size() * sizeof(Attribute));
        for (const auto& upload : job.textureUploads)
            addCopy(uint64_t(texelOffset) * sizeof(Attribute) + upload.slot * textureBytes, textureBytes);
        if (stagingBytes) {
            s.ensureUpload(frame.staging, stagingBytes, frame.retired);
            auto* bytes = frame.staging.data;
            size_t at = 0;
            if (uploadStatic) {
                std::memcpy(bytes, s.staticAttributeData.data(), s.staticAttributeData.size() * sizeof(Attribute));
                at += s.staticAttributeData.size() * sizeof(Attribute);
            }
            if (!colors.empty())
                std::memcpy(bytes + at, colors.data(), colors.size() * sizeof(Attribute));
            at += colors.size() * sizeof(Attribute);
            for (const auto& texture : job.textureUploads) {
                if (packedTextures)
                    std::memcpy(bytes + at, texture.pixels.data(), textureBytes);
                else {
                    auto* pixels = reinterpret_cast<Attribute*>(bytes + at);
                    for (size_t p = 0; p < texture.pixels.size(); ++p) {
                        auto pixel = texture.pixels[p];
                        pixels[p] = {float(pixel & 255) / 255.f, float((pixel >> 8) & 255) / 255.f,
                                     float((pixel >> 16) & 255) / 255.f, float(pixel >> 24) / 255.f};
                    }
                }
                at += textureBytes;
            }
        }
        if (uploadStatic)
            s.staticUploaded = true;
        if (!positions.empty())
            s.upload(frame.vertices, positions.data(), positions.size() * sizeof(Point), frame.retired);
        if (!staticPositions.empty()) {
            if (s.staticVertices.resource)
                frame.retired.push_back(s.staticVertices.resource);
            s.staticVertices = {};
            s.upload(s.staticVertices, staticPositions.data(), staticPositions.size() * sizeof(Point), frame.retired);
        }
        Parameters params{};
        std::copy(camera.currentViewToClip, camera.currentViewToClip + 16, params.projection);
        std::copy(camera.clipToCameraView, camera.clipToCameraView + 16, params.inverse);
        std::copy(camera.cameraRight, camera.cameraRight + 3, params.right);
        std::copy(camera.cameraUp, camera.cameraUp + 3, params.up);
        std::copy(camera.cameraForward, camera.cameraForward + 3, params.forward);
        params.sizeTime[0] = float(d.Width);
        params.sizeTime[1] = float(d.Height);
        params.sizeTime[2] = seconds;
        params.sizeTime[3] = reflections ? 1.f : 0.f;
        params.jitter[0] = camera.jitterPixels[0];
        params.jitter[1] = camera.jitterPixels[1];
        params.limits[0] = camera.nearPlane;
        params.limits[1] = float(totalCounts[0]);
        params.limits[2] = origin.x; // world-anchored water waves, caustics and foam
        params.limits[3] = origin.z;
        params.scene[0] = texelOffset;
        params.scene[1] = uint32_t(textureCount);
        params.scene[2] = Texture::size;
        params.scene[3] = job.coverageDebug;
        params.effects[0] = options.water && (totalCounts[1] || totalCounts[2]);
        params.effects[1] = options.shadows;
        params.effects[3] = (totalCounts[2] ? 1u : 0u) | (totalCounts[1] ? 2u : 0u) |
                            (options.requireWaterCoverage ? 4u : 0u) | (packedTextures ? 16u : 0u);
        const uint64_t skyLevel = models ? models->level : 0;
        if (s.skyLevel != skyLevel) {
            params.effects[3] |= 8;
            s.skyLevel = skyLevel;
        }
        if (models) {
            for (unsigned j = 0; j < 3; ++j)
                params.ambient[j] = std::isfinite(models->lighting.ambient[j])
                                        ? std::clamp(models->lighting.ambient[j], 0.f, 16.f)
                                        : 0.f;
            for (const auto& light : models->lighting.lights) {
                if (params.effects[2] >= 4)
                    break;
                bool valid = light.type <= SceneLight::Point;
                float lengthSquared = 0;
                for (unsigned j = 0; j < 3; ++j) {
                    valid &= std::isfinite(light.vector[j]) && std::isfinite(light.color[j]) && light.color[j] >= 0 &&
                             light.color[j] <= 16;
                    lengthSquared += light.vector[j] * light.vector[j];
                }
                if (light.type == SceneLight::Directional)
                    valid &= std::isfinite(lengthSquared) && lengthSquared > 1e-12f;
                else
                    valid &= std::isfinite(light.radiusSquared) && light.radiusSquared > 0;
                if (!valid)
                    continue;
                unsigned i = params.effects[2]++;
                std::copy(light.vector, light.vector + 3, params.lightVector[i]);
                std::copy(light.color, light.color + 3, params.lightColor[i]);
                params.lightVector[i][3] = light.type ? 1.f : 0.f;
                params.lightColor[i][3] = light.radiusSquared;
                if (light.type) {
                    params.lightVector[i][0] -= origin.x;
                    params.lightVector[i][1] -= origin.y;
                    params.lightVector[i][2] -= origin.z;
                }
            }
        }
        if (!job.diagnosticDump.empty())
            if (FILE* file = std::fopen(job.diagnosticDump.c_str(), "wb")) {
                const uint32_t header[] = {0x52545831, uint32_t(sizeof(params))};
                std::fwrite(header, sizeof(header), 1, file);
                std::fwrite(&params, sizeof(params), 1, file);
                auto record = [&](const Point* p, uint32_t kind, uint32_t object, uint32_t material) {
                    std::fwrite(p, sizeof(Point), 3, file);
                    uint32_t tags[] = {kind, object, material};
                    std::fwrite(tags, sizeof(tags), 1, file);
                };
                for (const auto source : {staticTriangles, std::span<const Triangle>(triangles)})
                    for (const auto& tri : source) {
                        Point p[3];
                        for (unsigned j = 0; j < 3; ++j)
                            p[j] = sub(tri.p[j], origin);
                        record(p,
                               tri.water            ? (tri.shadowGeometry ? 4 : 3)
                               : tri.shadowGeometry ? 0
                                                    : 1,
                               0, tri.texture);
                    }
                if (models)
                    for (const auto& instance : models->instances) {
                        const auto& mesh = *models->meshes[instance.mesh];
                        const auto& m = instance.transform;
                        if (cameraShell(mesh, m, origin))
                            continue;
                        for (const auto& tri : mesh.triangles) {
                            Point p[3];
                            for (unsigned j = 0; j < 3; ++j) {
                                const auto& v = tri.position[j];
                                p[j] = {m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3] - origin.x,
                                        m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7] - origin.y,
                                        m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11] - origin.z};
                            }
                            record(p, 2, instance.object,
                                   tri.material < models->textures.size() ? models->textures[tri.material]->nativeId
                                                                          : ~0u);
                        }
                    }
                std::fclose(file);
            }
        s.upload(frame.constants, &params, sizeof(params), frame.retired);
        stage.reset();
        stage.emplace(s.threadProfile, CpuProfile::RayBuild);
        s.interop->wait12(s.queue.Get(), job.produced); // the D3D11 input copies precede this pass on the GPU
        check(frame.allocator->Reset(), "DXR allocator reset");
        check(s.list->Reset(frame.allocator.Get(), nullptr), "DXR list reset");
        s.buffer(s.sky, 7 * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, frame.retired,
                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        for (const auto& c : copies)
            s.list->CopyBufferRegion(s.materials.Get(), c.destination, frame.staging.Get(), c.source, c.bytes);
        if (!copies.empty()) {
            const auto ready = transition(s.materials.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            s.list->ResourceBarrier(1, &ready);
        }
        if (!staticPositions.empty()) {
            uint64_t offset = 0;
            for (unsigned g = 0; g < 3; ++g) {
                if (s.staticCounts[g])
                    s.buildBlas(
                        s.staticBlas[g], s.staticVertices->GetGPUVirtualAddress() + offset, s.staticCounts[g] * 3,
                        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE, frame.retired, true);
                else if (s.staticBlas[g]) {
                    frame.retired.push_back(s.staticBlas[g]);
                    s.staticBlas[g].Reset();
                }
                offset += uint64_t(s.staticCounts[g]) * 3 * sizeof(Point);
            }
        }
        {
            uint64_t offset = 0;
            for (unsigned g = 0; g < 3; ++g) {
                if (counts[g])
                    s.buildBlas(s.blas[g], frame.vertices->GetGPUVirtualAddress() + offset, counts[g] * 3,
                                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD, frame.retired,
                                false);
                offset += uint64_t(counts[g]) * 3 * sizeof(Point);
            }
        }
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> temporaryInstances;
        auto& instances = cpuCache ? s.cpuInstances : temporaryInstances;
        instances.clear();
        instances.reserve(6 + (models ? models->instances.size() : 0));
        {
            // Static world first (translated from its base to the camera), then this frame's triangles.
            const auto translate = sub(s.staticBase, origin);
            uint32_t materialBase = 0;
            for (unsigned g = 0; g < 3; ++g) {
                if (s.staticCounts[g]) {
                    D3D12_RAYTRACING_INSTANCE_DESC i{};
                    i.Transform[0][0] = i.Transform[1][1] = i.Transform[2][2] = 1;
                    i.Transform[0][3] = translate.x;
                    i.Transform[1][3] = translate.y;
                    i.Transform[2][3] = translate.z;
                    i.InstanceID = (31u << 19) | materialBase;
                    i.InstanceMask = 1u << g;
                    i.AccelerationStructure = s.staticBlas[g]->GetGPUVirtualAddress();
                    instances.push_back(i);
                }
                materialBase += s.staticCounts[g];
            }
            for (unsigned g = 0; g < 3; ++g) {
                if (counts[g]) {
                    D3D12_RAYTRACING_INSTANCE_DESC i{};
                    i.Transform[0][0] = i.Transform[1][1] = i.Transform[2][2] = 1;
                    i.InstanceID = (31u << 19) | materialBase;
                    i.InstanceMask = 1u << g;
                    i.AccelerationStructure = s.blas[g]->GetGPUVirtualAddress();
                    instances.push_back(i);
                }
                materialBase += counts[g];
            }
        }
        if (models) {
            // New poses/meshes: positions go to this frame's upload buffer (a built BLAS no longer reads them).
            struct Build {
                NativeMesh* mesh;
                Deformable* slot;
                size_t first;
                uint32_t count;
            };
            std::vector<Build> builds;
            ++s.modelPass;
            s.modelPositions.clear();
            for (const auto& model : models->meshes) {
                auto& cached = s.nativeMeshes[model->identity];
                if (cached.built || model->triangles.empty())
                    continue;
                const size_t first = s.modelPositions.size();
                for (const auto& t : model->triangles)
                    for (const auto& p : t.position)
                        s.modelPositions.push_back({p[0], p[1], p[2]});
                const auto count = uint32_t(s.modelPositions.size() - first);
                Deformable* slot = nullptr;
                if (model->topology) {
                    auto& slots = s.deformables[model->topology];
                    for (auto& d : slots)
                        if (d.vertexCount == count && d.used != s.modelPass && !usedMeshes.contains(d.identity)) {
                            slot = &d;
                            break;
                        }
                    if (!slot) {
                        slot = &slots.emplace_back();
                        slot->vertexCount = count;
                    }
                    slot->used = s.modelPass;
                    slot->identity = model->identity;
                }
                builds.push_back({&cached, slot, first, count});
                cached.built = true;
            }
            if (!builds.empty()) {
                s.upload(frame.models, s.modelPositions.data(), s.modelPositions.size() * sizeof(Point), frame.retired);
                for (const auto& b : builds) {
                    const auto vertices = frame.models->GetGPUVirtualAddress() + b.first * sizeof(Point);
                    if (!b.slot) {
                        s.buildBlas(b.mesh->blas, vertices, b.count,
                                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE, frame.retired,
                                    true);
                        continue;
                    }
                    Deformable& d = *b.slot;
                    const bool refit = d.blas && d.updates < Impl::kRefits;
                    s.buildBlas(d.blas, vertices, b.count,
                                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
                                frame.retired, false, refit);
                    d.updates = refit ? d.updates + 1 : 0;
                    s.modelRefits += refit;
                    b.mesh->blas = d.blas;
                }
            }
            for (const auto& source : models->instances) {
                if (source.mesh >= models->meshes.size())
                    throw std::runtime_error("DXR invalid native model instance");
                const auto& model = models->meshes[source.mesh];
                if (model->triangles.empty())
                    continue;
                // Zero-opacity props must not become primary shadow receivers
                // or consume the bounded transparent reflection traversal.
                if (!std::isfinite(source.opacity))
                    throw std::runtime_error("DXR invalid native model opacity");
                const auto opacity = uint32_t(std::clamp(source.opacity, 0.f, 1.f) * 31 + .5f);
                if (!opacity || cameraShell(*model, source.transform, origin))
                    continue;
                D3D12_RAYTRACING_INSTANCE_DESC i{};
                std::memcpy(i.Transform, source.transform, sizeof(i.Transform));
                i.Transform[0][3] -= origin.x;
                i.Transform[1][3] -= origin.y;
                i.Transform[2][3] -= origin.z;
                i.InstanceID = modelOffsets[source.mesh] | (opacity << 19);
                i.InstanceMask = 1;
                i.AccelerationStructure = s.nativeMeshes.at(model->identity).blas->GetGPUVirtualAddress();
                instances.push_back(i);
            }
        }
        const unsigned instanceCount = unsigned(instances.size());
        s.upload(frame.instances, instances.data(), instances.size() * sizeof(instances[0]), frame.retired);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        in.NumDescs = instanceCount;
        in.InstanceDescs = frame.instances->GetGPUVirtualAddress();
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};
        s.device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &size);
        s.buffer(s.tlas, size.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, frame.retired,
                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        s.buffer(s.scratch, size.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 frame.retired, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = in;
        build.DestAccelerationStructureData = s.tlas->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = s.scratch->GetGPUVirtualAddress();
        s.list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        s.barrier(s.tlas.Get());
        D3D12_RESOURCE_BARRIER before[] = {
            transition(s.interop->texture(0), D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(s.interop->texture(3), D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(s.interop->texture(1), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
        s.list->ResourceBarrier(3, before);
        auto cpu = frame.heap->GetCPUDescriptorHandleForHeapStart();
        const auto stride = s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        s.device->CreateShaderResourceView(s.interop->texture(0), &srv, cpu);
        cpu.ptr += stride;
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        s.device->CreateShaderResourceView(s.interop->texture(3), &srv, cpu);
        cpu.ptr += stride;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        s.device->CreateUnorderedAccessView(s.interop->texture(1), nullptr, &uav, cpu);
        ID3D12DescriptorHeap* heaps[] = {frame.heap.Get()};
        s.list->SetDescriptorHeaps(1, heaps);
        s.list->SetComputeRootSignature(s.root.Get());
        s.list->SetPipelineState(s.pipeline.Get());
        s.list->SetComputeRootConstantBufferView(0, frame.constants->GetGPUVirtualAddress());
        s.list->SetComputeRootShaderResourceView(1, s.tlas->GetGPUVirtualAddress());
        s.list->SetComputeRootShaderResourceView(2, s.materials->GetGPUVirtualAddress());
        s.list->SetComputeRootDescriptorTable(3, frame.heap->GetGPUDescriptorHandleForHeapStart());
        s.list->SetComputeRootUnorderedAccessView(4, s.sky->GetGPUVirtualAddress());
        s.list->SetPipelineState(s.skyPipeline.Get());
        s.list->Dispatch(1, 1, 1);
        s.barrier(s.sky.Get());
        s.list->SetPipelineState(s.pipeline.Get());
        s.list->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
        for (auto& b : before)
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        s.list->ResourceBarrier(3, before);
        check(s.list->Close(), "DXR list close");
        ID3D12CommandList* lists[] = {s.list.Get()};
        s.queue->ExecuteCommandLists(1, lists);
        s.submitted(frame);
        // The D3D11 context waits for this value on the GPU before it copies the output; the CPU continues.
        s.interop->signal12(s.queue.Get(), job.consumed);
        job.consumedSignaled = true;
        if (s.debug) {
            bool errors = false;
            const UINT64 messages = s.debug->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < messages; ++i) {
                SIZE_T bytes = 0;
                s.debug->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(s.debug->GetMessage(i, message, &bytes)) &&
                    message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "[water-d3d12-error] %s\n", message->pDescription);
                    errors = true;
                }
            }
            s.debug->ClearStoredMessages();
            if (errors)
                throw std::runtime_error("DXR debug validation reported an error");
        }
        for (auto it = s.nativeMeshes.begin(); it != s.nativeMeshes.end();)
            if (!usedMeshes.contains(it->first)) {
                if (it->second.blas)
                    frame.retired.push_back(it->second.blas);
                it = s.nativeMeshes.erase(it);
            } else
                ++it;
        std::erase_if(s.modelWelds, [&](const auto& welds) { return !usedTopologies.contains(welds.first); });
        for (auto it = s.deformables.begin(); it != s.deformables.end();) {
            auto& slots = it->second;
            for (auto d = slots.begin(); d != slots.end();)
                if (d->used + Impl::kDeformableIdle < s.modelPass) {
                    if (d->blas)
                        frame.retired.push_back(d->blas);
                    d = slots.erase(d);
                } else
                    ++d;
            it = slots.empty() ? s.deformables.erase(it) : std::next(it);
        }
        s.setStatus("DXR scene evaluated: " + std::to_string(totalCounts[0]) + " opaque (" +
                    std::to_string(s.staticCounts[0]) + " resident) / " + std::to_string(totalCounts[2]) +
                    " native water / " + std::to_string(totalCounts[1]) + " water coverage triangles; " +
                    std::to_string(texturedTriangles) + " textured / " + std::to_string(textureCount) +
                    " material textures; " + std::to_string(models ? models->instances.size() : 0) +
                    " resident model instances; " + std::to_string(s.modelRefits) + " model BLAS refits; shadows=" +
                    (options.shadows ? (params.effects[2] ? std::to_string(params.effects[2]) + " native lights"
                                                          : "waiting for native lights")
                                     : "off"));
    }
}
} // namespace gsr::surface
#else
namespace gsr::surface {
struct RayWater::Impl {
    std::string status = "DXR water unavailable in this build (requires DXC and the D3D12 interop module)";
};
RayWater::RayWater(ID3D11Device*, ID3D11DeviceContext*) : impl(std::make_unique<Impl>()) {}
RayWater::~RayWater() = default;
bool RayWater::ready() const {
    return false;
}
std::string RayWater::status() const {
    return impl->status;
}
void RayWater::finish() {}
void RayWater::setStaticWorld(std::span<const Triangle>, uint64_t, uint64_t) {}
void RayWater::shareStaticWorld(std::shared_ptr<const std::vector<Triangle>>, uint64_t, uint64_t) {}
bool RayWater::submit(ID3D11Texture2D*, ID3D11Texture2D*, ID3D11Texture2D*, const nr::guides::FrameConstants&,
                      std::vector<Triangle>&, float, bool, std::span<const Texture>, std::shared_ptr<const SceneModels>,
                      const RayOptions&) {
    return false;
}
bool RayWater::render(ID3D11Texture2D*, ID3D11Texture2D*, ID3D11Texture2D*, const nr::guides::FrameConstants&,
                      std::span<const Triangle>, float, bool, std::span<const Texture>, const SceneModels*,
                      const RayOptions&) {
    return false;
}
} // namespace gsr::surface
#endif
