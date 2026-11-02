// Surface downscale at PAL 0x1c0f28 (see drakan_texscale.h).
//
// The guest function (dst = $a0, src = $a1, both texture surface objects) shrinks src into dst by an integer ratio
// r = src.w / dst.w: every output pixel is the average of the 2x2 block at the top left of the r x r source block,
// channel by channel (the masks and shifts come from 0x1be858). Surface fields: +0x18 vtable (+0x98 this adjust,
// +0x9c lock(this, &ptr, mode), +0xa0/+0xa4 unlock), +0x24 width, +0x28 height, +0x2c / +0x30 (divided by the ratios),
// +0x34 pitch, +0x38 bits per pixel, +0x3c flag. 8 bpp keeps the guest code (it picks random pixels of the block).
// 16 / 32 bpp run here on the locked memory: the guest loop costs about 80 recompiled instructions per
// channel and made 256 px truecolor textures load ten times slower than 8 bpp.
//
// The lock calls may block (the surface can be swapped in through the texture cache and its semaphores), so they run on
// the real context, never nested: the hook pushes a frame on the guest stack and tail-calls each guest step with $ra at
// a trampoline (a free slot of the function table); the trampoline continues the sequence by the stage in the frame.
//
// Fidelity to the guest loop (journal 2026-11-02/ootf-native-mips.md): the guest loop is preemptible (a cpTick(32) per
// output pixel and per row), so the native filter works row by row, charges the same simulated cycles and unwinds when
// the scheduler asks to (the row cursor lives in the frame; the trampoline resumes it). A failed source lock still
// unlocks the destination, like the guest. Callers that run guest code nested without preemption (menus, lifecycle,
// combat calls: a `guard < 64` loop that cannot follow a tail-call sequence) get the guest loop.
#include "drakan_texscale.h"

#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

bool ps2xNoPreemptActive() noexcept; // EeScheduler.cpp: this host thread is inside a nested non-preemptible guest call

namespace drakan_texscale {
namespace {
constexpr uint32_t kFnScale = 0x1c0f28u, kFnMasks = 0x1be858u;
constexpr uint32_t kFrame = 0x100u;
// frame fields (guest memory at the new $sp)
constexpr uint32_t fStage = 0x00, fRa = 0x04, fDst = 0x08, fSrc = 0x0c, fBpp = 0x10, fRatio = 0x14, fResult = 0x18,
                   fSp = 0x1c, fRow = 0x20, fOk = 0x24,
                   fBuf = 0x40; // buffer: shifts +8+8k, masks +0x28+4k, locked pointers +0x40 / +0x44
constexpr uint32_t kOutPtr = fBuf + 0x40, kInPtr = fBuf + 0x44;

PS2Runtime::RecompiledFunction g_orig = nullptr;
bool g_log = false, g_log2 = false, g_native = false, g_verify = false;
uint32_t g_tramp = 0,
         g_bppMask = 5; // DRAKAN_TEXSCALE_BPP: bit 0 = 16 bpp, 1 = 24, 2 = 32; 24 bpp stays on the guest loop
uint64_t g_calls = 0, g_nativeCalls = 0, g_nestedCalls = 0, g_verifyFails = 0, g_verified = 0;
// simulated EE cycles per output pixel: the guest loop's back-edge charge (cpTick(32)); DRAKAN_TEXSCALE_CHARGE=<n>
uint32_t g_cyclesPerPixel = 32;
uint32_t g_delayUs = 0; // DRAKAN_TEXSCALE_DELAY_US: busy wait per native call (test aid: is a failure speed dependent?)
double g_seconds = 0;

// Debug (DRAKAN_TEXSCALE_HEAPCHECK=1): the guest allocator at 0x1e18d8 keeps its free list at [a0 + 0x34] (node: +4
// size, +12 next). Every allocation first walks the list; a cycle or a node outside RAM is reported with the last
// native destinations (a stray filter write into a freed block would corrupt exactly such a node).
constexpr uint32_t kFnAlloc = 0x1e18d8u;
PS2Runtime::RecompiledFunction g_origAlloc = nullptr;
bool g_heapCheck = false;
struct Recent {
    uint32_t dbase, dlen, sbase, slen;
    uint64_t seq;
};
constexpr size_t kRing = 8192;
Recent g_ring[kRing];
uint64_t g_ringN = 0;

constexpr uint32_t kRamMask = 0x1fffffffu;

bool inRam(uint32_t a, uint32_t n) {
    a &= kRamMask;
    return a >= 0x100000u && static_cast<uint64_t>(a) + n <= PS2_RAM_SIZE;
}
uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, rdram + (a & kRamMask), 4);
    return v;
}
void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    std::memcpy(rdram + (a & kRamMask), &v, 4);
}

