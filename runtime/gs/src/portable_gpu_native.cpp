#include "portable_gpu.h"
#include <webgpu/wgpu.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace gsr::gpu {
namespace {
WGPUStringView sv(const char* s) {
    return {s, std::strlen(s)};
}
class NativeCompute final : public Compute {
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    WGPUBindGroupLayout layout_ = nullptr;
    std::array<WGPUBuffer, 4> buffers_{};
    std::array<size_t, 4> sizes_{};
    WGPUBuffer staging_ = nullptr;
    size_t stagingSize_ = 0;
    std::atomic<bool> failed_{false};
    std::string name_;

public:
    bool init() {
        WGPUInstanceExtras extras{};
        extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
        extras.backends = WGPUInstanceBackend_All;
        if (const char* backend = std::getenv("DRAKAN_GPU_BACKEND"); backend && std::strcmp(backend, "gl") == 0)
            extras.backends = WGPUInstanceBackend_GL;
        auto instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
        instanceDesc.nextInChain = &extras.chain;
        instance_ = wgpuCreateInstance(&instanceDesc);
        if (!instance_)
            return false;
        WGPUInstanceEnumerateAdapterOptions enumerate{};
        enumerate.backends = extras.backends;
        std::vector<WGPUAdapter> adapters(wgpuInstanceEnumerateAdapters(instance_, &enumerate, nullptr));
        wgpuInstanceEnumerateAdapters(instance_, &enumerate, adapters.data());
        unsigned best = 101;
        for (auto candidate : adapters) {
            auto candidateInfo = WGPU_ADAPTER_INFO_INIT;
            if (wgpuAdapterGetInfo(candidate, &candidateInfo) == WGPUStatus_Success) {
                const unsigned rank =
                    candidateInfo.adapterType == WGPUAdapterType_CPU ? 100u : unsigned(candidateInfo.adapterType);
                std::fprintf(stderr, "[portable-gpu] candidate=%.*s type=%u backend=%u\n",
                             int(candidateInfo.device.length), candidateInfo.device.data,
                             unsigned(candidateInfo.adapterType), unsigned(candidateInfo.backendType));
                if (rank < best) {
                    if (adapter_)
                        wgpuAdapterRelease(adapter_);
                    adapter_ = candidate;
                    candidate = nullptr;
                    best = rank;
                }
                wgpuAdapterInfoFreeMembers(candidateInfo);
            }
            if (candidate)
                wgpuAdapterRelease(candidate);
        }
        if (!adapter_)
            return false;
        auto info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter_, &info) == WGPUStatus_Success) {
            name_.assign(info.device.data, info.device.length);
            if (info.adapterType == WGPUAdapterType_CPU)
                name_ += " (software adapter)";
            wgpuAdapterInfoFreeMembers(info);
        }
        auto desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.uncapturedErrorCallbackInfo.userdata1 = this;
        desc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView msg, void* p,
                                                       void*) {
            static_cast<NativeCompute*>(p)->failed_ = true;
            std::fprintf(stderr, "[portable-gpu] %.*s\n", int(msg.length), msg.data);
        };
        auto deviceInfo = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        deviceInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceInfo.userdata1 = &device_;
        deviceInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView, void* p, void*) {
            if (status == WGPURequestDeviceStatus_Success)
                *static_cast<WGPUDevice*>(p) = d;
        };
        wgpuAdapterRequestDevice(adapter_, &desc, deviceInfo);
        if (!device_)
            return false;
        queue_ = wgpuDeviceGetQueue(device_);
        auto source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = sv(rasterShader());
        auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        shaderDesc.nextInChain = &source.chain;
        auto shader = wgpuDeviceCreateShaderModule(device_, &shaderDesc);
        if (failed_) {
            if (shader)
                wgpuShaderModuleRelease(shader);
            return false;
        }
        auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = sv("main");
        pipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
        wgpuShaderModuleRelease(shader);
        if (failed_ || !pipeline_)
            return false;
        layout_ = wgpuComputePipelineGetBindGroupLayout(pipeline_, 0);
        std::fprintf(stderr, "[portable-gpu] adapter=%s\n", name_.c_str());
        return layout_ && !failed_;
    }
    bool hasAdapter() const { return adapter_ != nullptr; }
    const std::string& adapter() const override { return name_; }
    bool run(Job& job) override {
        if (failed_ || !layout_)
            return false;
        const std::array<const std::vector<uint32_t>*, 4> arrays{&job.draws, &job.bins, &job.textures, &job.pixels};
        std::array<WGPUBindGroupEntry, 4> entries{};
        for (unsigned i = 0; i < 4; i++) {
            const size_t size =
                std::max<size_t>(4, i == 2 && job.textureWords ? size_t(job.textureWords) * 4 : arrays[i]->size() * 4);
            if (size > sizes_[i]) {
                if (buffers_[i])
                    wgpuBufferRelease(buffers_[i]);
                sizes_[i] = (size + 255) & ~size_t(255);
                auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                desc.size = sizes_[i];
                desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
                if (i == 3)
                    desc.usage |= WGPUBufferUsage_CopySrc;
                buffers_[i] = wgpuDeviceCreateBuffer(device_, &desc);
            }
            if (i == 2 && job.textureWords) {
                size_t source = 0;
                if (job.textureWrites.size() % 2)
                    return false;
                for (size_t w = 0; w < job.textureWrites.size(); w += 2) {
                    const size_t offset = job.textureWrites[w], count = job.textureWrites[w + 1];
                    if (offset > job.textureWords || count > job.textureWords - offset ||
                        source > job.textures.size() || count > job.textures.size() - source)
                        return false;
                    if (count)
                        wgpuQueueWriteBuffer(queue_, buffers_[2], offset * 4, job.textures.data() + source, count * 4);
                    source += count;
                }
                if (source != job.textures.size())
                    return false;
            } else if (!arrays[i]->empty())
                wgpuQueueWriteBuffer(queue_, buffers_[i], 0, arrays[i]->data(), arrays[i]->size() * 4);
            entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            entries[i].binding = i;
            entries[i].buffer = buffers_[i];
            entries[i].size = size;
        }
        const auto outputBytes = job.pixels.size() * 4;
        if (outputBytes > stagingSize_) {
            if (staging_)
                wgpuBufferRelease(staging_);
            stagingSize_ = (outputBytes + 255) & ~size_t(255);
            auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = stagingSize_;
            desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            staging_ = wgpuDeviceCreateBuffer(device_, &desc);
        }
        auto bindDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindDesc.layout = layout_;
        bindDesc.entryCount = 4;
        bindDesc.entries = entries.data();
        auto bind = wgpuDeviceCreateBindGroup(device_, &bindDesc);
        auto encoder = wgpuDeviceCreateCommandEncoder(device_, nullptr);
        auto pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (job.width + 7) / 8, (job.height + 7) / 8, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, buffers_[3], 0, staging_, 0, outputBytes);
        auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue_, 1, &command);
        struct Completion {
            std::atomic<bool> ready{false};
            bool success = false;
        } done;
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &done;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* p, void*) {
            auto& d = *static_cast<Completion*>(p);
            d.success = status == WGPUMapAsyncStatus_Success;
            d.ready = true;
        };
        wgpuBufferMapAsync(staging_, WGPUMapMode_Read, 0, outputBytes, callback);
        wgpuDevicePoll(device_, true, nullptr);
        // Poll is a completion fence: the callback cannot outlive its userdata.
        const bool ok = done.ready && done.success && !failed_;
        if (ok) {
            const void* data = wgpuBufferGetConstMappedRange(staging_, 0, outputBytes);
            if (data)
                std::memcpy(job.pixels.data(), data, outputBytes);
            else
                failed_ = true;
            wgpuBufferUnmap(staging_);
        }
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bind);
        return ok && !failed_;
    }
    ~NativeCompute() override {
        if (staging_)
            wgpuBufferRelease(staging_);
        for (auto b : buffers_)
            if (b)
                wgpuBufferRelease(b);
        if (layout_)
            wgpuBindGroupLayoutRelease(layout_);
        if (pipeline_)
            wgpuComputePipelineRelease(pipeline_);
        if (queue_)
            wgpuQueueRelease(queue_);
        if (device_)
            wgpuDeviceRelease(device_);
        if (adapter_)
            wgpuAdapterRelease(adapter_);
        if (instance_)
            wgpuInstanceRelease(instance_);
    }
};
} // namespace
std::unique_ptr<Compute> createCompute() {
    auto result = std::make_unique<NativeCompute>();
    if (!result->init() && !result->hasAdapter())
        return {};
    return result;
}
} // namespace gsr::gpu
