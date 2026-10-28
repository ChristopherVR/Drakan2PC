#pragma once
#include <cstdint>
#include <type_traits>
namespace gsr {
// ROW-VECTOR ROW-MAJOR matrices: clip = world * worldToClipVU.
// VU clip space is not canonical D3D NDC; apply viewport and GS XYOFFSET.
struct SceneGuideCamera {
    enum : uint32_t { Valid = 1, World = 2, Reset = 4, InfiniteFar = 8, HaveViewport = 16, EndWorld = 32 };
    // The inventory screen draws a centred view (drakan_inventory_views.inl): its page maps about the screen centre.
    static constexpr uint32_t CentredPage = 64;
    uint64_t serial = 0, captureTick = 0;
    uint32_t flags = 0, sourcePc = 0, sourceAddress = 0, rendererAddress = 0;
    int32_t guiScreen = -1;
    uint32_t frameWidth = 0, frameHeight = 0, reserved = 0;
    float worldToView[16]{};
    float viewToClipVU[16]{};
    float worldToClipVU[16]{};
    float cameraPosition[3]{}, cameraRight[3]{}, cameraUp[3]{}, cameraForward[3]{};
    float viewportScale[4]{}, viewportOffset[4]{}, fogClamp[4]{};
    float nearPlane = 32.0f, farPlane = 0.0f; // InfiniteFar means no finite far plane
    float projectionScale[2]{};               // already widescreen-adjusted
};
static_assert(std::is_trivially_copyable_v<SceneGuideCamera>);
static_assert(sizeof(SceneGuideCamera) == 352);
} // namespace gsr
