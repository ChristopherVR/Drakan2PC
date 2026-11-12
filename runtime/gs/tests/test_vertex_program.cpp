// The generated HLSL of the vertex stages (src/vertex_program_hlsl.h: stage bodies and arithmetic library from the
// one description, gsr/vertex_stage.h) against the CPU evaluation it must equal bit for bit. The shader runs over
// the shared test vertices (tests/vertex_program_cases.h) with stream output, and every integer it returns
// (position, depth, fog lane, colour, the bits of s, t and q) is compared with the stage evaluator. Both arithmetic
// libraries are run: doubles (when the adapter has double support in shaders) and integer limbs (always).
// tests/test_vertex_stage_webgpu.cpp is the same suite for the generated WGSL.
//
// Second part, per library: the entry point a draw binds (the program followed by the renderer's ordinary VSMain)
// against VSMain run on the CPU evaluation, bit for bit over every output, without and with a temporal jitter.
//
// GSR_VP_RECORDS=<file> adds records dumped from a VU1 recording (private data; the test passes without it).
// GSR_VP_WARP=1 runs on the software adapter, GSR_VP_ADAPTER=<n> on the n-th adapter of the machine.
// GSR_VP_ARITHMETIC=doubles | limbs runs one library only.
// GSR_VP_BENCH=<repeats> times the lit strip program (shader invocations per millisecond, stream output).
// GSR_VP_DUMP=<directory> writes the generated HLSL and the disassembly of each compiled shader there.
// GSR_VP_PLAIN_HLSL=<file>, GSR_VP_PROGRAM_STRICT=1: see compilePlain and compileProgram.
// Without a Direct3D 11 device the test is skipped (exit 0).
#include "gs_shaders.h"
#include "vertex_program_cases.h"
#include "vertex_program_hlsl.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#pragma comment(lib, "dxgi")

using Microsoft::WRL::ComPtr;
using namespace gsr;
using vpcases::Cases;
using vpcases::GpuVertex;

namespace {
double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void dump(const char* name, const void* data, size_t size) {
    const char* dir = std::getenv("GSR_VP_DUMP");
    if (!dir)
        return;
    if (FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "wb")) {
        std::fwrite(data, 1, size, f);
        std::fclose(f);
    }
}
void dumpAssembly(const std::string& name, ID3DBlob* blob) {
    ComPtr<ID3DBlob> text;
    if (std::getenv("GSR_VP_DUMP") &&
        SUCCEEDED(D3DDisassemble(blob->GetBufferPointer(), blob->GetBufferSize(), 0, nullptr, &text)))
        dump((name + ".asm").c_str(), text->GetBufferPointer(), text->GetBufferSize() - 1);
}

