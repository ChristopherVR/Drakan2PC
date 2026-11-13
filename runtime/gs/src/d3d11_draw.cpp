// Draws on Direct3D 11: the API calls behind the shared per-draw code (gs_draw.inl).
#include "d3d11_backend_impl.h"

namespace gsr {

namespace {
D3D11_BLEND toD3D(rhi::BlendFactor f) {
    static const D3D11_BLEND map[] = {D3D11_BLEND_ZERO,           D3D11_BLEND_ONE,           D3D11_BLEND_SRC1_COLOR,
                                      D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_SRC1_ALPHA,    D3D11_BLEND_INV_SRC1_ALPHA,
                                      D3D11_BLEND_SRC_ALPHA,      D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA,
                                      D3D11_BLEND_INV_DEST_ALPHA};
    return map[(size_t)f];
}
D3D11_BLEND_OP toD3D(rhi::BlendOp op) {
    static const D3D11_BLEND_OP map[] = {D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_SUBTRACT, D3D11_BLEND_OP_REV_SUBTRACT,
                                         D3D11_BLEND_OP_MIN, D3D11_BLEND_OP_MAX};
    return map[(size_t)op];
}
D3D11_COMPARISON_FUNC toD3D(rhi::Compare c) {
    static const D3D11_COMPARISON_FUNC map[] = {D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_LESS,
                                                D3D11_COMPARISON_EQUAL,         D3D11_COMPARISON_LESS_EQUAL,
                                                D3D11_COMPARISON_GREATER,       D3D11_COMPARISON_NOT_EQUAL,
                                                D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_ALWAYS};
    return map[(size_t)c];
}
} // namespace

void D3D11Backend::Impl::clearTarget(RT& rt, const float rgba[4]) {
    if (rt.rtv)
        ctx->ClearRenderTargetView(rt.rtv.Get(), rgba);
}

// Ring buffer: append with NO_OVERWRITE and discard only on wrap. A DISCARD per draw exhausts the driver's
// renaming pool once the GPU runs a frame behind, and Map then blocks until the GPU catches up.
// A failed buffer or Map (a removed device: d3d11_device_loss.cpp) hands the shared draw code a copy in vbFallback
// instead, so it never writes through a null pointer; nothing of that draw reaches the GPU.
DrawVertex* D3D11Backend::Impl::beginVertices(const DrawVertex* v, uint32_t count) {
    const uint32_t need = count * sizeof(DrawVertex);
    vbMapped = false;
    if (need > vbCapacity && !gone()) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = std::max(need, 16u << 20);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        vb.Reset();
        vbCapacity = 0;
        if (deviceOk(dev->CreateBuffer(&bd, nullptr, &vb), "draw vertex buffer")) {
            vbCapacity = bd.ByteWidth;
            vbOffset = vbCapacity; // the first map discards
        }
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (need <= vbCapacity) {
        D3D11_MAP vbMode = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (need > vbCapacity - vbOffset) {
            vbMode = D3D11_MAP_WRITE_DISCARD;
            vbOffset = 0;
        }
        if (SUCCEEDED(map(vb.Get(), vbMode, ms, "draw vertices", device_loss::Site::Map))) {
            vbDrawOffset = vbOffset;
            vbOffset += need;
            vbMapped = true;
            auto* const out = reinterpret_cast<DrawVertex*>(static_cast<uint8_t*>(ms.pData) + vbDrawOffset);
            std::memcpy(out, v, need);
            return out;
        }
    }
    vbFallback.assign(v, v + count);
    return vbFallback.data();
}

void D3D11Backend::Impl::endVertices() {
    if (vbMapped)
        ctx->Unmap(vb.Get(), 0);
}

void D3D11Backend::Impl::bindDraw(RT& rt, DepthBuf& db, bool useHw, GpuView texture, GpuView dest, float viewportW,
                                  float viewportH, const int32_t scissor[4]) {
    const UINT stride = sizeof(DrawVertex), offs = vbDrawOffset;
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offs);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    ctx->PSSetShader(useHw ? psHw.Get() : psSb.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {texture, dest};
    ctx->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* smps[2] = {smpPt.Get(), smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ctx->RSSetState(rast.Get());
    D3D11_VIEWPORT vp{0, 0, viewportW, viewportH, 0, 1};
    ctx->RSSetViewports(1, &vp);
    const D3D11_RECT sr{scissor[0], scissor[1], scissor[2], scissor[3]};
    ctx->RSSetScissorRects(1, &sr);
    ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
    ctx->OMSetRenderTargets(1, rtvs, db.dsv.Get());
}

void D3D11Backend::Impl::setPass(const PSConsts& k, const rhi::BlendState& blend, const rhi::DepthState& depth) {
    // cbPS is only written here: an upload identical to the buffer's current content is skipped.
    if (!cbPSValid || std::memcmp(&cbPSContent, &k, sizeof(k)) != 0) {
        D3D11_MAPPED_SUBRESOURCE cm{};
        if (SUCCEEDED(map(cbPS.Get(), D3D11_MAP_WRITE_DISCARD, cm, "draw constants", device_loss::Site::Map))) {
            std::memcpy(cm.pData, &k, sizeof(k));
            ctx->Unmap(cbPS.Get(), 0);
            cbPSContent = k;
            cbPSValid = true;
        } else
            cbPSValid = false; // the buffer holds what it held: the next draw uploads again
    }
    ID3D11BlendState* bs =
        getBlendState(blend.enable, toD3D(blend.op), toD3D(blend.src), toD3D(blend.dst), blend.writeMask);
    ctx->OMSetBlendState(bs, nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(getDepthState(toD3D(depth.compare), depth.write), 0);
}

void D3D11Backend::Impl::drawVertices(uint32_t count) {
    if (vbMapped) // the vertices of a failed Map never reached the buffer
        ctx->Draw(count, 0);
}

// The velocity pass of the colour pass just drawn, with the constants setPass uploaded for it. The final draw
// leaves the motion pipeline bound; every later user of the context binds its own state.
void D3D11Backend::Impl::drawVelocity(RT& rt, DepthBuf& db, uint32_t count, rhi::Compare depthCompare, bool restore,
                                      bool materialDraw, bool useHw) {
    if (!vbMapped) // see drawVertices
        return;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rt.motionRtv.GetAddressOf(), db.dsv.Get());
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(getDepthState(toD3D(depthCompare), false), 0);
    ctx->IASetInputLayout(motionLayout.Get());
    const UINT motionStride = sizeof(motion::Previous), motionOffset = motionVbDrawOffset;
    ctx->IASetVertexBuffers(1, 1, motionVb.GetAddressOf(), &motionStride, &motionOffset);
    ctx->VSSetShader(vsMotion.Get(), nullptr, 0);
    ctx->PSSetShader(psMotion.Get(), nullptr, 0);
    ctx->Draw(count, 0);
    if (!restore)
        return;
    ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rtvs, db.dsv.Get());
    ctx->IASetInputLayout(layout.Get());
    ID3D11Buffer* nullBuffer = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(1, 1, &nullBuffer, &zero, &zero);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ID3D11PixelShader* const colorShader =
        materialDraw ? (useHw ? materialGpu.psHw.Get() : materialGpu.psSb.Get()) : (useHw ? psHw.Get() : psSb.Get());
    ctx->PSSetShader(colorShader, nullptr, 0);
}

void D3D11Backend::Impl::endDraw() {
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (terrainGpu.timingOpen)
        endTerrainTiming(); // GSR_TERRAIN_GPU_TIMING
}

bool D3D11Backend::Impl::readTarget(RT& rt, uint32_t w, uint32_t h, Image& out, bool keepAlpha) {
    return readTexture(rt.tex.Get(), w, h, out, keepAlpha);
}

bool D3D11Backend::Impl::readBoundTexture(const TexBind& tb, Image& out) {
    ComPtr<ID3D11Resource> resource;
    tb.srv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    return readTexture(texture.Get(), desc.Width, desc.Height, out);
}

void D3D11Backend::Impl::logTemporalDraw(const RT& rt, const GsDrawState& s, uint32_t fpsm, int sx0, int sy0, int sx1,
                                         int sy1, bool zte, uint32_t ztst) {
    static uint64_t diagnosticFrame = ~0ull;
    static unsigned diagnosticCount = 0;
    if (diagnosticFrame != frame) {
        diagnosticFrame = frame;
        diagnosticCount = 0;
    }
    if (frame >= 1500 && frame < 2600 && diagnosticCount++ < 6)
        std::fprintf(stderr,
                     "[dlss-draw] f%llu cls=%u psm=%x target=%ux%u scissor=%d,%d..%d,%d zte=%d ztst=%u "
                     "sprite=%d vp=%d so=%d\n",
                     (unsigned long long)frame, rt.cls, fpsm, rt.w, rt.h, sx0, sy0, sx1, sy1, (int)zte, ztst,
                     (int)!!(s.flags & DF_SPRITE), (int)camCur.haveVP, (int)camCur.haveSO);
}

} // namespace gsr

#define GSR_IMPL D3D11Backend::Impl
#include "gs_draw.inl"
#undef GSR_IMPL