// Average of the 2x2 block per channel (the guest's masks / shifts), for one row of `Word` bit pixels.
template <typename Word>
void box(uint8_t* dstRow, const uint8_t* s0, const uint8_t* s1, uint32_t dstW, uint32_t strideBytes,
         const uint32_t* masks, const uint32_t* shifts) {
    for (uint32_t x = 0; x < dstW; ++x) {
        Word a, b, cc, d;
        std::memcpy(&a, s0, sizeof(Word));
        std::memcpy(&b, s0 + sizeof(Word), sizeof(Word));
        std::memcpy(&cc, s1, sizeof(Word));
        std::memcpy(&d, s1 + sizeof(Word), sizeof(Word));
        uint32_t out = 0;
        for (int k = 0; k < 4; ++k) {
            const uint32_t m = masks[k], sh = shifts[k] & 31u;
            const uint32_t sum = ((a & m) >> sh) + ((b & m) >> sh) + ((cc & m) >> sh) + ((d & m) >> sh);
            out |= ((sum + 2u) >> 2) << sh;
        }
        const Word w = static_cast<Word>(out);
        std::memcpy(dstRow + x * sizeof(Word), &w, sizeof(Word));
        s0 += strideBytes;
        s1 += strideBytes;
    }
}

// Geometry of one locked pair, read from the surfaces and the frame.
struct Geometry {
    uint32_t dw, dh, sw, sh, dpitch, spitch, dbase, sbase, bytes, bpp, rx;
    uint32_t shifts[4], masks[4];
};

bool geometry(const uint8_t* rdram, uint32_t fp, Geometry& g) {
    const uint32_t dst = rd32(rdram, fp + fDst), src = rd32(rdram, fp + fSrc);
    g.bpp = rd32(rdram, fp + fBpp);
    g.rx = rd32(rdram, fp + fRatio);
    g.dw = rd32(rdram, dst + 0x24);
    g.dh = rd32(rdram, dst + 0x28);
    g.sw = rd32(rdram, src + 0x24);
    g.sh = rd32(rdram, src + 0x28);
    g.spitch = rd32(rdram, src + 0x34);
    g.dpitch = rd32(rdram, dst + 0x34);
    g.dbase = rd32(rdram, fp + kOutPtr);
    g.sbase = rd32(rdram, fp + kInPtr);
    g.bytes = g.bpp >> 3;
    for (int k = 0; k < 4; ++k) {
        g.shifts[k] = rd32(rdram, fp + fBuf + 0x08 + 0x08 * k);
        g.masks[k] = rd32(rdram, fp + fBuf + 0x28 + 4 * k);
    }
    if (g_log2)
        std::fprintf(stderr, "[texscale] locked dst %x src %x\n", g.dbase, g.sbase);
    const uint64_t d0 = g.dbase & kRamMask, s0 = g.sbase & kRamMask;
    const bool overlap =
        d0 < s0 + static_cast<uint64_t>(g.spitch) * g.sh && s0 < d0 + static_cast<uint64_t>(g.dpitch) * g.dh;
    if (!inRam(g.dbase, g.dpitch * g.dh) || !inRam(g.sbase, g.spitch * g.sh) || g.spitch < g.sw * g.bytes ||
        g.dpitch < g.dw * g.bytes || overlap) {
        static int refused = 0;
        if (refused++ < 20)
            std::fprintf(stderr, "[texscale] refused: bpp %u dst %x (%ux%u pitch %u) src %x (%ux%u pitch %u)%s\n",
                         g.bpp, g.dbase, g.dw, g.dh, g.dpitch, g.sbase, g.sw, g.sh, g.spitch,
                         overlap ? " OVERLAP" : "");
        return false;
    }
    return true;
}

