// The settings overlay's WebGPU renderer (OverlayWebGpu.cpp) against known pixels: alpha blending, the screen layer's
// additive / opaque callbacks, clip rectangles, uploaded textures and the target copy. Exit 77 = no adapter.
#include "settings/OverlayWebGpu.h"

#include <gpu/Gpu.h>
#include <rhi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
constexpr uint32_t W = 64, H = 32;

struct Pixel {
    int r, g, b;
};

void expect(const std::vector<uint8_t>& px, uint32_t x, uint32_t y, Pixel want, const char* what) {
    const uint8_t* p = px.data() + (size_t(y) * W + x) * 4;
    const bool ok = std::abs(p[0] - want.r) <= 2 && std::abs(p[1] - want.g) <= 2 && std::abs(p[2] - want.b) <= 2;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s at (%u, %u): got %d %d %d, want %d %d %d\n", what, x, y, p[0], p[1], p[2],
                     want.r, want.g, want.b);
        ++g_failures;
    }
}

void beginFrame(dsettings::OverlayRenderer& r) {
    r.newFrame();
    ImGui::GetIO().DisplaySize = ImVec2(float(W), float(H));
    ImGui::NewFrame();
}

void endFrame(dsettings::OverlayRenderer& r, gsr::rhi::Texture& target) {
    ImGui::Render();
    r.render(&target, W, H, ImGui::GetDrawData());
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
    auto device = gsr::rhi::Device::create(*context);
    using namespace gsr::rhi;
    auto target = device->createTexture({W, H, 1, Format::RGBA8, Sampled | RenderTarget | CopySrc | CopyDst, "rt"});
    const float black[4] = {0, 0, 0, 1};
    device->clearColor(*target, black);

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    auto renderer = dsettings::createWebGpuOverlayRenderer(*device);
    if (!renderer->init()) {
        std::fprintf(stderr, "FAIL: init\n");
        return 1;
    }

    // Frame 1: blending, callbacks, clipping, an uploaded texture.
    beginFrame(*renderer);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(16, 16), IM_COL32(255, 0, 0, 255));
    dl->AddRectFilled(ImVec2(16, 0), ImVec2(32, 16), IM_COL32(255, 255, 255, 128));
    dl->AddRectFilled(ImVec2(32, 0), ImVec2(48, 16), IM_COL32(64, 0, 0, 255));
    dl->AddCallback(renderer->additiveCallback(), nullptr);
    dl->AddRectFilled(ImVec2(32, 0), ImVec2(48, 16), IM_COL32(64, 0, 0, 255));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    dl->AddRectFilled(ImVec2(48, 0), ImVec2(64, 16), IM_COL32(255, 255, 255, 255));
    float fade = 0.25f;
    dl->AddCallback(renderer->opaqueCallback(), &fade, sizeof fade);
    dl->AddRectFilled(ImVec2(48, 0), ImVec2(64, 16), IM_COL32(0, 0, 0, 255)); // alpha replaced by the fade
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    dl->PushClipRect(ImVec2(0, 16), ImVec2(8, 32));
    dl->AddRectFilled(ImVec2(0, 16), ImVec2(16, 32), IM_COL32(0, 255, 0, 255));
    dl->PopClipRect();
    const uint8_t blue[4 * 4] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const ImTextureID image = renderer->createTexture(blue, 2, 2);
    dl->AddImage(image, ImVec2(16, 16), ImVec2(32, 32));
    endFrame(*renderer, *target);

    // Frame 2: the target copy holds frame 1 (the red square drawn again from it, right of the blue one).
    uint32_t cw = 0, ch = 0;
    const ImTextureID copy = renderer->copyTarget(target.get(), &cw, &ch);
    if (!copy || cw != W || ch != H) {
        std::fprintf(stderr, "FAIL: copyTarget\n");
        ++g_failures;
    }
    beginFrame(*renderer);
    if (copy)
        ImGui::GetBackgroundDrawList()->AddImage(copy, ImVec2(40, 16), ImVec2(56, 32), ImVec2(0, 0),
                                                 ImVec2(16.0f / W, 16.0f / H));
    endFrame(*renderer, *target);

    uint32_t rw = 0, rh = 0;
    std::vector<uint8_t> px;
    if (!renderer->readTarget(target.get(), rw, rh, px) || rw != W || rh != H) {
        std::fprintf(stderr, "FAIL: readTarget\n");
        return 1;
    }
    expect(px, 8, 8, {255, 0, 0}, "opaque fill");
    expect(px, 24, 8, {128, 128, 128}, "alpha blend");
    expect(px, 40, 8, {128, 0, 0}, "additive callback");
    expect(px, 56, 8, {191, 191, 191}, "opaque callback (fade 0.25)");
    expect(px, 4, 24, {0, 255, 0}, "inside clip");
    expect(px, 12, 24, {0, 0, 0}, "outside clip");
    expect(px, 24, 24, {0, 0, 255}, "uploaded texture");
    expect(px, 48, 24, {255, 0, 0}, "target copy");

    renderer->releaseTexture(image);
    renderer->shutdown();
    ImGui::DestroyContext(ctx);
    device->flush();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("overlay_webgpu_test: ok\n");
    return 0;
}
