#pragma once
// Private helpers of the VU1 interpreter (friend of VU1Interpreter).
//
// VU1 results are tracked like this:
//  * VF / ACC / VI results are architecturally interlocked (a reader stalls until the result is ready), so they are
//    written at issue; only the stall timing (m_vfReady/m_viReady/m_accReady) is modelled.
//  * MAC / status / CLIP results are NOT interlocked (a reader sees the old value until 4 cycles later), so they travel
//    through a FIFO of pending entries (all with the same latency, hence ordered by ready cycle). The FIFO is settled
//    lazily: whenever something reads MAC/status/CLIP, before the Q commit touches the status register, at the end of a
//    run.
//  * Q, P and VU data memory stores keep their exact pending queues.
#include "runtime/ps2_vu1.h"

#if defined(_MSC_VER)
#define PS2_VU_FORCEINLINE __forceinline
#else
#define PS2_VU_FORCEINLINE inline __attribute__((always_inline))
#endif

struct Vu1Fast {
    struct Spare {
        uint32_t flagHead;   // index of the oldest pending flag entry
        uint32_t flagCount;  // number of pending flag entries (ring of VU1Interpreter::kMaxFlagEntries)
        uint64_t runBase;    // m_cycle when the program was started
        uint64_t storeReady; // cycle at which the most recent VU data memory store retires (it is applied at issue)
        uint32_t aotValid;   // VU1 micro memory equals the ahead-of-time compiled image (aot/)
        uint32_t sinkNone;   // benchmark mode: completed XGKICK packets are dropped instead of being sent to the GIF
        uint64_t xgLast;     // XGKICK: cycle up to which the PATH1 transfer progress has been accounted (it is applied
                             // lazily, see xgkickCatchUp)
        uint64_t blkFast, blkSlow,
            blkPairs;     // statistics: block entries run by the block code / sent to the exact path, pairs in blocks
        uint64_t why[10]; // statistics: reasons for not entering a block
        // Stall model: bit set = that VF lane / VI register / ACC lane still waits for a result (ready cycle > now).
        // A write schedules the clearing of its bits `latency` cycles ahead in the ring; every cycle retires its slot.
        struct Busy {
            uint64_t lo, hi;
            uint32_t va;
        };
        Busy pending;
        Busy clearAt[16];
        // Every exact-engine pipeline, ready table and this ring still hold the values resetScheduler gave them (only
        // the functional engine, which leaves them alone, ran since), so the next reset can skip clearing them.
        uint32_t schedClean;
    };
    static_assert(sizeof(Spare) <= 4096, "VU1Interpreter::m_spare is too small");
    static Spare& spare(VU1Interpreter& v) { return *reinterpret_cast<Spare*>(v.m_spare); }
    static const Spare& spare(const VU1Interpreter& v) { return *reinterpret_cast<const Spare*>(v.m_spare); }

    static PS2_VU_FORCEINLINE void advanceBusy(Spare& sp, uint64_t cycleNow) {
        Spare::Busy& c = sp.clearAt[cycleNow & 15u];
        sp.pending.lo &= ~c.lo;
        sp.pending.hi &= ~c.hi;
        sp.pending.va &= ~c.va;
        c = {};
    }
    static PS2_VU_FORCEINLINE bool regsBusy(const Spare& sp, const VU1Interpreter::DecodedInstructionPair& d) {
        return ((d.rdLo & sp.pending.lo) | (d.rdHi & sp.pending.hi) | (d.rdVa & sp.pending.va)) != 0u;
    }
    static PS2_VU_FORCEINLINE void retireBlockEntry(Spare& sp, uint64_t base, uint32_t cycles) {
        // The compiled-image table asserts a maximum register latency of four.
        // Entry is after the preceding pair advanced one cycle: only the next
        // three slots can still contain entry writes. Block writes are installed
        // separately after this retirement, as in the original full-ring scan.
        constexpr uint32_t horizon = VU1Interpreter::kFmacLatency - 1u;
        if (cycles >= horizon) {
            sp.pending = {};
            for (uint32_t i = 1; i <= horizon; ++i)
                sp.clearAt[(base + i) & 15u] = {};
            return;
        }
        const uint32_t advance = cycles < horizon ? cycles : horizon;
        for (uint32_t i = 1; i <= advance; ++i)
            advanceBusy(sp, base + i);
    }
    static PS2_VU_FORCEINLINE void installBlockWrite(Spare& sp, uint64_t end, uint32_t cycles, uint32_t offset,
                                                     uint64_t lo, uint64_t hi, uint32_t va) {
        constexpr uint32_t horizon = VU1Interpreter::kFmacLatency - 1u;
        if (cycles < horizon && ((sp.pending.lo & lo) | (sp.pending.hi & hi) | (sp.pending.va & va)) != 0u) {
            // A short block can overwrite an entry result before its old
            // completion slot retires. Cancel that earlier clear, as the exact
            // pair scheduler does, before installing the newest deadline.
            for (uint32_t ahead = 1; ahead < offset && ahead <= horizon - cycles; ++ahead) {
                Spare::Busy& older = sp.clearAt[(end + ahead) & 15u];
                older.lo &= ~lo;
                older.hi &= ~hi;
                older.va &= ~va;
            }
        }
        Spare::Busy& slot = sp.clearAt[(end + offset) & 15u];
        slot.lo |= lo;
        slot.hi |= hi;
        slot.va |= va;
        sp.pending.lo |= lo;
        sp.pending.hi |= hi;
        sp.pending.va |= va;
    }

