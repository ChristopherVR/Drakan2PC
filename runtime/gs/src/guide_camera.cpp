// Camera math shared by the temporal passes, see guide_camera.h.
#include "guide_camera.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace gsr::guide {
namespace {
bool finite(const float* a, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(a[i]))
            return false;
    return true;
}
} // namespace

void identity(double* a) {
    std::fill(a, a + 16, 0.0);
    for (int i = 0; i < 4; ++i)
        a[i * 4 + i] = 1;
}

void multiply(const double* a, const double* b, double* out) {
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    std::copy(r, r + 16, out);
}

bool inverse(const double* m, double* out) {
    double a[4][8]{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            a[i][j] = m[i * 4 + j];
            a[i][j + 4] = i == j ? 1 : 0;
        }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int i = col + 1; i < 4; ++i)
            if (std::abs(a[i][col]) > std::abs(a[pivot][col]))
                pivot = i;
        if (!std::isfinite(a[pivot][col]) || std::abs(a[pivot][col]) < 1e-20)
            return false;
        for (int j = 0; j < 8; ++j)
            std::swap(a[col][j], a[pivot][j]);
        const double div = a[col][col];
        for (int j = 0; j < 8; ++j)
            a[col][j] /= div;
        for (int i = 0; i < 4; ++i)
            if (i != col) {
                const double v = a[i][col];
                for (int j = 0; j < 8; ++j)
                    a[i][j] -= v * a[col][j];
            }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = a[i][j + 4];
            if (!std::isfinite(out[i * 4 + j]))
                return false;
        }
    // Verify the inverse, catching unstable/corrupt camera input.
    Matrix product{};
    multiply(m, out, product);
    for (int i = 0; i < 16; ++i)
        if (std::abs(product[i] - (i % 5 == 0 ? 1 : 0)) > 1e-5)
            return false;
    return true;
}

bool toFloats(const double* in, float* out) {
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(in[i]) || std::abs(in[i]) > std::numeric_limits<float>::max())
            return false;
        out[i] = static_cast<float>(in[i]);
    }
    return true;
}

bool cameraMatrix(const Mapping& map, const View& c, double* out) {
    if (!c.valid || !finite(c.vp, 16) || !finite(c.scale, 3) || !finite(c.offset, 3) || !finite(c.xyOffset, 2))
        return false;
    if (std::abs(c.scale[0]) < 1e-12 || std::abs(c.scale[1]) < 1e-12 || std::abs(c.scale[2]) < 1e-12)
        return false;
    // c.vp is column-major column-vector. Its raw array already stores the
    // transpose in row-major row-vector form. NO further transposition here.
    Matrix vp{}, viewport{};
    for (int i = 0; i < 16; ++i)
        vp[i] = c.vp[i];
    identity(viewport);
    const double fw = map.frameW, fh = map.frameH;
    const double range = double(map.nearGs) - map.farGs;
    viewport[0] = 2.0 * c.scale[0] / fw;
    viewport[5] = -2.0 * c.scale[1] / fh;
    viewport[10] = 16.0 * c.scale[2] / (double(map.zmax) * range);
    viewport[12] = 2.0 * (c.offset[0] - c.xyOffset[0] / 16.0) / fw - 1.0;
    viewport[13] = 1.0 - 2.0 * (c.offset[1] - c.xyOffset[1] / 16.0) / fh;
    viewport[14] = (16.0 * c.offset[2] / map.zmax - map.farGs) / range;
    multiply(vp, viewport, out);
    Matrix check{};
    return inverse(out, check);
}

bool frameCamera(const double* worldToClip, const float* worldToView, FrameCamera& out, const char** why) {
    auto fail = [&](const char* text) {
        if (why)
            *why = text;
        return false;
    };
    out = {};
    if (!worldToView || !finite(worldToView, 16))
        return fail("no finite worldToView");
    Matrix view{}, invView{}, projection{}, invProjection{};
    for (int i = 0; i < 16; ++i)
        view[i] = worldToView[i];
    // A rigid affine world-to-view matrix, not an MVP.
    if (std::abs(view[3]) + std::abs(view[7]) + std::abs(view[11]) + std::abs(view[15] - 1) > 1e-5 ||
        !inverse(view, invView))
        return fail("worldToView is not affine/invertible");
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double dot = 0;
            for (int k = 0; k < 3; ++k)
                dot += invView[i * 4 + k] * invView[j * 4 + k];
            if (std::abs(dot - (i == j ? 1 : 0)) > 1e-3)
                return fail("worldToView basis is not rigid");
        }
    multiply(invView, worldToClip, projection);
    if (!inverse(projection, invProjection))
        return fail("projection inverse failed");
    // Unproject normalized z = 1 (near) and z = 0 (far): infinite reverse Z keeps its far point at infinity.
    const double nw = invProjection[11] + invProjection[15], nz = invProjection[10] + invProjection[14];
    if (std::abs(nw) < 1e-20)
        return fail("invalid near endpoint");
    const double nearZ = nz / nw;
    const bool infiniteFar = std::abs(invProjection[15]) < 1e-20;
    const double farZ = infiniteFar ? nearZ : invProjection[14] / invProjection[15];
    if (!std::isfinite(nearZ) || std::abs(nearZ) <= 0 || (!infiniteFar && nearZ * farZ <= 0) ||
        std::abs(nearZ) > std::numeric_limits<float>::max())
        return fail("projection is not a supported reverse-Z camera");
    if (std::abs(projection[3]) + std::abs(projection[7]) > 1e-7 ||
        std::abs(projection[1]) + std::abs(projection[4]) > 1e-5 || std::abs(projection[0]) < 1e-12 ||
        std::abs(projection[5]) < 1e-12)
        return fail("unsupported oblique/skew projection");
    FrameCamera c;
    if (!toFloats(projection, c.viewToClip) || !toFloats(invProjection, c.clipToView))
        return fail("camera constants overflow");
    c.nearPlane = static_cast<float>(std::abs(nearZ));
    c.orthographic = std::abs(projection[11]) < 1e-12;
    for (int i = 0; i < 3; ++i) {
        c.position[i] = static_cast<float>(invView[12 + i]);
        c.right[i] = static_cast<float>(invView[i]);
        c.up[i] = static_cast<float>(invView[4 + i]);
        c.forward[i] = static_cast<float>(invView[8 + i] * (nearZ < 0 ? -1 : 1));
    }
    if (!finite(c.position, 3))
        return fail("invalid camera position");
    out = c;
    return true;
}

} // namespace gsr::guide
