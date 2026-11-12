// The generated WGSL of the batch path (src/vertex_program_wgsl.h: vertexBatchWgsl; the integer library, the pass
// bodies and the loop from the one description) against gsr/vertex_batch.h, the CPU evaluation proven against the
// microprogram, bit for bit: the WebGPU twin of tests/test_vertex_batch_program.cpp, on the same vertices
// (tests/vertex_batch_cases.h). A compute pass evaluates every vertex and writes the packet, the position of the
// previous frame's inputs and the row after the chain to a storage buffer.
//
// DRAKAN_GPU_BACKEND=vulkan | dx12 picks the graphics API under WebGPU. Exit 77 = no adapter (or no compute).
// GSR_VB_RECORDS=<file>[;<file>...] adds dumped records. GSR_VB_BENCH=<repeats> times chains of 1, 3, 7 and 13
// passes (shader invocations per millisecond, compute only).
#include "vertex_batch_cases.h"
#include "vertex_program_wgsl.h"

#include <gpu/Gpu.h>

#include <algorithm>
#include <chrono>
#include <string>

using namespace gsr;
using drakan::gpu::view;

namespace {
// The vertex is a vbcases::GpuVertex: the row, sel, and the previous frame's position row and sel.
const char* kBindings = R"WGSL(
struct VbVertex {
    pos: vec4u,
    col: vec4u,
    st: vec4u,
    sel: vec4u,
    ppos: vec4u,
    psel: vec4u,
}
@group(0) @binding(0) var<storage, read> vbVertices: array<VbVertex>;
@group(0) @binding(1) var<storage, read> vpConstants: array<vec4u>;
struct VbResult {
    now: VpRaw,
    previous: vec4u, // x, y, z of the previous frame's inputs (the motion-vector twin)
    row: VbRow,      // the row after the chain
}
@group(0) @binding(2) var<storage, read_write> vbResults: array<VbResult>;
fn vpConstant(row: u32) -> vec4u { return vpConstants[row]; }
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&vbVertices)) {
        let v = vbVertices[id.x];
        let row = vbChain(VbRow(v.pos, v.col, v.st), v.sel);
        let previous = vbEvaluatePrevious(VbRow(v.ppos, vec4u(), vec4u()), v.psel).place;
        vbResults[id.x] = VbResult(vbFinish(row), previous, row);
    }
}
)WGSL";
// What a draw computes: the packet alone.
const char* kBenchEntry = R"WGSL(
@compute @workgroup_size(64) fn bench(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&vbVertices)) {
        let v = vbVertices[id.x];
        vbResults[id.x].now = vbEvaluate(VbRow(v.pos, v.col, v.st), v.sel);
    }
}
)WGSL";

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr uint64_t kVertexBytes = sizeof(vbcases::GpuVertex), kResultBytes = vbcases::kResultWords * 4u;
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
    if (limits.maxStorageBuffersPerShaderStage < 3 || limits.maxStorageBufferBindingSize < kChunk * kResultBytes) {
        std::fprintf(stderr, "SKIP: no compute with three storage buffers on %s\n", context->adapterName().c_str());
        return 77;
    }
    std::printf("adapter: %s (%s%s)\n", context->adapterName().c_str(), drakan::gpu::backendName(context->backend()),
                context->fxcShaderCompiler() ? ", shaders through FXC" : "");
    WGPUDevice device = context->device();

    const std::string source = vertexBatchWgsl(true) + kBindings + kBenchEntry;
    const auto makePipeline = [&](WGPUShaderModule module, const char* entry) -> WGPUComputePipeline {
        auto desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = module;
        desc.compute.entryPoint = view(entry);
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &desc);
        context->poll(false);
        return context->failed() ? nullptr : pipeline;
    };
    const double compileStart = seconds();
    WGPUShaderModule module = context->shaderModule(source, "batch stage");
    if (!module) {
        std::printf("[FAIL] the generated WGSL does not compile: %s\n", context->lastError().c_str());
        return 1;
    }
    const double moduleDone = seconds();
    WGPUComputePipeline pipeline = makePipeline(module, "main");
    if (!pipeline) {
        std::printf("[FAIL] compute pipeline: %s\n", context->lastError().c_str());
        return 1;
    }
    const double pipelineDone = seconds();
    WGPUComputePipeline benchPipeline = makePipeline(module, "bench");
    if (!benchPipeline) {
        std::printf("[FAIL] compute pipeline of the packet alone: %s\n", context->lastError().c_str());
        return 1;
    }
    std::printf("generated WGSL: %zu characters; module created in %.0f ms, the test's pipeline (packet, previous "
                "position, row) in %.0f ms, the pipeline of the packet alone in %.0f ms\n",
                source.size(), (moduleDone - compileStart) * 1000.0, (pipelineDone - moduleDone) * 1000.0,
                (seconds() - pipelineDone) * 1000.0);

    const vbcases::Cases cases = vbcases::standardCases();
    vbcases::describe(cases);
    const vbcases::Reference ref = vbcases::reference(cases);

    const auto makeBuffer = [&](uint64_t bytes, WGPUBufferUsage usage) {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = usage;
        return wgpuDeviceCreateBuffer(device, &desc);
    };
    WGPUBuffer vertices = makeBuffer(kChunk * kVertexBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer results = makeBuffer(kChunk * kResultBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer staging = makeBuffer(kChunk * kResultBytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    // The constants table of a set of cases (every pass as it lies in a record, thirteen rows each) and the bind
    // group of a pipeline over it.
    const auto makeTable = [&](const vbcases::Cases& c) -> WGPUBuffer {
        const uint64_t bytes = c.passes.size() * sizeof(vp::Pass);
        if (limits.maxStorageBufferBindingSize < bytes)
            return nullptr;
        WGPUBuffer table = makeBuffer(bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(context->queue(), table, 0, c.passes.data(), bytes);
        return table;
    };
    const auto makeBindGroup = [&](WGPUComputePipeline of, WGPUBuffer table, uint64_t tableBytes) {
        WGPUBindGroupEntry entries[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                         WGPU_BIND_GROUP_ENTRY_INIT};
        WGPUBuffer bound[3] = {vertices, table, results};
        const uint64_t boundBytes[3] = {kChunk * kVertexBytes, tableBytes, kChunk * kResultBytes};
        for (uint32_t i = 0; i < 3; ++i) {
            entries[i].binding = i;
            entries[i].buffer = bound[i];
            entries[i].size = boundBytes[i];
        }
        auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = wgpuComputePipelineGetBindGroupLayout(of, 0);
        desc.entryCount = 3;
        desc.entries = entries;
        return wgpuDeviceCreateBindGroup(device, &desc);
    };
    WGPUBuffer table = makeTable(cases);
    if (!table) {
        std::fprintf(stderr, "SKIP: the pass table does not fit a storage buffer binding here\n");
        return 77;
    }
    WGPUBindGroup bindGroup = makeBindGroup(pipeline, table, cases.passes.size() * sizeof(vp::Pass));

    // One dispatch over the vertex buffer; with `readBytes` that much of the results is copied out.
    const auto dispatch = [&](WGPUComputePipeline with, WGPUBindGroup group, uint32_t count, uint64_t readBytes) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, with);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (count + 63u) / 64u, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (readBytes)
            wgpuCommandEncoderCopyBufferToBuffer(encoder, results, 0, staging, 0, readBytes);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(context->queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    };
    // Maps `bytes` of the staging buffer (waits for everything submitted); null after a failure.
    const auto mapStaging = [&](uint64_t bytes) -> const void* {
        int state = 0; // 1 = mapped, 2 = failed
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &state;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
            *static_cast<int*>(user) = status == WGPUMapAsyncStatus_Success ? 1 : 2;
        };
        wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, bytes, callback);
        while (state == 0 && !context->failed())
            context->poll(true);
        return state == 1 ? wgpuBufferGetConstMappedRange(staging, 0, bytes) : nullptr;
    };

    std::vector<uint32_t> got(cases.vertices.size() * vbcases::kResultWords);
    const double runStart = seconds();
    for (size_t first = 0; first < cases.vertices.size(); first += kChunk) {
        const uint32_t count = static_cast<uint32_t>(std::min<size_t>(kChunk, cases.vertices.size() - first));
        wgpuQueueWriteBuffer(context->queue(), vertices, 0, cases.vertices.data() + first, count * kVertexBytes);
        dispatch(pipeline, bindGroup, count, count * kResultBytes);
        const void* data = mapStaging(count * kResultBytes);
        if (!data) {
            std::printf("[FAIL] read back: %s\n", context->lastError().c_str());
            return 1;
        }
        std::memcpy(got.data() + first * vbcases::kResultWords, data, size_t(count) * kResultBytes);
        wgpuBufferUnmap(staging);
    }
    const uint64_t bad = vbcases::compareShader("WGSL", cases, ref, got.data());
    std::printf("generated WGSL of the batch path (integer library) against vp::evaluateVertex (the packet, the "
                "position of the previous frame's inputs, the row after the chain) on %s: %zu vertices, %llu differ "
                "(%.0f ms)\n",
                drakan::gpu::backendName(context->backend()), cases.vertices.size(), (unsigned long long)bad,
                (seconds() - runStart) * 1000.0);

    if (const char* text = std::getenv("GSR_VB_BENCH")) {
        const int repeats = std::max(3, std::atoi(text));
        constexpr int kDispatches = 8;
        for (const uint32_t passes : {1u, 3u, 7u, 13u}) {
            const vbcases::Cases benchCases = vbcases::benchCases(passes, kChunk);
            WGPUBuffer benchTable = makeTable(benchCases);
            WGPUBindGroup benchGroup =
                makeBindGroup(benchPipeline, benchTable, benchCases.passes.size() * sizeof(vp::Pass));
            wgpuQueueWriteBuffer(context->queue(), vertices, 0, benchCases.vertices.data(), kChunk * kVertexBytes);
            // A run ends with a mapped read of a result, so that the time covers the finished work.
            const auto run = [&] {
                for (int d = 0; d < kDispatches; ++d)
                    dispatch(benchPipeline, benchGroup, kChunk, d + 1 == kDispatches ? kResultBytes : 0);
                if (mapStaging(kResultBytes))
                    wgpuBufferUnmap(staging);
            };
            run();
            std::vector<double> rates;
            for (int r = 0; r < repeats; ++r) {
                const double start = seconds();
                run();
                rates.push_back(double(kChunk) * kDispatches / ((seconds() - start) * 1000.0));
            }
            std::sort(rates.begin(), rates.end());
            std::printf("WGSL integer library on %s, packet: chain of %2u passes, %.0f invocations per ms (median of "
                        "%d runs of %d x %u; lowest %.0f, highest %.0f)\n",
                        drakan::gpu::backendName(context->backend()), passes, rates[rates.size() / 2], repeats,
                        kDispatches, kChunk, rates.front(), rates.back());
            wgpuBindGroupRelease(benchGroup);
            wgpuBufferRelease(benchTable);
        }
    }
    if (context->failed()) {
        std::printf("[FAIL] device error: %s\n", context->lastError().c_str());
        return 1;
    }
    return bad ? 1 : 0;
}
