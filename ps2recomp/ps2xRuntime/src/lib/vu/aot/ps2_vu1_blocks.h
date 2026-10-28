#pragma once
// Block-level ahead-of-time compilation of the game's fixed VU1 microcode (see also ps2_vu1_aot_slice.inc).
//
// The compiler splits the image into blocks (straight runs of instruction pairs ending at a branch and its delay slot,
// at a jump target, before a pair the block code does not handle, or at a size limit) and works out, for each block,
// the cycle at which each pair issues when the block is entered with an idle pipeline: a pair issues one cycle after
// the previous one unless it reads a register that an earlier pair of the block has not finished writing (the same
// stall rule as Vu1Step::step, with the latencies of Vu1Decode). The run-time code then executes the pairs with those
// constants (Vu1Step::execPair, the same code as the pair-at-a-time path) and brings the busy-register state up to date
// once per block (Vu1Step::blockExit). The entry check (Vu1Step::blockCanEnter) falls back to the exact pair-at-a-time
// path whenever the assumptions do not hold.
//
// Pairs that are not handled by blocks (FDIV/EFU/XGKICK/WAITQ/WAITP, E/D/T bits, reserved encodings) always run through
// the exact path, and so does everything entered in the middle of a block.
#include "../ps2_vu1_decode.h"
#include "../ps2_vu1_step.h"

#include <array>
#include <cstdint>

