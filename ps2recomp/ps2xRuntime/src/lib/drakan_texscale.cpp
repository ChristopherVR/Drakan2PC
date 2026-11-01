// Surface downscale at PAL 0x1c0f28 (see drakan_texscale.h).
//
// The guest function (dst = $a0, src = $a1, both texture surface objects) shrinks src into dst by an integer ratio
// r = src.w / dst.w: every output pixel is the average of the 2x2 block at the top left of the r x r source block,
// channel by channel (the masks and shifts come from 0x1be858). Surface fields: +0x18 vtable (+0x98 this adjust,
// +0x9c lock(this, &ptr, mode), +0xa0/+0xa4 unlock), +0x24 width, +0x28 height, +0x2c / +0x30 (divided by the ratios),
// +0x34 pitch, +0x38 bits per pixel, +0x3c flag. 8 bpp keeps the guest code (it picks random pixels of the block).
// 16 / 24 / 32 bpp run here on the locked memory: the guest loop costs about 80 recompiled instructions per
// channel and made 256 px truecolor textures load ten times slower than 8 bpp.
//
// The lock calls may block (the surface can be swapped in through the texture cache and its semaphores), so they run on
// the real context, never nested: the hook pushes a frame on the guest stack and tail-calls each guest step with $ra at
// a trampoline (a free slot of the function table); the trampoline continues the sequence by the stage in the frame.
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

