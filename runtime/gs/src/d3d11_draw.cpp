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
        ctx.raw()->Unmap(vb.Get(), 0);
}

namespace {
uint64_t word(const void* p) {
    return (uint64_t)(uintptr_t)p;
}
uint64_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}
} // namespace

// The draw path binds through ctx.raw() and the draw state cache (draw_state_cache.h). With [video]
// draw_state_cache off every slot is set as before (Cache::set is always true) and endDraw releases at once; with it
// on, a slot that already holds the value is left alone and the release of a draw is held until another user of the
// context (TrackedContext) or a draw that samples a render target.
void D3D11Backend::Impl::releaseDrawBindings() {
    ID3D11DeviceContext* const c = ctx.raw();
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    c->PSSetShaderResources(0, 2, nulls);
    c->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11Backend::Impl::foreignContext() {
    if (!drawState.enabled())
        return;
    if (drawState.take())
        releaseDrawBindings();
}

void D3D11Backend::Impl::logDrawState(bool force) {
    if (!drawState.enabled() || (!force && (runtimeFlags().profileSeconds <= 0 || frame < drawStateLogFrame + 240)))
        return;
    drawStateLogFrame = frame;
    const auto& n = drawState.counters();
    std::fprintf(stderr,
                 "[draw-state] frame %llu: draws %llu, binds made %llu, left out %llu, state dropped %llu, releases "
                 "held %llu, carried out %llu\n",
                 (unsigned long long)frame, (unsigned long long)n.draws, (unsigned long long)n.sets,
                 (unsigned long long)n.skipped, (unsigned long long)n.forgets, (unsigned long long)n.holds,
                 (unsigned long long)n.releases);
}

void D3D11Backend::Impl::bindDraw(RT& rt, DepthBuf& db, bool useHw, GpuView texture, GpuView dest, float viewportW,
                                  float viewportH, const int32_t scissor[4], bool samplesTarget) {
    using draw_state::Slot;
    ID3D11DeviceContext* const c = ctx.raw();
    logDrawState(false);
    if (drawStateToggle) {
        const bool want = (frame / drawStateToggle) % 2 == 1;
        if (want != drawState.enabled()) {
            foreignContext(); // a held release first
            drawState.enable(want);
        }
    }
    drawState.beginDraw();
    drawSampledTarget = samplesTarget;
    if (samplesTarget)
        foreignContext(); // a target is read: bound from a released state, as without the cache
    const UINT stride = sizeof(DrawVertex), offs = vbDrawOffset;
    if (drawState.set(Slot::InputLayout, word(layout.Get())))
        c->IASetInputLayout(layout.Get());
    if (drawState.set(Slot::VertexBuffer0, draw_state::pair(word(vb.Get()), offs)))
        c->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offs);
    if (drawState.set(Slot::Topology, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST))
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (drawState.set(Slot::VertexShader, word(vs.Get())))
        c->VSSetShader(vs.Get(), nullptr, 0);
    if (drawState.set(Slot::VertexConstants, word(cbPS.Get())))
        c->VSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    if (drawState.set(Slot::PixelConstants, word(cbPS.Get())))
        c->PSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    ID3D11PixelShader* const ps = useHw ? psHw.Get() : psSb.Get();
    if (drawState.set(Slot::PixelShader, word(ps)))
        c->PSSetShader(ps, nullptr, 0);
    if (drawState.set(Slot::Textures, draw_state::pair(word(texture), word(dest)))) {
        ID3D11ShaderResourceView* srvs[2] = {texture, dest};
        c->PSSetShaderResources(0, 2, srvs);
    }
    if (drawState.set(Slot::Samplers, draw_state::pair(word(smpPt.Get()), word(smpLin.Get())))) {
        ID3D11SamplerState* smps[2] = {smpPt.Get(), smpLin.Get()};
        c->PSSetSamplers(0, 2, smps);
    }
    if (drawState.set(Slot::Rasterizer, word(rast.Get())))
        c->RSSetState(rast.Get());
    if (drawState.set(Slot::Viewport, draw_state::pair(floatBits(viewportW), floatBits(viewportH)))) {
        D3D11_VIEWPORT vp{0, 0, viewportW, viewportH, 0, 1};
        c->RSSetViewports(1, &vp);
    }
    if (drawState.set(Slot::Scissor, draw_state::words(scissor, 4 * sizeof(int32_t)))) {
        const D3D11_RECT sr{scissor[0], scissor[1], scissor[2], scissor[3]};
        c->RSSetScissorRects(1, &sr);
    }
    if (drawState.set(Slot::Targets, draw_state::pair(word(rt.rtv.Get()), word(db.dsv.Get())))) {
        ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
        c->OMSetRenderTargets(1, rtvs, db.dsv.Get());
    }
}