// Filter output row y (the guest's per-row loop).
void filterRow(uint8_t* rdram, const Geometry& g, uint32_t y) {
    uint8_t* drow = rdram + (g.dbase & kRamMask) + static_cast<uint64_t>(g.dpitch) * y;
    const uint8_t* s0 = rdram + (g.sbase & kRamMask) + static_cast<uint64_t>(g.spitch) * y * g.rx;
    const uint8_t* s1 = s0 + (g.bytes == 3 ? g.spitch : (g.spitch & ~(g.bytes - 1u)));
    const uint32_t stride = g.rx * g.bytes;
    if (g.bpp == 32) {
        box<uint32_t>(drow, s0, s1, g.dw, stride, g.masks, g.shifts);
    } else if (g.bpp == 16) {
        box<uint16_t>(drow, s0, s1, g.dw, stride, g.masks, g.shifts);
    } else {
        for (uint32_t x = 0; x < g.dw; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                const uint32_t sum = s0[ch] + s0[ch + 3] + s1[ch] + s1[ch + 3];
                drow[x * 3 + ch] = static_cast<uint8_t>((sum + 2u) >> 2);
            }
            s0 += stride;
            s1 += stride;
        }
    }
}

// Verify mode (DRAKAN_TEXSCALE_VERIFY=1): guard bytes around the destination, a snapshot of the source, and an
// independent per-channel reference of the whole result (a direct transliteration of the guest loop), compared after
// the last row. Failures print `[texscale-verify]`.
struct VerifyState {
    std::vector<uint8_t> before, after, srcSnap;
    uint32_t dbase = 0, sbase = 0, dlen = 0, slen = 0;
};
std::mutex g_verifyM;
std::unordered_map<uint32_t, VerifyState> g_verifyState; // by frame pointer

constexpr uint32_t kGuard = 64;

void verifyBegin(uint8_t* rdram, uint32_t fp, const Geometry& g) {
    VerifyState v;
    v.dbase = g.dbase & kRamMask;
    v.sbase = g.sbase & kRamMask;
    v.dlen = g.dpitch * g.dh;
    v.slen = g.spitch * g.sh;
    const uint32_t lo = v.dbase >= 0x100000u + kGuard ? v.dbase - kGuard : v.dbase;
    v.before.assign(rdram + lo, rdram + v.dbase);
    if (static_cast<uint64_t>(v.dbase) + v.dlen + kGuard <= PS2_RAM_SIZE)
        v.after.assign(rdram + v.dbase + v.dlen, rdram + v.dbase + v.dlen + kGuard);
    v.srcSnap.assign(rdram + v.sbase, rdram + v.sbase + v.slen);
    std::lock_guard<std::mutex> l(g_verifyM);
    g_verifyState[fp] = std::move(v);
}

