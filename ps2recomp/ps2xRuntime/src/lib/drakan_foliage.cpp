#include "drakan_foliage.h"
#include "drakan_guest_layout.h"

#include "drakan_config.h"
#include "drakan_extended_heap.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "world/Foliage.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace drakan_foliage {
namespace {

namespace wf = world::foliage;

constexpr uint32_t kRenderObject =
    drakan_layout::fn::kRenderObject;        // per-object render method, a0 = the registered handle
constexpr uint32_t kInlineParts = 0x1ed8f0u; // a1 = prepared mesh: parts (+0xc count, +0x10 array of 0x30)
constexpr uint32_t kCoreParts[2] = {0x1ec160u, 0x1ec8f0u};         // a0 = VU renderer, renderer +0xc = position source
constexpr uint32_t kContextPtr = drakan_layout::kEngineContextPtr; // word holding the engine object
constexpr uint32_t kHandleVtable = drakan_layout::kHandleVtable;
constexpr uint32_t kResourceDb = 0x4db4c0u; // record +8 = the object's class
constexpr uint32_t kModelManager = drakan_layout::kModelManager, kModelVt = 0x51cc98u, kModelLodVt = 0x519a00u;
constexpr uint32_t kTextureManager = drakan_layout::kTextureManager, kTextureVt = 0x51cfe8u;
constexpr uint32_t kMalloc = drakan_layout::fn::kMalloc, kSignalSema = 0x4bc180u, kHeapSemaWord = 0x515260u;
constexpr uint32_t kRamMask = 0x1fffffffu;
constexpr uint32_t kRamSize = PS2_RAM_SIZE;
// Static prop classes (level pieces 0x100d3 / 0x100d8, the tree class 0x10011): the only objects the tree look
// (wf::looksLikeTree) is tried on when the vegetation table does not know them.
constexpr uint32_t kPropClasses[] = {0x100d3u, 0x100d8u, 0x10011u};

struct IdKind {
    uint32_t id;
    uint8_t kind;
};
constexpr IdKind kIds[] = {
#include "drakan_foliage_ids.inc"
};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::atomic<int> g_mode{0};
std::atomic<float> g_strength{1.0f};
PS2Runtime::RecompiledFunction g_orig = nullptr;
PS2Runtime::RecompiledFunction g_origInline = nullptr;
PS2Runtime::RecompiledFunction g_origCore[2] = {};

// Statistics for DRAKAN_FOLIAGE_LOG.
bool g_log = false;
uint32_t g_hits[2]{}, g_leafObjects = 0, g_leafVertices = 0;
double g_leafSeconds = 0;

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
}
inline uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline void put32(uint8_t* ram, uint32_t a, uint32_t v) {
    std::memcpy(ram + a, &v, 4);
}

// The clock is read once per 32 drawn plants / trees (well under a millisecond apart), so a frame shares a few time
// stamps and the wind direction cache in world/Foliage.h hits.
double nowSeconds() {
    static const auto t0 = std::chrono::steady_clock::now();
    static double cached = 0;
    static uint32_t n = 0;
    if ((n++ & 31u) == 0)
        cached = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return cached;
}

// 0 tree, 1 plant, -1 not in the vegetation table.
int kindOf(uint32_t resourceId) {
    const IdKind* end = std::end(kIds);
    const IdKind* it =
        std::lower_bound(std::begin(kIds), end, resourceId, [](const IdKind& e, uint32_t id) { return e.id < id; });
    return it != end && it->id == resourceId ? it->kind : -1;
}