    static PS2_VU_FORCEINLINE void scheduleWrites(Spare& sp, const VU1Interpreter::DecodedInstructionPair& d,
                                                  uint64_t cycleNow) {
        for (uint32_t g = 0; g < d.grpCount; ++g) {
            // The newest write defines when a register is ready: drop clears that an older, shorter write scheduled
            // for these registers before this one completes (only registers that are still busy have such clears).
            if (((sp.pending.lo & d.grpLo[g]) | (sp.pending.hi & d.grpHi[g]) | (sp.pending.va & d.grpVa[g])) != 0u) {
                for (uint32_t ahead = 1; ahead < d.grpLat[g]; ++ahead) {
                    Spare::Busy& earlier = sp.clearAt[(cycleNow + ahead) & 15u];
                    earlier.lo &= ~d.grpLo[g];
                    earlier.hi &= ~d.grpHi[g];
                    earlier.va &= ~d.grpVa[g];
                }
            }
            Spare::Busy& c = sp.clearAt[(cycleNow + d.grpLat[g]) & 15u];
            c.lo |= d.grpLo[g];
            c.hi |= d.grpHi[g];
            c.va |= d.grpVa[g];
            sp.pending.lo |= d.grpLo[g];
            sp.pending.hi |= d.grpHi[g];
            sp.pending.va |= d.grpVa[g];
        }
    }

    // Functional engine (aot/ps2_vu1_func.cpp, ps2_vu1_func.h); a member so that it shares this friendship.
    struct Func;

    // Exact five-pair SQI/ISUBIU/NOP/IBGTZ/NOP fill loop, with idle pipelines.
    static bool nativeFill(VU1Interpreter& v, uint8_t* data, uint32_t dataSize, uint64_t budgetEnd,
                           uint32_t maxIterations, uint32_t& pairs);

    // XGKICK / PATH1 progress is applied lazily: the qwords that are due by the current cycle are copied when something
    // can observe the difference (a store into the not yet copied part of VU data memory, a new XGKICK, the end of the
    // run).
    static void xgkickCatchUp(VU1Interpreter& v);
    static void xgkickBeforeStore(VU1Interpreter& v, uint32_t address);

    // Apply every queued MAC/status/CLIP result whose ready cycle has been reached (FIFO order).
    static void settleFlags(VU1Interpreter& v);
    // Reserve the next FIFO slot (nullptr when the FIFO is full even after settling).
    static inline VU1Interpreter::FlagPipelineEntry* allocFlag(VU1Interpreter& v) {
        Spare& sp = spare(v);
        if (sp.flagCount == VU1Interpreter::kMaxFlagEntries)
            settleFlags(v);
        if (sp.flagCount == VU1Interpreter::kMaxFlagEntries)
            return nullptr;
        VU1Interpreter::FlagPipelineEntry* entry =
            &v.m_flagPipeline[(sp.flagHead + sp.flagCount) & (VU1Interpreter::kMaxFlagEntries - 1u)];
        ++sp.flagCount;
        return entry;
    }
};
