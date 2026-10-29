// HD movie frames: the host's HD image of a movie frame drawn over the native frame the game stamped into a frame
// buffer (WebGPU backend; see d3d11_movie.cpp and docs/reference/fmv.md section 9).
#include "wgpu_backend_impl.h"

#include <cmath>
#include <cstdio>

namespace gsr {

void WgpuBackend::Impl::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                       const MovieImage& image, uint64_t imageId) {
    if (dbp % 32u || !w || !h || image.rgba.size() < (size_t)image.width * image.height ||
        image.width > gpu->maxTextureSize() || image.height > gpu->maxTextureSize())
        return;
    if (movieTexId != imageId || !movieTex) {
        if (!movieTex || movieTexW != image.width || movieTexH != image.height) {
            movieTex = createTexture(image.width, image.height, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1,
                                     "hd movie");
            if (!movieTex)
                return;
            movieTexW = image.width;
            movieTexH = image.height;
        }
        gpu->writeTexture(*movieTex, 0, 0, 0, image.width, image.height, image.rgba.data(), image.width * 4u);
        cpuProfile.uploaded(uint64_t(image.width) * image.height * 4);
        movieTexId = imageId;
    }

    RT* rt = getRT(dbp / 32u, dbw, PSM_CT32, y + h);
    if (!rt || rt->wstate == 1)
        return;
    syncRT(*rt); // rows memory still leads reach the target first, so they cannot overwrite the HD frame later

    // Alpha 0x80 like the IPU's RGB32 output. Bilinear: the frame is resampled to the internal resolution.
    const float rect[4] = {(float)(x * scale), (float)(y * scale), (float)((x + w) * scale), (float)((y + h) * scale)};
    const float alpha[4] = {0, 0, 0, 128.0f / 255.0f};
    drawRect(*rt->tex, rect, movieTex.get(), alpha, {(int32_t)(x * scale), (int32_t)(y * scale), w * scale, h * scale});
    rt->copyValid = false;
    ++rt->writes;
    ++st.movieFrames;
}

} // namespace gsr
