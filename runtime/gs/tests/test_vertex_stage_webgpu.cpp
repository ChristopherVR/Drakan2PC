// The generated WGSL of the vertex stages (src/vertex_program_wgsl.h: the integer library and the stage bodies from
// the one description) against the CPU evaluation it must equal bit for bit: the WebGPU twin of
// tests/test_vertex_program.cpp, on the same vertices (tests/vertex_program_cases.h). A compute pass evaluates every
// vertex and writes the twelve integers of its packet to a storage buffer.
//
// DRAKAN_GPU_BACKEND=vulkan | dx12 picks the graphics API under WebGPU. Exit 77 = no adapter (or no compute).
// GSR_VP_BENCH=<repeats> also times the lit strip program (shader invocations per millisecond, compute only).
#include "vertex_program_cases.h"
#include "vertex_program_wgsl.h"

#include <gpu/Gpu.h>

#include <algorithm>
#include <chrono>
#include <string>

using namespace gsr;
using drakan::gpu::view;

namespace {
const char* kBindings = R"WGSL(
@group(0) @binding(0) var<storage, read> vpVertices: array<VpIn>;
@group(0) @binding(1) var<storage, read> vpConstants: array<vec4u>;
struct VpResult {
    now: VpRaw,
    previous: vec4u, // x, y, z of the same vertex with the constants sel.z names (the motion-vector twin)
}
@group(0) @binding(2) var<storage, read_write> vpResults: array<VpResult>;
fn vpConstant(row: u32) -> vec4u { return vpConstants[row]; }
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&vpVertices)) {
        let v = vpVertices[id.x];
        vpResults[id.x] = VpResult(vpEvaluate(v), vpEvaluateAtPrevious(v, v.sel.z).place);
    }
}
)WGSL";

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

