#include "settings/OverlayWebGpu.h"

#include <rhi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace dsettings {

namespace {

namespace rhi = gsr::rhi;

// Dear ImGui's vertex expanded for the rhi (float attributes only, no index buffer): position, uv, colour.
struct Vertex {
    float x, y, u, v;
    float r, g, b, a;
};

struct Constants {
    float scale[2], offset[2]; // pixels -> clip space
    float mode[4];             // x = 1: output alpha replaced by y (the "opaque" callback's constant factor)
};

const char* kWgslImGui = R"WGSL(
struct C {
    scale: vec2f,
    offset: vec2f,
    mode: vec4f,
}
@group(0) @binding(0) var<uniform> c: C;
@group(0) @binding(8) var Tex: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;
struct V {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}
@vertex fn vs(@location(0) p: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> V {
    var o: V;
    o.pos = vec4f(p * c.scale + c.offset, 0.0, 1.0);
    o.uv = uv;
    o.color = color;
    return o;
}
@fragment fn fs(i: V) -> @location(0) vec4f {
    let s = i.color * textureSample(Tex, SmpLin, i.uv);
    if (c.mode.x > 0.5) { return vec4f(s.rgb, c.mode.y); }
    return s;
}
)WGSL";

class WebGpuOverlayRenderer;
// The renderer inside render() on this thread: the blend callbacks act on it.
thread_local WebGpuOverlayRenderer* t_rendering = nullptr;
void blendAdditive(const ImDrawList*, const ImDrawCmd*);
void blendOpaque(const ImDrawList*, const ImDrawCmd* cmd);

rhi::BlendState normalBlend() {
    rhi::BlendState b;
    b.enable = true;
    b.src = rhi::BlendFactor::SrcAlpha;
    b.dst = rhi::BlendFactor::OneMinusSrcAlpha;
    b.alphaSrc = rhi::BlendFactor::One;
    b.alphaDst = rhi::BlendFactor::OneMinusSrcAlpha;
    return b;
}
rhi::BlendState additiveBlend() {
    rhi::BlendState b;
    b.enable = true;
    b.src = rhi::BlendFactor::One;
    b.dst = rhi::BlendFactor::One;
    b.alphaSrc = rhi::BlendFactor::Zero;
    b.alphaDst = rhi::BlendFactor::One;
    return b;
}
// src * k + dst * (1 - k), with k written as the fragment's alpha (Constants::mode).
rhi::BlendState opaqueBlend() {
    rhi::BlendState b;
    b.enable = true;
    b.src = rhi::BlendFactor::SrcAlpha;
    b.dst = rhi::BlendFactor::OneMinusSrcAlpha;
    b.alphaSrc = rhi::BlendFactor::Zero;
    b.alphaDst = rhi::BlendFactor::One;
    return b;
}

class WebGpuOverlayRenderer final : public OverlayRenderer {
public:
    explicit WebGpuOverlayRenderer(rhi::Device& device) : gpu_(device) {}