// Resource db record of a handle, 0 when anything is out of range.
uint32_t resourceRecord(const uint8_t* ram, uint32_t handle) {
    uint16_t idx;
    std::memcpy(&idx, ram + handle + 0x10, 2);
    if (idx == 0xffffu)
        return 0;
    const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
    if (!inRam(ctx, 0x4c0))
        return 0;
    const uint32_t res = u32(ram, ctx + 0x320) & kRamMask;
    if (!inRam(res, 0x14) || idx > u32(ram, res + 0x10))
        return 0;
    const uint32_t rec = (u32(ram, res + 8) & kRamMask) + static_cast<uint32_t>(idx) * 0x24u;
    return inRam(rec, 0x24) ? rec : 0;
}
uint32_t resourceId(const uint8_t* ram, uint32_t handle) {
    const uint32_t rec = resourceRecord(ram, handle);
    return rec ? u32(ram, rec) : 0;
}
bool propClass(const uint8_t* ram, uint32_t handle) {
    uint16_t idx;
    std::memcpy(&idx, ram + handle + 0x10, 2);
    if (idx == 0xffffu || !inRam(kResourceDb, 0x14) || idx > u32(ram, kResourceDb + 0x10))
        return false;
    const uint32_t rec = (u32(ram, kResourceDb + 8) & kRamMask) + static_cast<uint32_t>(idx) * 0x24u;
    if (!inRam(rec, 0x24))
        return false;
    const uint32_t cls = u32(ram, rec + 8);
    return std::find(std::begin(kPropClasses), std::end(kPropClasses), cls) != std::end(kPropClasses);
}

// A loaded resource (model or texture) by id: the manager's 128 buckets, chained through +20. Callers cache it.
uint32_t findResource(const uint8_t* ram, uint32_t manager, uint32_t vt, uint32_t alt, uint32_t id) {
    for (unsigned b = 0; b < 128; ++b) {
        uint32_t p = u32(ram, manager + 0x20 + b * 4) & kRamMask;
        unsigned guard = 0;
        while (inRam(p, 0xc8) && guard++ < 8192) {
            const uint32_t v = u32(ram, p + 0x18);
            if ((v == vt || (alt && v == alt)) && (u32(ram, p + 8) & 3) == 3 && u32(ram, p) == id)
                return p;
            p = u32(ram, p + 20) & kRamMask;
        }
    }
    return 0;
}

// Share of fully transparent texels of a texture record (32 x 32 samples; 8-bit palettes, 16 and 32-bit texels);
// -1 when its texels are not readable (yet).
float cutShare(const uint8_t* ram, uint32_t p) {
    const uint32_t width = u32(ram, p + 0x24), height = u32(ram, p + 0x28), stride = u32(ram, p + 0x34),
                   bits = u32(ram, p + 0x38), source = u32(ram, p + 0x50) & kRamMask,
                   palette = u32(ram, p + 0x3c) & kRamMask;
    if (!width || !height || width > 2048 || height > 2048 || stride > 8192 || !inRam(source, stride * height))
        return -1;
    if (bits != 8 && bits != 16 && bits != 32)
        return -1;
    uint32_t format = 0, colors = 0, pal = 0;
    if (bits == 8) {
        if (!inRam(palette, 16))
            return -1;
        format = u32(ram, palette);
        colors = u32(ram, palette + 4);
        pal = u32(ram, palette + 8) & kRamMask;
        if (!colors || colors > 256 || (format != 1 && format != 2) || !inRam(pal, colors * (format == 1 ? 4 : 2)))
            return -1;
    }
    unsigned low = 0, n = 0;
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            const uint32_t a = source + (y * height / 32) * stride + (x * width / 32) * (bits / 8);
            unsigned alpha = 128;
            if (bits == 8) {
                unsigned index = ram[a];
                // The CLUT lanes 8 / 16 are swapped for the GS upload (as in drakan_scene_models.h).
                if (colors == 256)
                    index = (index & ~24u) | ((index & 8) << 1) | ((index & 16) >> 1);
                if (index >= colors)
                    continue;
                if (format == 1)
                    alpha = ram[pal + index * 4 + 3];
                else {
                    uint16_t c;
                    std::memcpy(&c, ram + pal + index * 2, 2);
                    alpha = c & 0x8000 ? 128 : 0;
                }
            } else if (bits == 16) {
                uint16_t c;
                std::memcpy(&c, ram + a, 2);
                alpha = c & 0x8000 ? 128 : 0;
            } else
                alpha = ram[a + 3];
            low += alpha < 16;
            ++n;
        }
    return n ? static_cast<float>(low) / static_cast<float>(n) : 0;
}

inline uint64_t positionKey(const uint8_t* ram, uint32_t a) {
    uint32_t x, y, z;
    std::memcpy(&x, ram + a, 4);
    std::memcpy(&y, ram + a + 4, 4);
    std::memcpy(&z, ram + a + 8, 4);
    uint64_t h = 0x9e3779b97f4a7c15ull ^ x;
    h = (h ^ (h >> 29)) * 0xbf58476d1ce4e5b9ull ^ y;
    h = (h ^ (h >> 31)) * 0x94d049bb133111ebull ^ z;
    return h ^ (h >> 32);
}

