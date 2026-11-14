#include "drakan_terrain_chunks.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "hle/ModPack.h"
#include "ps2_alias_watch.h"
#include "ps2_function_variants.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/TerrainChunks.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace drakan_terrain_chunks {
namespace {

namespace rules = world::terrain_chunks;

constexpr const char* kVariant = "wide_terrain_chunks";
// Guest functions that must all come from the variant (checked against the registered set): the record users found
// in the PAL code and the terrain mesh drawer. Dead copies (0x154ba8, 0x165a80, 0x165cb0, 0x165f68) are patched too.
constexpr uint32_t kRequired[] = {0x154060u, 0x154d98u, 0x158148u, 0x159998u,
                                  0x15c948u, 0x15cba0u, 0x15e418u, 0x165840u};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_wide{false};

// ---- watch (diagnosis) ----

constexpr uint32_t kMalloc = 0x4ac628u, kRealloc = 0x4acef0u, kFree = 0x4acc40u;

PS2Runtime::RecompiledFunction g_origMalloc = nullptr, g_origRealloc = nullptr, g_origFree = nullptr;

// The chunk vector is (re)allocated by the stream loader FUN_0015c948 and the (unused) runtime builder FUN_00165cb0.
bool vectorSite(uint32_t ra) {
    return (ra >= 0x15c948u && ra < 0x15cba0u) || (ra >= 0x165cb0u && ra < 0x165f68u);
}

struct Key {
    uint32_t pc, offset;
    bool operator<(const Key& o) const { return pc != o.pc ? pc < o.pc : offset < o.offset; }
};
struct Count {
    uint64_t reads = 0, writes = 0;
};

std::mutex g_watchMutex;
std::map<uint32_t, uint32_t> g_vectors; // plain base -> bytes
std::map<Key, Count> g_hits;
std::map<uint32_t, uint64_t> g_stubHits; // ra of memset / memmove on an aliased address
uint64_t g_unmatched = 0;
std::chrono::steady_clock::time_point g_lastDump;

void dumpLocked(const char* why) {
    std::fprintf(stderr, "[chunk-watch] %s: %zu vectors, %zu (pc, field) pairs, %llu outside a vector\n", why,
                 g_vectors.size(), g_hits.size(), (unsigned long long)g_unmatched);
    uint32_t lastPc = 0xffffffffu;
    std::string line;
    for (const auto& [k, c] : g_hits) {
        if (k.pc != lastPc) {
            if (!line.empty())
                std::fprintf(stderr, "%s\n", line.c_str());
            char head[64];
            std::snprintf(head, sizeof(head), "[chunk-watch] pc 0x%06x:", k.pc);
            line = head;
            lastPc = k.pc;
        }
        char item[64];
        std::snprintf(item, sizeof(item), " +0x%02x r%llu w%llu", k.offset, (unsigned long long)c.reads,
                      (unsigned long long)c.writes);
        line += item;
    }
    if (!line.empty())
        std::fprintf(stderr, "%s\n", line.c_str());
    for (const auto& [ra, n] : g_stubHits)
        std::fprintf(stderr, "[chunk-watch] memset/memmove from ra 0x%06x: %llu\n", ra, (unsigned long long)n);
}

void watchAccess(R5900Context* ctx, uint32_t address, uint32_t bytes, bool write) {
    const uint32_t plain = address & ~kPs2AliasMask;
    const uint32_t stride = g_wide.load(std::memory_order_relaxed) ? rules::kWideStride : rules::kNarrowStride;
    std::lock_guard<std::mutex> lock(g_watchMutex);
    auto it = g_vectors.upper_bound(plain);
    if (it == g_vectors.begin() || plain >= std::prev(it)->first + std::prev(it)->second) {
        ++g_unmatched;
    } else {
        --it;
        Count& c = g_hits[Key{ctx ? ctx->pc : 0u, (plain - it->first) % stride}];
        (write ? c.writes : c.reads) += 1;
        (void)bytes;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastDump > std::chrono::seconds(10)) {
        g_lastDump = now;
        dumpLocked("periodic");
    }
}

uint32_t strip(uint32_t a) {
    return ps2IsAlias(a) ? (a & ~kPs2AliasMask) : a;
}

void noteResult(R5900Context* c, uint32_t ra, uint32_t oldPlain, uint32_t bytes) {
    if (c->pc != ra)
        return; // suspended inside: the resume finishes it (the vector is aliased when that call returns)
    const uint32_t v = GPR_U32(c, 2);
    std::lock_guard<std::mutex> lock(g_watchMutex);
    if (oldPlain)
        g_vectors.erase(oldPlain);
    if (v != 0 && !ps2IsAlias(v)) {
        g_vectors[v & 0x1fffffffu] = bytes;
        setReturnU32(c, (v & 0x1fffffffu) | kPs2AliasBase);
    }
}

void hookMalloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = GPR_U32(c, 31);
    const bool site = c->pc == kMalloc && vectorSite(ra);
    const uint32_t bytes = GPR_U32(c, 4);
    g_origMalloc(ram, c, rt);
    if (site)
        noteResult(c, ra, 0, bytes);
}

void hookRealloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = GPR_U32(c, 31);
    uint32_t oldPlain = 0;
    const bool entry = c->pc == kRealloc;
    if (entry && ps2IsAlias(GPR_U32(c, 4))) {
        oldPlain = strip(GPR_U32(c, 4));
        SET_GPR_U32(c, 4, oldPlain);
    }
    const bool site = entry && (oldPlain != 0 || vectorSite(ra));
    const uint32_t bytes = GPR_U32(c, 5);
    g_origRealloc(ram, c, rt);
    if (site)
        noteResult(c, ra, oldPlain, bytes);
}

void hookFree(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kFree && ps2IsAlias(GPR_U32(c, 4))) {
        const uint32_t plain = strip(GPR_U32(c, 4));
        SET_GPR_U32(c, 4, plain);
        std::lock_guard<std::mutex> lock(g_watchMutex);
        g_vectors.erase(plain);
    }
    g_origFree(ram, c, rt);
}

// The host libc stubs resolve guest pointers themselves (getMemPtr), which knows no alias: the aliased argument is
// logged with the caller and handed on plain.
struct MemStub {
    uint32_t address;
    PS2Runtime::RecompiledFunction orig;
};
MemStub g_memStubs[] = {{0x4cd27cu, nullptr}, {0x4cd32cu, nullptr}, {0x4cd430u, nullptr},
                        {0x4cd1e8u, nullptr}, {0x4cd108u, nullptr}, {0x4afd48u, nullptr}};

template <size_t I>
void hookMemStub(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (ps2IsAlias(GPR_U32(c, 4)) || ps2IsAlias(GPR_U32(c, 5))) {
        {
            std::lock_guard<std::mutex> lock(g_watchMutex);
            ++g_stubHits[GPR_U32(c, 31)];
        }
        SET_GPR_U32(c, 4, strip(GPR_U32(c, 4)));
        SET_GPR_U32(c, 5, strip(GPR_U32(c, 5)));
    }
    g_memStubs[I].orig(ram, c, rt);
}
constexpr PS2Runtime::RecompiledFunction kMemHooks[] = {&hookMemStub<0>, &hookMemStub<1>, &hookMemStub<2>,
                                                        &hookMemStub<3>, &hookMemStub<4>, &hookMemStub<5>};
static_assert(sizeof(kMemHooks) / sizeof(kMemHooks[0]) == sizeof(g_memStubs) / sizeof(g_memStubs[0]));

void installWatch(PS2Runtime& rt) {
    for (uint32_t f : {kMalloc, kRealloc, kFree})
        if (!rt.hasFunction(f))
            return;
    g_origMalloc = rt.lookupFunction(kMalloc);
    g_origRealloc = rt.lookupFunction(kRealloc);
    g_origFree = rt.lookupFunction(kFree);
    rt.replaceFunction(kMalloc, &hookMalloc);
    rt.replaceFunction(kRealloc, &hookRealloc);
    rt.replaceFunction(kFree, &hookFree);
    for (size_t i = 0; i < sizeof(g_memStubs) / sizeof(g_memStubs[0]); ++i) {
        if (!rt.hasFunction(g_memStubs[i].address))
            continue;
        g_memStubs[i].orig = rt.lookupFunction(g_memStubs[i].address);
        rt.replaceFunction(g_memStubs[i].address, kMemHooks[i]);
    }
    g_lastDump = std::chrono::steady_clock::now();
    g_ps2AliasWatch = &watchAccess;
    std::atexit([] {
        std::lock_guard<std::mutex> lock(g_watchMutex);
        dumpLocked("exit");
    });
    std::fprintf(stderr, "[chunk-watch] on: terrain chunk vectors are aliased at 0x%x\n", kPs2AliasBase);
}