void verifyEnd(uint8_t* rdram, uint32_t fp, const Geometry& g) {
    VerifyState v;
    {
        std::lock_guard<std::mutex> l(g_verifyM);
        auto it = g_verifyState.find(fp);
        if (it == g_verifyState.end())
            return;
        v = std::move(it->second);
        g_verifyState.erase(it);
    }
    if ((++g_verified & (g_verified - 1)) == 0)
        std::fprintf(stderr, "[texscale-verify] %llu native results checked, %llu differences\n",
                     static_cast<unsigned long long>(g_verified), static_cast<unsigned long long>(g_verifyFails));
    auto fail = [&](const char* what, uint32_t at) {
        if (g_verifyFails++ < 30)
            std::fprintf(
                stderr, "[texscale-verify] %s at %x: bpp %u dst %x (%ux%u pitch %u) src %x (%ux%u pitch %u) ratio %u\n",
                what, at, g.bpp, g.dbase, g.dw, g.dh, g.dpitch, g.sbase, g.sw, g.sh, g.spitch, g.rx);
    };
    const uint32_t lo = v.dbase - static_cast<uint32_t>(v.before.size());
    if (!v.before.empty() && std::memcmp(rdram + lo, v.before.data(), v.before.size()) != 0)
        fail("bytes before the destination changed", lo);
    if (!v.after.empty() && std::memcmp(rdram + v.dbase + v.dlen, v.after.data(), v.after.size()) != 0)
        fail("bytes after the destination changed", v.dbase + v.dlen);
    if (std::memcmp(rdram + v.sbase, v.srcSnap.data(), v.slen) != 0)
        fail("source changed", v.sbase);
    // reference: the guest's 2x2 average, one channel at a time, reading the source as it was
    const uint8_t* S = v.srcSnap.data();
    const uint32_t rowEven = g.bytes == 3 ? g.spitch : (g.spitch & ~(g.bytes - 1u));
    for (uint32_t y = 0; y < g.dh && (g.bpp == 16 || g.bpp == 32); ++y) {
        for (uint32_t x = 0; x < g.dw; ++x) {
            const uint64_t o0 = static_cast<uint64_t>(g.spitch) * y * g.rx + static_cast<uint64_t>(x) * g.rx * g.bytes;
            const uint64_t o1 = o0 + rowEven;
            if (o1 + 2 * g.bytes > v.slen) // the rx = 1 edge reads past the source: nothing to compare
                continue;
            auto px = [&](uint64_t o) {
                uint32_t w = 0;
                std::memcpy(&w, S + o, g.bytes);
                return w;
            };
            const uint32_t p[4] = {px(o0), px(o0 + g.bytes), px(o1), px(o1 + g.bytes)};
            uint32_t want = 0;
            for (int k = 0; k < 4; ++k) {
                uint32_t sum = 0;
                for (int i = 0; i < 4; ++i)
                    sum += (p[i] & g.masks[k]) >> (g.shifts[k] & 31u);
                want |= ((sum + 2u) >> 2) << (g.shifts[k] & 31u);
            }
            if (g.bpp == 16)
                want &= 0xffffu;
            uint32_t got = 0;
            const uint64_t dOff = static_cast<uint64_t>(g.dpitch) * y + static_cast<uint64_t>(x) * g.bytes;
            std::memcpy(&got, rdram + v.dbase + dOff, g.bytes);
            if (got != want) {
                fail("pixel differs", v.dbase + static_cast<uint32_t>(dOff));
                return;
            }
        }
    }
}

// Tail-call a guest function on the real context; it returns into the trampoline.
void callStep(R5900Context* c, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2) {
    SET_GPR_U32(c, 4, a0);
    SET_GPR_U32(c, 5, a1);
    SET_GPR_U32(c, 6, a2);
    SET_GPR_U32(c, 31, g_tramp);
    c->pc = fn;
    c->in_delay_slot = false;
}

// vtable call fn(obj + adjust, a1, a2) where fn = vt[slot], adjust = vt[slot - 4]; false when the table is unreadable.
bool callVirtual(uint8_t* rdram, R5900Context* c, uint32_t obj, uint32_t slot, uint32_t a1, uint32_t a2) {
    const uint32_t vt = rd32(rdram, obj + 0x18);
    if (!inRam(vt, 0xa8))
        return false;
    const int32_t adjust = static_cast<int16_t>(rd32(rdram, vt + slot - 4) & 0xffffu);
    callStep(c, rd32(rdram, vt + slot), obj + static_cast<uint32_t>(adjust), a1, a2);
    return true;
}

void finish(uint8_t* rdram, R5900Context* c, uint32_t fp) {
    const uint32_t ra = rd32(rdram, fp + fRa);
    SET_GPR_U32(c, 2, rd32(rdram, fp + fResult));
    SET_GPR_U32(c, 29, rd32(rdram, fp + fSp));
    SET_GPR_U32(c, 31, ra);
    c->pc = ra;
    c->in_delay_slot = false;
}

enum Stage : uint32_t { Masks = 1, LockDst, LockSrc, Rows, UnlockSrc, UnlockDst };

