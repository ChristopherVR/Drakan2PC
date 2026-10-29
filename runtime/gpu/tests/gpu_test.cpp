// drakan::gpu headless check: device creation, a WGSL render pipeline into an offscreen target, synchronous
// readback, and shader error reporting. Exit 77 = no adapter (skipped).
#include "gpu/Gpu.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace drakan::gpu;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// Left half red, right half green: checks the pipeline, the viewport and the readback row packing.
const char* kShader = R"(
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    if (pos.x < 50.0) { return vec4f(1.0, 0.0, 0.0, 1.0); }
    return vec4f(0.0, 1.0, 0.0, 1.0);
}
)";
} // namespace

int main() {
    ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return 77;
    }
    std::printf("adapter: %s (%s)\n", context->adapterName().c_str(), backendName(context->backend()));
    WGPUDevice device = context->device();

    // Invalid WGSL is reported, not fatal for the device.
    check(context->shaderModule("fn broken( {", "broken") == nullptr, "invalid WGSL rejected");
    check(!context->failed(), "a shader error does not fail the device");

    WGPUShaderModule module = context->shaderModule(kShader, "test");
    check(module != nullptr, "valid WGSL compiles");
    if (!module)
        return 1;

    constexpr uint32_t kWidth = 100, kHeight = 7; // odd height and a row size that needs padding
    auto textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = {kWidth, kHeight, 1};
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture target = wgpuDeviceCreateTexture(device, &textureDesc);
    WGPUTextureView targetView = wgpuTextureCreateView(target, nullptr);

    auto colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    auto fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = view("fs");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    auto pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = view("vs");
    pipelineDesc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    check(pipeline != nullptr, "render pipeline");

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    auto attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = targetView;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = {0, 0, 1, 1};
    auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &attachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(context->queue(), 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    std::vector<uint8_t> pixels;
    check(context->readTexture(target, 0, 0, kWidth, kHeight, 4, pixels), "readback");
    if (pixels.size() == size_t(kWidth) * kHeight * 4) {
        const auto at = [&](uint32_t x, uint32_t y) { return &pixels[(size_t(y) * kWidth + x) * 4]; };
        check(at(0, 0)[0] == 255 && at(0, 0)[1] == 0, "left pixel red");
        check(at(49, kHeight - 1)[0] == 255, "last red column, last row");
        check(at(50, kHeight - 1)[1] == 255 && at(50, kHeight - 1)[0] == 0, "first green column, last row");
        check(at(kWidth - 1, 3)[1] == 255 && at(kWidth - 1, 3)[3] == 255, "right pixel green, opaque");
    } else {
        check(false, "readback size");
    }
    // A sub-rectangle read keeps its own rows tight.
    check(context->readTexture(target, 48, 2, 4, 3, 4, pixels) && pixels.size() == 4 * 3 * 4 && pixels[0] == 255 &&
              pixels[2 * 4 + 1] == 255,
          "sub-rectangle readback");
    check(!context->failed(), "no device errors");

    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
    wgpuTextureViewRelease(targetView);
    wgpuTextureRelease(target);
    if (g_failures)
        return 1;
    std::printf("gpu_test: all checks passed\n");
    return 0;
}
