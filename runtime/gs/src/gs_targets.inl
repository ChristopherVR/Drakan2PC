// Render-target policy: target lookup and aliasing, which target a texture samples, dirty rows and row stamps,
// when a read needs a readback, image uploads into targets and what they prove clean. Shared by the hardware backends
// (gs_backend_shared.h): compiled once per backend with GSR_IMPL naming its Impl, after the backend's private header.
//
// Backend interface (members of GSR_IMPL):
//   beginReadback / endReadback   the pixels of a target's rows on the CPU (and their release)
//   copyTargetTop            the top-left rectangle of one target into another
// and hooks in namespace backend_hook, defined by the backend file before it includes this unit (temporary: free
// functions over the Impl so that this unit needed no new declarations in the backends' private headers, to become
// members of the Impl when the backend state is shared):
//   replaceTargetTexture(s, rt, newH)    the target's texture at a new height, old rows kept, its copy dropped
//   clearCanvasMargins(s, rt, margins)   the inventory canvas margins to half-transparent black; returns the method
//   targetUnusable(s, rt)                the target cannot be written any more (a removed device)
//   replicateTargetRect(s, rt, pixels, x, y, w, h)   native pixels into the target, replicated by the GPU; false if not
//   writeTargetRect(s, rt, pixels, x, y, w, h)       pixels already at the internal scale into the target
//   nativeTargetSource(s, rt), copyTargetBlock(...)  aliased texture blocks copied from the target (native pixels)
//   createDepthTexture(s, d)             the GPU depth buffer, cleared
//   prepareGridCapture(s, rt, W, H), saveGridRect(s, rt, px), restoreGridRect(s), gridRect(s)   item grid capture
//   kCaps.cleanRowRefresh    a row refreshed from memory proves its blocks clean (GSR_RB_CLEAN_SYNC)
//   kCaps.partialUploads     preparePartialPatch, patchTargetBlock (uploads that are not block aligned)
//   kCaps.readbackVerify     verifyRTBlocks
#include <array>
#include <map>

#ifndef GSR_IMPL
#error "define GSR_IMPL (the backend's Impl) and include the backend's private header first"
#endif

namespace gsr {

namespace {
bool cleanSyncEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RB_CLEAN_SYNC");
        return !value || value[0] != '0';
    }();
    return enabled;
}
// GSR_RT_REFRESH_STATS: at exit, how often each target was refreshed from memory, by rectangle size and path.
void noteRefresh(const RT& rt, uint32_t width, uint32_t height, bool gpu, size_t bytes) {
    static const bool enabled = std::getenv("GSR_RT_REFRESH_STATS") != nullptr;
    if (!enabled)
        return;
    struct Table {
        struct Row {
            uint64_t count = 0, bytes = 0;
        };
        std::map<std::array<uint32_t, 5>, Row> rows;
        ~Table() {
            for (const auto& [key, row] : rows)
                std::fprintf(stderr, "[rt-refresh] fbp=%x fbw=%u rect=%ux%u path=%s count=%llu MiB=%.1f\n", key[0],
                             key[1], key[2], key[3], key[4] ? "gpu" : "cpu", (unsigned long long)row.count,
                             row.bytes / 1048576.0);
        }
    };
    static Table table;
    auto& row = table.rows[{rt.fbp, rt.fbw, width, height, gpu ? 1u : 0u}];
    ++row.count;
    row.bytes += bytes;
}
void traceReadback(const char* kind, uint64_t frame, const RT& rt, uint32_t first = 0, uint32_t last = 0) {
    static const bool enabled = std::getenv("GSR_RB_TRACE") != nullptr;
    static unsigned remaining = 4096;
    if (enabled && remaining) {
        --remaining;
        std::fprintf(stderr,
                     "[rb-trace] %s frame=%llu fbp=%x fbw=%u psm=%x size=%ux%u dirty=%u..%u state=%u blocks=%x..%x "
                     "writes=%llu clean=%llu words=%zu\n",
                     kind, static_cast<unsigned long long>(frame), rt.fbp, rt.fbw, rt.psm, rt.w, rt.h, rt.dy0, rt.dy1,
                     rt.wstate, first, last, static_cast<unsigned long long>(rt.writes),
                     static_cast<unsigned long long>(rt.cleanWrites), rt.cleanWords.size());
    }
}
} // namespace

RT* GSR_IMPL::getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH) {
    fbw = std::max(fbw, 1u);
    const uint32_t cls = (psm == PSM_CT16 || psm == PSM_CT16S) ? 1u : 0u;
    const uint32_t pitchBytes = fbw * 64u * (cls ? 2u : 4u);
    const uint32_t base = fbp * 32u * 256u;
    uint32_t maxRows = base < kVramSize ? (kVramSize - base) / pitchBytes : 0;
    maxRows = std::max(maxRows, 64u);
    maxRows = std::min(maxRows, 2048u);
    RT* rt = nullptr;
    for (auto& r : rts)
        if (r->fbp == fbp && r->fbw == fbw && r->cls == cls) {
            rt = r.get();
            break;
        }
    const uint32_t ph = cls ? 64u : 32u;
    needH = std::min(std::max(needH, 1u), maxRows);
    if (!rt) {
        rts.push_back(std::make_unique<RT>());
        rt = rts.back().get();
        rt->fbp = fbp;
        rt->fbw = fbw;
        rt->cls = cls;
        rt->psm = psm;
        rt->w = fbw * 64u;
        allocRT(*rt, roundUp(std::min(std::max(needH, 512u), maxRows), ph));
    } else {
        rt->psm = psm;
        if (needH > rt->h)
            allocRT(*rt, roundUp(std::min(needH, maxRows), ph));
    }
    rt->lastUse = frame;
    return rt;
}