// Filter the remaining rows. Returns true when all rows are done; false when the scheduler asked to unwind (the context
// then resumes at the trampoline with the row cursor in the frame).
bool runRows(uint8_t* rdram, R5900Context* c, PS2Runtime* rt, uint32_t fp) {
    Geometry g;
    if (rd32(rdram, fp + fOk) == 0 || !geometry(rdram, fp, g))
        return true;
    const uint32_t rowCost = g_cyclesPerPixel * (g.dw + 1u);
    for (uint32_t y = rd32(rdram, fp + fRow); y < g.dh; ++y) {
        filterRow(rdram, g, y);
        if (rowCost && rt->cpTick(rowCost) && y + 1 < g.dh) { // the scheduler wants this thread off the CPU
            wr32(rdram, fp + fRow, y + 1);
            c->pc = g_tramp;
            c->in_delay_slot = false;
            return false;
        }
    }
    if (g_verify)
        verifyEnd(rdram, fp, g);
    return true;
}

void step(uint8_t* rdram, R5900Context* c, PS2Runtime* rt, uint32_t fp, uint32_t stage) {
    const uint32_t dst = rd32(rdram, fp + fDst), src = rd32(rdram, fp + fSrc);
    const uint32_t v0 = getRegU32(c, 2);
    auto go = [&](Stage next, bool ok) {
        if (ok) {
            wr32(rdram, fp + fStage, next);
        } else { // unreadable vtable: nothing more can be done safely, report failure
            wr32(rdram, fp + fResult, 0);
            finish(rdram, c, fp);
        }
    };
    switch (stage) {
        case Masks: // the masks are in the buffer; lock dst for writing
            go(LockDst, callVirtual(rdram, c, dst, 0x9c, fp + kOutPtr, 2));
            break;
        case LockDst:
            if (!v0) {
                wr32(rdram, fp + fResult, 0); // the guest returns 0 and does not unlock
                finish(rdram, c, fp);
                break;
            }
            go(LockSrc, callVirtual(rdram, c, src, 0x9c, fp + kInPtr, 1));
            break;
        case LockSrc: {
            if (!v0) { // the guest still unlocks the destination (and returns 0)
                wr32(rdram, fp + fResult, 0);
                go(UnlockDst, callVirtual(rdram, c, dst, 0xa4, 0, 0));
                break;
            }
            Geometry g;
            const bool ok = geometry(rdram, fp, g);
            wr32(rdram, fp + fOk, ok ? 1 : 0);
            wr32(rdram, fp + fRow, 0);
            if (ok) {
                g_ring[g_ringN % kRing] = {g.dbase & kRamMask, g.dpitch * g.dh, g.sbase & kRamMask, g.spitch * g.sh,
                                           g_ringN};
                ++g_ringN;
            }
            if (ok && g_verify)
                verifyBegin(rdram, fp, g);
            wr32(rdram, fp + fStage, Rows);
        }
            [[fallthrough]];
        case Rows: {
            if (!runRows(rdram, c, rt, fp))
                break; // unwound: the trampoline resumes here
            const uint32_t rx = rd32(rdram, fp + fRatio);
            wr32(rdram, dst + 0x2c, rd32(rdram, src + 0x2c) / rx);
            wr32(rdram, dst + 0x30,
                 rd32(rdram, src + 0x30) / rx); // the guest divides by the vertical ratio (== rx here)
            wr32(rdram, fp + fResult, rd32(rdram, fp + fOk) ? 1 : 0);
            go(UnlockSrc, callVirtual(rdram, c, src, 0xa4, 0, 0));
            break;
        }
        case UnlockSrc:
            go(UnlockDst, callVirtual(rdram, c, dst, 0xa4, 0, 0));
            break;
        default: // UnlockDst
            ++g_nativeCalls;
            if (g_delayUs) {
                const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(g_delayUs);
                while (std::chrono::steady_clock::now() < until) {
                }
            }
            finish(rdram, c, fp);
            break;
    }
}

void trampoline(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t fp = getRegU32(c, 29);
    // Masks stage: entered after 0x1be858 returned; the other stages after their guest step (Rows: after an unwind).
    step(rdram, c, rt, fp, rd32(rdram, fp + fStage));
}