// What a model needs for leaf wind, computed once per loaded model (validated by its address, id and arrays).
struct ModelInfo {
    uint32_t id = 0, nv = 0, nf = 0, pos = 0;
    bool leafy = false;
    bool incomplete = false; // a texture was not loaded yet: classify again later
    double retry = 0;
    float yMin = 0, yMax = 0, axisX = 0, axisZ = 0, rMax = 0;
    std::vector<uint8_t> leafMat;
    std::unordered_set<uint64_t> anchors; // positions a solid face uses
    std::vector<wf::LeafVertex> core;     // per vertex of the model's own position array
};
std::unordered_map<uint32_t, ModelInfo> g_models;      // by model address
std::unordered_map<uint32_t, uint32_t> g_modelById;    // model id -> address (0 = not found yet)
std::unordered_map<uint32_t, double> g_modelMissRetry; // model id -> time of the next search
std::unordered_map<uint64_t, float> g_textureCut;      // (texture id, record) -> share of cut texels

float textureCut(const uint8_t* ram, uint32_t textureId) {
    const uint32_t rec = findResource(ram, kTextureManager, kTextureVt, 0, textureId);
    if (!rec)
        return -1;
    const uint64_t key = uint64_t(textureId) << 32 | rec;
    const auto it = g_textureCut.find(key);
    if (it != g_textureCut.end())
        return it->second;
    const float share = cutShare(ram, rec);
    if (share >= 0)
        g_textureCut[key] = share;
    return share;
}

