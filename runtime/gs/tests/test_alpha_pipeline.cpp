// Pure CPU regressions and HLSL compilation. Does not create a D3D device.
#include "gs_alpha.h"
#include "gs_texdecode.h"
#include "gs_shaders.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned checks = 0;
static void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s (check %u)\n", text, checks);
        std::exit(1);
    }
}
int main() {
    for (unsigned aem = 0; aem < 2; ++aem)
        for (unsigned v = 0; v < 65536; ++v) {
            constexpr unsigned ta0 = 37, ta1 = 173;
            const std::uint64_t texa = ta0 | (std::uint64_t(aem) << 15) | (std::uint64_t(ta1) << 32);
            const unsigned expected = (v & 32768) ? ta1 : (aem && v == 0) ? 0 : ta0;
            check((gsr::expand16(v, texa) >> 24) == expected, "CT16 AEM preserves alpha-set black");
            gsr::ClutBuf clut{};
            clut.is16 = true;
            clut.raw[0] = v;
            check((gsr::clutColor(clut, 0, texa) >> 24) == expected, "16-bit palette uses same TEXA semantics");
        }
    for (unsigned source = 0; source < 256; ++source)
        for (bool fba : {false, true})
            for (bool fmt16 : {false, true}) {
                const unsigned stored = gsr::alpha::storedAlpha(source, fba, fmt16);
                const unsigned expected =
                    fmt16 ? ((fba || source >= 128) ? 128 : 0) : (fba && source < 128 ? source + 128 : source);
                check(stored == expected, "FBA stored alpha");
            }
    check(gsr::alpha::storedAlpha(64, true, false) == 192, "FBA 0x40 -> stored 0xc0");
    check((200 * 64) / 128 == 100 && (200 * 192) / 128 == 300, "FBA-before-blend regression is numerically distinct");
    for (unsigned afail = 0; afail < 4; ++afail)
        for (unsigned mask = 0; mask < 16; ++mask)
            for (bool depth : {false, true}) {
                const auto pass = gsr::alpha::failurePass(afail, (std::uint8_t)mask, depth);
                const unsigned expectedMask = afail == 1 ? mask : afail == 3 ? (mask & 7) : 0;
                check(pass.colorMask == expectedMask, "AFAIL channel mask");
                check(pass.depthWrite == (afail == 2 && depth), "AFAIL depth mask");
                check(gsr::alpha::blendForPass(true, pass.colorMask) == ((expectedMask & 7) != 0),
                      "AFAIL still blends RGB");
                check(!gsr::alpha::blendForPass(false, pass.colorMask), "disabled blending stays disabled");
            }
    check(gsr::alpha::blendForPass(true, 15), "alpha-test FB_ONLY failure must retain blending");
    check(gsr::alpha::blendForPass(true, 7), "alpha-test RGB_ONLY failure must retain blending");
    // Compile both actual pixel paths and shared motion coverage without a GPU.
    for (const char* hw : {"0", "1"})
        for (const char* entry : {"VSMain", "PSMain", "VSMotion", "PSMotion"}) {
            const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", hw}, {nullptr, nullptr}};
            Microsoft::WRL::ComPtr<ID3DBlob> code, error;
            const HRESULT result =
                D3DCompile(gsr::kHlslMain, std::strlen(gsr::kHlslMain), "gs_shaders", macros, nullptr, entry,
                           entry[0] == 'V' ? "vs_5_0" : "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
            if (FAILED(result))
                std::fprintf(stderr, "%s HW_BLEND=%s: %s\n", entry, hw,
                             error ? (const char*)error->GetBufferPointer() : "compile failed");
            check(SUCCEEDED(result), "actual GS HLSL compiles");
        }
    std::printf("PASS %u alpha/decode/policy checks and 8 actual shader compilations; no D3D device created\n", checks);
}