// true when the sequence started (the context now runs the first guest step), false to run the guest code.
bool begin(uint8_t* rdram, R5900Context* c) {
    const uint32_t dst = getRegU32(c, 4), src = getRegU32(c, 5), sp = getRegU32(c, 29);
    if (!g_tramp || !inRam(dst, 0x40) || !inRam(src, 0x40) || !inRam(sp, 0x400))
        return false;
    const uint32_t bpp = rd32(rdram, dst + 0x38);
    if ((bpp != 16 && bpp != 24 && bpp != 32) || !(g_bppMask & (bpp == 16 ? 1u : bpp == 24 ? 2u : 4u)))
        return false;
    const uint32_t dw = rd32(rdram, dst + 0x24), dh = rd32(rdram, dst + 0x28);
    const uint32_t sw = rd32(rdram, src + 0x24), sh = rd32(rdram, src + 0x28);
    if (!dw || !dh || sw < dw || sh < dh || rd32(rdram, src + 0x38) != bpp)
        return false;
    const uint32_t rx = sw / dw, ry = sh / dh;
    if (rx != ry || !(rx == 1 || rx == 2 || rx == 4 || rx == 8 || rx == 16))
        return false;
    // Nested non-preemptible callers (a `guard < 64` loop on a temporary context) cannot follow the tail-call sequence:
    // it would leave them with $sp at the frame and the locks held. They get the guest loop.
    if (ps2xNoPreemptActive()) {
        if ((++g_nestedCalls & (g_nestedCalls - 1)) == 0)
            std::fprintf(stderr, "[texscale] %llu calls ran the guest loop inside a nested non-preemptible call\n",
                         static_cast<unsigned long long>(g_nestedCalls));
        return false;
    }
    const uint32_t fp = (sp - kFrame) & ~0xfu;
    wr32(rdram, fp + fStage, Masks);
    wr32(rdram, fp + fRa, getRegU32(c, 31));
    wr32(rdram, fp + fDst, dst);
    wr32(rdram, fp + fSrc, src);
    wr32(rdram, fp + fBpp, bpp);
    wr32(rdram, fp + fRatio, rx);
    wr32(rdram, fp + fResult, 0);
    wr32(rdram, fp + fSp, sp);
    wr32(rdram, fp + fRow, 0);
    wr32(rdram, fp + fOk, 0);
    wr32(rdram, fp + kOutPtr, 0);
    wr32(rdram, fp + kInPtr, 0);
    SET_GPR_U32(c, 29, fp);
    // 0x1be858(dst, buffer): the four channels' shifts and masks
    callStep(c, drakan_region::guestAddress(kFnMasks), dst, fp + fBuf, 0);
    return true;
}

void reportBadNode(const uint8_t* rdram, uint32_t owner, uint32_t node, uint32_t prev, int steps, const char* why,
                   uint32_t ra) {
    static int reported = 0;
    if (reported++ >= 6)
        return;
    std::fprintf(stderr, "[texscale-heap] %s: allocator %x node %x (previous %x, step %d) ra=%x, native calls %llu\n",
                 why, owner, node, prev, steps, ra, static_cast<unsigned long long>(g_ringN));
    if (inRam(node, 16))
        std::fprintf(stderr, "[texscale-heap]   node words %08x %08x %08x %08x\n", rd32(rdram, node),
                     rd32(rdram, node + 4), rd32(rdram, node + 8), rd32(rdram, node + 12));
    const uint32_t lo = (node & kRamMask);
    const uint64_t n = std::min<uint64_t>(g_ringN, kRing);
    for (uint64_t i = 0; i < n; ++i) {
        const Recent& r = g_ring[(g_ringN - 1 - i) % kRing];
        if (lo + 64 >= r.dbase && lo < r.dbase + r.dlen + 64) {
            std::fprintf(stderr,
                         "[texscale-heap]   node lies in the destination of native call #%llu (%llu ago): dst %x+%x\n",
                         static_cast<unsigned long long>(r.seq), static_cast<unsigned long long>(i), r.dbase, r.dlen);
            return;
        }
    }
    std::fprintf(stderr, "[texscale-heap]   node is not near any of the last %llu native destinations\n",
                 static_cast<unsigned long long>(n));
}