// What a shader with ordinary float arithmetic (round to nearest, same order of operations, no operand rule) would
// send for the recorded vertices: printed for information, it decides nothing.
float asF(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
void reportPlainFloat(const Cases& c, size_t firstRecorded) {
    using vpcases::bitsOf;
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
const D3D11_INPUT_ELEMENT_DESC kProgramLayout[] = {
    {"VPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPNRM", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool buffer(UINT bytes, UINT bind, const void* data, ComPtr<ID3D11Buffer>& out,
                D3D11_USAGE usage = D3D11_USAGE_DEFAULT) const {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes;
        bd.BindFlags = bind;
        bd.Usage = usage;
        bd.CPUAccessFlags = usage == D3D11_USAGE_STAGING ? D3D11_CPU_ACCESS_READ : 0;
        D3D11_SUBRESOURCE_DATA init{data, 0, 0};
        return SUCCEEDED(dev->CreateBuffer(&bd, data ? &init : nullptr, &out));
    }
    // Blocks until everything submitted so far has run.
    void finish() const {
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> query;
        if (FAILED(dev->CreateQuery(&qd, &query)))
            return;
        ctx->End(query.Get());
        ctx->Flush();
        BOOL done = FALSE;
        while (ctx->GetData(query.Get(), &done, sizeof(done), 0) != S_OK || !done) {
        }
    }
};

// The entry point a draw binds: VSProgram (the program, then the renderer's ordinary VSMain) must return, bit for
// bit, what VSMain returns for the vertex the CPU evaluation gives (the vertex gs_core would have queued from the
// microprogram's packet). Both run on the same device with the same constants and the same compile flags. Returns
// the number of vertices that differ (or 1 after a set-up failure).
struct PlainVertex {
    float x, y, z, fog, r, g, b, a, s, t, q; // gsr::DrawVertex
};
// The ordinary vertex shader, compiled as the renderer compiles it. GSR_VP_PLAIN_HLSL=<file> takes its source from a
// file instead (to show that a change to VSMain leaves its results as they were).
ComPtr<ID3DBlob> compilePlain() {
    std::string source = kHlslMain;
    if (const char* path = std::getenv("GSR_VP_PLAIN_HLSL")) {
        if (FILE* f = std::fopen(path, "rb")) {
            source.clear();
            char buffer[4096];
            for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;)
                source.append(buffer, n);
            std::fclose(f);
            std::printf("the ordinary vertex shader is taken from %s\n", path);
        }
    }
    const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", "0"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(source.c_str(), source.size(), "gs_shaders", macros, nullptr, "VSMain", "vs_5_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
        std::printf("[FAIL] compile of VSMain: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return {};
    }
    dumpAssembly("VSMain", blob.Get());
    return blob;
}
// The draw's entry point for one library. The renderer's flags; GSR_VP_PROGRAM_STRICT=1 adds
// D3DCOMPILE_IEEE_STRICTNESS to this shader alone (the two entry points must agree even then).
ComPtr<ID3DBlob> compileProgram(const char* library, const std::string& programText) {
    const std::string source = std::string(kHlslMain) + programText;
    const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", "0"}, {nullptr, nullptr}};
    const UINT flags =
        D3DCOMPILE_OPTIMIZATION_LEVEL3 | (std::getenv("GSR_VP_PROGRAM_STRICT") ? D3DCOMPILE_IEEE_STRICTNESS : 0);
    ComPtr<ID3DBlob> blob, err;
    const double start = milliseconds();
    if (FAILED(D3DCompile(source.c_str(), source.size(), "gs_vertex_program", macros, nullptr, "VSProgram", "vs_5_0",
                          flags, 0, &blob, &err))) {
        std::printf("[FAIL] %s: compile of VSProgram: %s\n", library, err ? (const char*)err->GetBufferPointer() : "?");
        return {};
    }
    std::printf("%s: the draw's entry point compiled by FXC in %.0f ms to %zu bytes%s\n", library,
                milliseconds() - start, (size_t)blob->GetBufferSize(),
                (flags & D3DCOMPILE_IEEE_STRICTNESS) ? " (with IEEE strictness on this shader alone)" : "");
    dumpAssembly(std::string("VSProgram_") + library, blob.Get());
    return blob;
}
uint64_t entryPointDiffers(const Gpu& gpu, const char* library, ID3DBlob* programBlob, ID3DBlob* plainBlob,
                           const Cases& cases, const std::vector<vp::ir::Raw>& want, ID3D11Buffer* programVb,
                           ID3D11ShaderResourceView* constants, bool jitter) {
    ID3D11Device* dev = gpu.dev.Get();
    ID3D11DeviceContext* ctx = gpu.ctx.Get();
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
        const vp::Output o = vp::ir::Evaluator<>::toOutput(want[i]);
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
    if (!shader(programBlob, kProgramLayout, 4, programVs, programSo, programLayout) ||
        !shader(plainBlob, kPlainLayout, 3, plainVs, plainSo, plainLayout) ||
        !gpu.buffer(count * sizeof(PlainVertex), D3D11_BIND_VERTEX_BUFFER, plain.data(), plainVb) ||
        !gpu.buffer(sizeof(consts), D3D11_BIND_CONSTANT_BUFFER, consts, cb0) ||
        !gpu.buffer(16, D3D11_BIND_CONSTANT_BUFFER, offsets, cb5) ||
        !gpu.buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, out[0]) ||
        !gpu.buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, out[1]) ||
        !gpu.buffer(count * outStride, 0, nullptr, staging[0], D3D11_USAGE_STAGING) ||
        !gpu.buffer(count * outStride, 0, nullptr, staging[1], D3D11_USAGE_STAGING)) {
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
        if (std::memcmp(a + i * 12, b + i * 12, 48) != 0 && ++differ <= 4) {
            std::printf("  [FAIL] entry point (%s%s), vertex %u:", library, jitter ? ", jittered" : "", i);
            for (int w = 0; w < 12; ++w)
                std::printf(" %08x/%08x", a[i * 12 + w], b[i * 12 + w]);
            std::printf("\n");
        }
    ctx->Unmap(staging[0].Get(), 0);
    ctx->Unmap(staging[1].Get(), 0);
    std::printf("%s: the draw's entry point against the ordinary vertex shader on the CPU evaluation%s: %u vertices, "
                "%llu differ\n",
                library, jitter ? ", with a temporal jitter" : "", count, (unsigned long long)differ);
    return differ;
}

// One arithmetic library: compile, run every vertex with stream output, compare. Returns the differing vertices
// (or 1 after a failure).
uint64_t runLibrary(const Gpu& gpu, vp::Arithmetic arithmetic, const Cases& cases, const std::vector<vp::ir::Raw>& want,
                    const std::vector<vpcases::PreviousPlace>& previous, ID3D11Buffer* vb,
                    ID3D11ShaderResourceView* ksrv, ID3DBlob* plainBlob) {
    ID3D11Device* dev = gpu.dev.Get();
    ID3D11DeviceContext* ctx = gpu.ctx.Get();
    const char* library = vp::arithmeticName(arithmetic);
    const std::string text = vertexProgramHlsl(arithmetic, vpcases::testStages(), true);
    dump((std::string("vertex_program_") + library + ".hlsl").c_str(), text.data(), text.size());
    const D3D_SHADER_MACRO macros[] = {{"VP_RAW_ONLY", "1"}, {"VP_PREVIOUS", "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> blob, err;
    const double compileStart = milliseconds();
    if (FAILED(D3DCompile(text.c_str(), text.size(), "vertex_program", macros, nullptr, "VSProgramRaw", "vs_5_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0, &blob, &err))) {
        std::printf("[FAIL] %s: compile: %s\n", library, err ? (const char*)err->GetBufferPointer() : "?");
        return 1;
    }
    std::printf("%s: generated HLSL %zu characters, compiled by FXC in %.0f ms to %zu bytes\n", library, text.size(),
                milliseconds() - compileStart, (size_t)blob->GetBufferSize());
    dumpAssembly(std::string("VSProgramRaw_") + library, blob.Get());
    {
        // what a renderer compiles: the game's two stages without the probes
        const std::string game = vertexProgramHlsl(arithmetic);
        ComPtr<ID3DBlob> gameBlob;
        const double start = milliseconds();
        const D3D_SHADER_MACRO rawOnly[] = {{"VP_RAW_ONLY", "1"}, {nullptr, nullptr}};
        if (FAILED(D3DCompile(game.c_str(), game.size(), "vertex_program", rawOnly, nullptr, "VSProgramRaw", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &gameBlob, &err))) {
            std::printf("[FAIL] %s: compile of the game's stages alone\n", library);
            return 1;
        }
        std::printf("%s: the game's two stages alone: %zu characters, compiled by FXC in %.0f ms to %zu bytes\n",
                    library, game.size(), milliseconds() - start, (size_t)gameBlob->GetBufferSize());
    }
    static const D3D11_SO_DECLARATION_ENTRY kOut[] = {
        {0, "RAWPLACE", 0, 0, 4, 0},
        {0, "RAWCOLOUR", 0, 0, 4, 0},
        {0, "RAWSTQ", 0, 0, 4, 0},
        {0, "RAWPREVIOUS", 0, 0, 4, 0},
    };
    const UINT outStride = 64, count = (UINT)cases.vertices.size();
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11GeometryShader> so;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cb, ob, staging;
    const int32_t offsets[4] = {0, 0, 0, 0};
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), kOut, 4,
                                                         &outStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)) ||
        FAILED(dev->CreateInputLayout(kProgramLayout, 4, blob->GetBufferPointer(), blob->GetBufferSize(), &layout)) ||
        !gpu.buffer(16, D3D11_BIND_CONSTANT_BUFFER, offsets, cb) ||
        !gpu.buffer(count * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, ob) ||
        !gpu.buffer(count * outStride, 0, nullptr, staging, D3D11_USAGE_STAGING)) {
        std::printf("[FAIL] %s: the device refused the shader or a buffer\n", library);
        return 1;
    }
    const UINT stride = sizeof(GpuVertex), zero = 0;
    ID3D11Buffer* none = nullptr;
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &zero);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cb.GetAddressOf());
    ctx->VSSetShaderResources(8, 1, &ksrv);
    ctx->GSSetShader(so.Get(), nullptr, 0);
    ctx->SOSetTargets(1, ob.GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    ctx->SOSetTargets(1, &none, &zero);
    ctx->CopyResource(staging.Get(), ob.Get());
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        std::printf("[FAIL] %s: read back\n", library);
        return 1;
    }
    uint64_t bad = vpcases::compareShader(library, cases, want, previous, static_cast<const uint32_t*>(ms.pData));
    ctx->Unmap(staging.Get(), 0);
    std::printf("%s: generated HLSL against the CPU evaluation (the packet, and the position again with the previous "
                "frame's constants): %u vertices, %llu differ\n",
                library, count, (unsigned long long)bad);

    if (const char* repeatsText = std::getenv("GSR_VP_BENCH")) {
        // the lit strip program alone, 262144 vertices per draw, eight draws per run
        constexpr UINT kBatch = 262144, kDraws = 8;
        std::vector<GpuVertex> lit;
        for (const GpuVertex& v : cases.vertices)
            if (v.program == vp::StripsLit && lit.size() < kBatch)
                lit.push_back(v);
        while (lit.size() < kBatch)
            lit.push_back(lit[lit.size() % 1000]);
        ComPtr<ID3D11Buffer> litVb, litOut;
        if (gpu.buffer(kBatch * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, lit.data(), litVb) &&
            gpu.buffer(kBatch * outStride, D3D11_BIND_STREAM_OUTPUT, nullptr, litOut)) {
            ctx->IASetVertexBuffers(0, 1, litVb.GetAddressOf(), &stride, &zero);
            const auto run = [&] {
                for (UINT d = 0; d < kDraws; ++d) {
                    ctx->SOSetTargets(1, litOut.GetAddressOf(), &zero);
                    ctx->Draw(kBatch, 0);
                }
                gpu.finish();
            };
            run();
            std::vector<double> rates;
            const int repeats = std::max(3, std::atoi(repeatsText));
            for (int r = 0; r < repeats; ++r) {
                const double start = milliseconds();
                run();
                rates.push_back(double(kBatch) * kDraws / (milliseconds() - start));
            }
            ctx->SOSetTargets(1, &none, &zero);
            std::sort(rates.begin(), rates.end());
            std::printf("%s: lit strip program, %.0f invocations per ms (median of %d runs of %u x %u; lowest %.0f, "
                        "highest %.0f)\n",
                        library, rates[rates.size() / 2], repeats, kDraws, kBatch, rates.front(), rates.back());
        }
    }
    const ComPtr<ID3DBlob> programBlob = compileProgram(library, text);
    if (!programBlob || !plainBlob)
        return bad + 1;
    bad += entryPointDiffers(gpu, library, programBlob.Get(), plainBlob, cases, want, vb, ksrv, false);
    bad += entryPointDiffers(gpu, library, programBlob.Get(), plainBlob, cases, want, vb, ksrv, true);
    return bad;
}
} // namespace