void GSR_IMPL::rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const {
    const uint32_t ph = rt.pageH();
    const uint32_t r0 = y0 / ph, r1 = (y1 + ph - 1) / ph;
    lo = rt.base() + r0 * rt.fbw * 32u;
    hi = rt.base() + r1 * rt.fbw * 32u - 1u;
}

uint32_t GSR_IMPL::packPixel(uint32_t psm, const uint8_t* p) {
    switch (psm) {
        case PSM_CT16:
        case PSM_CT16S:
            return (uint32_t)(p[0] >> 3) | ((uint32_t)(p[1] >> 3) << 5) | ((uint32_t)(p[2] >> 3) << 10) |
                   ((p[3] >= 128) ? 0x8000u : 0u);
        case PSM_CT24:
            return p[0] | (p[1] << 8) | (p[2] << 16);
        default:
            return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

uint32_t GSR_IMPL::unpackPixel(uint32_t psm, uint32_t v) {
    switch (psm) {
        case PSM_CT16:
        case PSM_CT16S:
            return ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) |
                   ((v & 0x8000) ? 0x80000000u : 0u);
        case PSM_CT24:
            return (v & 0xFFFFFF) | 0x80000000u;
        default:
            return v;
    }
}

void GSR_IMPL::syncRT(RT& rt) {
    syncRows(rt, {0, (uint32_t)rt.rowStamp.size()});
}

void GSR_IMPL::flushToMemory(uint32_t first, uint32_t last) {
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        if (hi < first || lo > last)
            continue;
        traceReadback("write", frame, *r, first, last);
        readbackRT(*r);
    }
}

// True when every block of [first, last] that lies in the target's dirty rows was made identical on the GPU and in
// memory by an applied upload and nothing has written to the target since.
bool GSR_IMPL::blocksAreClean(const RT& rt, uint32_t first, uint32_t last) const {
    if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty())
        return false;
    uint32_t lo, hi;
    rowSpan(rt, rt.dy0, std::min(rt.dy1, rt.h), lo, hi);
    for (uint32_t b = std::max(first, lo); b <= std::min(hi, last); ++b)
        if (!((rt.cleanBlocks[b / 64] >> (b % 64)) & 1))
            return false;
    return true;
}

bool GSR_IMPL::readBitsAreClean(const RT& rt, const PartialUpload& requested) const {
    if (rt.cleanWrites != rt.writes || rt.cleanWords.empty())
        return false;
    uint32_t lo, hi;
    rowSpan(rt, rt.dy0, std::min(rt.dy1, rt.h), lo, hi);
    for (const auto& [block, masks] : requested.blocks) {
        if (block < lo || block > hi)
            continue;
        if (!rt.cleanBlocks.empty() && ((rt.cleanBlocks[block / 64] >> (block % 64)) & 1))
            continue;
        for (uint32_t i = 0; i < 64; ++i) {
            if (!masks[i])
                continue;
            const auto found = rt.cleanWords.find(block * 256 + i * 4);
            if (found == rt.cleanWords.end() || (found->second & masks[i]) != masks[i])
                return false;
        }
    }
    return true;
}

// flushToMemory for a reader of memory. The game streams textures through VRAM every frame, a CLUT right before its
// image, over blocks a scratch target occupies; those uploads are applied to the target as well, so asking for exactly
// those blocks must not read the whole target back (a GPU sync) to learn what the CPU just wrote.
void GSR_IMPL::flushForRead(uint32_t first, uint32_t last, const VramRect* rect) {
    if (!readbackSkipEnabled()) {
        flushToMemory(first, last);
        return;
    }
    PartialUpload requested; // the exact bits of the read (kCaps.partialUploads)
    bool prepared = false, usable = false;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        if (hi < first || lo > last)
            continue;
        bool clean = blocksAreClean(*r, first, last);
        const PartialUpload* exact = nullptr;
        if constexpr (kCaps.partialUploads)
            if (!clean && rect && !r->cleanWords.empty()) {
                if (!prepared) {
                    prepared = true;
                    usable = requested.prepare(rect->psm, rect->bp, rect->bw, rect->x, rect->y, rect->w, rect->h);
                    if (usable)
                        requested.advance(uint64_t(rect->w) * rect->h);
                }
                if (usable && readBitsAreClean(*r, requested)) {
                    clean = true;
                    exact = &requested;
                }
            }
        if (clean) {
            ++st.rtReadbacksSkipped;
            if constexpr (kCaps.readbackVerify) {
                static const bool verify = std::getenv("GSR_RB_VERIFY") != nullptr;
                if (verify)
                    verifyRTBlocks(*r, std::max(first, lo), std::min(last, hi), 0, ~0u, ~0u, 0, exact);
            }
            continue;
        }
        traceReadback("read", frame, *r, first, last);
        static const bool traceReader = std::getenv("GSR_RB_DRAW_TRACE") != nullptr;
        static unsigned readersLeft = 64;
        if (traceReader && readersLeft && uint64_t(r->w) * (r->dy1 - r->dy0) <= 16384) {
            --readersLeft;
            if (rect)
                std::fprintf(stderr, "[rb-reader] target=%x psm=%x bp=%x bw=%u xy=%u,%u wh=%u,%u\n", r->fbp, rect->psm,
                             rect->bp, rect->bw, rect->x, rect->y, rect->w, rect->h);
            else
                std::fprintf(stderr, "[rb-reader] target=%x blocks=%x..%x no-rectangle\n", r->fbp, first, last);
        }
        readbackRT(*r);
    }
}

