// Material detail shading, backend-neutral part (no graphics API here): the material table of a texture pack, the
// rule for which draws take part, the per-draw shader constants, the packed material texel and a CPU transcription of
// the shader arithmetic (gs_material.inc) for tests. Both hardware backends are meant to consume this header; the
// Direct3D 11 one does (d3d11_material.cpp). docs/reference/hd-textures.md, "Material maps".
#pragma once
#include <gsr/scene_guide_camera.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace gsr::material {

// Class ids of materials.txt (analysis/scripts/material_maps.py, CLASSES). Stable: new classes go at the end.
enum Class : uint8_t { None = 0, Terrain, Stone, Wood, Metal, Character, Foliage, Surface, ClassCount };

inline const char* className(uint8_t c) {
    static const char* const names[ClassCount] = {"none",  "terrain",   "stone",   "wood",
                                                  "metal", "character", "foliage", "surface"};
    return c < ClassCount ? names[c] : "none";
}

// Player setting `material_detail`: which classes are shaded.
//   Off        today's picture (the material shaders are never bound)
//   Hard       terrain, stone, wood, metal and generic surfaces: relief that reads as carved or built
//   All        also characters and foliage (painted cloth folds and leaves get relief; opt-in, it changes their look)
enum class Mode : uint8_t { Off = 0, Hard = 1, All = 2 };

constexpr uint32_t classMask(Mode mode) {
    const uint32_t hard = (1u << Terrain) | (1u << Stone) | (1u << Wood) | (1u << Metal) | (1u << Surface);
    return mode == Mode::Off ? 0u : mode == Mode::Hard ? hard : hard | (1u << Character) | (1u << Foliage);
}

struct Record {
    uint8_t cls = None;
    float roughness = 0.78f, metallic = 0;
};