// Builds the leaf data of model `m`. `known` = the vegetation table lists the object as a tree (then any cut-out
// material counts; otherwise the model must look like a tree).
void buildModel(const uint8_t* ram, uint32_t m, bool known, ModelInfo& mi) {
    mi = ModelInfo{};
    mi.id = u32(ram, m);
    mi.nv = u32(ram, m + 0x1c);
    mi.nf = u32(ram, m + 0x20);
    mi.pos = u32(ram, m + 0x24) & kRamMask;
    const uint32_t fp = u32(ram, m + 0x28) & kRamMask, lib = u32(ram, m + 0x38) & kRamMask, nm = u32(ram, m + 0x40);
    if (!mi.nv || mi.nv > 65535 || !inRam(mi.pos, mi.nv * 12) || !mi.nf || mi.nf > 100000 || !inRam(fp, mi.nf * 12) ||
        !nm || nm > 256 || !inRam(lib, nm * 4))
        return;
    std::vector<wf::MaterialStats> mats(nm);
    for (uint32_t i = 0; i < nm; ++i) {
        mats[i].cutShare = textureCut(ram, u32(ram, lib + i * 4));
        if (mats[i].cutShare < 0) {
            mi.incomplete = true;
            mats[i].cutShare = 0;
        }
        mats[i].yMin = 1e30f;
        mats[i].yMax = -1e30f;
    }
    mi.yMin = 1e30f;
    mi.yMax = -1e30f;
    for (uint32_t v = 0; v < mi.nv; ++v) {
        const float y = f32(ram, mi.pos + v * 12 + 4);
        if (!std::isfinite(y))
            return;
        mi.yMin = std::min(mi.yMin, y);
        mi.yMax = std::max(mi.yMax, y);
    }
    // 1 = used by a leaf face, 2 = used by a solid face.
    std::vector<uint8_t> use(mi.nv, 0);
    for (uint32_t f = 0; f < mi.nf; ++f) {
        uint16_t fv[6];
        std::memcpy(fv, ram + fp + f * 12, 12);
        if (fv[4] >= nm)
            continue;
        wf::MaterialStats& ms = mats[fv[4]];
        ++ms.faces;
        const bool leaf = wf::leafMaterial(ms.cutShare);
        for (int k = 0; k < ((fv[5] & 4) ? 4 : 3); ++k) {
            if (fv[k] >= mi.nv)
                continue;
            const float y = f32(ram, mi.pos + fv[k] * 12 + 4);
            ms.yMin = std::min(ms.yMin, y);
            ms.yMax = std::max(ms.yMax, y);
            use[fv[k]] |= leaf ? 1 : 2;
        }
    }
    mi.leafMat.resize(nm);
    bool anyLeaf = false;
    for (uint32_t i = 0; i < nm; ++i) {
        mi.leafMat[i] = wf::leafMaterial(mats[i].cutShare) && mats[i].faces;
        anyLeaf |= mi.leafMat[i] != 0;
    }
    mi.leafy = anyLeaf && (known || wf::looksLikeTree(mats.data(), mats.size(), mi.yMin, mi.yMax));
    if (!mi.leafy)
        return;
    if (g_log)
        std::fprintf(stderr, "[foliage] leaves for model %x (%s): %u materials, height %.0f, %u faces\n", mi.id,
                     known ? "vegetation table" : "looks like a tree", nm, mi.yMax - mi.yMin, mi.nf);
    // Trunk axis: the mean of the solid vertices in the lowest third; the crown's reach from it.
    double sx = 0, sz = 0;
    uint32_t n = 0;
    const float low = mi.yMin + (mi.yMax - mi.yMin) / 3.0f;
    for (uint32_t v = 0; v < mi.nv; ++v)
        if (use[v] & 2) {
            mi.anchors.insert(positionKey(ram, mi.pos + v * 12));
            if (f32(ram, mi.pos + v * 12 + 4) <= low) {
                sx += f32(ram, mi.pos + v * 12);
                sz += f32(ram, mi.pos + v * 12 + 8);
                ++n;
            }
        }
    if (n) {
        mi.axisX = static_cast<float>(sx / n);
        mi.axisZ = static_cast<float>(sz / n);
    }
    for (uint32_t v = 0; v < mi.nv; ++v)
        if (use[v] & 1) {
            const float dx = f32(ram, mi.pos + v * 12) - mi.axisX, dz = f32(ram, mi.pos + v * 12 + 8) - mi.axisZ;
            mi.rMax = std::max(mi.rMax, std::sqrt(dx * dx + dz * dz));
        }
    mi.core.resize(mi.nv);
    for (uint32_t v = 0; v < mi.nv; ++v) {
        const uint32_t a = mi.pos + v * 12;
        const float x = f32(ram, a), y = f32(ram, a + 4), z = f32(ram, a + 8);
        const float r = std::sqrt((x - mi.axisX) * (x - mi.axisX) + (z - mi.axisZ) * (z - mi.axisZ));
        const bool anchored = use[v] != 1 || mi.anchors.count(positionKey(ram, a));
        mi.core[v] =
            wf::leafVertex(x, y, z, wf::leafWeight(y, r, mi.yMin, mi.yMax, mi.rMax, anchored), mi.yMax - mi.yMin);
    }
}

// The leaf data of a handle's model, nullptr when it has none (cached; a miss is searched again after 2 s).
const ModelInfo* modelOf(const uint8_t* ram, uint32_t handle, bool known, double now) {
    const uint32_t id = u32(ram, handle + 8);
    uint32_t m = 0;
    if (auto it = g_modelById.find(id); it != g_modelById.end()) {
        m = it->second;
        if (!inRam(m, 0xc8) || u32(ram, m) != id ||
            (u32(ram, m + 0x18) != kModelVt && u32(ram, m + 0x18) != kModelLodVt) || (u32(ram, m + 8) & 3) != 3)
            m = 0;
    }
    if (!m) {
        if (auto r = g_modelMissRetry.find(id); r != g_modelMissRetry.end() && now < r->second)
            return nullptr;
        m = findResource(ram, kModelManager, kModelVt, kModelLodVt, id);
        g_modelById[id] = m;
        if (!m) {
            g_modelMissRetry[id] = now + 2.0;
            return nullptr;
        }
    }
    ModelInfo& mi = g_models[m];
    if (mi.id != id || mi.nv != u32(ram, m + 0x1c) || mi.nf != u32(ram, m + 0x20) ||
        mi.pos != (u32(ram, m + 0x24) & kRamMask) || (mi.incomplete && now >= mi.retry)) {
        buildModel(ram, m, known, mi);
        mi.retry = now + 2.0;
        if (g_log && mi.leafy)
            std::fprintf(stderr, "[foliage] model %x first drawn by the object at %.0f %.0f %.0f\n", id,
                         f32(ram, handle + 0x14), f32(ram, handle + 0x18), f32(ram, handle + 0x1c));
    }
    return mi.leafy ? &mi : nullptr;
}