// Image upload into memory a dirty render target occupies. The game stamps 16x16 patches into both frame buffers and
// streams textures over scratch targets every frame; reading the whole target back first is a GPU sync per upload.
// An upload aligned to its format's blocks rewrites whole 256-byte blocks, so instead those blocks are copied into the
// target texture when the upload completes (memory read in the target's format, as the GS would). Blocks the upload
// does not cover stay GPU-owned, so a later readback still yields the right bytes.
bool GSR_IMPL::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                 uint32_t h) {
    uploadPartial = false;
    const PsmGeom g = geomOf(psm);
    if (!w || !h || x % g.blockW || y % g.blockH || w % g.blockW || h % g.blockH) {
        if constexpr (kCaps.partialUploads)
            return beginPartialUpload(psm, bp, bw, x, y, w, h);
        return false;
    }
    uint32_t lo, hi;
    spanBlocks(psm, bp, bw, x, y, w, h, lo, hi);
    bool overlaps = false;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t rlo, rhi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), rlo, rhi);
        if (rhi < lo || rlo > hi)
            continue;
        // Temporal world pass in progress (the texture holds a render rectangle), or a stale target: one readback
        // makes a target the GPU no longer draws into clean, instead of patching it on every later upload.
        if (r->wstate == 1 || frame > r->drawFrame + 1)
            return false;
        overlaps = true;
    }
    if (!overlaps)
        return false; // nothing to read back anyway
    uploadBlocks.assign(kVramBlocks / 64, 0);
    uploadLo = lo;
    uploadHi = hi;
    for (uint32_t by = y; by < y + h; by += g.blockH)
        for (uint32_t bx = x; bx < x + w; bx += g.blockW) {
            const uint32_t b = blockOf(psm, bp, bw, bx, by);
            uploadBlocks[b / 64] |= 1ull << (b % 64);
        }
    // Rows of these targets that memory already leads must reach the GPU before the rows are claimed below.
    for (auto& r : rts)
        if (r->dirty && touchesUpload(*r))
            syncRT(*r);
    uploadActive = true;
    return true;
}

bool GSR_IMPL::uploadCovers(uint32_t block) const {
    return (uploadBlocks[block / 64] >> (block % 64)) & 1;
}

// Pixel rows [y0, y1) of the target whose page rows intersect the upload's block span.
bool GSR_IMPL::uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const {
    const uint32_t ph = rt.pageH(), rowBlocks = rt.fbw * 32u;
    y0 = rt.h;
    y1 = 0;
    for (uint32_t r = 0; r * ph < rt.h; ++r) {
        const uint32_t first = rt.base() + r * rowBlocks, last = first + rowBlocks - 1;
        if (last < uploadLo || first > uploadHi)
            continue;
        y0 = std::min(y0, r * ph);
        y1 = std::max(y1, std::min(rt.h, (r + 1) * ph));
    }
    return y0 < y1;
}

bool GSR_IMPL::touchesUpload(const RT& rt) const {
    const PsmGeom g = geomOf(rt.psm);
    uint32_t y0, y1;
    if (!uploadRows(rt, y0, y1))
        return false;
    for (uint32_t by = y0; by < y1; by += g.blockH)
        for (uint32_t bx = 0; bx < rt.w; bx += g.blockW)
            if (uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by)))
                return true;
    return false;
}

void GSR_IMPL::completeTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool apply,
                                    uint64_t written) {
    if (!uploadActive)
        return;
    if constexpr (kCaps.partialUploads)
        if (uploadPartial) {
            completePartialUpload(apply, written);
            return;
        }
    if (!apply && written)
        return;
    for (auto& r : rts) {
        RT& rt = *r;
        if (!rt.dirty)
            continue; // read back meanwhile: memory is authoritative and syncRT uploads the rows as usual
        const PsmGeom g = geomOf(rt.psm);
        const uint32_t ph = rt.pageH();
        uint32_t y0, y1;
        if (!uploadRows(rt, y0, y1))
            continue;
        bool touched = false;
        // Blocks of consecutive applied uploads accumulate while nothing else wrote to the target in between.
        if (apply && (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty())) {
            rt.cleanBlocks.assign(kVramBlocks / 64, 0);
            rt.cleanWords.clear();
        }
        // The runs of covered blocks and their vertical joins are shared with the WebGPU backend (rt_upload_runs.h).
        touched = rt_upload::uploadedRuns(
            rt, y0, y1, apply, [&](uint32_t block) { return uploadCovers(block); },
            [&](uint32_t bx, uint32_t by, uint32_t, uint32_t, uint32_t end) {
                if (apply)
                    for (uint32_t cx = bx; cx < std::min(end, rt.w); cx += g.blockW) {
                        const uint32_t cb = blockOf(rt.psm, rt.base(), rt.fbw, cx, by);
                        rt.cleanBlocks[cb / 64] |= 1ull << (cb % 64);
                    }
                // The GPU copy is (or will be) current for these rows; never refresh them from memory.
                if (by / ph < rt.rowStamp.size()) {
                    rt.rowStamp[by / ph] = mem.clock();
                    // A row outside the dirty range now holds the uploaded image (it may have held read back draws):
                    // without this, a texture uploaded there was still sampled from the target's mirror of it.
                    if (apply && !rt_source::rowInRange(by / ph, ph, rt.dy0, std::min(rt.dy1, rt.h)))
                        rt.rowFromMemory[by / ph] = 1;
                }
            },
            [&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) { uploadTargetRect(rt, x, y, w, h); });
        if (touched && apply) {
            rt.copyValid = false;
            ++rt.writes;
            rt.cleanWrites = rt.writes;
            st.targetUploads++;
            trimDirtyRows(rt);
        }
    }
    if (apply)
        uploadActive = false;
}

