// The vertex-program shader (src/vertex_program_hlsl.h) against the CPU evaluation it must equal bit for bit
// (gsr/vertex_program.h): the shader runs over generated vertices with stream output, and every integer it returns
// (position, depth, fog lane, colour, the bits of s, t and q) is compared with vp::evaluate().
//
// Inputs: matrices and viewport constants of the game's magnitudes with random vertices, plus operands chosen to hit
// the rounding rules (results that are exact, that lie just above or below a float, sums of very unlike magnitudes,
// denormals, the largest finite values, zero and negative w). GSR_VP_RECORDS=<file> adds records dumped from a VU1
// recording (private data; the test passes without it). Skips (exit 0) when the adapter has no double support.
//
// Second part: the entry point a draw binds (the program followed by the renderer's ordinary VSMain) against VSMain
// run on the CPU evaluation, bit for bit over every output.
#include "gsr/vertex_program.h"
#include "gs_shaders.h"
#include "vertex_program_hlsl.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace gsr;

namespace {
struct GpuVertex {
    vp::Vertex in;
    uint32_t constants, program, pad[2];
};
struct GpuOut {
    uint32_t place[4], colour[4], stq[4];
};

uint32_t bitsOf(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

struct Cases {
    std::vector<vp::Constants> constants;
    std::vector<GpuVertex> vertices;
};

void addGenerated(Cases& c, uint32_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const auto pick = [&](std::initializer_list<uint32_t> list) { return *(list.begin() + rng() % list.size()); };
    // operands that stress the rules, as raw bits
    const auto odd = [&]() -> uint32_t {
        switch (rng() % 12) {
            case 0:
                return pick({0u, 0x80000000u});
            case 1:
                return pick({0x00000001u, 0x807FFFFFu, 0x00800000u}); // denormals and the smallest normal
            case 2:
                return pick({0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFEu, 0x7F800000u, 0xFFC00000u}); // largest, inf, NaN
            case 3:
                return bitsOf(1.0f) + rng() % 3 - 1;
            case 4:
                return bitsOf(unit(rng) * 1e-12f);
            case 5:
                return bitsOf(unit(rng) * 1e12f);
            default:
                return bitsOf(unit(rng) * 4096.0f);
        }
    };
    for (uint32_t i = 0; i < count; ++i) {
        const bool wild = (i % 4) == 3; // every fourth case takes arbitrary operands everywhere
        if (i % 8 == 0) {
            vp::Constants k{};
            for (int r = 0; r < 4; ++r)
                for (int l = 0; l < 4; ++l)
                    k.mvp[r][l] = wild ? odd() : bitsOf(unit(rng) * (r == 3 ? 3000.0f : 1.6f));
            const float scale[4] = {320.0f + unit(rng), -240.0f + unit(rng), -8388608.0f * (1.0f + 0.1f * unit(rng)),
                                    0.0f};
            const float offset[4] = {2048.0f, 2048.0f, 8388608.0f, 255.0f + unit(rng)};
            const float fog[4] = {0.0f, 100.0f + 50.0f * unit(rng), 60000.0f + 1000.0f * unit(rng),
                                  0.004f * (1.0f + unit(rng))};
            for (int l = 0; l < 4; ++l) {
                k.scale[l] = wild && (rng() & 1) ? odd() : bitsOf(scale[l]);
                k.offset[l] = wild && (rng() & 1) ? odd() : bitsOf(offset[l]);
                k.fog[l] = wild && (rng() & 1) ? odd() : bitsOf(fog[l]);
                k.colour[l] = wild ? (int32_t)rng() : (int32_t)(rng() % 256);
            }
            for (int r = 0; r < 4; ++r)
                for (int l = 0; l < 4; ++l) {
                    k.lightDir[r][l] = wild ? odd() : bitsOf(unit(rng) / 127.0f);
                    k.lightColour[r][l] = wild && (rng() % 4 == 0) ? (int32_t)rng() : (int32_t)(rng() % 200);
                }
            c.constants.push_back(k);
        }
        GpuVertex v{};
        for (int l = 0; l < 4; ++l) {
            v.in.pos[l] = wild ? odd() : bitsOf(l == 3 ? 1.0f : unit(rng) * 900.0f);
            v.in.st[l] = wild ? odd() : bitsOf(l == 2 ? 1.0f : unit(rng) * 8.0f);
            v.in.normal[l] = wild && (rng() % 4 == 0) ? (int32_t)rng() : (int32_t)(rng() % 255) - 127;
        }
        v.constants = (uint32_t)c.constants.size() - 1;
        v.program = (i & 1) ? vp::StripsLit : vp::StripsFlat;
        c.vertices.push_back(v);
    }
}

// Records as the VU1 replay dumps them (DRAKAN_VU_BENCH_GPUPROG_DUMP): u32 size, then the record.
void addRecords(Cases& c, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return;
    std::vector<uint8_t> buf;
    uint32_t size = 0, records = 0;
    while (std::fread(&size, 4, 1, f) == 1 && size < (1u << 20)) {
        buf.resize(size);
        if (std::fread(buf.data(), 1, size, f) != size)
            break;
        vp::View rec;
        if (!vp::parse(buf.data(), size, rec))
            continue;
        c.constants.push_back(*rec.constants);
        for (uint32_t i = 0; i < rec.header.vertexCount; ++i)
            c.vertices.push_back(GpuVertex{rec.vertices[i], (uint32_t)c.constants.size() - 1, rec.header.program, {}});
        ++records;
    }
    std::fclose(f);
    std::printf("%u recorded runs added from %s\n", records, path);
}
// What a shader with ordinary float arithmetic (round to nearest, same order of operations, no operand rule) would
// send for the recorded vertices: printed for information, it decides nothing.
float asF(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
void reportPlainFloat(const Cases& c, size_t firstRecorded) {
    uint64_t n = 0, xy = 0, xyMore = 0, z = 0, fog = 0, colour = 0, st = 0;
    for (size_t i = firstRecorded; i < c.vertices.size(); ++i) {
        const GpuVertex& v = c.vertices[i];
        const vp::Constants& k = c.constants[v.constants];
        const vp::Output exact = vp::evaluate(v.program, k, v.in);
        float clip[4];
        for (int l = 0; l < 4; ++l)
            clip[l] = asF(k.mvp[0][l]) * asF(v.in.pos[0]) + asF(k.mvp[1][l]) * asF(v.in.pos[1]) +
                      asF(k.mvp[2][l]) * asF(v.in.pos[2]) + asF(k.mvp[3][l]) * asF(v.in.pos[3]);
        const float q = 1.0f / clip[3];
        const float depth = std::max(std::min(clip[3], asF(k.fog[2])), asF(k.fog[1]));
        int32_t fixed[4];
        for (int l = 0; l < 4; ++l) {
            float acc = asF(k.offset[l]);
            if (l == 3)
                acc -= asF(k.fog[3]) * depth;
            const double value = (double)(acc + clip[l] * asF(k.scale[l]) * q) * 16.0;
            fixed[l] = value >= 2147483647.0 ? 0x7FFFFFFF : value <= -2147483648.0 ? INT32_MIN : (int32_t)value;
        }
        ++n;
        const int dx = (int)(uint16_t)fixed[0] - (int)exact.x, dy = (int)(uint16_t)fixed[1] - (int)exact.y;
        xy += dx != 0 || dy != 0;
        xyMore += std::abs(dx) > 1 || std::abs(dy) > 1;
        z += (uint32_t)fixed[2] != exact.z;
        fog += (uint8_t)((uint32_t)fixed[3] >> 4) != exact.fog;
        st += bitsOf(asF(v.in.st[0]) * q) != exact.s || bitsOf(asF(v.in.st[1]) * q) != exact.t;
        if (v.program == vp::StripsLit) {
            float term[4];
            for (int l = 0; l < 4; ++l)
                term[l] = std::max(0.0f, asF(k.lightDir[0][l]) * (float)v.in.normal[0] +
                                             asF(k.lightDir[1][l]) * (float)v.in.normal[1] +
                                             asF(k.lightDir[2][l]) * (float)v.in.normal[2] +
                                             asF(k.lightDir[3][l]) * (float)v.in.normal[3]);
            bool same = true;
            const uint8_t want[3] = {exact.r, exact.g, exact.b};
            for (int l = 0; l < 3; ++l) {
                const float sum = (float)k.lightColour[0][l] * term[0] + (float)k.lightColour[1][l] * term[1] +
                                  (float)k.lightColour[2][l] * term[2] + (float)k.lightColour[3][l] * term[3] +
                                  (float)k.colour[l];
                same = same && (uint8_t)(int32_t)std::min(sum, 200.0f) == want[l];
            }
            colour += !same;
        }
    }
    if (n)
        std::printf("for information, ordinary float arithmetic on the %llu recorded vertices: x or y differs on "
                    "%llu (%.2f %%; by more than one sixteenth of a pixel on %llu), depth on %llu (%.1f %%), fog on "
                    "%llu, a colour channel on %llu, s or t bits on %llu (%.1f %%)\n",
                    (unsigned long long)n, (unsigned long long)xy, 100.0 * xy / n, (unsigned long long)xyMore,
                    (unsigned long long)z, 100.0 * z / n, (unsigned long long)fog, (unsigned long long)colour,
                    (unsigned long long)st, 100.0 * st / n);
}
// The entry point a draw binds: VSProgram (the program, then the renderer's ordinary VSMain) must return, bit for
// bit, what VSMain returns for the vertex the CPU evaluation gives (the vertex gs_core would have queued from the
// microprogram's packet). Both run on the same device with the same constants. Returns the number of vertices that
// differ (or 1 after a set-up failure). With a jitter the count is printed for information only.
struct PlainVertex {
    float x, y, z, fog, r, g, b, a, s, t, q; // gsr::DrawVertex
};
uint64_t entryPointDiffers(ID3D11Device* dev, ID3D11DeviceContext* ctx, const Cases& cases, ID3D11Buffer* programVb,
                           ID3D11ShaderResourceView* constants, bool jitter) {
    const std::string source = std::string(kHlslMain) + kHlslVertexProgram;
    const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", "0"}, {nullptr, nullptr}};
    // The same flags as the renderer's ordinary shaders: with D3DCOMPILE_IEEE_STRICTNESS on the program alone the
    // inlined VSMain is compiled differently from the stand-alone one and the positions differ in their last bits.
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> programBlob, plainBlob, err;
    if (FAILED(D3DCompile(source.c_str(), source.size(), "gs_vertex_program", macros, nullptr, "VSProgram", "vs_5_0",
                          flags, 0, &programBlob, &err)) ||
        FAILED(D3DCompile(source.c_str(), source.size(), "gs_shaders", macros, nullptr, "VSMain", "vs_5_0", flags, 0,
                          &plainBlob, &err))) {
        std::printf("  [FAIL] compile of the entry points: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return 1;
    }
    static const D3D11_INPUT_ELEMENT_DESC kProgramLayout[] = {
        {"VPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPNRM", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D11_INPUT_ELEMENT_DESC kPlainLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D11_SO_DECLARATION_ENTRY kOut[] = {
        {0, "SV_Position", 0, 0, 4, 0},
        {0, "COLOR", 0, 0, 4, 0},
        {0, "TEXCOORD", 0, 0, 3, 0},
        {0, "TEXCOORD", 1, 0, 1, 0},
    };
    const UINT outStride = 48, count = (UINT)cases.vertices.size(), zero = 0;
    // The CPU side of the comparison: the vertex gs_core's kick() and toDV() make of a packet vertex.
    const int32_t offsets[4] = {27648, 29184, 0, 0};
    std::vector<PlainVertex> plain(count);
    for (UINT i = 0; i < count; ++i) {
        const GpuVertex& v = cases.vertices[i];
        const vp::Output o = vp::evaluate(v.program, cases.constants[v.constants], v.in);
        PlainVertex& d = plain[i];
        d.x = ((int)o.x - offsets[0]) * (1.0f / 16.0f);
        d.y = ((int)o.y - offsets[1]) * (1.0f / 16.0f);
        d.z = (float)o.z;
        d.fog = o.fog;
        d.r = o.r;
        d.g = o.g;
        d.b = o.b;
        d.a = o.a;
        std::memcpy(&d.s, &o.s, 4);
        std::memcpy(&d.t, &o.t, 4);
        std::memcpy(&d.q, &o.q, 4);
    }
    // cb_rt = target size, z range and scale; cb_g.yz = the sub-pixel jitter of a temporal draw (zero otherwise)
    float consts[48] = {};
    consts[0] = 640.0f;
    consts[1] = 448.0f;
    consts[2] = 16777215.0f;
    consts[3] = 3.0f;
    consts[9 * 4 + 1] = jitter ? 0.000732421875f : 0.0f;
    consts[9 * 4 + 2] = jitter ? -0.00048828125f : 0.0f;
    const auto buffer = [&](UINT bytes, UINT bind, const void* data, D3D11_USAGE usage, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes;
        bd.BindFlags = bind;
        bd.Usage = usage;
        bd.CPUAccessFlags = usage == D3D11_USAGE_STAGING ? D3D11_CPU_ACCESS_READ : 0;
        D3D11_SUBRESOURCE_DATA init{data, 0, 0};
        return SUCCEEDED(dev->CreateBuffer(&bd, data ? &init : nullptr, &out));
    };
    ComPtr<ID3D11VertexShader> programVs, plainVs;
    ComPtr<ID3D11GeometryShader> programSo, plainSo;
    ComPtr<ID3D11InputLayout> programLayout, plainLayout;
    ComPtr<ID3D11Buffer> plainVb, cb0, cb5, out[2], staging[2];
    const auto shader = [&](ID3DBlob* blob, const D3D11_INPUT_ELEMENT_DESC* il, UINT n, ComPtr<ID3D11VertexShader>& vs,
                            ComPtr<ID3D11GeometryShader>& so, ComPtr<ID3D11InputLayout>& layout) {
        return SUCCEEDED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) &&
               SUCCEEDED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                                   kOut, 4, &outStride, 1,
                                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)) &&
               SUCCEEDED(dev->CreateInputLayout(il, n, blob->GetBufferPointer(), blob->GetBufferSize(), &layout));
    };
    if (!shader(programBlob.Get(), kProgramLayout, 4, programVs, programSo, programLayout) ||
        !shader(plainBlob.Get(), kPlainLayout, 3, plainVs, plainSo, plainLayout) ||
        !buffer(count * sizeof(PlainVertex), D3D11_BIND_VERTEX_BUFFER, plain.data(), D3D11_USAGE_DEFAULT, plainVb) ||
        !buffer(sizeof(consts), D3D11_BIND_CONSTANT_BUFFER, consts, D3D11_USAGE_DEFAULT, cb0) ||
        !buffer(16, D3D11_BIND_CONSTANT_BUFFER, offsets, D3D11_USAGE_DEFAULT, cb5) ||
        !buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, D3D11_USAGE_DEFAULT, out[0]) ||
        !buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, D3D11_USAGE_DEFAULT, out[1]) ||
        !buffer(count * outStride, 0, nullptr, D3D11_USAGE_STAGING, staging[0]) ||
        !buffer(count * outStride, 0, nullptr, D3D11_USAGE_STAGING, staging[1])) {
        std::printf("  [FAIL] set-up of the entry point comparison\n");
        return 1;
    }
    ID3D11Buffer* none = nullptr;
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetConstantBuffers(0, 1, cb0.GetAddressOf());
    ctx->VSSetConstantBuffers(5, 1, cb5.GetAddressOf());
    ctx->VSSetShaderResources(8, 1, &constants);
    // the program path
    UINT stride = sizeof(GpuVertex);
    ctx->IASetInputLayout(programLayout.Get());
    ctx->IASetVertexBuffers(0, 1, &programVb, &stride, &zero);
    ctx->VSSetShader(programVs.Get(), nullptr, 0);
    ctx->GSSetShader(programSo.Get(), nullptr, 0);
    ctx->SOSetTargets(1, out[0].GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    // the ordinary path
    stride = sizeof(PlainVertex);
    ctx->IASetInputLayout(plainLayout.Get());
    ctx->IASetVertexBuffers(0, 1, plainVb.GetAddressOf(), &stride, &zero);
    ctx->VSSetShader(plainVs.Get(), nullptr, 0);
    ctx->GSSetShader(plainSo.Get(), nullptr, 0);
    ctx->SOSetTargets(1, out[1].GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    ctx->SOSetTargets(1, &none, &zero);
    D3D11_MAPPED_SUBRESOURCE ms[2]{};
    for (int k = 0; k < 2; ++k) {
        ctx->CopyResource(staging[k].Get(), out[k].Get());
        if (FAILED(ctx->Map(staging[k].Get(), 0, D3D11_MAP_READ, 0, &ms[k]))) {
            std::printf("  [FAIL] read back of the entry point comparison\n");
            return 1;
        }
    }
    const uint32_t* a = static_cast<const uint32_t*>(ms[0].pData);
    const uint32_t* b = static_cast<const uint32_t*>(ms[1].pData);
    uint64_t differ = 0;
    for (UINT i = 0; i < count; ++i)
        if (std::memcmp(a + i * 12, b + i * 12, 48) != 0 && ++differ <= 4 && !jitter) {
            std::printf("  [FAIL] entry point, vertex %u:", i);
            for (int w = 0; w < 12; ++w)
                std::printf(" %08x/%08x", a[i * 12 + w], b[i * 12 + w]);
            std::printf("\n");
        }
    ctx->Unmap(staging[0].Get(), 0);
    ctx->Unmap(staging[1].Get(), 0);
    if (jitter)
        std::printf("for information, the same with a temporal jitter on the position: %llu of %u vertices differ "
                    "(the last bit of a clip coordinate; the prototype draws no jittered picture)\n",
                    (unsigned long long)differ, count);
    else
        std::printf("the draw's entry point against the ordinary vertex shader on the CPU evaluation: %u vertices, "
                    "%llu differ\n",
                    count, (unsigned long long)differ);
    return differ;
}
} // namespace

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const bool warp = std::getenv("GSR_VP_WARP") != nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                                 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        std::printf("no Direct3D 11 device: skipped\n");
        return 0;
    }
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    if (FAILED(dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) ||
        !doubles.DoublePrecisionFloatShaderOps) {
        std::printf("no double precision in shaders on this adapter: skipped\n");
        return 0;
    }
    const D3D_SHADER_MACRO macros[] = {{"VP_RAW_ONLY", "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(kHlslVertexProgram, std::strlen(kHlslVertexProgram), "vertex_program", macros, nullptr,
                          "VSProgramRaw", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0,
                          &blob, &err))) {
        std::printf("[FAIL] compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return 1;
    }
    static const D3D11_INPUT_ELEMENT_DESC kLayout[] = {
        {"VPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPNRM", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D11_SO_DECLARATION_ENTRY kOut[] = {
        {0, "RAWPLACE", 0, 0, 4, 0},
        {0, "RAWCOLOUR", 0, 0, 4, 0},
        {0, "RAWSTQ", 0, 0, 4, 0},
    };
    const UINT outStride = sizeof(GpuOut);
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11GeometryShader> so;
    ComPtr<ID3D11InputLayout> layout;
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), kOut, 3,
                                                         &outStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)) ||
        FAILED(dev->CreateInputLayout(kLayout, 4, blob->GetBufferPointer(), blob->GetBufferSize(), &layout))) {
        std::printf("[FAIL] the device refused the shader\n");
        return 1;
    }

    Cases cases;
    addGenerated(cases, 400000, 20261112u);
    const size_t generated = cases.vertices.size();
    if (const char* path = std::getenv("GSR_VP_RECORDS"))
        addRecords(cases, path);
    reportPlainFloat(cases, generated);

    const auto buffer = [&](UINT bytes, UINT bind, const void* data, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes;
        bd.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA init{data, 0, 0};
        return SUCCEEDED(dev->CreateBuffer(&bd, data ? &init : nullptr, &out));
    };
    const UINT count = (UINT)cases.vertices.size();
    ComPtr<ID3D11Buffer> vb, kb, cb, ob, staging;
    const int32_t offsets[4] = {0, 0, 0, 0};
    ComPtr<ID3D11ShaderResourceView> ksrv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)cases.constants.size() * 16;
    D3D11_BUFFER_DESC stage{};
    stage.ByteWidth = count * outStride;
    stage.Usage = D3D11_USAGE_STAGING;
    stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!buffer(count * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, cases.vertices.data(), vb) ||
        !buffer((UINT)(cases.constants.size() * sizeof(vp::Constants)), D3D11_BIND_SHADER_RESOURCE,
                cases.constants.data(), kb) ||
        !buffer(16, D3D11_BIND_CONSTANT_BUFFER, offsets, cb) ||
        !buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, ob) ||
        FAILED(dev->CreateShaderResourceView(kb.Get(), &sd, &ksrv)) ||
        FAILED(dev->CreateBuffer(&stage, nullptr, &staging))) {
        std::printf("[FAIL] buffers\n");
        return 1;
    }
    const UINT stride = sizeof(GpuVertex), zero = 0;
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &zero);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cb.GetAddressOf());
    ctx->VSSetShaderResources(8, 1, ksrv.GetAddressOf());
    ctx->GSSetShader(so.Get(), nullptr, 0);
    ctx->SOSetTargets(1, ob.GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    ID3D11Buffer* none = nullptr;
    ctx->SOSetTargets(1, &none, &zero);
    ctx->CopyResource(staging.Get(), ob.Get());
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        std::printf("[FAIL] read back\n");
        return 1;
    }
    const GpuOut* got = static_cast<const GpuOut*>(ms.pData);
    uint64_t bad = 0, badPlace = 0, badColour = 0, badStq = 0;
    for (UINT i = 0; i < count; ++i) {
        const GpuVertex& v = cases.vertices[i];
        const vp::Output o = vp::evaluate(v.program, cases.constants[v.constants], v.in);
        const GpuOut& g = got[i];
        const bool place = g.place[0] == o.x && g.place[1] == o.y && g.place[2] == o.z &&
                           ((g.place[3] >> 4) & 0xFF) == o.fog && (((g.place[3] >> 15) & 1u) != 0u) == o.adc;
        const bool colour = g.colour[0] == o.r && g.colour[1] == o.g && g.colour[2] == o.b && g.colour[3] == o.a;
        const bool stq = g.stq[0] == o.s && g.stq[1] == o.t && g.stq[2] == o.q;
        badPlace += !place;
        badColour += !colour;
        badStq += !stq;
        if (!(place && colour && stq) && ++bad <= 8)
            std::printf("  [FAIL] vertex %u (program %u): gpu place %08x %08x %08x %08x colour %u %u %u %u stq %08x "
                        "%08x %08x; cpu %04x %04x %08x fog %u skip %d colour %u %u %u %u stq %08x %08x %08x\n",
                        i, v.program, g.place[0], g.place[1], g.place[2], g.place[3], g.colour[0], g.colour[1],
                        g.colour[2], g.colour[3], g.stq[0], g.stq[1], g.stq[2], o.x, o.y, o.z, o.fog, (int)o.adc, o.r,
                        o.g, o.b, o.a, o.s, o.t, o.q);
    }
    ctx->Unmap(staging.Get(), 0);
    bad += entryPointDiffers(dev.Get(), ctx.Get(), cases, vb.Get(), ksrv.Get(), false);
    entryPointDiffers(dev.Get(), ctx.Get(), cases, vb.Get(), ksrv.Get(), true);
    std::printf("vertex program shader against the CPU evaluation: %u vertices, %llu differ (position, depth or fog "
                "%llu, colour %llu, texture coordinate %llu)\n",
                count, (unsigned long long)bad, (unsigned long long)badPlace, (unsigned long long)badColour,
                (unsigned long long)badStq);
    return bad ? 1 : 0;
}
