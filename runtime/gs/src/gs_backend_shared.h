// What the Direct3D 11 backend (d3d11_*.cpp) and the WebGPU backend (wgpu_*.cpp) have in common, written once.
//
// The GS emulation of a hardware backend (what to cache, when to upload, how a draw is classified, which state maps to
// which pipeline, target lifetime, present decisions) lives in the gs_*.inl files next to this header. Each of them
// defines member functions of a backend's Impl and is compiled once per backend:
//
//     #define GSR_IMPL D3D11Backend::Impl      // or WgpuBackend::Impl
//     #include "gs_texture_cache.inl"
//
// A backend file holds graphics API calls only. What the shared code needs from a backend is listed here, in three
// parts: the types below that a backend instantiates with its resource handles, the capability flags
// (BackendCaps, `static constexpr BackendCaps kCaps` in the Impl) and the member functions named in the "backend
// interface" comment of each .inl file. A capability a backend lacks is asked for with `if constexpr (kCaps.x)`, so
// the backend declares the functions behind it without defining them; the logic is never forked.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace gsr {

// What a backend can do beyond the common interface. The shared code asks; it does not test for a backend.
struct BackendCaps {
    // Sampled textures can be created (with their pixels) on a texture worker thread, so the render thread does not
    // stall on a large HD image (gs_texture_cache.inl, pushDone).
    bool texturesOffThread = false;
    // Material detail (material_detail.h): the material pixel program and its map binding exist.
    bool materialDetail = false;
    // Terrain detail and side projection (terrain_detail.h): the terrain shader fragment exists.
    bool terrainDetail = false;
    // Motion vectors: a velocity pass after every colour pass of a draw inside an object motion scope.
    bool motionVectors = false;
    // A temporal upscaler that renders the world at a reduced resolution (DLSS); also its diagnostics.
    bool temporalUpscaler = false;
    // Readback prefetch: a staged copy of a scratch target the game stopped drawing, started without waiting.
    bool readbackPrefetch = false;
    // GSR_RB_VERIFY: compare a target's GPU content with memory after a readback was avoided.
    bool readbackVerify = false;
};

// ---- draw (gs_draw.inl) ----

// PS constant flag bits (mirror gs_shaders.h and gs_shaders_wgsl.h)
enum : uint32_t {
    PF_TME = 1u << 0,
    PF_FGE = 1u << 1,
    PF_FST = 1u << 2,
    PF_TCC = 1u << 3,
    PF_ATE = 1u << 6,
    PF_DATE = 1u << 12,
    PF_DATM = 1u << 13,
    PF_ABE = 1u << 14,
    PF_PABE = 1u << 15,
    PF_DTHE = 1u << 16,
    PF_CCLAMP = 1u << 17,
    PF_FBA = 1u << 18,
    PF_FB16 = 1u << 19,
    PF_TEXRT = 1u << 22,
    PF_TERRAIN = 1u << 26,      // terrain layer: world-space detail (gs_terrain.inc)
    PF_TERRAIN_SIDE = 1u << 27, // terrain layer whose steep faces may sample the texture from the side
};

// The constants block of a draw; the same layout in HLSL and WGSL.
struct PSConsts {
    float rt[4];
    uint32_t a[4];
    uint32_t b[4];
    uint32_t c[4];
    float fog[4];
    uint32_t d[4];
    float e[4];
    float src[4];
    float f[4];
    float g[4];
    float h[4];
    float spriteBounds[4];
};
static_assert(sizeof(PSConsts) % 16 == 0 && sizeof(PSConsts) <= 256, "constant buffer / uniform layout");

struct DispConsts {
    float r1[4], r2[4], bg[4];
    uint32_t m[4];
};

inline uint32_t roundUp(uint32_t v, uint32_t a) {
    return (v + a - 1) / a * a;
}

// ---- texture cache (gs_texture_cache.inl) ----

struct TexKey {
    uint32_t tbp, tbw, psm, tw, th;
    uint32_t mxl, clutKind;
    uint32_t pad;
    uint64_t clutHash, texa, mip1, mip2;
    bool operator==(const TexKey& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};
struct TexKeyHash {
    size_t operator()(const TexKey& k) const {
        const uint64_t* p = reinterpret_cast<const uint64_t*>(&k);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(TexKey) / 8; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return (size_t)h;
    }
};
static_assert(sizeof(TexKey) % 8 == 0, "TexKey must have no padding");

// upscaled / replaced texture, shared by every TexEntry with the same content (hash of the decoded level 0 + dims)
struct UpKey {
    uint64_t hash;
    uint32_t factor;
    uint32_t packed; // algo | wrapS << 8 | wrapT << 9 | mxl << 12 | kind << 16 (0 upscale, 1 replacement)
    bool operator==(const UpKey& o) const { return hash == o.hash && factor == o.factor && packed == o.packed; }
};
struct UpKeyHash {
    size_t operator()(const UpKey& k) const {
        return (size_t)(k.hash ^ ((uint64_t)k.factor * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)k.packed << 20));
    }
};