// After an applied upload the GPU rows of every covered block equal memory. Rows of the dirty range whose blocks are
// all covered therefore need no readback any more: take them off both ends of the range, and clear the flag when
// nothing is left. (A texture sampled from the very blocks that were just uploaded used to read the whole target back
// to get data the CPU had just written.)
void GSR_IMPL::trimDirtyRows(RT& rt) {
    if (!rt.dirty || !readbackSkipEnabled())
        return;
    const PsmGeom g = geomOf(rt.psm);
    const auto rowCovered = [&](uint32_t by) {
        for (uint32_t bx = 0; bx < rt.w; bx += g.blockW)
            if (!uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by)))
                return false;
        return true;
    };
    uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    while (y0 < y1) {
        const uint32_t by = y0 / g.blockH * g.blockH;
        if (!rowCovered(by))
            break;
        y0 = by + g.blockH;
    }
    while (y1 > y0) {
        const uint32_t by = (y1 - 1) / g.blockH * g.blockH;
        if (!rowCovered(by))
            break;
        y1 = by;
    }
    if constexpr (kCaps.readbackVerify)
        if (std::getenv("GSR_RB_VERIFY") && (y0 > rt.dy0 || y1 < std::min(rt.dy1, rt.h) || y0 >= y1)) {
            // rows about to be declared clean must equal memory
            const uint32_t top = std::min(y0, std::min(rt.dy1, rt.h));
            verifyRTBlocks(rt, 0, ~0u, rt.dy0, top);
            if (y1 > top)
                verifyRTBlocks(rt, 0, ~0u, y1, std::min(rt.dy1, rt.h));
        }
    // Rows that leave the dirty range here hold the uploaded image, not draws.
    const uint32_t ph = rt.pageH(), oldEnd = std::min(rt.dy1, rt.h);
    for (uint32_t r = rt.dy0 / ph; r < (oldEnd + ph - 1) / ph && r < rt.rowFromMemory.size(); ++r)
        if (y0 >= y1 || (r + 1) * ph <= y0 || r * ph >= y1)
            rt.rowFromMemory[r] = 1;
    if (y0 >= y1) {
        rt.dirty = false;
        rt.seen = mem.clock();
        ++st.rtDirtyCleared;
    } else {
        rt.dy0 = y0;
        rt.dy1 = y1;
    }
}

// A 32-bit texture whose memory overlaps a dirty 32-bit render target at another base (the HUD atlas aliases the
// target the orb liquid is rendered into). Reading the target back is a full GPU sync per frame; both store raw GS
// words, so the target's blocks are copied into the texture on the GPU instead.
RT* GSR_IMPL::aliasedTarget(uint32_t psm, uint32_t mxl, const std::vector<std::pair<uint32_t, uint32_t>>& spans) {
    if (psm != PSM_CT32 || mxl != 0 || (scale > 1 && !nativeReadbackEnabled))
        return nullptr;
    RT* found = nullptr;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        for (const auto& sp : spans) {
            if (hi < sp.first || lo > sp.second)
                continue;
            if (found || r->psm != PSM_CT32 || r->wstate == 1) {
                return nullptr;
            }
            found = r.get();
            break;
        }
    }
    return found;
}

// Render targets are allocated with at least 512 rows, so several of them can cover the same local memory (a 128
// wide shadow map next to older 128 wide targets of other effects). The texture must come from the target that
// really owns it: exact base address first, then a target that was drawn to in the rows of the texture, then the
// most recently used one. (The first match in creation order sampled a stale target at an offset.)
RT* GSR_IMPL::findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox, float& oy) {
    uint32_t cls;
    if (psm == PSM_CT32 || psm == PSM_CT24)
        cls = 0;
    else if (psm == PSM_CT16 || psm == PSM_CT16S)
        cls = 1;
    else
        return nullptr;
    RT* best = nullptr;
    int bestScore = -1;
    float bx = 0, by = 0;
    for (auto& r : rts) {
        if (r->cls != cls || r->fbw != std::max(tbw, 1u))
            continue;
        if (cls == 1 && r->psm != psm)
            continue;
        if (tbp < r->base())
            continue;
        const uint32_t off = tbp - r->base();
        if (off % 32u)
            continue;
        const uint32_t page = off / 32u;
        const uint32_t px = (page % r->fbw) * 64u, py = (page / r->fbw) * r->pageH();
        if (px >= r->w || py >= r->h)
            continue;
        // A right-page origin cannot be a linear SRV subview when the
        // texture crosses the parent row: GS addresses carry to the next
        // page, while SampleLevel clamps at the parent's right edge.
        // Decode through VRAM instead. Keep zero-origin partial-use views
        // (e.g. logical1024 sampling only512) on their existing fast path.
        if (px != 0 && w > r->w - px)
            continue;
        if (!rt_source::targetOwnsTexture(rowsFromMemory(*r, py, h), off != 0, py, h, r->h))
            continue;
        int score = 0;
        if (off == 0)
            score = 1000;
        else if (r->dirty && r->dy0 < py + h && r->dy1 > py)
            score = 500;
        if (score < bestScore || (score == bestScore && r->lastUse <= (best ? best->lastUse : 0)))
            continue;
        best = r.get();
        bestScore = score;
        bx = (float)px;
        by = (float)py;
    }
    if (best) {
        ox = bx;
        oy = by;
    }
    return best;
}