// Leaf data of one prepared draw array (a group of the inline path), by its source address.
struct GroupInfo {
    const ModelInfo* model = nullptr;
    uint32_t n = 0;
    uint64_t check = 0;
    bool any = false;
    std::vector<wf::LeafVertex> v;
};
std::unordered_map<uint32_t, GroupInfo> g_groups;

const GroupInfo& groupOf(const uint8_t* ram, uint32_t src, uint32_t n, const ModelInfo& mi) {
    GroupInfo& g = g_groups[src];
    const uint64_t check = positionKey(ram, src) ^ (positionKey(ram, src + (n - 1) * 12) * 31u) ^ n;
    if (g.model == &mi && g.n == n && g.check == check)
        return g;
    g.model = &mi;
    g.n = n;
    g.check = check;
    g.any = false;
    g.v.assign(n, {});
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t a = src + i * 12;
        const float x = f32(ram, a), y = f32(ram, a + 4), z = f32(ram, a + 8);
        const float r = std::sqrt((x - mi.axisX) * (x - mi.axisX) + (z - mi.axisZ) * (z - mi.axisZ));
        const bool anchored = mi.anchors.count(positionKey(ram, a)) != 0;
        g.v[i] = wf::leafVertex(x, y, z, wf::leafWeight(y, r, mi.yMin, mi.yMax, mi.rMax, anchored), mi.yMax - mi.yMin);
        g.any |= g.v[i].w > 0;
    }
    return g;
}

// Guest memory for the moved copies. The engine's packets reference position arrays and the DMA reads them when the
// chain is sent, after the object's render call, so every object needs its own copy that stays put until then: one
// block from the game's heap, carved per (object, array) and refilled every frame.
uint32_t g_pool = 0, g_poolSize = 0, g_poolUsed = 0, g_poolFailures = 0;
struct Slot {
    uint32_t address = 0, bytes = 0;
};
std::unordered_map<uint64_t, Slot> g_slots;

bool ensurePool(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (g_pool)
        return true;
    if (g_poolFailures > 64)
        return false;
    const int sema = static_cast<int>(u32(ram, kHeapSemaWord));
    if (rt->eeScheduler().pollSemaphore(sema) != sema) {
        ++g_poolFailures;
        return false;
    }
    drakan_lifecycle::Event e;
    e.rdram = ram;
    e.runtime = rt;
    e.ctx = c;
    const uint32_t want = drakan_extended_heap::heapWillExtend() ? (6u << 20) : (1u << 20);
    const uint32_t block = drakan_lifecycle::callGuest(e, kMalloc, want) & kRamMask;
    drakan_lifecycle::callGuest(e, kSignalSema, static_cast<uint32_t>(sema));
    if (!block || (block & 15u) || !inRam(block, want)) {
        ++g_poolFailures;
        return false;
    }
    g_pool = block;
    g_poolSize = want;
    std::fprintf(stderr, "[foliage] leaf buffer 0x%x (%u KiB)\n", g_pool, want >> 10);
    return true;
}

uint32_t slotFor(uint32_t handle, uint32_t src, uint32_t bytes) {
    const uint64_t key = uint64_t(handle) << 32 | src;
    bytes = (bytes + 15u) & ~15u;
    if (const auto it = g_slots.find(key); it != g_slots.end() && it->second.bytes == bytes)
        return it->second.address;
    if (g_poolUsed + bytes > g_poolSize) {
        // Full (new objects keep coming, a level change): start over. At worst one frame draws a few leaves from a
        // reused copy.
        g_slots.clear();
        g_poolUsed = 0;
        if (bytes > g_poolSize)
            return 0;
    }
    Slot& fresh = g_slots[key];
    fresh.address = g_pool + g_poolUsed;
    fresh.bytes = bytes;
    g_poolUsed += bytes;
    return fresh.address;
}