bool installVariants(PS2Runtime& rt) {
    const std::vector<Ps2FunctionVariant> variants = ps2FunctionVariants(kVariant);
    for (uint32_t need : kRequired) {
        bool found = false;
        for (const auto& v : variants)
            found = found || v.start == need;
        if (!found) {
            std::fprintf(stderr, "[terrain-chunks] the exe has no %s copy of 0x%x: wide chunks off\n", kVariant, need);
            return false;
        }
    }
    // All or none: a mix of record layouts would corrupt the heap.
    std::vector<PS2Runtime::RecompiledFunction> originals;
    for (const auto& v : variants) {
        if (!rt.hasFunction(v.start)) {
            std::fprintf(stderr, "[terrain-chunks] 0x%x is not in the function table: wide chunks off\n", v.start);
            return false;
        }
        originals.push_back(rt.lookupFunction(v.start));
    }
    size_t slots = 0;
    for (size_t i = 0; i < variants.size(); ++i) {
        const size_t n = ps2InstallFunctionVariant(rt, variants[i], originals[i]);
        if (n == 0) {
            std::fprintf(stderr, "[terrain-chunks] could not install the copy of 0x%x (already replaced?)\n",
                         variants[i].start);
            for (size_t j = 0; j < i; ++j) { // undo
                for (uint32_t pc = variants[j].start; pc < variants[j].end; pc += 4u)
                    if (rt.hasFunction(pc) && rt.lookupFunction(pc) == variants[j].fn)
                        rt.replaceFunction(pc, originals[j]);
            }
            return false;
        }
        slots += n;
    }
    std::fprintf(stderr, "[terrain-chunks] wide: %zu functions (%zu table slots), up to %u textures per chunk\n",
                 variants.size(), slots, rules::maxTextures(true));
    return true;
}

std::string trimmed(const std::string& v) {
    size_t a = 0, b = v.size();
    while (a < b && std::isspace(static_cast<unsigned char>(v[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1])))
        --b;
    return v.substr(a, b - a);
}

// The largest `terrain_chunk_textures` (mod.toml) of the packs mods.ini enables; 0 when none declares it. Read here,
// before the mod manager loads the packs, because the variants must be in place before any other module wraps one of
// those functions.
uint32_t packChunkTextures(std::string& which) {
    const char* modsEnv = std::getenv("DRAKAN_MODS");
    const std::filesystem::path dir = modsEnv && *modsEnv ? std::string(modsEnv) : drakan_cfg::exeDir() + "mods";
    std::ifstream ini(dir / "mods.ini");
    uint32_t most = 0;
    std::string line;
    while (std::getline(ini, line)) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[' || eq == std::string::npos)
            continue;
        const std::string name = trimmed(line.substr(0, eq));
        std::string value = trimmed(line.substr(eq + 1));
        for (char& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name.empty() || !(value == "on" || value == "1" || value == "true" || value == "yes"))
            continue;
        std::error_code ec;
        std::filesystem::path path = dir / name;
        if (!std::filesystem::is_directory(path, ec))
            path = dir / (name + ".zip");
        std::string err;
        const auto archive = hle::mods::openArchive(path.string(), err);
        std::vector<uint8_t> bytes;
        if (!archive || !archive->read("mod.toml", bytes))
            continue;
        std::string text(bytes.begin(), bytes.end());
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string row = trimmed(text.substr(start, end - start));
            start = end + 1;
            const size_t e = row.find('=');
            if (e == std::string::npos || trimmed(row.substr(0, e)) != "terrain_chunk_textures")
                continue;
            const uint32_t n = static_cast<uint32_t>(std::strtoul(row.c_str() + e + 1, nullptr, 10));
            if (n > most) {
                most = n;
                which = name;
            }
        }
    }
    return most;
}

} // namespace

bool wide() {
    return g_wide.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.exchange(true))
        return;
    if (!drakan_region::build().identityAddresses)
        return;
    // A pack whose chunks list more textures than the disc layout holds gets the wide layout whatever the setting says:
    // the disc functions would overwrite the record and hang (docs/journal/2026-11-01/ootf-texture-limit.md).
    std::string pack;
    const uint32_t packNeeds = packChunkTextures(pack);
    const bool setting = drakan_cfg::getBool("general", "terrain_wide_chunks", "DRAKAN_TERRAIN_WIDE_CHUNKS", false);
    if (setting || packNeeds > rules::maxTextures(false)) {
        if (!setting)
            std::fprintf(stderr, "[terrain-chunks] pack '%s' lists up to %u textures per chunk: wide layout on\n",
                         pack.c_str(), packNeeds);
        g_wide = installVariants(rt);
        if (!g_wide.load() && packNeeds > rules::maxTextures(false))
            std::fprintf(stderr, "[terrain-chunks] pack '%s' needs wide chunks, which this exe cannot provide\n",
                         pack.c_str());
    } else {
        std::fprintf(stderr, "[terrain-chunks] disc layout: up to %u textures per chunk\n", rules::maxTextures(false));
    }
    if (const char* w = std::getenv("DRAKAN_TERRAIN_CHUNK_WATCH"); w && *w && *w != '0')
        installWatch(rt);
}

} // namespace drakan_terrain_chunks