// Page rows of the texture [py, py + h) inside the target (first = total, second = rows whose GPU copy merely mirrors
// memory: outside the dirty range and filled from memory, or about to be because memory is newer).
std::pair<uint32_t, uint32_t> GSR_IMPL::rowsFromMemory(const RT& rt, uint32_t py, uint32_t h) const {
    const uint32_t ph = rt.pageH(), end = std::min(py + h, rt.h);
    const uint32_t dirtyEnd = std::min(rt.dy1, rt.h);
    uint32_t total = 0, fromMemory = 0;
    for (uint32_t r = py / ph; r * ph < end && r < rt.rowStamp.size(); ++r) {
        ++total;
        if (rt.dirty && rt_source::rowInRange(r, ph, rt.dy0, dirtyEnd))
            continue; // drawn since the last readback: the GPU owns it
        const uint32_t lo = rt.base() + r * rt.fbw * 32u;
        if (rt.rowFromMemory[r] || (lo < kVramBlocks && mem.maxStamp(lo, rt.fbw * 32u) > rt.rowStamp[r]))
            ++fromMemory;
    }
    return {total, fromMemory};
}

void GSR_IMPL::flushAll() {
    for (auto& r : rts)
        readbackRT(*r);
}

void GSR_IMPL::readbackRT(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ReadbackRT);
    // A guest screenshot can read the closed world before any HUD draw/display latch triggers its deferred pass.
    // Do not finalize an active world or a late native-water submission that still owns its geometry stream.
    if (pendingTemporalEnd && !engineWorldOpen && !nativeWaterPass && rt.neuralState == 1) {
        pendingTemporalEnd = false;
        for (auto& target : rts)
            finalizeNeuralWorld(*target);
    }
    if (rt.wstate == 1)
        finalizeWorld(rt); // temporal upscaler: the target holds the half-drawn render rectangle
    if (!rt.dirty || rt.dy1 <= rt.dy0) {
        rt.dirty = false;
        return;
    }
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    const uint32_t rows = y1 - y0;
    rt.readbackFrame = frame;
    uint32_t readScale = scale; // target pixels per native pixel in the rows read
    size_t pitch = 0;
    if (const uint8_t* src = beginReadback(rt, y0, y1, readScale, pitch)) {
        CpuProfile::Scope packProfile(cpuProfile, CpuProfile::ReadbackPack);
        const uint32_t half = readScale / 2;
        for (uint32_t y = 0; y < rows; ++y) {
            const uint8_t* row = src + (size_t)(y * readScale + half) * pitch;
            for (uint32_t x = 0; x < rt.w; ++x) {
                // An upload streaming into this target owns its blocks (see beginTargetUpload).
                if (uploadActive && uploadPartial) {
                    const uint32_t address = pixelAddr(rt.psm, rt.base(), rt.fbw, x, y0 + y) & (kVramSize - 1);
                    const uint32_t shift = (address & 3) * 8;
                    const uint32_t pixelMask = rt.psm == PSM_CT32   ? ~0u
                                               : rt.psm == PSM_CT24 ? 0xffffffu
                                                                    : 0xffffu << shift;
                    const uint32_t mask = pixelMask & ~partialUpload.mask(address);
                    const uint32_t gpu = packPixel(rt.psm, row + size_t(x * readScale + half) * 4);
                    uint32_t cpu;
                    std::memcpy(&cpu, mem.data() + (address & ~3u), 4);
                    const uint32_t merged = (cpu & ~mask) | ((gpu << shift) & mask);
                    std::memcpy(mem.data() + (address & ~3u), &merged, 4);
                } else if (!uploadActive || !uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, x, y0 + y)))
                    mem.write(rt.psm, rt.base(), rt.fbw, x, y0 + y,
                              packPixel(rt.psm, row + (size_t)(x * readScale + half) * 4));
            }
        }
        endReadback();
    }
    mem.touchRect(rt.psm, rt.base(), rt.fbw, 0, y0, rt.w, rows);
    const uint32_t ph = rt.pageH();
    for (uint32_t r = y0 / ph; r < (y1 + ph - 1) / ph && r < rt.rowStamp.size(); ++r) {
        rt.rowStamp[r] = mem.clock();
        rt.rowFromMemory[r] = 0; // these rows keep the draws that were just read back
    }
    rt.seen = mem.clock();
    rt.dirty = false;
    st.rtReadbacks++;
}

