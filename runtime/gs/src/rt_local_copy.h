// Local-to-local transfer between two whole PSMCT32 render targets, done as one GPU copy. The game copies its 512x512
// frame buffer to the other one several times a second; through memory that is two full readbacks, a per-pixel CPU
// copy and a later re-upload of the destination. The decision and the bookkeeping are here for both backends
// (D3D11Backend::Impl::copyLocal, WgpuBackend::Impl::copyLocal); a backend only issues the copy itself.
#pragma once
#include "gs_internal.h"
#include "gs_mem.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace gsr::rt_copy {

// GSR_GPU_COPY=0 copies through memory as before.
inline bool gpuCopyEnabled() {
    static const bool enabled = [] {
        const char* e = std::getenv("GSR_GPU_COPY");
        return !(e && e[0] == '0');
    }();
    return enabled;
}

template <class RT>
struct Plan {
    RT* src = nullptr;
    RT* dst = nullptr;
    uint32_t sourceFirst = 0, sourceLast = 0; // VRAM blocks of the source rectangle
    explicit operator bool() const { return src && dst; }
};

// Only taken when the result is provably the same: both ends are exactly the targets, no other target shares either
// memory range (it would need the memory the copy no longer writes), and neither target is mid temporal world pass.
// rts holds smart pointers to RT; rowSpan(target, y0, y1, lo, hi) gives the VRAM blocks of its rows [y0, y1).
template <class Targets, class RowSpan>
auto plan(const Targets& rts, uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm,
          uint32_t dbp, uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, RowSpan&& rowSpan)
    -> Plan<typename Targets::value_type::element_type> {
    using RT = typename Targets::value_type::element_type;
    Plan<RT> none;
    if (!gpuCopyEnabled() || !readbackSkipEnabled() || spsm != PSM_CT32 || dpsm != PSM_CT32 || sx || sy || dx || dy ||
        !w || !h)
        return none;
    Plan<RT> p;
    for (auto& r : rts) {
        if (r->psm != PSM_CT32 || r->w != w || !r->tex)
            continue;
        if (r->base() == sbp && r->fbw == sbw)
            p.src = r.get();
        if (r->base() == dbp && r->fbw == dbw)
            p.dst = r.get();
    }
    if (!p.src || !p.dst || p.src == p.dst || h > p.src->h || h > p.dst->h || p.src->wstate == 1 || p.dst->wstate == 1)
        return none;
    uint32_t dlo, dhi;
    spanBlocks(PSM_CT32, sbp, sbw, 0, 0, w, h, p.sourceFirst, p.sourceLast);
    spanBlocks(PSM_CT32, dbp, dbw, 0, 0, w, h, dlo, dhi);
    if (!(p.sourceLast < dlo || dhi < p.sourceFirst))
        return none; // the ranges overlap each other
    for (auto& r : rts) {
        if (r.get() == p.src || r.get() == p.dst)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, 0u, r->h, lo, hi);
        if (!(hi < p.sourceFirst || lo > p.sourceLast) || !(hi < dlo || lo > dhi))
            return none; // another target aliases the memory involved
    }
    return p;
}

// After the GPU copy. Memory is not written (the GPU owns the destination); the stamps still move so anything decoded
// from those blocks is rebuilt, and the destination's rows count as current so a refresh does not push stale memory
// over them.
template <class RT>
void finish(LocalMemory& mem, RT& dst, uint32_t dbp, uint32_t dbw, uint32_t w, uint32_t h, uint64_t frame) {
    mem.touchRect(PSM_CT32, dbp, dbw, 0, 0, w, h);
    const uint32_t ph = dst.pageH();
    for (uint32_t r = 0; r * ph < h && r < dst.rowStamp.size(); ++r)
        dst.rowStamp[r] = mem.clock();
    dst.dy0 = 0;
    dst.dy1 = dst.dirty ? std::max(dst.dy1, h) : h;
    dst.dirty = true;
    dst.copyValid = false;
    dst.drawFrame = frame;
    ++dst.writes;
}

} // namespace gsr::rt_copy