// GpuTexture is the backend's handle of a sampled texture: default constructible and movable, `explicit operator
// bool` (false = none / creation failed), `get()` (what a draw binds) and `reset()`.
template <class GpuTexture>
struct UpEntryOf {
    GpuTexture tex; // none = the job failed, keep the native texture
    uint32_t factor = 1;
    uint32_t mxl = 0; // last level of the copy (upscaledMipLevelMax): the draw's LOD clamp while it is bound
    uint64_t bytes = 0;
    uint64_t lastFrame = 0;
    // Material map of the texture (material_detail.h, packTexels), none when it has none.
    GpuTexture material;
    uint8_t materialClass = 0;
};
template <class GpuTexture>
struct UpDoneOf {
    UpKey key;
    uint32_t factor = 1, w = 0, h = 0; // physical level 0 size
    std::vector<std::vector<uint32_t>> levels;
    uint64_t us = 0;
    bool replaced = false;
    uint32_t gen = 0; // upscale cache generation the job was started in; stale results are dropped
    // The GPU texture (createUpTexture). With BackendCaps::texturesOffThread it is created by pushDone on the thread
    // that finished the job: creating a 2048 pixel image with its mip chain copies about 22 MiB inside the driver,
    // which stalled the render thread for tens of milliseconds when pumpUp did it. Otherwise pumpUp creates it.
    // `levels` is emptied once the texture exists.
    GpuTexture tex;
    uint32_t levelCount = 0;
    uint64_t bytes = 0;
    // Material map loaded by the same job (loadMaterialLevels): levels until the texture was made.
    std::vector<std::vector<uint32_t>> materialLevels;
    uint32_t materialW = 0, materialH = 0;
    uint8_t materialClass = 0;
    GpuTexture material;
    uint64_t materialBytes = 0;
};

template <class GpuTexture>
struct TexEntryOf {
    GpuTexture tex;
    std::shared_ptr<UpEntryOf<GpuTexture>> up; // upscaled / replaced version (preferred when set)
    UpKey upKey{};
    bool upWanted = false;
    bool fontAtlas = false, menuArtwork = false;
    uint64_t previewVersion = 0, previewHash = 0;
    uint64_t paletteWait = 0; // index-plane hash whose HD index map was still building: decode again once it is ready
    float averageColor[3]{.35f, .35f, .35f};
    uint8_t minimumAlpha = 0;
    uint32_t reflectionTexture = ~0u;
    uint64_t stamp = 0;
    uint64_t checkedClock = 0;
    uint64_t lastFrame = 0;
    uint64_t bytes = 0;
    uint64_t rawHash = 0;              // LocalMemory::hashBlocks over spans when decoded
    const void* aliasTarget = nullptr; // RT whose dirty blocks were copied in from its GPU texture
    uint64_t aliasWrites = 0;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    std::vector<std::vector<uint32_t>>
        cpu; // CPU copies of the levels (only kept when Config::onTexture is set, for the audit)
    uint32_t w = 0, h = 0;
};

// Per texture-memory slot bookkeeping for palette / pixel animated textures (see resolveTexture): a slot that is
// re-decoded many times in a short time is "volatile" (the highlighted menu entry, water / fire palette effects); it is
// never dumped beyond the first variants and never replaced / upscaled.
struct SlotInfo {
    uint64_t windowStart = 0, lastDecode = 0, lastHash = 0;
    uint32_t count = 0, changes = 0, sameRun = 0;
    bool isVolatile = false, hashValid = false;
    // indexed volatile slots: index-plane hash of the last decode; unchanged indices with a new decode = palette-only
    // animation, which gets the HD index map path (gs_palette_hd.h)
    uint64_t lastIndexHash = 0;
    bool indexValid = false;
};

// HD index map of one palette-animated index plane, built once on the worker pool (ready is set last).
struct PalettePlane {
    std::atomic<bool> ready{false};
    bool fromPack = false; // guided by an HD pack image (preferred) instead of the live upscaler
    uint32_t factor = 1;
    std::vector<uint8_t> map; // (w * factor) x (h * factor) palette indices
    uint64_t lastFrame = 0;
};

} // namespace gsr