void D3D11Backend::Impl::setPass(const PSConsts& k, const rhi::BlendState& blend, const rhi::DepthState& depth) {
    // cbPS is only written here: an upload identical to the buffer's current content is skipped.
    if (!cbPSValid || std::memcmp(&cbPSContent, &k, sizeof(k)) != 0) {
        CpuProfile::Scope constantsProfile(cpuProfile, CpuProfile::DrawConstants);
        D3D11_MAPPED_SUBRESOURCE cm{};
        if (SUCCEEDED(map(cbPS.Get(), D3D11_MAP_WRITE_DISCARD, cm, "draw constants", device_loss::Site::Map))) {
            std::memcpy(cm.pData, &k, sizeof(k));
            ctx.raw()->Unmap(cbPS.Get(), 0);
            cbPSContent = k;
            cbPSValid = true;
        } else
            cbPSValid = false; // the buffer holds what it held: the next draw uploads again
    }
    ID3D11DeviceContext* const c = ctx.raw();
    ID3D11BlendState* bs =
        getBlendState(blend.enable, toD3D(blend.op), toD3D(blend.src), toD3D(blend.dst), blend.writeMask);
    if (drawState.set(draw_state::Slot::Blend, word(bs)))
        c->OMSetBlendState(bs, nullptr, 0xFFFFFFFFu);
    ID3D11DepthStencilState* const ds = getDepthState(toD3D(depth.compare), depth.write);
    if (drawState.set(draw_state::Slot::Depth, word(ds)))
        c->OMSetDepthStencilState(ds, 0);
}

void D3D11Backend::Impl::drawVertices(uint32_t count) {
    if (vbMapped) // the vertices of a failed Map never reached the buffer
        ctx.raw()->Draw(count, 0);
}

// The velocity pass of the colour pass just drawn, with the constants setPass uploaded for it. The final draw
// leaves the motion pipeline bound; every later user of the context binds its own state.
void D3D11Backend::Impl::drawVelocity(RT& rt, DepthBuf& db, uint32_t count, rhi::Compare depthCompare, bool restore,
                                      bool materialDraw, bool useHw) {
    if (!vbMapped) // see drawVertices
        return;
    using draw_state::Slot;
    ID3D11DeviceContext* const c = ctx.raw();
    // Without the cache the targets are unbound first, as always; with it the new target replaces the old one.
    if (!drawState.enabled())
        c->OMSetRenderTargets(0, nullptr, nullptr);
    if (drawState.set(Slot::Targets, draw_state::pair(word(rt.motionRtv.Get()), word(db.dsv.Get()))))
        c->OMSetRenderTargets(1, rt.motionRtv.GetAddressOf(), db.dsv.Get());
    if (drawState.set(Slot::Blend, word(blendOff.Get())))
        c->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ID3D11DepthStencilState* const ds = getDepthState(toD3D(depthCompare), false);
    if (drawState.set(Slot::Depth, word(ds)))
        c->OMSetDepthStencilState(ds, 0);
    if (drawState.set(Slot::InputLayout, word(motionLayout.Get())))
        c->IASetInputLayout(motionLayout.Get());
    const UINT motionStride = sizeof(motion::Previous), motionOffset = motionVbDrawOffset;
    if (drawState.set(Slot::VertexBuffer1, draw_state::pair(word(motionVb.Get()), motionOffset)))
        c->IASetVertexBuffers(1, 1, motionVb.GetAddressOf(), &motionStride, &motionOffset);
    if (drawState.set(Slot::VertexShader, word(vsMotion.Get())))
        c->VSSetShader(vsMotion.Get(), nullptr, 0);
    if (drawState.set(Slot::PixelShader, word(psMotion.Get())))
        c->PSSetShader(psMotion.Get(), nullptr, 0);
    c->Draw(count, 0);
    if (!restore)
        return;
    if (!drawState.enabled())
        c->OMSetRenderTargets(0, nullptr, nullptr);
    if (drawState.set(Slot::Targets, draw_state::pair(word(rt.rtv.Get()), word(db.dsv.Get())))) {
        ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
        c->OMSetRenderTargets(1, rtvs, db.dsv.Get());
    }
    if (drawState.set(Slot::InputLayout, word(layout.Get())))
        c->IASetInputLayout(layout.Get());
    if (drawState.set(Slot::VertexBuffer1, draw_state::pair(0, 0))) {
        ID3D11Buffer* nullBuffer = nullptr;
        const UINT zero = 0;
        c->IASetVertexBuffers(1, 1, &nullBuffer, &zero, &zero);
    }
    if (drawState.set(Slot::VertexShader, word(vs.Get())))
        c->VSSetShader(vs.Get(), nullptr, 0);
    ID3D11PixelShader* const colorShader =
        materialDraw ? (useHw ? materialGpu.psHw.Get() : materialGpu.psSb.Get()) : (useHw ? psHw.Get() : psSb.Get());
    if (drawState.set(Slot::PixelShader, word(colorShader)))
        c->PSSetShader(colorShader, nullptr, 0);
}

void D3D11Backend::Impl::endDraw() {
    if (drawState.endDraw(drawSampledTarget)) {
        releaseDrawBindings();
        drawState.released(draw_state::pair(0, 0), draw_state::pair(0, 0));
    }
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