    bool init() override {
        rhi::ProgramDesc d;
        d.wgsl = kWgslImGui;
        d.uniformSlots = 1;
        d.textureSlots = 1;
        d.samplers = true;
        d.vertexStride = sizeof(Vertex);
        d.attributes = {{0, 0, 2}, {1, 8, 2}, {2, 16, 4}};
        d.label = "settings overlay";
        std::string error;
        program_ = gpu_.createProgram(d, &error);
        if (!program_) {
            std::fprintf(stderr, "[settings] WebGPU overlay shader failed: %s\n", error.c_str());
            return false;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "drakan_rhi_webgpu";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        return true;
    }
    void shutdown() override {
        invalidateFonts();
        textures_.clear();
        frame_.reset();
        program_.reset();
    }
    void newFrame() override {
        if (font_)
            return;
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        font_ = createTexture(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        io.Fonts->SetTexID(font_);
    }
    void invalidateFonts() override {
        if (font_)
            releaseTexture(font_);
        font_ = 0;
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().Fonts->SetTexID(0);
    }

    ImTextureID createTexture(const void* rgba, uint32_t w, uint32_t h) override {
        if (!rgba || !w || !h)
            return 0;
        rhi::TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = rhi::Format::RGBA8;
        d.usage = rhi::Sampled | rhi::CopyDst;
        d.label = "overlay image";
        rhi::TexturePtr t = gpu_.createTexture(d);
        if (!t)
            return 0;
        gpu_.writeTexture(*t, 0, 0, 0, w, h, rgba);
        textures_[t.get()] = t;
        return reinterpret_cast<ImTextureID>(t.get());
    }
    void releaseTexture(ImTextureID texture) override { textures_.erase(reinterpret_cast<rhi::Texture*>(texture)); }

    ImTextureID copyTarget(void* target, uint32_t* w, uint32_t* h) override {
        auto* t = static_cast<rhi::Texture*>(target);
        if (!t || !(t->desc().usage & rhi::CopySrc))
            return 0;
        if (!frame_ || frame_->width() != t->width() || frame_->height() != t->height() ||
            frame_->desc().format != t->desc().format) {
            rhi::TextureDesc d;
            d.width = t->width();
            d.height = t->height();
            d.format = t->desc().format;
            d.usage = rhi::Sampled | rhi::CopyDst;
            d.label = "overlay frame copy";
            frame_ = gpu_.createTexture(d);
            if (!frame_)
                return 0;
        }
        gpu_.copyTexture(*t, 0, 0, 0, *frame_, 0, 0, 0, t->width(), t->height());
        *w = frame_->width();
        *h = frame_->height();
        return reinterpret_cast<ImTextureID>(frame_.get());
    }
    ImDrawCallback additiveCallback() override { return &blendAdditive; }
    ImDrawCallback opaqueCallback() override { return &blendOpaque; }

    enum class Mode { Normal, Additive, Opaque };
    // The blend for the following draws; `fade` = the opaque mode's constant factor.
    void setMode(Mode mode, float fade = 1.0f) {
        k_.mode[0] = mode == Mode::Opaque ? 1.0f : 0.0f;
        k_.mode[1] = std::clamp(fade, 0.0f, 1.0f);
        gpu_.setBlend(mode == Mode::Additive ? additiveBlend() : mode == Mode::Opaque ? opaqueBlend() : normalBlend());
        gpu_.setUniforms(0, &k_, sizeof k_);
    }

    void render(void* target, uint32_t w, uint32_t h, ImDrawData* data) override {
        auto* t = static_cast<rhi::Texture*>(target);
        if (!t || !data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0 || !program_)
            return;
        k_ = {};
        k_.scale[0] = 2.0f / data->DisplaySize.x;
        k_.scale[1] = -2.0f / data->DisplaySize.y;
        k_.offset[0] = -1.0f - data->DisplayPos.x * k_.scale[0];
        k_.offset[1] = 1.0f - data->DisplayPos.y * k_.scale[1];
        gpu_.setTargets(t, nullptr, 0);
        gpu_.setProgram(program_.get());
        gpu_.setDepth({});
        gpu_.setTopology(rhi::Topology::TriangleList);
        gpu_.setViewport(0, 0, static_cast<float>(t->width()), static_cast<float>(t->height()));
        setMode(Mode::Normal);
        t_rendering = this;
        const ImVec2 clipOff = data->DisplayPos, clipScale = data->FramebufferScale;
        const float fbW = std::min(static_cast<float>(w), static_cast<float>(t->width()));
        const float fbH = std::min(static_cast<float>(h), static_cast<float>(t->height()));
        for (int n = 0; n < data->CmdListsCount; ++n) {
            const ImDrawList* list = data->CmdLists[n];
            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.UserCallback) {
                    if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
                        setMode(Mode::Normal);
                    else
                        cmd.UserCallback(list, &cmd);
                    continue;
                }
                const float x0 = std::max(0.0f, (cmd.ClipRect.x - clipOff.x) * clipScale.x);
                const float y0 = std::max(0.0f, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
                const float x1 = std::min(fbW, (cmd.ClipRect.z - clipOff.x) * clipScale.x);
                const float y1 = std::min(fbH, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
                if (x1 <= x0 || y1 <= y0 || !cmd.ElemCount)
                    continue;
                auto* tex = reinterpret_cast<rhi::Texture*>(cmd.GetTexID());
                if (!tex)
                    continue;
                vertices_.resize(cmd.ElemCount);
                const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset;
                const ImDrawVert* vtx = list->VtxBuffer.Data + cmd.VtxOffset;
                for (unsigned i = 0; i < cmd.ElemCount; ++i) {
                    const ImDrawVert& s = vtx[idx[i]];
                    Vertex& o = vertices_[i];
                    o.x = s.pos.x;
                    o.y = s.pos.y;
                    o.u = s.uv.x;
                    o.v = s.uv.y;
                    o.r = ((s.col >> IM_COL32_R_SHIFT) & 0xff) / 255.0f;
                    o.g = ((s.col >> IM_COL32_G_SHIFT) & 0xff) / 255.0f;
                    o.b = ((s.col >> IM_COL32_B_SHIFT) & 0xff) / 255.0f;
                    o.a = ((s.col >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
                }
                const int32_t sx = static_cast<int32_t>(x0), sy = static_cast<int32_t>(y0);
                gpu_.setScissor({sx, sy, static_cast<uint32_t>(x1) - static_cast<uint32_t>(sx),
                                 static_cast<uint32_t>(y1) - static_cast<uint32_t>(sy)});
                gpu_.setTexture(0, tex);
                gpu_.draw(vertices_.data(), cmd.ElemCount);
            }
        }
        t_rendering = nullptr;
    }

    bool readTarget(void* target, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgba) override {
        auto* t = static_cast<rhi::Texture*>(target);
        if (!t || !(t->desc().usage & rhi::CopySrc))
            return false;
        const rhi::Format f = t->desc().format;
        if (f != rhi::Format::RGBA8 && f != rhi::Format::BGRA8)
            return false;
        if (!gpu_.readTexture(*t, 0, 0, 0, t->width(), t->height(), rgba))
            return false;
        w = t->width();
        h = t->height();
        if (f == rhi::Format::BGRA8)
            for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                std::swap(rgba[i], rgba[i + 2]);
        return true;
    }

private:
    rhi::Device& gpu_;
    rhi::ProgramPtr program_;
    ImTextureID font_ = 0;
    std::unordered_map<rhi::Texture*, rhi::TexturePtr> textures_;
    rhi::TexturePtr frame_;
    std::vector<Vertex> vertices_;
    Constants k_{};
};

void blendAdditive(const ImDrawList*, const ImDrawCmd*) {
    if (t_rendering)
        t_rendering->setMode(WebGpuOverlayRenderer::Mode::Additive);
}
// The frame copy has no usable alpha: "opaque" crops blend by the screen's fade (callback data, a float; 1 without).
void blendOpaque(const ImDrawList*, const ImDrawCmd* cmd) {
    float fade = 1.0f;
    if (cmd && cmd->UserCallbackData && cmd->UserCallbackDataSize == sizeof(float))
        std::memcpy(&fade, cmd->UserCallbackData, sizeof fade);
    if (t_rendering)
        t_rendering->setMode(WebGpuOverlayRenderer::Mode::Opaque, fade);
}

} // namespace

std::unique_ptr<OverlayRenderer> createWebGpuOverlayRenderer(gsr::rhi::Device& device) {
    return std::make_unique<WebGpuOverlayRenderer>(device);
}

} // namespace dsettings
