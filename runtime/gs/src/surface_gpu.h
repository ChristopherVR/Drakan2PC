#pragma once
// The ray-surface reconstruction of shader-drawn strip batches on the GPU (GSR_VERTEX_SURFACE=gpu).
//
// With the GPU vertex path a strip batch's vertices exist only on the GPU (streamed out, surface_builder.h RawVertex).
// The CPU reconstruction (reconstructDraw on the SurfaceBuilder thread) needs them back the same frame, which waits
// for the GPU. Here a compute pass does the same work on the GPU instead: it turns the streamed vertices into the ray
// pass's frame triangles (camera-relative positions and the seven attributes RayWorldCache packs), and the D3D12 ray
// pass builds them into an acceleration structure of their own without the CPU ever reading them.
//
// What runs on the CPU per draw is what does not depend on the vertices: the draw state, the camera the builder would
// convert the draw with, the eligibility terms. The kernel computes the rest exactly as reconstructDraw and
// submitDraw do (the minimum vertex alpha, the un-projection in double precision, the resident-triangle match, the
// attributes), with IEEE float operations: division and square root are rounded correctly through double precision.
// Each streamed triangle owns one slot; a triangle the CPU path would not append has NaN positions, which makes it
// an inactive triangle in DXR.
//
// Not carried over (counted, GSR_SURFACE_GPU_VERIFY reports them): ResidentScene::observe of a streamed triangle that
// matches a resident one (a material projection update of the static world), the 250,000-triangle cap of a target's
// list. Draws in a resident-water state stay on the CPU path (their water flag decides whether the pass runs).
#include "ray_world_cache.h"
#include "resident_scene.h"
#include "surface_builder.h"
#include "surface_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace gsr::surface {

// One streamed draw for the kernel (StructuredBuffer, 16 words).
struct GpuDraw {
    uint32_t first = 0;  // first vertex in the stream
    uint32_t count = 0;  // vertices, a multiple of 3
    uint32_t slot = 0;   // first output triangle slot
    uint32_t camera = 0; // index into the camera table
    int32_t ofx = 0, ofy = 0;
    uint32_t flags = 0;
    uint32_t alphaTest = 0, alphaRef = 0, tfx = 0, texMinimumAlpha = 0, texture = ~0u;
    uint32_t pad[4]{};
    enum : uint32_t {
        Iip = 1,
        Zmask = 2,
        Blend = 4,        // DF_ABE
        Alpha44 = 8,      // s.alpha == 0x44
        AlphaTestOn = 16, // s.test & 1
        TextureAlpha = 32,
        MaterialEligible = 64,
        HasTexture = 128, // haveTex && !texIsRT: the triangle carries a material texture
    };
};
static_assert(sizeof(GpuDraw) == 64);

// One camera of the builder (RasterToWorld), StructuredBuffer, 48 words.
struct GpuCamera {
    uint32_t inverse[32]{}; // 16 doubles, low word first
    float xy[2]{};
    float offset[3]{};
    float scale[3]{};
    uint32_t valid = 0;
    uint32_t pad[7]{};
};
static_assert(sizeof(GpuCamera) == 192);

inline GpuCamera gpuCameraOf(const RasterToWorld& t) {
    GpuCamera c;
    std::memcpy(c.inverse, t.inverse, sizeof(t.inverse));
    c.xy[0] = t.xy[0];
    c.xy[1] = t.xy[1];
    for (int i = 0; i < 3; ++i) {
        c.offset[i] = t.camera.viewportOffset[i];
        c.scale[i] = t.camera.viewportScale[i];
    }
    c.valid = t.valid ? 1u : 0u;
    return c;
}

// The resident triangles' match candidates of ResidentScene::prepareMatch (dry land only: the kernel never sees a
// water-state draw), by cell. Cells are sorted by key for a binary search.
struct GpuMatchCell {
    uint32_t keyHigh = 0, keyLow = 0, first = 0, count = 0;
};
struct GpuMatchEntry {
    float x0, y0, z0, ax, az, bx, bz, det, rdet, nx, ny, nz, length, pad[3];
};
static_assert(sizeof(GpuMatchEntry) == 64);
struct GpuMatchTable {
    uint64_t geometryVersion = 0;
    unsigned mathMode = 0;
    uint64_t serial = 0; // changes with every rebuild (the kernel uploads it again)
    std::vector<GpuMatchCell> cells;
    std::vector<GpuMatchEntry> entries;
};