// Refresh the page rows [rows.first, rows.end) the CPU wrote since the target last had them. Rows outside keep their
// stamp (rt.partialSync), so the stamp scan of a later call still finds them stale.
void GSR_IMPL::syncRows(RT& rt, rt_sync::Rows rows) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::SyncRT);
    const bool whole = rows.first == 0 && rows.end >= rt.rowStamp.size();
    if (rt.seen == mem.clock() && !rt.partialSync)
        return;
    const uint32_t ph = rt.pageH();
    const uint32_t pagesPerRow = rt.fbw;
    for (uint32_t r = rows.first; r < rows.end && r < rt.rowStamp.size(); ++r) {
        const uint32_t lo = rt.base() + r * pagesPerRow * 32u;
        if (lo >= kVramBlocks)
            break;
        if (mem.maxStamp(lo, pagesPerRow * 32u) <= rt.rowStamp[r])
            continue;
        const uint32_t y0 = r * ph, y1 = std::min(y0 + ph, rt.h);
        const uint32_t rows = y1 - y0;
        uploadTargetRect(rt, 0, y0, rt.w, rows);
        rt.rowStamp[r] = mem.clock();
        rt.rowFromMemory[r] = 1;
        rt.copyValid = false;
        // A CPU row refresh establishes equality, unlike a draw. Keep prior
        // known-equal blocks in other rows instead of discarding that proof
        // when a disjoint texture upload refreshes another page row.
        if (kCaps.cleanRowRefresh && rt.psm == PSM_CT32 && cleanSyncEnabled()) {
            if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty()) {
                rt.cleanBlocks.assign(kVramBlocks / 64, 0);
                rt.cleanWords.clear();
            }
            for (uint32_t b = lo; b < std::min(kVramBlocks, lo + pagesPerRow * 32u); ++b)
                rt.cleanBlocks[b / 64] |= 1ull << (b % 64);
            rt.cleanWrites = rt.writes + 1;
        }
        ++rt.writes;
        st.rtUploads++;
    }
    if (whole) {
        rt.seen = mem.clock();
        rt.partialSync = false;
    } else
        rt.partialSync = true;
}

// Local-to-local transfer between two whole PSMCT32 render targets (the game copies its 512x512 frame buffer to
// the other one several times a second). Through memory that is two full readbacks, a per-pixel CPU copy and a later
// re-upload of the destination; here it is one GPU copy, and the destination becomes GPU-owned like after a draw. Only
// taken when the result is provably the same: both ends are exactly the targets, no other target shares either
// memory range (it would need the memory the copy no longer writes), and neither target is mid temporal world pass.
bool GSR_IMPL::copyLocal(uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm,
                         uint32_t dbp, uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    // The decision and the bookkeeping: rt_local_copy.h.
    const auto planned = rt_copy::plan(
        rts, spsm, sbp, sbw, sx, sy, dpsm, dbp, dbw, dx, dy, w, h,
        [&](const RT& t, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) { rowSpan(t, y0, y1, lo, hi); });
    if (!planned)
        return false;
    RT *src = planned.src, *dst = planned.dst;
    const uint32_t slo = planned.sourceFirst, shi = planned.sourceLast;
    syncRT(*src); // rows of the source the CPU wrote since the target was last current
    if (h < dst->h)
        syncRT(*dst); // rows below the copy stay as they are
    copyTargetTop(*src, *dst, w, h);
    rt_copy::finish(mem, *dst, dbp, dbw, w, h, frame);
    ++st.rtGpuCopies;
    if constexpr (kCaps.readbackVerify) {
        static const bool verify = std::getenv("GSR_RB_VERIFY") != nullptr;
        if (verify) {
            // The memory path would have produced the source's memory contents at the destination.
            flushToMemory(slo, shi);
            verifyRTBlocks(*dst, 0, ~0u, 0, h, src->base(), src->fbw);
        }
    }
    (void)slo;
    (void)shi;
    return true;
}

bool GSR_IMPL::beginPartialUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h) {
    if constexpr (kCaps.partialUploads) {
        static const bool enabled = [] {
            const char* value = std::getenv("GSR_PARTIAL_UPLOAD");
            return !value || value[0] != '0';
        }();
        if (!enabled || !readbackSkipEnabled() || !partialUpload.prepare(psm, bp, bw, x, y, w, h))
            return false;
        uploadBlocks.assign(kVramBlocks / 64, 0);
        uploadLo = kVramBlocks;
        uploadHi = 0;
        for (const auto& [block, masks] : partialUpload.blocks) {
            (void)masks;
            uploadBlocks[block / 64] |= 1ull << (block % 64);
            uploadLo = std::min(uploadLo, block);
            uploadHi = std::max(uploadHi, block);
        }
        bool overlaps = false;
        for (const auto& r : rts) {
            if (!r->dirty || !touchesUpload(*r))
                continue;
            if (r->psm != PSM_CT32 || r->wstate == 1 || frame > r->drawFrame + 1)
                return false;
            overlaps = true;
        }
        if (!overlaps || !preparePartialPatch())
            return false;
        for (auto& r : rts)
            if (r->dirty && touchesUpload(*r))
                syncRT(*r);
        uploadPartial = uploadActive = true;
        return true;
    }
    return false;
}

