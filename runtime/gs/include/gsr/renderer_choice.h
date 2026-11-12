// The player's renderer name (drakan.ini [video] renderer, env PS2X_GS) as a gsr::Config choice. Standard C++ only.
#pragma once
#include "gsr.h"

#include <cctype>
#include <string>

namespace gsr {

struct RendererChoice {
    Config::Api api = Config::Api::WebGpu;
    Config::GpuApi gpuApi = Config::GpuApi::Auto;
    bool known = true; // false: the name is not one of ours (the default renderer is used)
};

// webgpu / auto / gsr = the shared renderer on its automatic API; vulkan / vk, d3d12 / dx12, opengl / gl = the shared
// renderer on that API; d3d11 = Direct3D 11. Case-insensitive; surrounding blanks are ignored.
inline RendererChoice parseRendererChoice(const std::string& text) {
    std::string name;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    RendererChoice choice;
    if (name == "d3d11" || name == "dx11")
        choice.api = Config::Api::D3D11;
    else if (name == "vulkan" || name == "vk")
        choice.gpuApi = Config::GpuApi::Vulkan;
    else if (name == "d3d12" || name == "dx12")
        choice.gpuApi = Config::GpuApi::D3D12;
    else if (name == "opengl" || name == "gl")
        choice.gpuApi = Config::GpuApi::OpenGL;
    else
        choice.known = name.empty() || name == "webgpu" || name == "auto" || name == "gsr";
    return choice;
}

inline const char* gpuApiName(Config::GpuApi api) {
    switch (api) {
        case Config::GpuApi::Vulkan:
            return "Vulkan";
        case Config::GpuApi::D3D12:
            return "D3D12";
        case Config::GpuApi::OpenGL:
            return "OpenGL";
        default:
            return "auto";
    }
}

} // namespace gsr