// materials.txt: "<16 hex hash> <class id> <class> <mean roughness> <metallic>", '#' comments.
class Table {
public:
    void clear() { records_.clear(); }
    bool empty() const { return records_.empty(); }
    size_t size() const { return records_.size(); }
    // Returns the number of records read from the text.
    size_t parse(const char* text) {
        size_t added = 0;
        while (text && *text) {
            const char* end = std::strchr(text, '\n');
            const size_t length = end ? size_t(end - text) : std::strlen(text);
            if (length >= 18 && text[0] != '#') {
                char line[160];
                const size_t n = std::min(length, sizeof(line) - 1);
                std::memcpy(line, text, n);
                line[n] = 0;
                char* after = nullptr;
                const uint64_t hash = std::strtoull(line, &after, 16);
                unsigned cls = 0;
                char name[32];
                float roughness = 0.78f, metallic = 0;
                if (after == line + 16 &&
                    std::sscanf(after, " %u %31s %f %f", &cls, name, &roughness, &metallic) >= 1 && cls > None &&
                    cls < ClassCount) {
                    Record r;
                    r.cls = uint8_t(cls);
                    r.roughness = std::isfinite(roughness) ? std::clamp(roughness, 0.05f, 1.0f) : 0.78f;
                    r.metallic = std::isfinite(metallic) ? std::clamp(metallic, 0.0f, 1.0f) : 0.0f;
                    records_[hash] = r;
                    ++added;
                }
            }
            if (!end)
                break;
            text = end + 1;
        }
        return added;
    }
    bool load(const std::string& path) {
        clear();
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        std::string text;
        char buffer[65536];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            text.append(buffer, n);
        std::fclose(f);
        parse(text.c_str());
        return !records_.empty();
    }
    const Record* find(uint64_t hash) const {
        const auto it = records_.find(hash);
        return it == records_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<uint64_t, Record> records_;
};

// File names of a material next to the HD image <dir>/<hash>.png: <dir>/materials/<hash>_n.png and _orm.png.
inline std::string mapPath(const std::string& packDir, uint64_t hash, const char* suffix) {
    char name[40];
    std::snprintf(name, sizeof(name), "/materials/%016llx_%s.png", (unsigned long long)hash, suffix);
    return packDir + name;
}

// The texel the renderer samples: R, G = normal x, y (128 = 0, +y toward the image bottom), B = occlusion divided by
// the map's mean occlusion (200 = 1.0, so shading with it keeps the texture's mean brightness), A = roughness.
// `normal` and `orm` are the decoded images (R in the low byte); either may be empty (flat normal / class roughness).
inline void packTexels(const std::vector<uint32_t>& normal, const std::vector<uint32_t>& orm, size_t count,
                       float classRoughness, std::vector<uint32_t>& out) {
    out.resize(count);
    const bool haveNormal = normal.size() == count, haveOrm = orm.size() == count;
    double meanOcclusion = 255.0;
    if (haveOrm && count) {
        uint64_t sum = 0;
        for (uint32_t p : orm)
            sum += p & 255u;
        meanOcclusion = std::max(1.0, double(sum) / double(count));
    }
    const uint32_t flatRoughness = uint32_t(std::clamp(classRoughness, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t nx = haveNormal ? (normal[i] & 255u) : 128u, ny = haveNormal ? ((normal[i] >> 8) & 255u) : 128u;
        uint32_t occlusion = 200u, roughness = flatRoughness;
        if (haveOrm) {
            occlusion = uint32_t(std::clamp(double(orm[i] & 255u) / meanOcclusion * 200.0 + 0.5, 0.0, 255.0));
            roughness = (orm[i] >> 8) & 255u;
        }
        out[i] = nx | (ny << 8) | (occlusion << 16) | (roughness << 24);
    }
}

// 2x2 box filter for the material mip chain. Normals average as vectors (x, y only: a rough area flattens toward
// the surface normal with distance, which is what removes shimmer), occlusion and roughness as scalars.
inline void downsample(const std::vector<uint32_t>& src, uint32_t w, uint32_t h, std::vector<uint32_t>& out) {
    const uint32_t ow = std::max(1u, w / 2), oh = std::max(1u, h / 2);
    out.resize(size_t(ow) * oh);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x) {
            const uint32_t x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
            const uint32_t y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
            const uint32_t p[4] = {src[size_t(y0) * w + x0], src[size_t(y0) * w + x1], src[size_t(y1) * w + x0],
                                   src[size_t(y1) * w + x1]};
            uint32_t o = 0;
            for (int c = 0; c < 32; c += 8)
                o |=
                    ((((p[0] >> c) & 255u) + ((p[1] >> c) & 255u) + ((p[2] >> c) & 255u) + ((p[3] >> c) & 255u) + 2u) >>
                     2)
                    << c;
            out[size_t(y) * ow + x] = o;
        }
}

// What a draw must be to take material shading (besides having a material of an enabled class): a perspective,
// depth-tested, textured triangle of the 3D world whose texture colour is modulated by the vertex colour. Sprites,
// UI, decal / highlight texture functions, sky layers (drawn without a depth test), render-target textures and
// region-wrapped textures (the hardware sampler of the material map cannot repeat a sub-rectangle) are left alone.
struct DrawTraits {
    bool worldOpen = false; // the engine's world phase is open and its camera is valid
    bool textured = false;  // a decoded (not render target) texture is bound
    bool sprite = false, fst = false;
    bool depthTested = false;      // ZTE with GEQUAL / GREATER
    uint32_t textureFunction = 0;  // TEX0.TFX
    uint32_t wrapS = 0, wrapT = 0; // CLAMP.WMS / WMT
    bool additive = false;         // a blend that adds the source to the frame (glows): not a surface
};
constexpr bool eligible(const DrawTraits& d) {
    return d.worldOpen && d.textured && !d.sprite && !d.fst && d.depthTested && d.textureFunction == 0 &&
           d.wrapS <= 1 && d.wrapT <= 1 && !d.additive;
}

// Constant buffer CBM of gs_material.inc (five float4).
struct Consts {
    float proj[4]{};
    float light[4]{};
    float param[4]{};
    float tex[4]{};
    float color[4]{};
};
static_assert(sizeof(Consts) == 80);

struct Settings {
    float strength = 1.0f;                             // material_detail_strength: 0 .. 2
    float specular = 0.0f;                             // material_specular: 0 .. 1
    float occlusion = 0.5f;                            // share of the cavity occlusion applied
    float backShade = 0.35f;                           // relief contrast kept on faces turned away from the light
    float fadeStart = 30000.0f, fadeLength = 30000.0f; // view distance (world units, about 500 per metre)
    uint32_t debugView = 0;
};

// The light the relief is shaded with, in world space (unit vector toward the light) and its colour (0..1).
struct Light {
    float toLight[3]{0.35f, 0.80f, 0.48f};
    float color[3]{1, 1, 1};
};
// Without a captured sun (no lighting snapshot, a cave) the relief is lit from above and to the side, which is how
// the textures themselves were painted.
inline Light defaultLight() {
    Light l;
    const float n = std::sqrt(l.toLight[0] * l.toLight[0] + l.toLight[1] * l.toLight[1] + l.toLight[2] * l.toLight[2]);
    for (float& c : l.toLight)
        c /= n;
    return l;
}
// A captured directional light: `vector` as the game stores it for the layer (SceneLight::vector), `color` its colour.
// `vectorPointsToLight` says which way the stored vector runs.
inline Light sunLight(const float vector[3], const float color[3], bool vectorPointsToLight) {
    Light l = defaultLight();
    const float n = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (!std::isfinite(n) || n < 1e-6f)
        return l;
    const float sign = vectorPointsToLight ? 1.0f : -1.0f;
    for (int i = 0; i < 3; ++i)
        l.toLight[i] = sign * vector[i] / n;
    float peak = std::max({color[0], color[1], color[2], 1e-6f});
    for (int i = 0; i < 3; ++i)
        l.color[i] = std::isfinite(color[i]) ? std::clamp(color[i] / peak, 0.0f, 1.0f) : 1.0f;
    return l;
}

// Shader constants of one draw. `renderScaleX/Y` = render-target pixels per native pixel of the draw (SV_Position is
// in render-target pixels), `xyoffset` the GS XYOFFSET register. Returns false when the camera cannot give a view ray
// (the draw is then drawn without material shading).
inline bool makeConsts(const SceneGuideCamera& camera, uint64_t xyoffset, float renderScaleX, float renderScaleY,
                       const Light& light, const Settings& settings, uint32_t texWidth, uint32_t texHeight,
                       Consts& out) {
    const uint32_t need = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    if ((camera.flags & need) != need || renderScaleX <= 0 || renderScaleY <= 0 || !texWidth || !texHeight)
        return false;
    const float sx = camera.viewToClipVU[0], sy = camera.viewToClipVU[5];
    const float vx = camera.viewportScale[0], vy = camera.viewportScale[1];
    if (!std::isfinite(sx) || !std::isfinite(sy) || std::fabs(sx) < 1e-9f || std::fabs(sy) < 1e-9f ||
        !std::isfinite(vx) || !std::isfinite(vy) || std::fabs(vx) < 1e-9f || std::fabs(vy) < 1e-9f)
        return false;
    const float ox = float(xyoffset & 65535) / 16.0f - camera.viewportOffset[0];
    const float oy = float((xyoffset >> 32) & 65535) / 16.0f - camera.viewportOffset[1];
    // clip = view * viewToClipVU with clip.w = wz * view.z (wz = -1: the camera looks down -z). For clip.w = 1 the
    // ray is (ndc.x / sx, ndc.y / sy, 1 / wz), and ndc.x = (native x + xyoffset - viewport offset) / viewport scale.
    const float wz = camera.viewToClipVU[11];
    if (!std::isfinite(wz) || std::fabs(wz) < 1e-9f)
        return false;
    out.proj[0] = 1.0f / (renderScaleX * vx * sx);
    out.proj[1] = ox / (vx * sx);
    out.proj[2] = 1.0f / (renderScaleY * vy * sy);
    out.proj[3] = oy / (vy * sy);
    // Row vectors: view = world * worldToView, so a direction uses the upper 3x3 the same way.
    float l[3]{};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            l[j] += light.toLight[i] * camera.worldToView[i * 4 + j];
    const float n = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    if (!std::isfinite(n) || n < 1e-6f)
        return false;
    for (int j = 0; j < 3; ++j)
        out.light[j] = l[j] / n;
    out.light[3] = std::clamp(settings.strength, 0.0f, 4.0f);
    out.param[0] = std::clamp(settings.specular, 0.0f, 4.0f);
    out.param[1] = std::clamp(settings.occlusion, 0.0f, 1.0f);
    out.param[2] = std::clamp(settings.backShade, 0.0f, 1.0f);
    out.param[3] = float(settings.debugView);
    out.tex[0] = 1.0f / float(texWidth);
    out.tex[1] = 1.0f / float(texHeight);
    out.tex[2] = settings.fadeStart;
    out.tex[3] = settings.fadeLength > 0 ? 1.0f / settings.fadeLength : 0.0f;
    for (int i = 0; i < 3; ++i)
        out.color[i] = light.color[i];
    out.color[3] = 1.0f / wz;
    return true;
}

// ---- CPU transcription of gs_material.inc (materialShade) for tests ----
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

struct ShadeInput {
    Vec3 position, dPdx, dPdy; // view space at the pixel and its screen derivatives
    float dudx = 0, dvdx = 0, dudy = 0, dvdy = 0;
    uint8_t texel[4]{128, 128, 200, 200}; // packed material texel (packTexels)
    float viewDistance = 0;               // 1 / q
};
struct ShadeOutput {
    float scale = 1;    // multiplies the colour texel
    float specular = 0; // added, 0..1 of full white before the light colour
    Vec3 normal, tangent, bitangent, shaded;
    bool applied = false;
};
inline ShadeOutput shade(const ShadeInput& in, const Consts& k) {
    ShadeOutput o;
    Vec3 g = cross(in.dPdx, in.dPdy);
    const float gl = dot(g, g);
    if (!(gl > 1e-30f))
        return o;
    g = g * (1.0f / std::sqrt(gl));
    const Vec3 a = cross(in.dPdy, g), b = cross(g, in.dPdx);
    Vec3 t = a * in.dudx + b * in.dudy, bt = a * in.dvdx + b * in.dvdy;
    const float tl = dot(t, t), bl = dot(bt, bt);
    if (!(tl > 1e-30f) || !(bl > 1e-30f))
        return o;
    t = t * (1.0f / std::sqrt(tl));
    bt = bt * (1.0f / std::sqrt(bl));
    const Vec3 n = dot(g, in.position) > 0 ? g * -1.0f : g;
    const float nx = (float(in.texel[0]) - 128.0f) / 127.0f, ny = (float(in.texel[1]) - 128.0f) / 127.0f;
    const float fade = std::clamp(1.0f - (in.viewDistance - k.tex[2]) * k.tex[3], 0.0f, 1.0f);
    const float strength = k.light[3] * fade;
    const Vec3 l{k.light[0], k.light[1], k.light[2]};
    const float towards = std::clamp(dot(n, l) * 2.0f + 0.5f, 0.0f, 1.0f);
    const float facing = k.param[2] + (1.0f - k.param[2]) * towards;
    const float delta = (nx * dot(t, l) + ny * dot(bt, l)) * facing;
    const float shadeTerm = std::clamp(1.0f + strength * delta, 0.5f, 1.6f);
    const float occlusion =
        1.0f + (std::min(float(in.texel[2]) / 200.0f, 1.2f) - 1.0f) * (k.param[1] * std::min(strength, 1.0f));
    o.scale = shadeTerm * occlusion;
    o.normal = n;
    o.tangent = t;
    o.bitangent = bt;
    const float nz = std::sqrt(std::clamp(1.0f - nx * nx - ny * ny, 0.0f, 1.0f));
    o.shaded = normalize(t * nx + bt * ny + n * nz);
    if (k.param[0] > 0) {
        const Vec3 v = normalize(in.position * -1.0f), h = normalize(l + v);
        const float rough = std::max(float(in.texel[3]) / 255.0f, 0.08f);
        const float a2 = rough * rough, shininess = 2.0f / (a2 * a2) - 2.0f;
        const float lobe = std::pow(std::clamp(dot(o.shaded, h), 0.0f, 1.0f), std::max(shininess, 1.0f));
        o.specular =
            k.param[0] * fade * (1.0f - rough) * (1.0f - rough) * lobe * std::clamp(dot(n, l) * 4.0f, 0.0f, 1.0f);
    }
    o.applied = true;
    return o;
}

} // namespace gsr::material