int main() {
    Gpu gpu;
    const bool warp = std::getenv("GSR_VP_WARP") != nullptr;
    ComPtr<IDXGIAdapter> chosen;
    if (const char* index = std::getenv("GSR_VP_ADAPTER")) {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapters((UINT)std::atoi(index), &chosen))) {
            std::printf("no adapter %s: skipped\n", index);
            return 0;
        }
    }
    const D3D_DRIVER_TYPE type = chosen ? D3D_DRIVER_TYPE_UNKNOWN
                                 : warp ? D3D_DRIVER_TYPE_WARP
                                        : D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(D3D11CreateDevice(chosen.Get(), type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &gpu.dev, nullptr,
                                 &gpu.ctx))) {
        std::printf("no Direct3D 11 device: skipped\n");
        return 0;
    }
    char adapterName[160] = "?";
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(gpu.dev.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetDesc(&desc)))
            std::snprintf(adapterName, sizeof(adapterName), "%ls", desc.Description);
    }
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    const bool doubleSupport =
        SUCCEEDED(gpu.dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
        doubles.DoublePrecisionFloatShaderOps;
    std::printf("Direct3D 11 on %s, double support in shaders: %s\n", adapterName, doubleSupport ? "yes" : "no");

    const vpcases::StandardCases standard = vpcases::standardCases();
    const Cases& cases = standard.cases;
    reportPlainFloat(cases, standard.generated + standard.probes);
    const std::vector<vp::ir::Raw> want = vpcases::reference(cases);
    const std::vector<vpcases::PreviousPlace> previous = vpcases::referencePrevious(cases);
    std::printf("%zu vertices: %zu generated, %zu probes, %zu recorded\n", cases.vertices.size(), standard.generated,
                standard.probes, standard.recorded);

    const UINT count = (UINT)cases.vertices.size();
    ComPtr<ID3D11Buffer> vb, kb;
    ComPtr<ID3D11ShaderResourceView> ksrv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)cases.constants.size() * 16;
    if (!gpu.buffer(count * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, cases.vertices.data(), vb) ||
        !gpu.buffer((UINT)(cases.constants.size() * sizeof(vp::Constants)), D3D11_BIND_SHADER_RESOURCE,
                    cases.constants.data(), kb) ||
        FAILED(gpu.dev->CreateShaderResourceView(kb.Get(), &sd, &ksrv))) {
        std::printf("[FAIL] buffers\n");
        return 1;
    }
    // The library choice a renderer makes per adapter (vp::chooseArithmetic); here every available one is run.
    const vp::ArithmeticRequest only = vp::parseArithmeticRequest(std::getenv("GSR_VP_ARITHMETIC"));
    const ComPtr<ID3DBlob> plainBlob = compilePlain();
    uint64_t bad = 0;
    for (const vp::ArithmeticRequest request : {vp::ArithmeticRequest::Doubles, vp::ArithmeticRequest::Limbs}) {
        if (only != vp::ArithmeticRequest::Auto && only != request)
            continue;
        const vp::Arithmetic arithmetic = vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport, request);
        if (arithmetic == vp::Arithmetic::None) {
            std::printf("doubles: not available on this adapter (a renderer evaluates on the CPU or takes limbs)\n");
            continue;
        }
        bad += runLibrary(gpu, arithmetic, cases, want, previous, vb.Get(), ksrv.Get(), plainBlob.Get());
    }
    std::printf("vertex program shaders against the CPU evaluation: %llu differ in total\n", (unsigned long long)bad);
    return bad ? 1 : 0;
}
