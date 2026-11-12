// Material detail (material_detail.h): the pack's material table, which draws take part, the per-draw constants, the
// packed material texel and its mip chain, and the shading arithmetic (the CPU transcription of gs_material.inc).
// On Windows the material variant of the real pixel shader is also compiled (no device).
#include "material_detail.h"

#include <cstdio>

#ifdef _WIN32
#include "gs_shaders.h"
#include <d3dcompiler.h>
#include <cstring>
#endif

static int g_fail = 0;
static void expect(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++g_fail;
}
static bool closeTo(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

using namespace gsr;
using namespace gsr::material;

namespace {
// The camera the host publishes: row vectors, the camera looks down -z of view space (clip.w = -view.z), screen y
// grows downward (negative viewport scale), 640 x 512 frame centred at 2048.
SceneGuideCamera makeCamera() {
    SceneGuideCamera c;
    c.flags = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    c.frameWidth = 640;
    c.frameHeight = 512;
    for (int i = 0; i < 4; ++i)
        c.worldToView[i * 5] = 1;
    c.viewToClipVU[0] = 1.5f;
    c.viewToClipVU[5] = 2.0f;
    c.viewToClipVU[11] = -1;
    c.viewToClipVU[14] = 32;
    c.viewportScale[0] = 320;
    c.viewportScale[1] = -256;
    c.viewportOffset[0] = 2048;
    c.viewportOffset[1] = 2048;
    return c;
}
const uint64_t kXyOffset = (uint64_t(2048 - 320) * 16) | ((uint64_t(2048 - 256) * 16) << 32);

// A wall facing the camera at distance `depth`, its texture upright: u grows to the right of the screen, v downward,
// `texelsPerPixel` texels per pixel. Returns the shading input at pixel (px, py).
ShadeInput wallPixel(const Consts& k, float px, float py, float depth, float texelsPerPixel, uint8_t nx, uint8_t ny) {
    auto position = [&](float x, float y) {
        return Vec3{x * k.proj[0] + k.proj[1], y * k.proj[2] + k.proj[3], k.color[3]} * depth;
    };
    ShadeInput in;
    in.position = position(px, py);
    in.dPdx = position(px + 1, py) - in.position;
    in.dPdy = position(px, py + 1) - in.position;
    in.dudx = texelsPerPixel;
    in.dvdy = texelsPerPixel;
    in.texel[0] = nx;
    in.texel[1] = ny;
    in.viewDistance = depth;
    return in;
}
Consts constsWithLight(float x, float y, float z, Settings settings = {}) {
    Light light;
    const float n = std::sqrt(x * x + y * y + z * z);
    light.toLight[0] = x / n;
    light.toLight[1] = y / n;
    light.toLight[2] = z / n;
    Consts k;
    const bool ok = makeConsts(makeCamera(), kXyOffset, 1, 1, light, settings, 128, 128, k);
    expect(ok, "constants from a valid world camera");
    return k;
}
} // namespace

static void tableTest() {
    std::printf("material table\n");
    Table t;
    const size_t n = t.parse("# <hash> <class id> <class> <mean roughness> <metallic>\n"
                             "00112233445566aa 2 stone 0.861 0.0\n"
                             "ffeeddccbbaa0099 4 metal 0.400 1.0\r\n"
                             "short 1 terrain 0.9 0\n"
                             "0123456789abcdef 0 none 0.5 0\n"
                             "0123456789abcdee 99 future 0.5 0\n"
                             "1111111111111111 5 character 0.750 0.0");
    expect(n == 3 && t.size() == 3, "three valid records; comments, short lines, class 0 and unknown classes skipped");
    const Record* stone = t.find(0x00112233445566aaull);
    expect(stone && stone->cls == Stone && closeTo(stone->roughness, 0.861f), "stone record");
    const Record* metal = t.find(0xffeeddccbbaa0099ull);
    expect(metal && metal->cls == Metal && closeTo(metal->metallic, 1.0f), "metal record (CRLF line)");
    expect(!t.find(0x0123456789abcdefull) && !t.find(42), "unknown hashes have no material");
    expect(classMask(Mode::Off) == 0, "mode off shades no class");
    expect((classMask(Mode::Hard) & (1u << Stone)) && (classMask(Mode::Hard) & (1u << Terrain)) &&
               !(classMask(Mode::Hard) & (1u << Character)) && !(classMask(Mode::Hard) & (1u << Foliage)),
           "mode hard: stone and terrain, not characters or foliage");
    expect((classMask(Mode::All) & (1u << Character)) && (classMask(Mode::All) & (1u << Foliage)),
           "mode all adds characters and foliage");
    expect(mapPath("textures", 0xabcull, "n") == "textures/materials/0000000000000abc_n.png", "map file name");
}

static void eligibilityTest() {
    std::printf("which draws take material shading\n");
    DrawTraits world;
    world.worldOpen = world.textured = world.depthTested = true;
    expect(eligible(world), "a depth-tested perspective world triangle with a modulated texture");
    auto without = [&](auto change) {
        DrawTraits d = world;
        change(d);
        return eligible(d);
    };
    expect(!without([](DrawTraits& d) { d.worldOpen = false; }), "not outside the world phase (menus, HUD)");
    expect(!without([](DrawTraits& d) { d.textured = false; }), "not untextured or render-target textured draws");
    expect(!without([](DrawTraits& d) { d.sprite = true; }), "not sprites");
    expect(!without([](DrawTraits& d) { d.fst = true; }), "not screen-space textured draws");
    expect(!without([](DrawTraits& d) { d.depthTested = false; }), "not the sky (no depth test)");
    expect(!without([](DrawTraits& d) { d.textureFunction = 1; }), "not decal textures");
    expect(!without([](DrawTraits& d) { d.wrapS = 2; }) && !without([](DrawTraits& d) { d.wrapT = 3; }),
           "not region-wrapped textures");
    expect(!without([](DrawTraits& d) { d.additive = true; }), "not additive glows");
    expect(without([](DrawTraits& d) { d.wrapS = 1; }), "clamped textures are fine");
}

static void constsTest() {
    std::printf("per-draw constants\n");
    Consts k = constsWithLight(0, 0, 1);
    // the frame centre is the view axis; the right edge is at view x / -z = 1 / 1.5
    Vec3 centre{320 * k.proj[0] + k.proj[1], 256 * k.proj[2] + k.proj[3], k.color[3]};
    expect(closeTo(centre.x, 0) && closeTo(centre.y, 0) && closeTo(centre.z, -1), "frame centre maps to the view axis");
    expect(closeTo(640 * k.proj[0] + k.proj[1], 1.0f / 1.5f) && closeTo(0 * k.proj[2] + k.proj[3], 1.0f / 2.0f),
           "frame edges map to the frustum (right edge +x, top edge +y)");
    Consts scaled;
    Light light = defaultLight();
    expect(makeConsts(makeCamera(), kXyOffset, 4, 4, light, Settings{}, 64, 128, scaled) &&
               closeTo(2560 * scaled.proj[0] + scaled.proj[1], 1.0f / 1.5f) && closeTo(scaled.tex[0], 1.0f / 64) &&
               closeTo(scaled.tex[1], 1.0f / 128),
           "render scale 4: positions are in render-target pixels; texture size reciprocal");
    SceneGuideCamera invalid = makeCamera();
    invalid.flags = SceneGuideCamera::Valid;
    expect(!makeConsts(invalid, kXyOffset, 1, 1, light, Settings{}, 64, 64, k), "no constants without a world camera");
    SceneGuideCamera turned = makeCamera(); // yaw 90 degrees: world +x becomes view -z
    std::memset(turned.worldToView, 0, sizeof turned.worldToView);
    turned.worldToView[0 * 4 + 2] = -1;
    turned.worldToView[1 * 4 + 1] = 1;
    turned.worldToView[2 * 4 + 0] = 1;
    turned.worldToView[15] = 1;
    Light east;
    east.toLight[0] = 1;
    east.toLight[1] = east.toLight[2] = 0;
    expect(makeConsts(turned, kXyOffset, 1, 1, east, Settings{}, 64, 64, k) && closeTo(k.light[0], 0) &&
               closeTo(k.light[1], 0) && closeTo(k.light[2], -1),
           "the light direction is rotated into view space");
    const float sun[3] = {0, 2, 0}, colour[3] = {2, 1, 0.5f};
    const Light fromSun = sunLight(sun, colour, true);
    expect(closeTo(fromSun.toLight[1], 1) && closeTo(fromSun.color[0], 1) && closeTo(fromSun.color[2], 0.25f),
           "captured sun: unit direction, colour scaled to its peak");
    const float none[3] = {0, 0, 0};
    expect(closeTo(sunLight(none, colour, true).toLight[1], defaultLight().toLight[1]), "a zero sun falls back");
}

static void shadeTest() {
    std::printf("shading\n");
    Settings plain;
    plain.occlusion = 0;
    {
        const Consts k = constsWithLight(0.4f, 0.5f, 0.8f, plain);
        const ShadeOutput o = shade(wallPixel(k, 200, 300, 4000, 0.25f, 128, 128), k);
        expect(o.applied && o.scale == 1.0f && o.specular == 0.0f, "a flat normal leaves the texel exactly unchanged");
        expect(closeTo(o.normal.z, 1) && closeTo(o.tangent.x, 1) && closeTo(o.bitangent.y, -1),
               "frame of an upright wall: normal to the eye, u to the right, v down the screen");
    }
    {
        // normal leaning to +u (right): brighter when the light comes from the right, darker from the left
        const Consts right = constsWithLight(0.7f, 0, 0.7f, plain), left = constsWithLight(-0.7f, 0, 0.7f, plain);
        const float lit = shade(wallPixel(right, 320, 256, 4000, 0.25f, 190, 128), right).scale;
        const float unlit = shade(wallPixel(left, 320, 256, 4000, 0.25f, 190, 128), left).scale;
        expect(lit > 1.05f && unlit < 0.95f, "a slope facing the light brightens, one facing away darkens");
        expect(closeTo(lit - 1, 1 - unlit, 1e-3f), "the change is symmetric (the texture's mean does not move)");
    }
    {
        // +y of the map points down the image: a normal with green > 128 leans to the bottom of the screen
        const Consts below = constsWithLight(0, -0.7f, 0.7f, plain), above = constsWithLight(0, 0.7f, 0.7f, plain);
        expect(shade(wallPixel(below, 320, 256, 4000, 0.25f, 128, 190), below).scale > 1.05f &&
                   shade(wallPixel(above, 320, 256, 4000, 0.25f, 128, 190), above).scale < 0.95f,
               "green points down the image (DirectX convention)");
    }
    {
        // mirrored and rotated texture coordinates follow the texture, not the screen
        const Consts right = constsWithLight(0.7f, 0, 0.7f, plain);
        ShadeInput mirrored = wallPixel(right, 320, 256, 4000, 0.25f, 190, 128);
        mirrored.dudx = -mirrored.dudx;
        expect(shade(mirrored, right).scale < 0.95f, "mirrored u: the same texel now leans left");
        ShadeInput rotated = wallPixel(right, 320, 256, 4000, 0.25f, 128, 190);
        rotated.dudx = 0;
        rotated.dvdx = 0.25f; // v grows to the right
        rotated.dudy = -0.25f;
        rotated.dvdy = 0;
        expect(shade(rotated, right).scale > 1.05f, "rotated texture: +v now points right, toward the light");
        ShadeInput stretched = wallPixel(right, 320, 256, 4000, 0.25f, 190, 128);
        stretched.dudx *= 8; // stretched texture: the frame is normalised, the relief keeps its strength
        expect(closeTo(shade(stretched, right).scale,
                       shade(wallPixel(right, 320, 256, 4000, 0.25f, 190, 128), right).scale, 1e-3f),
               "anisotropic texture stretch does not change the strength");
    }
    {
        Settings strong = plain;
        strong.strength = 4;
        const Consts k = constsWithLight(0.7f, 0, 0.7f, strong);
        expect(closeTo(shade(wallPixel(k, 320, 256, 4000, 0.25f, 255, 128), k).scale, 1.6f) &&
                   closeTo(shade(wallPixel(k, 320, 256, 4000, 0.25f, 1, 128), k).scale, 0.5f),
               "the factor is clamped to 0.5 .. 1.6");
        Settings off = plain;
        off.strength = 0;
        const Consts k0 = constsWithLight(0.7f, 0, 0.7f, off);
        expect(shade(wallPixel(k0, 320, 256, 4000, 0.25f, 255, 128), k0).scale == 1.0f, "strength 0 changes nothing");
        Settings offOccluded;
        offOccluded.strength = 0;
        offOccluded.occlusion = 1;
        const Consts k1 = constsWithLight(0.7f, 0, 0.7f, offOccluded);
        ShadeInput cavity = wallPixel(k1, 320, 256, 4000, 0.25f, 255, 128);
        cavity.texel[2] = 120;
        expect(shade(cavity, k1).scale == 1.0f, "strength 0 also leaves occlusion out");
    }
    {
        const Consts k = constsWithLight(0.7f, 0, 0.7f, plain);
        const float nearScale = shade(wallPixel(k, 320, 256, 4000, 0.25f, 190, 128), k).scale;
        const float midScale = shade(wallPixel(k, 320, 256, 45000, 0.25f, 190, 128), k).scale;
        const float farScale = shade(wallPixel(k, 320, 256, 70000, 0.25f, 190, 128), k).scale;
        expect(nearScale > midScale && midScale > 1.0f && farScale == 1.0f, "the detail fades out with distance");
    }
    {
        // a wall turned from the light keeps a share of its relief
        Settings s = plain;
        const Consts k = constsWithLight(0.7f, 0, -0.7f, s);
        const float back = shade(wallPixel(k, 320, 256, 4000, 0.25f, 190, 128), k).scale;
        const Consts front = constsWithLight(0.7f, 0, 0.7f, s);
        const float lit = shade(wallPixel(front, 320, 256, 4000, 0.25f, 190, 128), front).scale;
        expect(back > 1.0f && closeTo(back - 1, (lit - 1) * s.backShade, 1e-3f),
               "faces turned from the light: reduced relief");
    }
    {
        Settings occluded;
        occluded.occlusion = 1;
        const Consts k = constsWithLight(0, 0, 1, occluded);
        ShadeInput cavity = wallPixel(k, 320, 256, 4000, 0.25f, 128, 128);
        cavity.texel[2] = 150;
        ShadeInput open = cavity;
        open.texel[2] = 200;
        expect(closeTo(shade(cavity, k).scale, 0.75f) && shade(open, k).scale == 1.0f,
               "occlusion darkens cavities; the map's mean (200) is neutral");
    }
    {
        Settings glossy = plain;
        glossy.specular = 1;
        const Consts k = constsWithLight(0, 0, 1, glossy); // light behind the eye: mirror direction on a facing wall
        ShadeInput smooth = wallPixel(k, 320, 256, 4000, 0.25f, 128, 128);
        smooth.texel[3] = 60;
        ShadeInput rough = smooth;
        rough.texel[3] = 230;
        const ShadeOutput a = shade(smooth, k), b = shade(rough, k);
        expect(a.specular > 0.2f && b.specular < 0.02f && a.scale == 1.0f,
               "specular: smooth surfaces only, colour scale apart");
        const Consts side = constsWithLight(1, 0, 0.02f, glossy);
        expect(shade(wallPixel(side, 320, 256, 4000, 0.25f, 128, 128), side).specular < 0.01f,
               "next to no highlight at grazing light");
    }
    {
        ShadeInput degenerate;
        const Consts k = constsWithLight(0, 0, 1, plain);
        expect(!shade(degenerate, k).applied && shade(degenerate, k).scale == 1.0f, "a degenerate pixel is left alone");
    }
}

static void texelTest() {
    std::printf("packed texel and mip chain\n");
    // normal image: R = x, G = y; orm image: R = occlusion, G = roughness
    const std::vector<uint32_t> normal = {0x00ff8040u, 0x00ff80c0u, 0x00ff4080u, 0x00ffc080u};
    const std::vector<uint32_t> orm = {0x000080ffu, 0x0000c0ffu, 0x00004099u, 0x0000ff99u};
    std::vector<uint32_t> packed;
    packTexels(normal, orm, 4, 0.5f, packed);
    expect((packed[0] & 255) == 0x40 && ((packed[0] >> 8) & 255) == 0x80 && (packed[3] >> 24) == 0xff,
           "normal x, y and roughness are carried over");
    const double mean = (255 + 255 + 153 + 153) / 4.0;
    expect(((packed[0] >> 16) & 255) == uint32_t(255 / mean * 200 + 0.5) &&
               ((packed[2] >> 16) & 255) == uint32_t(153 / mean * 200 + 0.5),
           "occlusion is stored relative to the map's mean (200 = mean)");
    std::vector<uint32_t> flat;
    packTexels({}, {}, 3, 0.8f, flat);
    expect(flat.size() == 3 && flat[1] == (128u | (128u << 8) | (200u << 16) | (204u << 24)),
           "missing maps give a flat normal, neutral occlusion and the class roughness");
    std::vector<uint32_t> half;
    downsample(packed, 2, 2, half);
    expect(half.size() == 1 && (half[0] & 255) == 128 && ((half[0] >> 8) & 255) == 128,
           "opposite slopes average to a flat normal in the next mip level");
    std::vector<uint32_t> row;
    downsample({1, 3, 5, 7}, 4, 1, row);
    expect(row.size() == 2 && row[0] == 2 && row[1] == 6, "a one-row level halves along x only");
}

#ifdef _WIN32
static void shaderTest() {
    std::printf("material variant of the pixel shader\n");
    for (const char* hw : {"1", "0"})
        for (bool material : {false, true}) {
            const D3D_SHADER_MACRO macros[] = {
                {"HW_BLEND", hw}, {material ? "MATERIAL" : nullptr, "1"}, {nullptr, nullptr}};
            ID3DBlob *code = nullptr, *errors = nullptr;
            const HRESULT hr = D3DCompile(kHlslMain, std::strlen(kHlslMain), "gs_shaders", macros, nullptr, "PSMain",
                                          "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(hr) && errors)
                std::fprintf(stderr, "%s\n", (const char*)errors->GetBufferPointer());
            char what[96];
            std::snprintf(what, sizeof what, "PSMain compiles (HW_BLEND=%s, %s)", hw,
                          material ? "material variant" : "plain");
            expect(SUCCEEDED(hr), what);
            if (code)
                code->Release();
            if (errors)
                errors->Release();
        }
}
#endif

int main() {
    tableTest();
    eligibilityTest();
    constsTest();
    shadeTest();
    texelTest();
#ifdef _WIN32
    shaderTest();
#endif
    std::printf(g_fail ? "FAILED: %d\n" : "all material detail checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
