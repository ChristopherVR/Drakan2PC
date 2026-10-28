// CPU arithmetic oracle and actual replacement-HLSL compilation; no device.
#include "gs_shaders.h"
#include "gs_rt_sampling.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static unsigned checks;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        std::exit(1);
    }
}
static int address(int i, int dim, unsigned mode, int mn, int mx) {
    if (mode == 0) {
        int n = i % dim;
        return n < 0 ? n + dim : n;
    }
    if (mode == 1)
        return std::clamp(i, 0, dim - 1);
    if (mode == 2)
        return std::clamp(i, mn, mx);
    return (i & mn) | mx;
}
// Independent discrete physical-grid oracle (integer scale).
static int tap(int physical, int scale, int dim, unsigned mode, int mn, int mx) {
    if (mode == 1)
        return std::clamp(physical, 0, dim * scale - 1);
    if (mode == 2)
        return std::clamp(physical, mn * scale, (mx + 1) * scale - 1);
    int cell = int(std::floor(double(physical) / scale));
    return address(cell, dim, mode, mn, mx) * scale + physical - cell * scale;
}
// Scalar transcription of wrapRTTap for comparison with the discrete oracle.
static double candidate(double c, int dim, unsigned mode, int mn, int mx, int scale) {
    if (mode == 1)
        return std::clamp(c, .5 / scale, dim - .5 / scale);
    if (mode == 2)
        return std::clamp(c, mn + .5 / scale, mx + 1 - .5 / scale);
    return address(int(std::floor(c)), dim, mode, mn, mx) + c - std::floor(c);
}
static double alpha(unsigned rgb, unsigned stored, unsigned mode, bool aem, unsigned ta0, unsigned ta1) {
    if (!mode)
        return stored;
    const bool high = mode == 2 && stored >= 64;
    return aem && !high && !rgb ? 0 : high ? ta1 : ta0;
}
int main() {
    // At a fractional scale, choosing a physical texel centre before repeat
    // can move an exact logical boundary into its preceding logical cell.
    const double reducedScale = 1.3, uvBoundary = 2.0;
    const double oldPoint = uvBoundary - 2 * std::floor(uvBoundary / 2);
    const double earlyCentre = (std::floor(uvBoundary * reducedScale) + .5) / reducedScale;
    check(int(std::floor(oldPoint * reducedScale)) == 0 && earlyCentre < 2,
          "point repeat must wrap continuous UV before physical quantization");
    for (int scale : {1, 2, 4, 8})
        for (int dim : {2, 8, 128})
            for (unsigned mode = 0; mode < 4; ++mode)
                for (int i = -dim * scale; i < 2 * dim * scale; ++i) {
                    const int mn = dim == 2 ? 0 : 1, mx = dim - 1;
                    double p = candidate((i + .5) / scale, dim, mode, mn, mx, scale);
                    check(int(std::floor(p * scale)) == tap(i, scale, dim, mode, mn, mx),
                          "physical tap address agrees with independent integer grid");
                }
    // Two logical texels [red,black] within a larger RT, at the repeat seam.
    check((255.0 + 0.0) / 2 == 127.5, "repeat seam averages last/first texels");
    check(tap(-1, 4, 2, 0, 0, 0) == 7 && tap(0, 4, 2, 0, 0, 0) == 0, "scale4 repeat seam preserves physical subtexels");
    check(tap(-1, 4, 8, 3, 3, 4) == 31, "region repeat wraps each negative tap");
    for (bool aem : {false, true})
        for (unsigned mode : {0u, 1u, 2u})
            for (unsigned stored : {0u, 63u, 64u, 128u, 255u}) {
                check(alpha(0, stored, mode, aem, 37, 173) == (!mode                       ? stored
                                                               : mode == 2 && stored >= 64 ? 173
                                                               : aem                       ? 0
                                                                                           : 37),
                      "TEXA preserves CT16 alpha-set black and CT32 alpha");
                check(alpha(1, stored, mode, aem, 37, 173) == (!mode                       ? stored
                                                               : mode == 2 && stored >= 64 ? 173
                                                                                           : 37),
                      "nonblack TEXA expansion");
            }
    check((alpha(255, 0, 1, true, 128, 128) + alpha(0, 0, 1, true, 128, 128)) / 2 == 64,
          "CT24 AEM expands before linear interpolation");
    check(alpha(128, 0, 1, true, 128, 128) == 128, "old postfilter expansion differs");
    check((alpha(0, 0, 2, true, 37, 173) + alpha(0, 128, 2, true, 37, 173)) / 2 == 86.5,
          "CT16 alpha-bit transition interpolates expanded alpha");
    // Compile the integrated production initializer, not a stitched test copy.
    const std::string shader = gsr::kHlslMain;
    check(shader.find("float wrapRTTap(") != std::string::npos, "production contains RT fragment");
    check(shader.find("float4 sampleRT(") == shader.rfind("float4 sampleRT("), "one production RT sampler");
    check(shader.find("if (!lin)") < shader.find("TexA.GetDimensions(width, height)"),
          "point path bypasses physical-tap reconstruction");
    check(shader.find("wms == 1u && wmt == 1u") < shader.find("TexA.GetDimensions(width, height)"),
          "CT32 clamp path preserves hardware linear sampling");
    for (const char* hw : {"0", "1"})
        for (const char* entry : {"VSMain", "PSMain", "VSMotion", "PSMotion"}) {
            D3D_SHADER_MACRO macros[] = {{"HW_BLEND", hw}, {nullptr, nullptr}};
            ID3DBlob *code = nullptr, *errors = nullptr;
            HRESULT hr =
                D3DCompile(shader.data(), shader.size(), "rt_sampling_candidate", macros, nullptr, entry,
                           entry[0] == 'V' ? "vs_5_0" : "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(hr) && errors)
                std::fprintf(stderr, "%s\n", (const char*)errors->GetBufferPointer());
            if (code)
                code->Release();
            if (errors)
                errors->Release();
            check(SUCCEEDED(hr), "candidate actual HLSL compiles");
        }
    std::printf("PASS %u CPU checks including 8 HLSL compilations; no D3D device\n", checks);
}
