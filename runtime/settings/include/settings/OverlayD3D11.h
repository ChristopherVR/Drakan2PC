// The settings overlay's Direct3D 11 renderer (imgui_impl_dx11). Windows only.
#pragma once
#include <memory>

#include "settings/OverlayRenderer.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace dsettings {

// Frame target: an ID3D11RenderTargetView*. Pipeline state touched while drawing is restored.
std::unique_ptr<OverlayRenderer> createD3D11OverlayRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

} // namespace dsettings