int main() {
    drakan::gpu::ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = drakan::gpu::Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return 77;
    }
    const WGPULimits limits = context->limits();
    constexpr uint32_t kChunk = 262144; // vertices per dispatch
    if (limits.maxStorageBuffersPerShaderStage < 3 || limits.maxStorageBufferBindingSize < kChunk * 64ull) {
        std::fprintf(stderr, "SKIP: no compute with three storage buffers on %s\n", context->adapterName().c_str());
        return 77;
    }
    std::printf("adapter: %s (%s%s)\n", context->adapterName().c_str(), drakan::gpu::backendName(context->backend()),
                context->fxcShaderCompiler() ? ", shaders through FXC" : "");
    WGPUDevice device = context->device();

    const std::string source = vertexProgramWgsl(vpcases::testStages(), true) + kBindings;
    const double compileStart = seconds();
    WGPUShaderModule module = context->shaderModule(source, "vertex stages");
    if (!module) {
        std::printf("[FAIL] the generated WGSL does not compile: %s\n", context->lastError().c_str());
        return 1;
    }
    auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = view("main");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    context->poll(false);
    if (!pipeline || context->failed()) {
        std::printf("[FAIL] compute pipeline: %s\n", context->lastError().c_str());
        return 1;
    }
    std::printf("generated WGSL: %zu characters; module and pipeline created in %.0f ms\n", source.size(),
                (seconds() - compileStart) * 1000.0);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    const vpcases::StandardCases standard = vpcases::standardCases();
    const vpcases::Cases& cases = standard.cases;
    const std::vector<vp::ir::Raw> want = vpcases::reference(cases);
    const std::vector<vpcases::PreviousPlace> previous = vpcases::referencePrevious(cases);

    const auto makeBuffer = [&](uint64_t bytes, WGPUBufferUsage usage) {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = usage;
        return wgpuDeviceCreateBuffer(device, &desc);
    };
    const uint64_t constantBytes = cases.constants.size() * sizeof(vp::Constants);
    if (limits.maxStorageBufferBindingSize < constantBytes) {
        std::fprintf(stderr, "SKIP: the constants table does not fit a storage buffer binding here\n");
        return 77;
    }
    WGPUBuffer vertices = makeBuffer(kChunk * 64ull, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer constants = makeBuffer(constantBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer results = makeBuffer(kChunk * 64ull, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer staging = makeBuffer(kChunk * 64ull, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(context->queue(), constants, 0, cases.constants.data(), constantBytes);
    WGPUBindGroupEntry entries[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT};
    WGPUBuffer bound[3] = {vertices, constants, results};
    const uint64_t boundBytes[3] = {kChunk * 64ull, constantBytes, kChunk * 64ull};
    for (uint32_t i = 0; i < 3; ++i) {
        entries[i].binding = i;
        entries[i].buffer = bound[i];
        entries[i].size = boundBytes[i];
    }
    auto bindDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindDesc.layout = layout;
    bindDesc.entryCount = 3;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // One dispatch over the vertex buffer; with `read` the results are copied out and mapped.
    const auto dispatch = [&](uint32_t count, bool read) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (count + 63u) / 64u, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (read)
            wgpuCommandEncoderCopyBufferToBuffer(encoder, results, 0, staging, 0, uint64_t(count) * 64u);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(context->queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    };

    std::vector<uint32_t> got(cases.vertices.size() * 16);
    for (size_t first = 0; first < cases.vertices.size(); first += kChunk) {
        const uint32_t count = static_cast<uint32_t>(std::min<size_t>(kChunk, cases.vertices.size() - first));
        wgpuQueueWriteBuffer(context->queue(), vertices, 0, cases.vertices.data() + first, uint64_t(count) * 64u);
        dispatch(count, true);
        int state = 0; // 1 = mapped, 2 = failed
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &state;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
            *static_cast<int*>(user) = status == WGPUMapAsyncStatus_Success ? 1 : 2;
        };
        wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, uint64_t(count) * 64u, callback);
        while (state == 0 && !context->failed())
            context->poll(true);
        const void* data = state == 1 ? wgpuBufferGetConstMappedRange(staging, 0, uint64_t(count) * 64u) : nullptr;
        if (!data) {
            std::printf("[FAIL] read back: %s\n", context->lastError().c_str());
            return 1;
        }
        std::memcpy(got.data() + first * 16, data, size_t(count) * 64u);
        wgpuBufferUnmap(staging);
    }
    const uint64_t bad = vpcases::compareShader("WGSL", cases, want, previous, got.data());
    std::printf("generated WGSL (integer library) against the CPU evaluation (the packet, and the position again with "
                "the previous frame's constants) on %s: %zu vertices (%zu generated, "
                "%zu probes, %zu recorded), %llu differ\n",
                drakan::gpu::backendName(context->backend()), cases.vertices.size(), standard.generated,
                standard.probes, standard.recorded, (unsigned long long)bad);

    if (const char* text = std::getenv("GSR_VP_BENCH")) {
        std::vector<vpcases::GpuVertex> lit;
        for (const vpcases::GpuVertex& v : cases.vertices)
            if (v.program == vp::StripsLit && lit.size() < kChunk)
                lit.push_back(v);
        while (lit.size() < kChunk)
            lit.push_back(lit[lit.size() % 1000]);
        wgpuQueueWriteBuffer(context->queue(), vertices, 0, lit.data(), kChunk * 64ull);
        // A run ends with a mapped read of one result, so that the time covers the finished work.
        const auto finish = [&] {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, results, uint64_t(kChunk - 1) * 64u, staging, 0, 64);
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
            wgpuQueueSubmit(context->queue(), 1, &commands);
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            int state = 0;
            auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.userdata1 = &state;
            callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
                *static_cast<int*>(user) = status == WGPUMapAsyncStatus_Success ? 1 : 2;
            };
            wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, 64, callback);
            while (state == 0 && !context->failed())
                context->poll(true);
            if (state == 1)
                wgpuBufferUnmap(staging);
        };
        dispatch(kChunk, false);
        finish();
        std::vector<double> rates;
        const int repeats = std::max(3, std::atoi(text));
        constexpr int kDispatches = 8;
        for (int r = 0; r < repeats; ++r) {
            const double start = seconds();
            for (int d = 0; d < kDispatches; ++d)
                dispatch(kChunk, false);
            finish();
            rates.push_back(double(kChunk) * kDispatches / ((seconds() - start) * 1000.0));
        }
        std::sort(rates.begin(), rates.end());
        std::printf("lit strip program, WGSL integer library on %s: %.0f invocations per ms (median of %d runs of "
                    "%d x %u; lowest %.0f, highest %.0f)\n",
                    drakan::gpu::backendName(context->backend()), rates[rates.size() / 2], repeats, kDispatches, kChunk,
                    rates.front(), rates.back());
    }
    if (context->failed()) {
        std::printf("[FAIL] device error: %s\n", context->lastError().c_str());
        return 1;
    }
    return bad ? 1 : 0;
}