// The object whose render call is running and what moves its leaves.
struct Current {
    bool active = false;
    uint32_t handle = 0;
    const ModelInfo* model = nullptr;
    wf::LeafFrame frame;
    std::vector<uint32_t> filled; // arrays already refilled during this call
};
Current g_cur;

// Copies `n` source vertices to the object's slot and moves its leaf vertices. Returns the slot, 0 for none.
uint32_t fillCopy(uint8_t* ram, uint32_t src, uint32_t n, const wf::LeafVertex* v) {
    const uint32_t dst = slotFor(g_cur.handle, src, n * 12);
    if (!dst || !inRam(dst, n * 12))
        return 0;
    if (std::find(g_cur.filled.begin(), g_cur.filled.end(), dst) != g_cur.filled.end())
        return dst;
    g_cur.filled.push_back(dst);
    std::memcpy(ram + dst, ram + src, n * 12);
    for (uint32_t i = 0; i < n; ++i) {
        if (v[i].w <= 0)
            continue;
        float o[3];
        wf::leafOffset(g_cur.frame, v[i], o);
        for (int k = 0; k < 3; ++k) {
            const float p = f32(ram, dst + i * 12 + k * 4) + o[k];
            std::memcpy(ram + dst + i * 12 + k * 4, &p, 4);
        }
        ++g_leafVertices;
    }
    return dst;
}

struct Swap {
    uint32_t at, raw;
};

// Inline path (0x1ed8f0, a1 = the prepared mesh): the leaf groups read the object's moved copies for this call.
void hookInline(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!g_cur.active) {
        g_origInline(ram, c, rt);
        return;
    }
    const ModelInfo& mi = *g_cur.model;
    const uint32_t mesh = GPR_U32(c, 5) & kRamMask;
    Swap swaps[256];
    uint32_t nswaps = 0;
    if (inRam(mesh, 0x34)) {
        const uint32_t count = u32(ram, mesh + 0xc), parts = u32(ram, mesh + 0x10) & kRamMask;
        for (uint32_t i = 0; count <= 4096 && i < count && inRam(parts + i * 0x30, 0x30); ++i) {
            const uint32_t part = parts + i * 0x30;
            const uint32_t mat = u32(ram, part);
            if (mat >= mi.leafMat.size() || !mi.leafMat[mat])
                continue;
            const uint32_t groups = u32(ram, part + 0x28), base = u32(ram, part + 0x20) & kRamMask;
            for (uint32_t g = 0; groups <= 65536 && g < groups && inRam(base + g * 16, 16) && nswaps < 256; ++g) {
                const uint32_t at = base + g * 16 + 8, n = u32(ram, base + g * 16), raw = u32(ram, at);
                const uint32_t src = raw & kRamMask;
                if (!n || n > 256 || !inRam(src, n * 12))
                    continue;
                const GroupInfo& gi = groupOf(ram, src, n, mi);
                if (!gi.any)
                    continue;
                const uint32_t dst = fillCopy(ram, src, n, gi.v.data());
                if (!dst)
                    continue;
                swaps[nswaps++] = {at, raw};
                put32(ram, at, (raw & ~kRamMask) | dst);
            }
        }
    }
    g_origInline(ram, c, rt);
    for (uint32_t i = nswaps; i-- > 0;)
        put32(ram, swaps[i].at, swaps[i].raw);
}

// Core path (0x1ec160 / 0x1ec8f0): renderer +0xc points into the model's own position array.
template <int W>
void hookCore(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!g_cur.active || g_cur.model->core.empty()) {
        g_origCore[W](ram, c, rt);
        return;
    }
    const ModelInfo& mi = *g_cur.model;
    const uint32_t renderer = GPR_U32(c, 4) & kRamMask;
    uint32_t raw = 0, dst = 0;
    if (inRam(renderer, 0x10)) {
        raw = u32(ram, renderer + 0xc);
        const uint32_t src = raw & kRamMask;
        if (src >= mi.pos && src < mi.pos + mi.nv * 12 && (src - mi.pos) % 12 == 0) {
            const uint32_t first = (src - mi.pos) / 12;
            dst = fillCopy(ram, src, mi.nv - first, mi.core.data() + first);
            if (dst)
                put32(ram, renderer + 0xc, (raw & ~kRamMask) | dst);
        }
    }
    g_origCore[W](ram, c, rt);
    if (dst)
        put32(ram, renderer + 0xc, raw);
}