// The same candidates and terms as ResidentScene::prepareMatch, in the calling thread's floating-point mode.
inline void buildMatchTable(const ResidentScene& scene, GpuMatchTable& table) {
    table.cells.clear();
    table.entries.clear();
    struct Keyed {
        int64_t key;
        uint32_t index;
    };
    std::vector<Keyed> keyed;
    keyed.reserve(scene.triangles.size());
    for (uint32_t index = 0; index < scene.triangles.size(); ++index) {
        const auto& tri = scene.triangles[index];
        if (tri.water)
            continue;
        const float cx = (tri.p[0].x + tri.p[1].x + tri.p[2].x) / 3, cz = (tri.p[0].z + tri.p[1].z + tri.p[2].z) / 3;
        const int64_t key = int64_t(uint64_t(uint32_t(int32_t(std::floor(cx / 2048.f)))) << 32 |
                                    uint32_t(int32_t(std::floor(cz / 2048.f))));
        keyed.push_back({key, index});
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const Keyed& a, const Keyed& b) { return uint64_t(a.key) < uint64_t(b.key); });
    for (size_t i = 0; i < keyed.size();) {
        GpuMatchCell cell;
        cell.keyHigh = uint32_t(uint64_t(keyed[i].key) >> 32);
        cell.keyLow = uint32_t(uint64_t(keyed[i].key));
        cell.first = uint32_t(table.entries.size());
        size_t j = i;
        for (; j < keyed.size() && keyed[j].key == keyed[i].key; ++j) {
            const auto& tri = scene.triangles[keyed[j].index];
            const float ax = tri.p[1].x - tri.p[0].x, az = tri.p[1].z - tri.p[0].z, bx = tri.p[2].x - tri.p[0].x,
                        bz = tri.p[2].z - tri.p[0].z;
            const float det = ax * bz - az * bx;
            const auto normal = cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0]));
            const float len = length(normal);
            if (std::abs(det) < 1 || len < 1)
                continue;
            GpuMatchEntry e{tri.p[0].x, tri.p[0].y, tri.p[0].z, ax,       az,       bx,  bz,
                            det,        1.f / det,  normal.x,   normal.y, normal.z, len, {0, 0, 0}};
            std::memcpy(&e.pad[0], &keyed[j].index, 4); // the resident triangle (match()'s result), as bits
            table.entries.push_back(e);
        }
        cell.count = uint32_t(table.entries.size()) - cell.first;
        if (cell.count)
            table.cells.push_back(cell);
        i = j;
    }
}

// The draw-state terms of submitDraw that do not depend on the vertices. `waterState` draws stay on the CPU path.
inline bool gpuDrawOf(const DrawInput& in, GpuDraw& g, bool& waterState) {
    const GsDrawState& s = in.s;
    const bool zmsk = (s.zbuf >> 32) & 1;
    waterState = in.nativeWaterPass || (in.residentWater && zmsk && in.haveTex && !in.texIsRT && (s.flags & DF_ABE));
    const bool textureAlpha = in.haveTex && bitsOf(s.tex0, 34, 1) != 0;
    const bool materialEligible = in.haveTex && !in.texIsRT && (!(s.flags & DF_ABE) || s.alpha == 0x44) &&
                                  (!(s.test & 1) || bitsOf(s.test, 1, 3) != 0);
    g.ofx = (int)(s.xyoffset & 0xFFFF);
    g.ofy = (int)((s.xyoffset >> 32) & 0xFFFF);
    g.flags = ((s.flags & DF_IIP) ? GpuDraw::Iip : 0u) | (zmsk ? GpuDraw::Zmask : 0u) |
              ((s.flags & DF_ABE) ? GpuDraw::Blend : 0u) | (s.alpha == 0x44 ? GpuDraw::Alpha44 : 0u) |
              ((s.test & 1) ? GpuDraw::AlphaTestOn : 0u) | (textureAlpha ? GpuDraw::TextureAlpha : 0u) |
              (materialEligible ? GpuDraw::MaterialEligible : 0u) |
              (in.haveTex && !in.texIsRT ? GpuDraw::HasTexture : 0u);
    g.alphaTest = uint32_t(bitsOf(s.test, 1, 3));
    g.alphaRef = uint32_t(bitsOf(s.test, 4, 8));
    g.tfx = uint32_t(bitsOf(s.tex0, 35, 2));
    g.texMinimumAlpha = in.texMinimumAlpha;
    g.texture = in.haveTex && !in.texIsRT ? in.reflectionTexture : ~0u;
    return !waterState;
}