void checkHeap(const uint8_t* rdram, uint32_t owner, uint32_t ra) {
    if (!inRam(owner, 0x60))
        return;
    uint32_t node = rd32(rdram, owner + 0x34), prev = owner + 0x34;
    uint32_t slow = node;
    for (int steps = 0; node; ++steps) {
        if (!inRam(node, 16) || (node & 3u)) {
            reportBadNode(rdram, owner, node, prev, steps, "free-list node outside RAM or unaligned", ra);
            return;
        }
        prev = node;
        node = rd32(rdram, node + 12);
        if ((steps & 1) && node) { // Floyd: the slow pointer advances every second step
            slow = rd32(rdram, slow + 12);
            if (slow == node) {
                reportBadNode(rdram, owner, node, prev, steps, "free-list cycle", ra);
                return;
            }
        }
        if (steps > 2000000) {
            reportBadNode(rdram, owner, node, prev, steps, "free list longer than 2M nodes", ra);
            return;
        }
    }
}

void allocHook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == drakan_region::guestAddress(kFnAlloc))
        checkHeap(rdram, getRegU32(c, 4), getRegU32(c, 31));
    g_origAlloc(rdram, c, rt);
}

void hook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != drakan_region::guestAddress(kFnScale)) { // resumed inside the original (a yielded call)
        g_orig(rdram, c, rt);
        return;
    }
    ++g_calls;
    if (g_native && begin(rdram, c))
        return;
    if (!g_log) {
        g_orig(rdram, c, rt);
        return;
    }
    const uint32_t dst = getRegU32(c, 4), src = getRegU32(c, 5);
    const auto t0 = std::chrono::steady_clock::now();
    g_orig(rdram, c, rt);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    g_seconds += s;
    if (inRam(dst, 0x40) && inRam(src, 0x40))
        std::fprintf(stderr,
                     "[texscale] #%llu guest src %ux%u bpp %u -> dst %ux%u bpp %u  %.2f ms (guest total %.2f s, %llu "
                     "native)\n",
                     static_cast<unsigned long long>(g_calls), rd32(rdram, src + 0x24), rd32(rdram, src + 0x28),
                     rd32(rdram, src + 0x38), rd32(rdram, dst + 0x24), rd32(rdram, dst + 0x28), rd32(rdram, dst + 0x38),
                     s * 1000.0, g_seconds, static_cast<unsigned long long>(g_nativeCalls));
}
} // namespace

void install(PS2Runtime& runtime) {
    const char* mode = std::getenv("DRAKAN_TEXSCALE");
    const char* log = std::getenv("DRAKAN_TEXSCALE_LOG");
    g_log = log != nullptr;
    g_log2 = log && std::atoi(log) >= 2;
    g_native = !(mode && mode[0] == '0'); // on by default; DRAKAN_TEXSCALE=0 runs the guest loop (A/B tests)
    if (const char* v = std::getenv("DRAKAN_TEXSCALE_VERIFY"))
        g_verify = std::atoi(v) != 0;
    if (const char* ch = std::getenv("DRAKAN_TEXSCALE_CHARGE"))
        g_cyclesPerPixel = static_cast<uint32_t>(std::atoi(ch));
    if (const char* d = std::getenv("DRAKAN_TEXSCALE_DELAY_US"))
        g_delayUs = static_cast<uint32_t>(std::atoi(d));
    if (const char* m = std::getenv("DRAKAN_TEXSCALE_BPP"))
        g_bppMask = static_cast<uint32_t>(std::atoi(m));
    g_heapCheck = std::getenv("DRAKAN_TEXSCALE_HEAPCHECK") != nullptr;
    if (g_heapCheck && drakan_region::supports(drakan_region::Feature::DualWield)) {
        const uint32_t al = drakan_region::guestAddress(kFnAlloc);
        if (runtime.hasFunction(al)) {
            g_origAlloc = runtime.lookupFunction(al);
            runtime.replaceFunction(al, &allocHook);
        }
    }
    if (!g_native && !g_log)
        return;
    if (!drakan_region::supports(drakan_region::Feature::DualWield)) // PAL profile
        return;
    const uint32_t a = drakan_region::guestAddress(kFnScale);
    if (!runtime.hasFunction(a))
        return;
    for (uint32_t t = 0x100000u; t < 0x100100u && !g_tramp; t += 4)
        if (!runtime.hasFunction(t) && runtime.registerFunction(t, &trampoline))
            g_tramp = t;
    g_orig = runtime.lookupFunction(a);
    runtime.replaceFunction(a, &hook);
}
} // namespace drakan_texscale