void report(const uint8_t* ram, double t, float strength) {
    static double next = 0;
    if (t < next)
        return;
    next = t + 5.0;
    float wind[3] = {0, 0, 0};
    const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
    if (inRam(ctx, 0x430))
        for (int i = 0; i < 3; ++i)
            wind[i] = f32(ram, ctx + 0x40c + i * 4);
    std::fprintf(stderr,
                 "[foliage] drawn since last report: trees %u plants %u, leaf objects %u, leaf vertices %u, "
                 "leaf time %.2f ms, buffer %u / %u KiB, level wind %.3f %.3f %.3f (strength %.2f)\n",
                 g_hits[0], g_hits[1], g_leafObjects, g_leafVertices, g_leafSeconds * 1000.0, g_poolUsed >> 10,
                 g_poolSize >> 10, wind[0], wind[1], wind[2], strength);
    g_hits[0] = g_hits[1] = g_leafObjects = g_leafVertices = 0;
    g_leafSeconds = 0;
}

// The first version's whole-object lean (plants always, trees in classic mode): a shear of the model's up axis for
// the duration of the call.
void drawLeaning(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t h, int kind, float strength) {
    const float px = f32(ram, h + 0x14), pz = f32(ram, h + 0x1c);
    // Column 1 of the 3x3 at +0x20 (floats at +0x24, +0x30, +0x3c) is where the model's up axis ends up in the world.
    constexpr uint32_t kCol1[3] = {0x24, 0x30, 0x3c};
    float up[3];
    for (int i = 0; i < 3; ++i)
        up[i] = f32(ram, h + kCol1[i]);
    const float len = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (!std::isfinite(px) || !std::isfinite(pz) || !std::isfinite(len) || len < 1e-6f || len > 1e6f) {
        g_orig(ram, c, rt);
        return;
    }
    wf::Lean lean = wf::windLean(kind, px, pz, h, nowSeconds(), strength);
    if (kind == wf::Plant) {
        // Grass bends away from whoever the player controls (Rynn, or Arokh when mounted).
        const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
        const uint32_t actor =
            inRam(ctx, 0x4c0) ? u32(ram, ctx + drakan_layout::engine::kControlledActor) & kRamMask : 0;
        if (inRam(actor, 0x20) && u32(ram, actor) == kHandleVtable) {
            const wf::Lean push = wf::pushLean(px - f32(ram, actor + 0x14), pz - f32(ram, actor + 0x1c), strength);
            lean.x += push.x;
            lean.z += push.z;
        }
    }
    lean = wf::clampLean(lean);
    float add[3];
    wf::shearAdd(lean, len, add);
    for (int i = 0; i < 3; ++i) {
        const float v = up[i] + add[i];
        std::memcpy(ram + h + kCol1[i], &v, 4);
    }
    if (g_log)
        ++g_hits[kind];
    g_orig(ram, c, rt);
    // The change lives for this draw call only: the true transform goes back before anything else can read it.
    for (int i = 0; i < 3; ++i)
        std::memcpy(ram + h + kCol1[i], &up[i], 4);
}