// One resident observe the kernel found (a streamed triangle over a resident one, of an opaque textured draw).
struct GpuObserve {
    uint32_t slot = 0, index = 0, texture = 0;
    float p[3][3]{};
    float uv[3][2]{};
};
static_assert(sizeof(GpuObserve) == 72);
// The triangle observe() reads from it (positions, UVs, texture).
inline Triangle observedTriangle(const GpuObserve& o) {
    Triangle t;
    for (unsigned j = 0; j < 3; ++j) {
        t.p[j] = {o.p[j][0], o.p[j][1], o.p[j][2]};
        t.uv[j][0] = o.uv[j][0];
        t.uv[j][1] = o.uv[j][1];
    }
    t.texture = o.texture;
    return t;
}

// The kernel's output layout, in 32-bit words: positions (9 per slot), then attributes (7 float4 per slot). It lives
// in an R32_UINT texture of kRowWords columns (Direct3D 11 and 12 share textures, not buffers), copied into a buffer
// row by row on the D3D12 side: row r holds words [r * kRowWords, (r + 1) * kRowWords).
struct GpuSurfaceLayout {
    static constexpr uint32_t kRowWords = 16384; // 64 KiB rows: a multiple of D3D12's 256-byte footprint pitch
    uint32_t slots = 0;
    uint32_t rows() const { return uint32_t((bytes() / 4 + kRowWords - 1) / kRowWords); }
    uint64_t positionBytes() const { return uint64_t(slots) * 9 * 4; }
    uint64_t attributeOffset() const { return positionBytes(); }
    uint64_t attributeBytes() const { return uint64_t(slots) * kTriangleAttributes * sizeof(Attribute); }
    uint64_t bytes() const { return positionBytes() + attributeBytes(); }
};

// The ray pass's frame-triangle packing (RayWorldCache::append, originalPack) of one triangle: its camera-relative
// positions and its seven attributes. The reference of the kernel's output for GSR_SURFACE_GPU_VERIFY and the tests.
inline void packFrameTriangle(const Triangle& tri, Point origin, size_t textures, float positions[9],
                              Attribute attributes[kTriangleAttributes]) {
    for (unsigned j = 0; j < 3; ++j) {
        const Point p = sub(tri.p[j], origin);
        positions[j * 3 + 0] = p.x;
        positions[j * 3 + 1] = p.y;
        positions[j * 3 + 2] = p.z;
    }
    std::vector<Attribute> out;
    out.reserve(kTriangleAttributes);
    packFrameAttributes(tri, textures, out);
    std::copy(out.begin(), out.end(), attributes);
}

// The streamed draws of one world of one target, waiting for the world's ray pass (gs_draw.inl).
struct GpuSurfaceDraws {
    struct Record {
        SurfaceList* list = nullptr;
        uint64_t epoch = 0;
        GpuDraw draw;
        uint32_t reference = 0; // GSR_SURFACE_GPU_VERIFY: first vertex of the CPU evaluation in `references`
        DrawInput in;           // GSR_SURFACE_GPU_VERIFY: the draw as the CPU path would have submitted it
    };
    std::vector<Record> records;
    std::vector<GpuCamera> cameras;
    std::vector<RasterToWorld> cameraTransforms; // GSR_SURFACE_GPU_VERIFY: the same cameras for the reference
    uint64_t cameraStage = ~0ull;                // the builder's stage count the last table entry belongs to
    std::vector<DrawVertex> references;
    GpuMatchTable match;
    std::shared_ptr<const GpuMatchTable> matchShared;
    // Counters for the log line.
    uint64_t draws = 0, triangles = 0, cpuDraws = 0, overflows = 0, worlds = 0, droppedRecords = 0;
    uint64_t verifiedSlots = 0, verifyDifferences = 0, verifyActive = 0, lostObserves = 0, verifyWorlds = 0;
    uint64_t lateObserves = 0;   // resident observes of streamed triangles, applied a few worlds late
    uint64_t verifyChanging = 0; // left-out observes that would have replaced a stored projection
    void clear() {
        records.clear();
        cameras.clear();
        cameraTransforms.clear();
        cameraStage = ~0ull;
        references.clear();
    }
};

} // namespace gsr::surface
