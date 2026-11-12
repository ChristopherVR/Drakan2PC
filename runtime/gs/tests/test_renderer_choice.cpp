// drakan.ini [video] renderer / PS2X_GS names -> gsr::Config (gsr/renderer_choice.h).
#include "gsr/renderer_choice.h"

#include <cstdio>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    using gsr::Config;
    using gsr::parseRendererChoice;
    {
        const auto c = parseRendererChoice("vulkan");
        expect(c.known && c.api == Config::Api::WebGpu && c.gpuApi == Config::GpuApi::Vulkan, "vulkan");
    }
    {
        const auto c = parseRendererChoice("  Vulkan\t");
        expect(c.known && c.gpuApi == Config::GpuApi::Vulkan, "case and blanks");
    }
    expect(parseRendererChoice("vk").gpuApi == Config::GpuApi::Vulkan, "vk");
    {
        const auto c = parseRendererChoice("webgpu");
        expect(c.known && c.api == Config::Api::WebGpu && c.gpuApi == Config::GpuApi::Auto, "webgpu");
    }
    {
        const auto c = parseRendererChoice("");
        expect(c.known && c.api == Config::Api::WebGpu && c.gpuApi == Config::GpuApi::Auto, "empty");
    }
    {
        const auto c = parseRendererChoice("D3D11");
        expect(c.known && c.api == Config::Api::D3D11 && c.gpuApi == Config::GpuApi::Auto, "d3d11");
    }
    {
        const auto c = parseRendererChoice("d3d12");
        expect(c.known && c.api == Config::Api::WebGpu && c.gpuApi == Config::GpuApi::D3D12, "d3d12");
    }
    expect(parseRendererChoice("dx12").gpuApi == Config::GpuApi::D3D12, "dx12");
    expect(parseRendererChoice("gl").gpuApi == Config::GpuApi::OpenGL, "gl");
    expect(parseRendererChoice("opengl").gpuApi == Config::GpuApi::OpenGL, "opengl");
    {
        // An unknown name keeps the default renderer and is reported.
        const auto c = parseRendererChoice("metal");
        expect(!c.known && c.api == Config::Api::WebGpu && c.gpuApi == Config::GpuApi::Auto, "unknown name");
    }
    expect(Config().gpuApi == Config::GpuApi::Auto, "Config default");
    std::printf("%s\n", g_failures ? "renderer choice: FAILED" : "renderer choice: ok");
    return g_failures ? 1 : 0;
}