void GSR_IMPL::completePartialUpload(bool apply, uint64_t written) {
    if constexpr (kCaps.partialUploads) {
        partialUpload.advance(written);
        // A progress notification never changes GPU state; only transfer completion
        // (including an abandoned transfer's actual prefix) publishes the patch.
        if (!apply && written)
            return;
        for (auto& r : rts) {
            RT& rt = *r;
            if (!rt.dirty || !touchesUpload(rt))
                continue;
            if (apply && (rt.psm != PSM_CT32 || rt.wstate == 1)) {
                // Another GIF path may change the target while an image streams.
                // Resolve that unusual case through memory, preserving only the
                // actual written prefix, rather than patching a different layout.
                readbackRT(rt);
                std::fill(rt.rowStamp.begin(), rt.rowStamp.end(), 0);
                rt.seen = 0;
                syncRT(rt);
                continue;
            }
            uint32_t y0, y1;
            if (!uploadRows(rt, y0, y1))
                continue;
            const uint32_t ph = rt.pageH();
            if (!apply) {
                for (uint32_t y = y0; y < y1; y += ph)
                    rt.rowStamp[y / ph] = mem.clock();
                continue;
            }
            bool touched = false;
            if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty()) {
                rt.cleanBlocks.assign(kVramBlocks / 64, 0);
                rt.cleanWords.clear();
            }
            for (uint32_t by = y0; by < y1; by += 8)
                for (uint32_t bx = 0; bx < rt.w; bx += 8) {
                    const uint32_t block = blockOf(PSM_CT32, rt.base(), rt.fbw, bx, by);
                    if (!uploadCovers(block))
                        continue;
                    std::array<uint32_t, 128> patch{};
                    bool any = false, full = true;
                    for (uint32_t yy = 0; yy < 8; ++yy)
                        for (uint32_t xx = 0; xx < 8; ++xx) {
                            const uint32_t i = (yy * 8 + xx) * 2;
                            patch[i] = partialUpload.mask(pixelAddr(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy));
                            patch[i + 1] = mem.read(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy);
                            any |= patch[i] != 0;
                            full &= patch[i] == ~0u;
                            if (patch[i])
                                rt.cleanWords[pixelAddr(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy)] |= patch[i];
                        }
                    if (!any)
                        continue;
                    patchTargetBlock(rt, bx, by, patch.data());
                    if (full)
                        rt.cleanBlocks[block / 64] |= 1ull << (block % 64);
                    rt.rowStamp[by / ph] = mem.clock();
                    if (!rt_source::rowInRange(by / ph, ph, rt.dy0, std::min(rt.dy1, rt.h)))
                        rt.rowFromMemory[by / ph] = 1; // as in completeTargetUpload
                    touched = true;
                }
            if (touched) {
                rt.copyValid = false;
                ++rt.writes;
                rt.cleanWrites = rt.writes;
                ++st.targetUploads;
            }
        }
        if (apply) {
            uploadActive = uploadPartial = false;
            partialUpload.clear();
        }
    }
}

// ---- target storage ----

void GSR_IMPL::allocRT(RT& rt, uint32_t newH) {
    backend_hook::replaceTargetTexture(*this, rt, newH);
    rt.copyValid = false;
    ++rt.writes;
    rt.inventoryCanvasFrame = {};
    rt.h = newH;
    rt.rowStamp.resize(newH / rt.pageH(), 0);
    rt.rowFromMemory.resize(rt.rowStamp.size(), 1);
    rt.seen = 0; // force a stamp scan for the new rows
}

void GSR_IMPL::clearInventoryCanvas(RT& rt, bool artwork) {
    const bool displayTarget =
        rt.displaySeenFrame != ~0ull && frame >= rt.displaySeenFrame && frame - rt.displaySeenFrame <= 4;
    auto margins =
        hud::inventoryCanvasMargins(rt.w, std::min(rt.h, rt.displayHeight), scale, hudScale, true, displayTarget);
    if (artwork && hudScale > 0 && hudScale < 1) {
        const unsigned height = std::min(rt.h, 512u) * scale;
        margins.count = 1;
        margins.rect[0] = {0, 0, rt.w * scale, height};
    }
    if (!margins.count || !rt.inventoryCanvasFrame.begin(frame))
        return;
    const char* method = backend_hook::clearCanvasMargins(*this, rt, margins);
    if (artwork)
        drawWideBackdrop(rt, sceneCamera.guiScreen);
    rt.copyValid = false;
    const uint32_t height = std::min(rt.h, rt.displayHeight);
    ++rt.writes;
    if (!rt.dirty) {
        rt.dirty = true;
        rt.dy0 = 0;
        rt.dy1 = height;
    } else {
        rt.dy0 = 0;
        rt.dy1 = std::max(rt.dy1, height);
    }
    if (runtimeFlags().hudCanvasLog && inventoryCanvasLogLines < 32) {
        ++inventoryCanvasLogLines;
        std::fprintf(stderr, "[hud-canvas] f=%llu fbp=%u method=%s rects=%u leftEnd=%u rightStart=%u height=%u\n",
                     (unsigned long long)frame, rt.fbp, method, margins.count, margins.rect[0].right,
                     margins.count > 1 ? margins.rect[1].left : margins.rect[0].left, height * scale);
    }
}

// A rectangle of the target (native pixels) from memory, replicated to the internal scale.
void GSR_IMPL::uploadTargetRect(RT& rt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (backend_hook::targetUnusable(*this, rt))
        return;
    const size_t count = size_t(width) * height;
    if (uploadPixels.size() < count)
        uploadPixels.resize(count);
    if (rt.psm == PSM_CT32)
        mem.readRectCT32(rt.base(), rt.fbw, x, y, width, height, uploadPixels.data());
    else
        for (uint32_t row = 0; row < height; ++row)
            for (uint32_t col = 0; col < width; ++col)
                uploadPixels[size_t(row) * width + col] =
                    unpackPixel(rt.psm, mem.read(rt.psm, rt.base(), rt.fbw, x + col, y + row));
    const uint32_t* pixels = uploadPixels.data();
    const size_t scaledCount = count * scale * scale;
    // Above scale 1 the GPU replicates the pixels; tiny rectangles are not worth a GPU pass (the rule is
    // rt_upload_runs.h). A replication that fails wrote nothing: the CPU path below rewrites the whole rectangle.
    if (rt_upload::gpuReplication(scale, scaledCount) &&
        backend_hook::replicateTargetRect(*this, rt, pixels, x, y, width, height)) {
        cpuProfile.uploaded(count * sizeof(uint32_t));
        st.targetRefreshBytes += count * sizeof(uint32_t);
        noteRefresh(rt, width, height, true, count * sizeof(uint32_t));
        return;
    }
    if (scale > 1) {
        if (scaledUploadPixels.size() < scaledCount)
            scaledUploadPixels.resize(scaledCount);
        expandRenderTargetRows(pixels, scaledUploadPixels.data(), width, height, scale,
                               runtimeFlags().rtUploadSpecialize);
        pixels = scaledUploadPixels.data();
    }
    backend_hook::writeTargetRect(*this, rt, pixels, x, y, width, height);
    cpuProfile.uploaded(scaledCount * sizeof(uint32_t));
    st.targetRefreshBytes += scaledCount * sizeof(uint32_t);
    noteRefresh(rt, width, height, false, scaledCount * sizeof(uint32_t));
}