namespace Vu1Blocks {
using Dec = Vu1Step::Dec;

inline constexpr uint32_t kMaxPairs = 32; // pairs per block
inline constexpr uint32_t kMaxEvents = 6; // distinct "ready after the block" offsets
inline constexpr uint32_t kMaxBlocks = 640;
inline constexpr uint32_t kBits = 160; // busy-register bits: 64 lo + 64 hi + 32 va

struct Info {
    uint16_t first = 0, count = 0, cycles = 0;
    bool branchEnd = false;
    bool recovery = false;
    bool qSafe = true; // see Vu1Step::blockCanEnter
    uint8_t evCount = 0;
    uint8_t T[kMaxPairs]{};
    uint64_t eLo = 0, eHi = 0;
    uint32_t eVa = 0;
    // the same split by what is needed when: reads of busy entry registers at issue cycle 0..2, and all registers
    // written
    uint64_t rLo[3]{}, rHi[3]{};
    uint32_t rVa[3]{};
    uint64_t wLo = 0, wHi = 0;
    uint32_t wVa = 0;
    Vu1Step::BlockEvent ev[kMaxEvents]{};
};

struct Table {
    uint32_t count = 0;
    uint32_t maxLat = 0;
    Info b[kMaxBlocks]{};
};

constexpr bool getBit(const Dec& d, bool read, uint32_t bit) {
    // bit space shared with the busy masks of Vu1Fast (lo, hi, va)
    if (read)
        return bit < 64    ? ((d.rdLo >> bit) & 1u) != 0
               : bit < 128 ? ((d.rdHi >> (bit - 64)) & 1u) != 0
                           : ((d.rdVa >> (bit - 128)) & 1u) != 0;
    return false;
}

constexpr bool grpBit(const Dec& d, uint32_t g, uint32_t bit) {
    return bit < 64    ? ((d.grpLo[g] >> bit) & 1u) != 0
           : bit < 128 ? ((d.grpHi[g] >> (bit - 64)) & 1u) != 0
                       : ((d.grpVa[g] >> (bit - 128)) & 1u) != 0;
}

constexpr void setMaskBit(uint64_t& lo, uint64_t& hi, uint32_t& va, uint32_t bit) {
    if (bit < 64)
        lo |= 1ull << bit;
    else if (bit < 128)
        hi |= 1ull << (bit - 64);
    else
        va |= 1u << (bit - 128);
}

// Pairs the block code can run (everything the exact path does is allowed except what interacts with the other
// pipelines or ends the program).
constexpr bool blockEligible(const Dec& d) {
    return !d.upperUsage.reserved && !d.lowerUsage.reserved && !d.needsExtra && !d.eBit && !d.dBit && !d.tBit;
}

template <size_t Pairs>
constexpr Info schedule(const uint64_t (&image)[Pairs], uint32_t start, uint32_t cnt, bool branchEnd, uint32_t maxLat) {
    Info info{};
    info.first = static_cast<uint16_t>(start);
    info.count = static_cast<uint16_t>(cnt);
    info.branchEnd = branchEnd;
    int32_t ready[kBits]{};
    bool written[kBits]{};
    uint32_t now = 0;
    for (uint32_t k = 0; k < cnt; ++k) {
        const Dec d = Vu1Decode::decodePair(static_cast<uint32_t>(image[start + k]),
                                            static_cast<uint32_t>(image[start + k] >> 32), true);
        uint32_t need = now;
        for (uint32_t b = 0; b < kBits; ++b)
            if (getBit(d, true, b) && written[b] && static_cast<uint32_t>(ready[b]) > need)
                need = static_cast<uint32_t>(ready[b]);
        info.T[k] = static_cast<uint8_t>(need);
        if (d.fmKind >= 1 && d.fmKind <= 5 && d.fmB == 4)
            info.qSafe = false; // reads Q
        if (!d.iBit) {
            const uint8_t opHi = static_cast<uint8_t>((d.lower >> 25) & 0x7Fu);
            if (opHi >= 0x14 && opHi <= 0x17)
                info.qSafe = false; // FSEQ / FSSET / FSAND / FSOR
        }
        if (need + 1 < maxLat) // busy bits of the entry state that are still busy at this cycle
            for (uint32_t b = 0; b < kBits; ++b)
                if (getBit(d, true, b) && !written[b]) {
                    setMaskBit(info.eLo, info.eHi, info.eVa, b);
                    setMaskBit(info.rLo[need], info.rHi[need], info.rVa[need], b);
                }
        for (uint32_t g = 0; g < d.grpCount; ++g)
            for (uint32_t b = 0; b < kBits; ++b)
                if (grpBit(d, g, b)) {
                    ready[b] = static_cast<int32_t>(need + d.grpLat[g]);
                    written[b] = true;
                    setMaskBit(info.wLo, info.wHi, info.wVa, b);
                }
        now = need + 1;
    }
    info.cycles = static_cast<uint16_t>(now);
    for (uint32_t b = 0; b < kBits; ++b) {
        if (!written[b] || static_cast<uint32_t>(ready[b]) <= now)
            continue;
        const uint8_t off = static_cast<uint8_t>(static_cast<uint32_t>(ready[b]) - now);
        uint32_t slot = 0;
        while (slot < info.evCount && info.ev[slot].off != off)
            ++slot;
        if (slot == info.evCount) {
            info.ev[slot].off = off;
            ++info.evCount;
        }
        setMaskBit(info.ev[slot].lo, info.ev[slot].hi, info.ev[slot].va, b);
    }
    return info;
}

template <size_t Pairs>
constexpr Table build(const uint64_t (&image)[Pairs], const uint32_t* entries, uint32_t entryCount,
                      const uint32_t* recoveryEntries = nullptr, uint32_t recoveryCount = 0) {
    Table t{};
    constexpr uint32_t n = static_cast<uint32_t>(Pairs);
    std::array<bool, Pairs> elig{}, isBr{}, leader{};
    std::array<uint8_t, Pairs> endMark{};

    for (uint32_t i = 0; i < n; ++i) {
        const Dec d =
            Vu1Decode::decodePair(static_cast<uint32_t>(image[i]), static_cast<uint32_t>(image[i] >> 32), true);
        elig[i] = blockEligible(d);
        isBr[i] = !d.iBit && d.lowerUsage.pipeline == Vu1Decode::PipelineBranch;
        for (uint32_t g = 0; g < d.grpCount; ++g)
            if (d.grpLat[g] > t.maxLat)
                t.maxLat = d.grpLat[g];
        if (d.eBit || d.dBit || d.tBit) {
            if (i + 1 < n)
                leader[i + 1] = true;
            if (i + 2 < n)
                leader[i + 2] = true;
        }
        if (isBr[i]) {
            const uint32_t lower = d.lower;
            const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
            if (i + 2 < n)
                leader[i + 2] = true; // after the delay slot (also the return address of BAL / JALR)
            if (opHi == 0x20 || opHi == 0x21 || opHi == 0x28 || opHi == 0x29 || (opHi >= 0x2C && opHi <= 0x2F)) {
                const int32_t target = static_cast<int32_t>(i) + 1 + static_cast<int32_t>(IMM11(lower));
                if (target >= 0 && target < static_cast<int32_t>(n))
                    leader[target] = true;
            }
        }
    }
    for (uint32_t e = 0; e < entryCount; ++e)
        if (entries[e] < n)
            leader[entries[e]] = true;

    uint32_t i = 0;
    while (i < n && t.count < kMaxBlocks) {
        if (!elig[i]) {
            ++i;
            continue;
        }
        const uint32_t start = i;
        uint32_t cnt = 0;
        bool branchEnd = false;
        for (;;) {
            if (isBr[i]) {
                if (cnt + 2 <= kMaxPairs && i + 1 < n && elig[i + 1] && !isBr[i + 1]) {
                    cnt += 2;
                    i += 2;
                    branchEnd = true;
                }
                break;
            }
            ++cnt;
            ++i;
            if (i >= n || !elig[i] || leader[i] || cnt >= kMaxPairs)
                break;
        }
        if (cnt == 0) {
            ++i; // a branch whose delay slot the blocks cannot take: left to the exact path
            continue;
        }
        if (cnt < 2)
            continue; // single pair: not worth a block

        // schedule
        t.b[t.count++] = schedule(image, start, cnt, branchEnd, t.maxLat);
    }
    // A failed block entry executes one exact pair. Re-entry points let a
    // later instruction use compiled scheduling after that hazard clears.
    // Each suffix has its own complete entry checks and static schedule.
    const uint32_t originalCount = t.count;
    for (uint32_t e = 0; e < recoveryCount; ++e)
        for (uint32_t b = 0; b < originalCount; ++b) {
            const Info& parent = t.b[b];
            const uint32_t first = recoveryEntries[e];
            const uint32_t end = parent.first + parent.count;
            if (first <= parent.first || first + 1 >= end)
                continue;
            if (t.count >= kMaxBlocks)
                throw "VU recovery block table capacity exceeded";
            Info suffix = schedule(image, first, end - first, parent.branchEnd, t.maxLat);
            suffix.recovery = true;
            t.b[t.count++] = suffix;
            break;
        }
    return t;
}
} // namespace Vu1Blocks