// Leaf wind for one tree: its leaf vertices are moved in private copies while the trunk keeps the shared arrays.
bool drawLeaves(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t h, bool known, float strength) {
    const double t = nowSeconds();
    const ModelInfo* mi = modelOf(ram, h, known, t);
    if (!mi || !ensurePool(ram, c, rt))
        return false;
    float r[9];
    for (int i = 0; i < 9; ++i)
        r[i] = f32(ram, h + 0x20 + i * 4);
    float toModel[9];
    if (!wf::invert3(r, toModel))
        return false;
    const float px = f32(ram, h + 0x14), pz = f32(ram, h + 0x1c);
    const float upLen = std::sqrt(r[1] * r[1] + r[4] * r[4] + r[7] * r[7]);
    if (!std::isfinite(px) || !std::isfinite(pz) || !std::isfinite(upLen))
        return false;
    // The level's wind (the engine's eased gust vector, engine +0x40c / +0x414, which also blows its particles).
    float lx = 0, lz = 0;
    const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
    if (inRam(ctx, 0x430)) {
        lx = f32(ram, ctx + 0x40c);
        lz = f32(ram, ctx + 0x414);
    }
    const auto start = g_log ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    g_cur.active = true;
    g_cur.handle = h;
    g_cur.model = mi;
    g_cur.filled.clear();
    g_cur.frame =
        wf::leafFrame(wf::steerWind(lx, lz, t), px, pz, h, t, strength, (mi->yMax - mi->yMin) * upLen, toModel);
    if (g_log) {
        ++g_leafObjects;
        ++g_hits[wf::Tree];
    }
    g_orig(ram, c, rt);
    g_cur.active = false;
    if (g_log)
        g_leafSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

void hookRenderObject(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!g_enabled.load(std::memory_order_relaxed) || g_cur.active) {
        g_orig(ram, c, rt);
        return;
    }
    const uint32_t h = GPR_U32(c, 4) & kRamMask;
    if (!inRam(h, 0x120) || u32(ram, h) != kHandleVtable) {
        g_orig(ram, c, rt);
        return;
    }
    const Mode mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    const float strength = g_strength.load(std::memory_order_relaxed);
    const int kind = kindOf(resourceId(ram, h));
    if (g_log)
        report(ram, nowSeconds(), strength);
    if (kind == wf::Plant) {
        drawLeaning(ram, c, rt, h, kind, strength);
        return;
    }
    if (kind == wf::Tree && mode == Mode::Classic) {
        drawLeaning(ram, c, rt, h, kind, strength);
        return;
    }
    // Leaves: static objects only (an animated model draws from its own pose).
    if (mode == Mode::All && !u32(ram, h + 0x50) && (kind == wf::Tree || propClass(ram, h)) &&
        drawLeaves(ram, c, rt, h, kind == wf::Tree, strength))
        return;
    g_orig(ram, c, rt);
}

} // namespace

Mode modeFromText(const char* text) {
    std::string u = text ? text : "";
    for (char& ch : u)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back())))
        u.pop_back();
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.front())))
        u.erase(u.begin());
    if (u == "grass")
        return Mode::Grass;
    if (u == "all" || u == "1" || u == "on" || u == "true" || u == "yes" || u == "leaves")
        return Mode::All;
    if (u == "classic")
        return Mode::Classic;
    return Mode::Off;
}

void configure(Mode m, float s) {
    g_strength.store(std::clamp(s, wf::kMinStrength, wf::kMaxStrength), std::memory_order_relaxed);
    g_mode.store(static_cast<int>(m), std::memory_order_relaxed);
    g_enabled.store(m != Mode::Off, std::memory_order_relaxed);
}

Mode mode() {
    return static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

float strength() {
    return g_strength.load(std::memory_order_relaxed);
}

bool available() {
    return g_installed.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kRenderObject) || !rt.hasFunction(kInlineParts) ||
        !rt.hasFunction(kCoreParts[0]) || !rt.hasFunction(kCoreParts[1]) || !rt.hasFunction(kMalloc))
        return;
    configure(
        modeFromText(drakan_cfg::getString("video", "foliage_wind", "DRAKAN_FOLIAGE_WIND", "off").c_str()),
        static_cast<float>(drakan_cfg::getFloat("video", "foliage_wind_strength", "DRAKAN_FOLIAGE_STRENGTH", 1.0)));
    g_log = std::getenv("DRAKAN_FOLIAGE_LOG") != nullptr;
    g_orig = rt.lookupFunction(kRenderObject);
    rt.replaceFunction(kRenderObject, &hookRenderObject);
    g_origInline = rt.lookupFunction(kInlineParts);
    rt.replaceFunction(kInlineParts, &hookInline);
    g_origCore[0] = rt.lookupFunction(kCoreParts[0]);
    rt.replaceFunction(kCoreParts[0], &hookCore<0>);
    g_origCore[1] = rt.lookupFunction(kCoreParts[1]);
    rt.replaceFunction(kCoreParts[1], &hookCore<1>);
    g_installed = true;
    static const char* const kNames[] = {"off", "grass", "all", "classic"};
    std::fprintf(stderr, "[foliage] installed (%s, strength %.2f)\n", kNames[g_mode.load() & 3], g_strength.load());
}

} // namespace drakan_foliage