void GSR_IMPL::copyAliasedBlocks(RT& rt, GpuTexture& texture, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h) {
    syncRT(rt); // rows memory leads must be in the target before its blocks are copied
    const auto source = backend_hook::nativeTargetSource(*this, rt);
    if (!source)
        return;
    // Native position of every target block (32-bit blocks are 8x8 pixels).
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> position;
    for (uint32_t y = 0; y < rt.h; y += 8)
        for (uint32_t x = 0; x < rt.w; x += 8)
            position[blockOf(PSM_CT32, rt.base(), rt.fbw, x, y)] = {x, y};
    for (uint32_t y = 0; y < h; y += 8)
        for (uint32_t x = 0; x < w; x += 8) {
            const auto it = position.find(blockOf(PSM_CT32, tbp, std::max(tbw, 1u), x, y));
            if (it == position.end())
                continue;
            const auto [sx, sy] = it->second;
            backend_hook::copyTargetBlock(*this, rt, source, texture, sx, sy, x, y, w, h);
        }
    st.aliasedTextures++;
}

DepthBuf* GSR_IMPL::getDepth(uint32_t zbp, uint32_t w, uint32_t h) {
    for (auto& d : depths)
        if (d->zbp == zbp && d->w == w && d->h == h) {
            d->lastUse = frame;
            return d.get();
        }
    depths.push_back(std::make_unique<DepthBuf>());
    DepthBuf* d = depths.back().get();
    d->zbp = zbp;
    d->w = w;
    d->h = h;
    backend_hook::createDepthTexture(*this, *d);
    d->lastUse = frame;
    return d;
}

// ---- item grid capture (scene-camera marker 0x437170, gs_backend_api.inl) ----

void GSR_IMPL::gridCaptureStart(RT& rt) {
    GridCapture& g = gridCapture;
    g.rt = &rt;
    uint32_t W = 0, H = 0; // the target's pixels
    if (!backend_hook::prepareGridCapture(*this, rt, W, H)) {
        g.rt = nullptr;
        return;
    }
    // The grid's box (GUI frame units) through the page map the inventory draws use, plus a margin for the icons that
    // stand out of their cells; whole target when the box is unusable.
    const auto map = hud::inventoryMap(rt.w, hudScale, inventoryHudFrame.centred);
    float x0 = map.x(g.box[0]), x1 = map.x(g.box[2]), y0 = g.box[1], y1 = g.box[3];
    constexpr float kMargin = 24.0f;
    if (!(x1 > x0 && y1 > y0) || !std::isfinite(x0 + x1 + y0 + y1)) {
        x0 = y0 = 0;
        x1 = (float)rt.w;
        y1 = (float)rt.h;
    }
    const auto clampPx = [&](float v, uint32_t hi) { return (uint32_t)std::clamp(v * (float)scale, 0.0f, (float)hi); };
    const uint32_t px[4] = {clampPx(x0 - kMargin, W), clampPx(y0 - kMargin, H), clampPx(x1 + kMargin, W),
                            clampPx(y1 + kMargin, H)}; // left top right bottom
    if (px[2] <= px[0] || px[3] <= px[1]) {
        g.rt = nullptr;
        return;
    }
    backend_hook::saveGridRect(*this, rt, px);
    g.rtW = rt.w;
    g.rtH = rt.h;
    g.hudScale = hudScale;
    g.centred = inventoryHudFrame.centred;
}

void GSR_IMPL::gridCaptureEnd() {
    GridCapture& g = gridCapture;
    if (g.open && g.rt && g.save && g.tex) {
        // Keep what the grid drew, then put back what was there before it.
        backend_hook::restoreGridRect(*this);
        g.rt->copyValid = false;
        ++g.rt->writes;
        ++g.serial;
        g.frame = frame;
        if (g.serial <= 3 || g.serial % 1000 == 0) {
            const std::array<uint32_t, 4> px = backend_hook::gridRect(*this);
            std::fprintf(stderr,
                         "[gs-grid-capture] #%llu target %ux%u fbp %x box %.1f,%.1f..%.1f,%.1f -> px %u,%u..%u,%u "
                         "(hud %.3f centred %d)\n",
                         (unsigned long long)g.serial, g.rt->w, g.rt->h, g.rt->fbp, g.box[0], g.box[1], g.box[2],
                         g.box[3], px[0], px[1], px[2], px[3], g.hudScale, g.centred ? 1 : 0);
        }
    } else if (g.open) {
        static unsigned misses = 0;
        if (misses++ < 3)
            std::fprintf(stderr, "[gs-grid-capture] scope ended without a display draw\n");
    }
    g.open = false;
    g.rt = nullptr;
}

} // namespace gsr