namespace drakan_texscale {
namespace {
constexpr uint32_t kFnScale = 0x1c0f28u, kFnMasks = 0x1be858u;
constexpr uint32_t kFrame = 0x100u;
// frame fields (guest memory at the new $sp)
constexpr uint32_t fStage = 0x00, fRa = 0x04, fDst = 0x08, fSrc = 0x0c, fBpp = 0x10, fRatio = 0x14, fResult = 0x18,
                   fSp = 0x1c, fBuf = 0x40; // buffer: shifts +8+8k, masks +0x28+4k, locked pointers +0x40 / +0x44
constexpr uint32_t kOutPtr = fBuf + 0x40, kInPtr = fBuf + 0x44;

PS2Runtime::RecompiledFunction g_orig = nullptr;
bool g_log = false, g_log2 = false, g_native = false;
uint32_t g_tramp = 0, g_bppMask = 5; // DRAKAN_TEXSCALE_BPP: bit 0 = 16 bpp, 1 = 24, 2 = 32; 24 bpp stays on the guest
                                     // loop (Islands Night corrupted the heap with it, cause open)
uint64_t g_calls = 0, g_nativeCalls = 0;
bool g_chargeCycles = true; // DRAKAN_TEXSCALE_CHARGE=0 turns the cycle charge off (test aid)
uint32_t g_delayUs = 0; // DRAKAN_TEXSCALE_DELAY_US: busy wait per native call (test aid: is a failure speed dependent?)
double g_seconds = 0;

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

// Filter the locked surfaces. false: memory unreachable (nothing written).
bool filter(uint8_t* rdram, uint32_t dst, uint32_t src, uint32_t bpp, uint32_t rx, uint32_t fp) {
    const uint32_t dw = rd32(rdram, dst + 0x24), dh = rd32(rdram, dst + 0x28), sh = rd32(rdram, src + 0x28);
    const uint32_t sw = rd32(rdram, src + 0x24);
    const uint32_t spitch = rd32(rdram, src + 0x34), dpitch = rd32(rdram, dst + 0x34);
    const uint32_t dbase = rd32(rdram, fp + kOutPtr), sbase = rd32(rdram, fp + kInPtr);
    const uint32_t bytes = bpp >> 3;
    if (g_log2)
        std::fprintf(stderr, "[texscale] locked dst %x src %x\n", dbase, sbase);
    if (!inRam(dbase, dpitch * dh) || !inRam(sbase, spitch * sh) || spitch < sw * bytes || dpitch < dw * bytes) {
        static int refused = 0;
        if (refused++ < 20)
            std::fprintf(stderr, "[texscale] refused: bpp %u dst %x (%ux%u pitch %u) src %x (%ux%u pitch %u)\n", bpp,
                         dbase, dw, dh, dpitch, sbase, sw, sh, spitch);
        return false;
    }
    uint32_t shifts[4], masks[4];
    for (int k = 0; k < 4; ++k) {
        shifts[k] = rd32(rdram, fp + fBuf + 0x08 + 0x08 * k);
        masks[k] = rd32(rdram, fp + fBuf + 0x28 + 4 * k);
    }
    for (uint32_t y = 0; y < dh; ++y) {
        uint8_t* drow = rdram + (dbase & kRamMask) + static_cast<uint64_t>(dpitch) * y;
        const uint8_t* s0 = rdram + (sbase & kRamMask) + static_cast<uint64_t>(spitch) * y * rx;
        const uint8_t* s1 = s0 + (bytes == 3 ? spitch : (spitch & ~(bytes - 1u)));
        const uint32_t stride = rx * bytes;
        if (bpp == 32) {
            box<uint32_t>(drow, s0, s1, dw, stride, masks, shifts);
        } else if (bpp == 16) {
            box<uint16_t>(drow, s0, s1, dw, stride, masks, shifts);
        } else {
            for (uint32_t x = 0; x < dw; ++x) {
                for (int ch = 0; ch < 3; ++ch) {
                    const uint32_t sum = s0[ch] + s0[ch + 3] + s1[ch] + s1[ch + 3];
                    drow[x * 3 + ch] = static_cast<uint8_t>((sum + 2u) >> 2);
                }
                s0 += stride;
                s1 += stride;
            }
        }
    }
    return true;
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

enum Stage : uint32_t { Masks = 1, LockDst, LockSrc, UnlockSrc, UnlockDst };

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
            if (!v0) {
                wr32(rdram, fp + fResult, 0);
                finish(rdram, c, fp);
                break;
            }
            const uint32_t rx = rd32(rdram, fp + fRatio), bpp = rd32(rdram, fp + fBpp);
            const bool ok = filter(rdram, dst, src, bpp, rx, fp);
            wr32(rdram, dst + 0x2c, rd32(rdram, src + 0x2c) / rx);
            wr32(rdram, dst + 0x30,
                 rd32(rdram, src + 0x30) / rx); // the guest divides by the vertical ratio (== rx here)
            wr32(rdram, fp + fResult, ok ? 1 : 0);
            go(UnlockSrc, callVirtual(rdram, c, src, 0xa4, 0, 0));
            // The EE scheduler is cycle driven: the guest loop charged its back-edges (about 300 cycles per output
            // pixel), the host filter must charge the same simulated time or threads interleave differently (stalls
            // otherwise).
            if (g_chargeCycles)
                rt->cpTick(std::min<uint32_t>(rd32(rdram, dst + 0x24) * rd32(rdram, dst + 0x28) * 300u, 4000000u));
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
    // Masks stage: entered after 0x1be858 returned; the other stages after their guest step.
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
    const uint32_t fp = (sp - kFrame) & ~0xfu;
    wr32(rdram, fp + fStage, Masks);
    wr32(rdram, fp + fRa, getRegU32(c, 31));
    wr32(rdram, fp + fDst, dst);
    wr32(rdram, fp + fSrc, src);
    wr32(rdram, fp + fBpp, bpp);
    wr32(rdram, fp + fRatio, rx);
    wr32(rdram, fp + fResult, 0);
    wr32(rdram, fp + fSp, sp);
    wr32(rdram, fp + kOutPtr, 0);
    wr32(rdram, fp + kInPtr, 0);
    SET_GPR_U32(c, 29, fp);
    // 0x1be858(dst, buffer): the four channels' shifts and masks
    callStep(c, drakan_region::guestAddress(kFnMasks), dst, fp + fBuf, 0);
    return true;
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
    // opt-in: intermittent heap stalls on Islands Night / Book (journal 2026-11-02/ootf-native-mips.md)
    g_native = mode && mode[0] == '1';
    if (const char* ch = std::getenv("DRAKAN_TEXSCALE_CHARGE"))
        g_chargeCycles = std::atoi(ch) != 0;
    if (const char* d = std::getenv("DRAKAN_TEXSCALE_DELAY_US"))
        g_delayUs = static_cast<uint32_t>(std::atoi(d));
    if (const char* m = std::getenv("DRAKAN_TEXSCALE_BPP"))
        g_bppMask = static_cast<uint32_t>(std::atoi(m));
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
