// Camera math shared by the temporal passes of both backends (D3D11 nr_guides.cpp, WebGPU wgpu_temporal.cpp): the
// engine's world-to-clip matrix (SceneGuideCamera) turned into a matrix from world space to the clip space of a frame's
// pixels and GS depth, so `clip(current) * inverse(current) * previous` reprojects a pixel into the previous frame.
// Matrices are mathematical row-major, ROW-VECTOR (v' = v * M), doubles.
#pragma once
#include <cstdint>

namespace gsr::guide {

using Matrix = double[16];

struct View {
    float vp[16]{};      // SceneGuideCamera::worldToClipVU (column-major column-vector = row-major row-vector)
    float scale[3]{};    // viewportScale xyz
    float offset[3]{};   // viewportOffset xyz
    float xyOffset[2]{}; // GS XYOFFSET of the frame (1/16 pixels)
    bool valid = false;
};

// The frame's pixel / depth mapping: frameW x frameH native pixels, GS depth normalised by zmax with the engine's
// reverse depth range [farGs, nearGs].
struct Mapping {
    uint32_t frameW = 0, frameH = 0;
    float zmax = 1, nearGs = 1, farGs = 0;
};

void identity(double* m);
void multiply(const double* a, const double* b, double* out);
// false when singular or the product does not verify (unstable / corrupt camera input)
bool inverse(const double* m, double* out);
// World -> clip of the frame: x, y in [-1, 1] over the frame (y up), z = depth mapped to [0, 1] over [farGs, nearGs].
bool cameraMatrix(const Mapping& map, const View& view, double* out);
// to float, false when a value is not finite / out of float range
bool toFloats(const double* in, float* out);

// The camera of a frame for passes that unproject pixels (the WebGPU ray pass; nr::guides::FrameConstants on D3D11):
// view <-> clip of `worldToClip` (cameraMatrix) and the world-space camera basis from the engine's rigid worldToView.
struct FrameCamera {
    float viewToClip[16]{}, clipToView[16]{}; // row-major row-vector
    float position[3]{}, right[3]{}, up[3]{}, forward[3]{};
    float nearPlane = 0;
    bool orthographic = false;
};
// False (with `why`) for a non-rigid view or an unsupported (non reverse-Z, oblique) projection.
bool frameCamera(const double* worldToClip, const float* worldToView, FrameCamera& out, const char** why = nullptr);

} // namespace gsr::guide
