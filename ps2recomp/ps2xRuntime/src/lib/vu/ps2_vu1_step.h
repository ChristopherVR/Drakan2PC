#pragma once
// One instruction pair of the VU1 interpreter (friend of VU1Interpreter).
//
// step() is the body of the interpreter loop. It is used with a run-time decoded pair by VU1Interpreter::run() and with
// a compile-time constant pair by the ahead-of-time compiled microcode (aot/): after inlining, every decoded-pair field
// is a constant and the unused paths disappear.
#include "runtime/ps2_vu1.h"
#include "ps2_vu1_detail.h"
#include "ps2_vu1_fast.h"
#include "ps2_vu1_simd.h"
#include "ps2_vu1_flag_batch.h"
#include "ps2_vu1_ring.h"

#include <bit>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <smmintrin.h>

#if defined(_MSC_VER)
#define PS2_VU_FORCEINLINE __forceinline
#define PS2_VU_NOINLINE __declspec(noinline)
#else
#define PS2_VU_FORCEINLINE inline __attribute__((always_inline))
#define PS2_VU_NOINLINE __attribute__((noinline))
#endif

// ============================================================================
// Arithmetic FMAC ops (ADD/SUB/MUL/MADD/MSUB, all operand forms, with and without accumulator destination).
// Vectorised equivalent of execUpper + applyFmacDest*: the result lanes are computed in single precision (round toward
// zero, as before); when every destination lane (and for MADD/MSUB every product lane) is an ordinary number the flags
// only depend on the sign bits. Otherwise the exact results are recomputed in double precision (as the reference code
// does, where long double == double on MSVC) and classified into zero / underflow / overflow with the same rules as
// normalizeFmacExactResult.
// ============================================================================
namespace Vu1FmacDetail {
// bit c set = component c is selected by DEST (DEST bit 3-c); the same table reverses a 4-bit lane mask
inline constexpr uint8_t kRev4[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

// vector mask with lane c = all ones when bit c of the index is set
struct CompMasks {
    alignas(16) uint32_t m[16][4];
    constexpr CompMasks() : m() {
        for (int bits = 0; bits < 16; ++bits)
            for (int c = 0; c < 4; ++c)
                m[bits][c] = ((bits >> c) & 1) ? 0xFFFFFFFFu : 0u;
    }
};
inline constexpr CompMasks kCompMasks;

PS2_VU_FORCEINLINE __m128 compMask(int bits) {
    return _mm_load_ps(reinterpret_cast<const float*>(kCompMasks.m[bits & 15]));
}

// bit c = lane c is +-0
PS2_VU_FORCEINLINE int zeroLanes(__m128 v) {
    const __m128i m = _mm_and_si128(_mm_castps_si128(v), _mm_set1_epi32(0x7FFFFFFF));
    return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(m, _mm_setzero_si128())));
}

// bit c = lane c holds an ordinary number (exponent field 8..246)
PS2_VU_FORCEINLINE int ordinaryLanes(__m128 v) {
    return Vu1Simd::ordinaryLanes(v);
}

struct Classes {
    int neg, zero, under, over; // lane bit masks (bit c = component c)
};

// Flags of an FMAC result when the class (ordinary / exact zero) of every destination lane, and for MADD/MSUB of every
// product lane, is known without recomputing in double precision (ADD/SUB: a zero sum is exactly zero; MUL/MADD/MSUB: a
// zero product or an exactly zero operand). Returns false when the exact (double precision) classification is needed
// (Vu1Step::fmacSlow).
PS2_VU_FORCEINLINE bool flagsKnownClasses(uint8_t kind, __m128 vs, __m128 b, __m128 product, __m128 result, int comps,
                                          int ordR, int ordP, uint32_t& mac, uint32_t& status, uint32_t& extraSticky) {
    const bool productSum = kind >= 4;
    const int zeroR = zeroLanes(result);
    const int opZero = (kind == 1 || kind == 2) ? 0 : (zeroLanes(vs) | zeroLanes(b));
    if ((((ordR | (kind <= 2 ? zeroR : (zeroR & opZero))) & comps) == comps) &&
        (!productSum || (((ordP | opZero) & comps) == comps))) {
        const int zero = zeroR & comps;
        const int negative = _mm_movemask_ps(result) & comps;
        mac = static_cast<uint32_t>(kRev4[zero]) | (static_cast<uint32_t>(kRev4[negative]) << 4);
        status = (zero != 0 ? 0x1u : 0u) | (negative != 0 ? 0x2u : 0u);
        extraSticky = 0u;
        if (productSum) {
            // product flags: sign, and zero for an exactly zero product (a zero operand)
            extraSticky =
                (((opZero & comps) != 0) ? 0x1u : 0u) | (((_mm_movemask_ps(product) & comps) != 0) ? 0x2u : 0u);
        }
        return true;
    }
    return false;
}

// Same rules as VU1Interpreter::normalizeFmacExactResult, for 4 doubles.
inline Classes classifyExact(__m128d lo, __m128d hi) {
    const __m128d absMask = _mm_castsi128_pd(_mm_set1_epi64x(0x7FFFFFFFFFFFFFFFll));
    const __m128d magLo = _mm_and_pd(lo, absMask);
    const __m128d magHi = _mm_and_pd(hi, absMask);
    const __m128d zero = _mm_setzero_pd();
    const __m128d fmax = _mm_set1_pd(static_cast<double>(std::numeric_limits<float>::max()));
    const __m128d fmin = _mm_set1_pd(static_cast<double>(std::numeric_limits<float>::min()));
    Classes c;
    c.neg = _mm_movemask_pd(lo) | (_mm_movemask_pd(hi) << 2);
    const int z = _mm_movemask_pd(_mm_cmpeq_pd(magLo, zero)) | (_mm_movemask_pd(_mm_cmpeq_pd(magHi, zero)) << 2);
    const int over =
        (_mm_movemask_pd(_mm_cmpgt_pd(magLo, fmax)) | (_mm_movemask_pd(_mm_cmpgt_pd(magHi, fmax)) << 2)) & ~z;
    const int under =
        (_mm_movemask_pd(_mm_cmplt_pd(magLo, fmin)) | (_mm_movemask_pd(_mm_cmplt_pd(magHi, fmin)) << 2)) & ~z & ~over;
    c.zero = z;
    c.over = over;
    c.under = under;
    return c;
}
} // namespace Vu1FmacDetail

struct Vu1Step {
    using Dec = VU1Interpreter::DecodedInstructionPair;
    using VfAccess = VU1Interpreter::VfAccess;

    struct Ctx {
        uint64_t budgetEnd;
        uint8_t* vuData;
        uint32_t dataSize;
        GS* gs;
        PS2Memory* memory;
        uint32_t codeSize;
        bool programEnded;
        uint64_t pairs; // pairs executed by this run (statistics)
    };

    static bool packetKernel(VU1Interpreter& v, Ctx& c);
    static bool uvKernel(VU1Interpreter& v, Ctx& c);
    static bool transformKernel(VU1Interpreter& v, Ctx& c);

    // VF00 is architectural constant at pair entry. The pair reset is retained;
    // fold its source value instead of repeatedly loading and normalizing it.
    static PS2_VU_FORCEINLINE __m128 readOperand(const VU1State& state, uint8_t reg, bool normalized = false) {
        return reg == 0     ? _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f)
               : normalized ? _mm_loadu_ps(state.vf[reg])
                            : Vu1Simd::normalize(_mm_loadu_ps(state.vf[reg]));
    }
    static PS2_VU_FORCEINLINE __m128 readBroadcast(const VU1State& state, uint8_t reg, uint8_t component,
                                                   bool normalized = false) {
        return reg == 0     ? _mm_set1_ps(component == 3 ? 1.0f : 0.0f)
               : normalized ? _mm_set1_ps(state.vf[reg][component])
                            : Vu1Simd::normalize(_mm_set1_ps(state.vf[reg][component]));
    }

    static PS2_VU_FORCEINLINE bool timedQEnabled() {
        static const bool enabled = [] {
            const char* setting = std::getenv("DRAKAN_VU_TIMED_Q");
            return !(setting && setting[0] == '0');
        }();
        return enabled;
    }
    static PS2_VU_FORCEINLINE bool traceScalarsIdle(const VU1Interpreter& v) { return v.m_minReady == ~0ull; }
    static PS2_VU_FORCEINLINE bool blockQDue(const VU1Interpreter& v, uint64_t end) {
        return v.m_fdiv.valid && v.m_fdiv.readyCycle < end;
    }

    static PS2_VU_FORCEINLINE uint32_t microMask(const VU1Interpreter& v) {
        return v.m_unit == VU1Interpreter::Unit::VU1 ? 0x3fffu : 0x0fffu;
    }
    static PS2_VU_FORCEINLINE uint32_t resolveTraceBranch(VU1Interpreter& v, uint32_t fallthrough) {
        auto& state = v.m_state;
        state.pc = state.branchPending ? state.branchTarget & microMask(v) : fallthrough;
        state.branchPending = false;
        state.branchDelay = 0;
        return state.pc;
    }

    static PS2_VU_FORCEINLINE int32_t readBranchVi(const VU1Interpreter& v, uint8_t reg) {
        if (reg == 0u)
            return 0;
        if (v.m_viBranchBackupValid && v.m_viBranchBackupReg == reg)
            return v.m_viBranchBackupValue;
        return v.m_state.vi[reg];
    }

    // Rare cases of an FMAC op (zero / denormal / overflowing lanes): out of line, in ps2_vu1_upper.cpp.
    static void fmacSlow(uint8_t kind, const float* vs4, const float* b4, const float* acc4, const float* product4,
                         float* result4, int comps, int ordR, int ordP, uint32_t* flags3);

    template <bool BatchFmac = false, uint8_t NormalInputs = 0>
    static PS2_VU_FORCEINLINE void upperFmac(VU1Interpreter& v, const Dec& decoded, Vu1FlagBatch* batch = nullptr) {
        using namespace Vu1FmacDetail;
        auto& m_state = v.m_state;
        const uint32_t instr = decoded.upper;
        v.m_currentUpperInstruction = instr;
        const uint8_t dest = DEST(instr);
        if (dest == 0u)
            return; // nothing is written and no flags are produced

        const uint8_t ft = FT(instr);
        const uint8_t fs = FS(instr);
        const __m128 vs = readOperand(m_state, fs, (NormalInputs & 1u) != 0);
        __m128 b;
        switch (decoded.fmB) {
            case 0:
            case 1:
            case 2:
            case 3:
                b = readBroadcast(m_state, ft, decoded.fmB, (NormalInputs & 2u) != 0);
                break;
            case 4:
                b = Vu1Simd::normalize(_mm_set1_ps(m_state.q));
                break;
            case 5:
                b = Vu1Simd::normalize(_mm_set1_ps(m_state.i));
                break;
            default:
                b = readOperand(m_state, ft, (NormalInputs & 2u) != 0);
                break;
        }

        upperFmacOperands<BatchFmac, NormalInputs>(v, decoded, vs, b, batch);
    }

    struct FmacValue {
        __m128 value;
        uint32_t mac, status, extraSticky;
    };
    // Arithmetic and per-lane flags have no cross-lane dependencies. Native
    // XY kernels can use the other two lanes for a second independent vertex.
    static PS2_VU_FORCEINLINE FmacValue calculateFmac(const Dec& decoded, __m128 vs, __m128 b, __m128 acc) {
        using namespace Vu1FmacDetail;
        const uint8_t dest = DEST(decoded.upper);
        __m128 result;
        __m128 product = _mm_setzero_ps();
        const uint8_t kind = decoded.fmKind;
        const bool productSum = kind >= 4;
        switch (kind) {
            case 1:
                result = Vu1Simd::add(vs, b);
                break;
            case 2:
                result = Vu1Simd::subtract(vs, b);
                break;
            case 3:
                result = Vu1Simd::multiply(vs, b);
                break;
            case 4:
                product = Vu1Simd::multiply(vs, b);
                result = Vu1Simd::add(acc, product);
                break;
            default:
                product = Vu1Simd::multiply(vs, b);
                result = Vu1Simd::subtract(acc, product);
                break;
        }

        const int comps = kRev4[dest];
        uint32_t mac, status, extraSticky;
        const int ordR = ordinaryLanes(result);
        const int ordP = productSum ? ordinaryLanes(product) : 0xF;
        if (((ordR & ordP) & comps) == comps) {
            const int negative = _mm_movemask_ps(result) & comps;
            mac = static_cast<uint32_t>(kRev4[negative]) << 4;
            status = negative != 0 ? 0x2u : 0u;
            extraSticky = (productSum && (_mm_movemask_ps(product) & comps) != 0) ? 0x2u : 0u;
        } else if (flagsKnownClasses(kind, vs, b, product, result, comps, ordR, ordP, mac, status, extraSticky)) {
        } else {
            alignas(16) float vs4[4], b4[4], acc4[4], product4[4], result4[4];
            _mm_store_ps(vs4, vs);
            _mm_store_ps(b4, b);
            _mm_store_ps(acc4, acc);
            _mm_store_ps(product4, product);
            _mm_store_ps(result4, result);
            uint32_t flags3[3];
            fmacSlow(kind, vs4, b4, acc4, product4, result4, comps, ordR, ordP, flags3);
            result = _mm_load_ps(result4);
            mac = flags3[0];
            status = flags3[1];
            extraSticky = flags3[2];
        }

        return {result, mac, status, extraSticky};
    }

    template <bool BatchFmac = false, uint8_t NormalInputs = 0>
    static PS2_VU_FORCEINLINE void upperFmacOperands(VU1Interpreter& v, const Dec& decoded, __m128 vs, __m128 b,
                                                     Vu1FlagBatch* batch = nullptr) {
        using namespace Vu1FmacDetail;
        auto& m_state = v.m_state;
        const uint32_t instr = decoded.upper;
        v.m_currentUpperInstruction = instr;
        const uint8_t dest = DEST(instr);
        if (dest == 0u)
            return;
        const __m128 acc = decoded.fmKind >= 4 ? ((NormalInputs & 4u) ? _mm_loadu_ps(m_state.acc)
                                                                      : Vu1Simd::normalize(_mm_loadu_ps(m_state.acc)))
                                               : _mm_setzero_ps();
        const auto flags = calculateFmac(decoded, vs, b, acc);
        const __m128 result = flags.value;
        const uint32_t mac = flags.mac, status = flags.status, extraSticky = flags.extraSticky;
        const int comps = kRev4[dest];

        if constexpr (BatchFmac)
            batch->add(mac, status, extraSticky);
        else {
            VU1Interpreter::FlagPipelineEntry* entry = Vu1Fast::allocFlag(v);
            if (!entry) {
                v.reportReservedInstruction(true, 0xFFFFFFFFu);
            } else {
                entry->readyCycle = v.m_cycle + VU1Interpreter::kFmacLatency;
                entry->issueCycle = v.m_cycle;
                entry->mac = mac;
                entry->status = status;
                entry->extraSticky = extraSticky;
                entry->clip = 0;
                entry->valid = true;
                entry->writesMac = true;
                entry->writesStatus = true;
                entry->writesSticky = false;
                entry->writesClip = false;
            }
        }
        float* target = decoded.fmToAcc ? v.m_upperAccDst : v.m_upperDst;
        // DEST selects the lanes that are written (DEST bit 3-c <-> component c)
        _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), result, comps));
    }

    // The other frequent upper-unit instructions (MAX/MINI, ITOF/FTOI, ABS, CLIP), vectorised; same results as
    // VU1Interpreter::execUpper. Everything else calls execUpper.
    static PS2_VU_FORCEINLINE void upperOther(VU1Interpreter& v, const Dec& decoded) {
        using namespace Vu1FmacDetail;
        auto& m_state = v.m_state;
        const uint32_t instr = decoded.upper;
        const uint8_t op = static_cast<uint8_t>(instr & 0x3Fu);
        const uint8_t dest = DEST(instr);
        const uint8_t ft = FT(instr);
        const uint8_t fs = FS(instr);
        v.m_currentUpperInstruction = instr;

        // ---- MAX / MINI (no flags)
        const bool isMax = (op >= 0x10u && op <= 0x13u) || op == 0x1Du || op == 0x2Bu;
        const bool isMin = (op >= 0x14u && op <= 0x17u) || op == 0x1Fu || op == 0x2Fu;
        if (isMax || isMin) {
            const __m128 vs = readOperand(m_state, fs);
            __m128 b;
            if (op < 0x18u)
                b = readBroadcast(m_state, ft, op & 3u);
            else if (op == 0x1Du || op == 0x1Fu)
                b = Vu1Simd::normalize(_mm_set1_ps(m_state.i));
            else
                b = readOperand(m_state, ft);
            const __m128 result = isMax ? _mm_max_ps(vs, b) : _mm_min_ps(vs, b);
            float* target = v.m_upperDst;
            _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), result, kRev4[dest]));
            return;
        }

        if (op >= 0x3Cu) {
            const uint8_t special = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
            float* target = v.m_upperDst; // special-group results go to FT
            switch (special) {
                case 0x10: // ITOF0
                case 0x11: // ITOF4
                case 0x12: // ITOF12
                case 0x13: // ITOF15
                {
                    __m128 result = Vu1Simd::fromInt(_mm_loadu_si128(reinterpret_cast<const __m128i*>(m_state.vf[fs])));
                    if (special == 0x11)
                        result = Vu1Simd::multiply(result, _mm_set1_ps(1.0f / 16.0f));
                    else if (special == 0x12)
                        result = Vu1Simd::multiply(result, _mm_set1_ps(1.0f / 4096.0f));
                    else if (special == 0x13)
                        result = Vu1Simd::multiply(result, _mm_set1_ps(1.0f / 32768.0f));
                    _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), result, kRev4[dest]));
                    return;
                }
                case 0x14: // FTOI0
                case 0x15: // FTOI4
                case 0x16: // FTOI12
                case 0x17: // FTOI15
                {
                    const float scale = special == 0x14   ? 1.0f
                                        : special == 0x15 ? 16.0f
                                        : special == 0x16 ? 4096.0f
                                                          : 32768.0f;
                    const __m128 value = readOperand(m_state, fs);
                    const __m128 result = _mm_castsi128_ps(Vu1Simd::toFixed(value, scale));
                    _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), result, kRev4[dest]));
                    return;
                }
                case 0x1D: // ABS
                {
                    const __m128 vs = readOperand(m_state, fs);
                    const __m128 result =
                        _mm_andnot_ps(_mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(0x80000000u))), vs);
                    _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), result, kRev4[dest]));
                    return;
                }
                case 0x1F: // CLIP
                {
                    uint32_t wBits = 0u;
                    std::memcpy(&wBits, &m_state.vf[ft][3], sizeof(wBits));
                    const int32_t limit =
                        (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;
                    const auto exceedsClipPlane = [limit](float value, uint32_t signMask) {
                        uint32_t bits = 0u;
                        std::memcpy(&bits, &value, sizeof(bits));
                        bits ^= signMask;
                        int32_t orderedBits = 0;
                        std::memcpy(&orderedBits, &bits, sizeof(orderedBits));
                        return orderedBits > limit;
                    };
                    uint32_t flags = 0u;
                    if (exceedsClipPlane(m_state.vf[fs][0], 0x00000000u))
                        flags |= 0x01u;
                    if (exceedsClipPlane(m_state.vf[fs][0], 0x80000000u))
                        flags |= 0x02u;
                    if (exceedsClipPlane(m_state.vf[fs][1], 0x00000000u))
                        flags |= 0x04u;
                    if (exceedsClipPlane(m_state.vf[fs][1], 0x80000000u))
                        flags |= 0x08u;
                    if (exceedsClipPlane(m_state.vf[fs][2], 0x00000000u))
                        flags |= 0x10u;
                    if (exceedsClipPlane(m_state.vf[fs][2], 0x80000000u))
                        flags |= 0x20u;
                    v.queueClip(flags);
                    return;
                }
                default:
                    break;
            }
        }
        v.execUpper(instr);
    }

    static PS2_VU_FORCEINLINE void advanceCycle(VU1Interpreter& v) {
        ++v.m_cycle;
        v.m_state.cycles = v.m_cycle;
        Vu1Fast::advanceBusy(Vu1Fast::spare(v), v.m_cycle);
        if (v.m_cycle >= v.m_minReady)
            v.commitReadyPipelines();
        // XGKICK progress is applied lazily (Vu1Fast::xgkickCatchUp)
    }

    // Keep the store in the compiled pair so its constant DEST eliminates the
    // generic lane loop and the register value need not be spilled for a call.
    // PATH1 still consumes every due overlapping qword before memory changes.
    static PS2_VU_FORCEINLINE void store(VU1Interpreter& v, uint32_t address, const uint32_t words[4], uint8_t lanes) {
        auto& sp = Vu1Fast::spare(v);
        if (v.m_xgkick.active && v.m_activeVuData && v.m_activeVuDataSize >= 16u) {
            const uint64_t due = (uint64_t(v.m_xgkick.cycleCredit) + (v.m_cycle - sp.xgLast)) / 2u;
            if (due) {
                const uint32_t nq = v.m_activeVuDataSize / 16u;
                const uint32_t start = (v.m_xgkick.sourceAddress + v.m_xgkick.copiedBytes) / 16u;
                if (Vu1Ring::distance(address / 16u, start, nq) < due)
                    v.progressXgkick();
            }
        }
        sp.storeReady = v.m_cycle + 1u;
        if (v.m_activeVuData && address + 16u <= v.m_activeVuDataSize) {
            auto* dst = reinterpret_cast<uint32_t*>(v.m_activeVuData + address);
            if (lanes == 15u)
                std::memcpy(dst, words, 16u);
            else
                for (uint32_t c = 0; c < 4u; ++c)
                    if (lanes & (8u >> c))
                        dst[c] = words[c];
        }
    }

    // The frequent lower-unit instructions, copied from VU1Interpreter::execLower (ps2_vu1_lower.cpp) so that they
    // inline and fold for a constant instruction word; everything else calls execLower.
    static PS2_VU_FORCEINLINE void lower(VU1Interpreter& v, const Dec& d, Ctx& c) {
        auto& m_state = v.m_state;
        const uint32_t instr = d.lower;
        if (instr == 0x00000000u || instr == 0x8000033Cu) // NOP
            return;

        const uint8_t opHi = static_cast<uint8_t>((instr >> 25) & 0x7F);
        const uint32_t pcMask = v.m_unit == VU1Interpreter::Unit::VU1 ? 0x3FFFu : 0x0FFFu;
        uint8_t* const vuData = c.vuData;
        const uint32_t dataSize = c.dataSize;

        switch (opHi) {
            case 0x00: // LQ
            {
                uint8_t it = FT(instr);
                uint8_t is = VIS(instr);
                uint8_t dest = (instr >> 21) & 0xF;
                int16_t imm = IMM11(instr);
                uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize) {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    Vu1Simd::writeLanes(m_state.vf[it], tmp, dest);
                }
                return;
            }
            case 0x01: // SQ
            {
                uint8_t is = FS(instr);
                uint8_t it = VIT(instr);
                uint8_t dest = (instr >> 21) & 0xF;
                int16_t imm = IMM11(instr);
                uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[it] + imm)) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize) {
                    uint32_t words[4]{};
                    std::memcpy(words, m_state.vf[is], sizeof(words));
                    store(v, addr, words, dest);
                }
                return;
            }
            case 0x04: // ILW
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                uint8_t dest = (instr >> 21) & 0xF;
                int16_t imm = IMM11(instr);
                uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize) {
                    int comp = 0;
                    if (dest & 0x8)
                        comp = 0;
                    else if (dest & 0x4)
                        comp = 1;
                    else if (dest & 0x2)
                        comp = 2;
                    else
                        comp = 3;
                    uint32_t val;
                    std::memcpy(&val, vuData + addr + comp * 4, 4);
                    if (it != 0)
                        m_state.vi[it] = (int32_t)(int16_t)(val & 0xFFFF);
                }
                return;
            }
            case 0x05: // ISW
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                uint8_t dest = (instr >> 21) & 0xF;
                int16_t imm = IMM11(instr);
                uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize) {
                    const uint32_t val = static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[it] & 0xFFFF));
                    const uint32_t words[4] = {val, val, val, val};
                    store(v, addr, words, dest);
                }
                return;
            }
            case 0x08: // IADDIU
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
                if (it != 0)
                    m_state.vi[it] = (int16_t)(m_state.vi[is] + imm);
                return;
            }
            case 0x09: // ISUBIU
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
                if (it != 0)
                    m_state.vi[it] = (int16_t)(m_state.vi[is] - imm);
                return;
            }
            case 0x20: // B
            {
                int16_t imm = IMM11(instr);
                uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                m_state.branchPending = true;
                m_state.branchTarget = target;
                m_state.branchDelay = 1;
                return;
            }
            case 0x28: // IBEQ
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) == (int16_t)readBranchVi(v, it)) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x29: // IBNE
            {
                uint8_t it = VIT(instr);
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) != (int16_t)readBranchVi(v, it)) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x2C: // IBLTZ
            {
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) < 0) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x2D: // IBGTZ
            {
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) > 0) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x2E: // IBLEZ
            {
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) <= 0) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x2F: // IBGEZ
            {
                uint8_t is = VIS(instr);
                int16_t imm = IMM11(instr);
                if ((int16_t)readBranchVi(v, is) >= 0) {
                    uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
                    m_state.branchPending = true;
                    m_state.branchTarget = target;
                    m_state.branchDelay = 1;
                }
                return;
            }
            case 0x40: {
                const uint8_t funct = instr & 0x3Fu;
                const uint8_t vfT = FT(instr);
                const uint8_t vfS = FS(instr);
                const uint8_t viT = VIT(instr);
                const uint8_t viS = VIS(instr);
                const uint8_t viD = VID(instr);
                const uint8_t dest = (instr >> 21) & 0xF;

                switch (funct) {
                    case 0x30: // IADD
                        if (viD != 0)
                            m_state.vi[viD] = (int16_t)(m_state.vi[viS] + m_state.vi[viT]);
                        return;
                    case 0x31: // ISUB
                        if (viD != 0)
                            m_state.vi[viD] = (int16_t)(m_state.vi[viS] - m_state.vi[viT]);
                        return;
                    case 0x32: // IADDI
                    {
                        int16_t imm5 = (int16_t)((int32_t)((instr >> 6) & 0x1F) << 27 >> 27);
                        if (viT != 0)
                            m_state.vi[viT] = (int16_t)(m_state.vi[viS] + imm5);
                        return;
                    }
                    case 0x34: // IAND
                        if (viD != 0)
                            m_state.vi[viD] = m_state.vi[viS] & m_state.vi[viT];
                        return;
                    case 0x35: // IOR
                        if (viD != 0)
                            m_state.vi[viD] = m_state.vi[viS] | m_state.vi[viT];
                        return;
                    case 0x3C:
                    case 0x3D:
                    case 0x3E:
                    case 0x3F: {
                        const uint8_t funct2 = (uint8_t)((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
                        switch (funct2) {
                            case 0x30: // MOVE
                            {
                                float tmp[4];
                                std::memcpy(tmp, m_state.vf[vfS], 16);
                                Vu1Simd::writeLanes(m_state.vf[vfT], tmp, dest);
                                return;
                            }
                            case 0x31: // MR32
                            {
                                float tmp[4] = {m_state.vf[vfS][1], m_state.vf[vfS][2], m_state.vf[vfS][3],
                                                m_state.vf[vfS][0]};
                                Vu1Simd::writeLanes(m_state.vf[vfT], tmp, dest);
                                return;
                            }
                            case 0x34: // LQI
                            {
                                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viS]) * 16u;
                                addr &= (dataSize - 1);
                                if (addr + 16 <= dataSize) {
                                    float tmp[4];
                                    std::memcpy(tmp, vuData + addr, 16);
                                    Vu1Simd::writeLanes(m_state.vf[vfT], tmp, dest);
                                }
                                if (viS != 0)
                                    m_state.vi[viS] = (int16_t)(m_state.vi[viS] + 1);
                                return;
                            }
                            case 0x3B: // WAITQ: readiness handled by step before issue
                                return;
                            case 0x3C: // MTIR
                            {
                                const uint32_t component = (instr >> 21) & 3u;
                                uint32_t bits;
                                std::memcpy(&bits, &m_state.vf[vfS][component], 4);
                                if (viT != 0)
                                    m_state.vi[viT] = static_cast<int16_t>(bits & 0xffffu);
                                return;
                            }
                            case 0x3D: // MFIR
                            {
                                const int32_t value = static_cast<int16_t>(m_state.vi[viS] & 0xffffu);
                                float result[4];
                                for (unsigned lane = 0; lane < 4; ++lane)
                                    std::memcpy(&result[lane], &value, 4);
                                Vu1Simd::writeLanes(m_state.vf[vfT], result, dest);
                                return;
                            }
                            case 0x35: // SQI
                            {
                                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viT]) * 16u;
                                addr &= (dataSize - 1);
                                if (addr + 16 <= dataSize) {
                                    uint32_t words[4]{};
                                    std::memcpy(words, m_state.vf[vfS], sizeof(words));
                                    store(v, addr, words, dest);
                                }
                                if (viT != 0)
                                    m_state.vi[viT] = (int16_t)(m_state.vi[viT] + 1);
                                return;
                            }
                            default:
                                break;
                        }
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }
        v.execLower(instr, vuData, dataSize, *c.gs, c.memory, d.upper);
    }

    // Everything a pair does between its issue and the bookkeeping of the clock: both units, the shadowed upper
    // write-back and the delayed branch read of a VI register. Shared by step() (the exact pair-at-a-time path) and by
    // the block-level code (aot/ps2_vu1_blocks.h), which supplies a statically scheduled v.m_cycle instead of the
    // run-time stall loop.
    template <bool BatchFmac = false, uint8_t NormalInputs = 0>
    static PS2_VU_FORCEINLINE void execPair(VU1Interpreter& v, const Dec& decoded, Ctx& c,
                                            Vu1FlagBatch* batch = nullptr) {
        auto& m_state = v.m_state;
        uint8_t writtenVi = 0u;
        int32_t oldVi = 0;
        if ((decoded.lowerUsage.viWrite & 0xFFFEu) != 0u) {
            writtenVi =
                static_cast<uint8_t>(std::countr_zero(static_cast<uint32_t>(decoded.lowerUsage.viWrite & 0xFFFEu)));
            oldVi = m_state.vi[writtenVi];
        }

        const VfAccess upperWrite = decoded.upperUsage.vfWrite;
        const bool hasUpperWrite = upperWrite.reg != 0u;
        // The upper unit writes its result straight into the register file, unless the lower unit of the same pair
        // reads or writes that register (upperVfShadowReg): then it computes into a private buffer that is applied
        // after the lower unit has run, so the lower unit sees the old value. (ACC is never touched by the lower unit.)
        alignas(16) float upperVfOut[4];
        float* upperDst = upperVfOut;
        if (decoded.fmKind != 0xFFu) {
            if (hasUpperWrite) {
                if (decoded.upperVfShadowReg == 0u)
                    upperDst = m_state.vf[upperWrite.reg];
                else
                    std::memcpy(upperVfOut, m_state.vf[upperWrite.reg], sizeof(upperVfOut));
            }
            // Upper NOPs do not consume either destination pointer. In particular,
            // store-only packet blocks need no upper-unit scratch setup per pair.
            v.m_upperDst = upperDst;
            v.m_upperAccDst = m_state.acc;
        }

        if (decoded.iBit) {
            if (decoded.fmKind == 0xFFu) {
            } else if (decoded.fmKind != 0u)
                upperFmac<BatchFmac, NormalInputs>(v, decoded, batch);
            else
                upperOther(v, decoded);
            float immediate = 0.0f;
            std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
            m_state.i = VU1Interpreter::normalizeOperand(immediate);
        } else {
            if (decoded.fmKind == 0xFFu) {
            } else if (decoded.fmKind != 0u)
                upperFmac<BatchFmac, NormalInputs>(v, decoded, batch);
            else
                upperOther(v, decoded);
            if (decoded.suppressedLowerVf != 0u) {
                // The lower write to the register the upper unit writes is dropped.
                float suppressed[4];
                std::memcpy(suppressed, m_state.vf[decoded.suppressedLowerVf], sizeof(suppressed));
                lower(v, decoded, c);
                std::memcpy(m_state.vf[decoded.suppressedLowerVf], suppressed, sizeof(suppressed));
            } else {
                lower(v, decoded, c);
            }
        }

        v.m_viBranchBackupValid = false;

        // VF / ACC / VI results are interlocked: the register file is updated at issue and only the stall timing
        // (markPairWrites) is modelled. The lower unit executed first (above), so it saw the old upper-unit registers.
        if (upperDst == upperVfOut && hasUpperWrite)
            Vu1Simd::writeLanes(m_state.vf[upperWrite.reg], upperVfOut, upperWrite.lanes);

        if (writtenVi != 0u && decoded.lowerUsage.delaysNextBranchRead) {
            v.m_viBranchBackupValue = oldVi;
            v.m_viBranchBackupReg = writtenVi;
            v.m_viBranchBackupValid = true;
        }
    }

    static PS2_VU_FORCEINLINE void resetZeroRegs(VU1Interpreter& v) {
        auto& m_state = v.m_state;
        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        m_state.vi[0] = 0;
    }

    // Executes the pair at v.m_state.pc. Returns false when the run must stop (budget exhausted, reserved instruction,
    // end of program).
    static PS2_VU_FORCEINLINE bool step(VU1Interpreter& v, const Dec& decoded, Ctx& c) {
        auto& m_state = v.m_state;
        if (decoded.upperUsage.reserved || decoded.lowerUsage.reserved) {
            v.reportReservedInstruction(decoded.upperUsage.reserved,
                                        decoded.upperUsage.reserved ? decoded.upper : decoded.lower);
            return false;
        }

        for (;;) {
            if (decoded.lowerUsage.pipeline == VU1Interpreter::PipelineXgkick)
                Vu1Fast::xgkickCatchUp(v); // a new XGKICK waits for the previous transfer
            uint64_t readyCycle = Vu1Fast::regsBusy(Vu1Fast::spare(v), decoded) ? v.m_cycle + 1u : v.m_cycle;
            if (decoded.needsExtra)
                readyCycle = std::max(readyCycle, v.calculatePairReadyCycle(decoded));
            if (readyCycle <= v.m_cycle)
                break;
            if (readyCycle >= c.budgetEnd) {
                v.advanceTo(c.budgetEnd);
                break;
            }
            v.advanceTo(readyCycle);
        }
        if (v.m_cycle >= c.budgetEnd)
            return false;

        ++c.pairs;
        execPair(v, decoded, c);
        Vu1Fast::scheduleWrites(Vu1Fast::spare(v), decoded, v.m_cycle);
        resetZeroRegs(v);

        uint32_t nextPc = m_state.pc + 8u;
        if (nextPc >= c.codeSize)
            nextPc = 0u;
        m_state.pc = nextPc;

        if (m_state.branchPending) {
            if (m_state.branchDelay == 0u) {
                m_state.pc = m_state.branchTarget & microMask(v);
                m_state.branchPending = false;
            } else {
                --m_state.branchDelay;
            }
        }

        const bool dHalt = decoded.dBit && m_state.dBitEnabled;
        const bool tHalt = decoded.tBit && m_state.tBitEnabled;
        const bool haltBit = dHalt || tHalt;
        const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == VU1Interpreter::PipelineBranch;

        if (m_state.haltAfterDelaySlot) {
            m_state.stoppedByD = v.m_pendingHaltD;
            m_state.stoppedByT = v.m_pendingHaltT;
            c.programEnded = true;
        } else if (m_state.ebit)
            c.programEnded = true;
        else if (haltBit && !haltBranch) {
            m_state.stoppedByD = dHalt;
            m_state.stoppedByT = tHalt;
            c.programEnded = true;
        } else if (decoded.eBit)
            m_state.ebit = true;
        else if (haltBranch) {
            m_state.haltAfterDelaySlot = true;
            v.m_pendingHaltD = dHalt;
            v.m_pendingHaltT = tHalt;
        }

        advanceCycle(v);
        return !c.programEnded;
    }

    // ------------------------------------------------------------------------------------------------------------------
    // Block-level execution support (aot/ps2_vu1_blocks.h). A block is a straight run of pairs whose issue cycles were
    // computed at build time for the case that no register the block reads (before writing it) or writes is still busy
    // when the block is entered and that no other pipeline (Q/P/XGKICK/...) is active. Under those entry conditions the
    // run-time stall wait, the register-busy bookkeeping and the clock of every single pair are replaced by constants
    // and by one update of the busy state at the end of the block. Anything else takes the exact pair-at-a-time path
    // (step()).
    // ------------------------------------------------------------------------------------------------------------------
    struct BlockEvent {
        uint8_t off; // cycles after the end of the block at which the registers become ready
        uint64_t lo, hi;
        uint32_t va;
    };

    // Busy registers of the entry state that a pair of the block reads at its (static) issue cycle 0..2: ready by then?
    static PS2_VU_NOINLINE bool blockReadsReady(const VU1Interpreter& v, const uint64_t* rLo, const uint64_t* rHi,
                                                const uint32_t* rVa) {
        const Vu1Fast::Spare& sp = Vu1Fast::spare(v);
        uint64_t cl = 0, ch = 0;
        uint32_t cv = 0;
        for (uint32_t t = 0; t < 3; ++t) {
            if (t > 0) {
                const Vu1Fast::Spare::Busy& slot = sp.clearAt[(v.m_cycle + t) & 15u];
                cl |= slot.lo;
                ch |= slot.hi;
                cv |= slot.va;
            }
            if (((sp.pending.lo & ~cl & rLo[t]) | (sp.pending.hi & ~ch & rHi[t]) | (sp.pending.va & ~cv & rVa[t])) !=
                0u)
                return false;
        }
        return true;
    }

    // Q-safe blocks can defer FDIV retirement to their exit. Q/status-sensitive
    // blocks use exact timed retirement before each pair when enabled. All
    // other pending pipelines retain the pair-at-a-time fallback.
    static PS2_VU_FORCEINLINE bool blockCanEnter(const VU1Interpreter& v, const Ctx& c, uint32_t cycles, bool qSafe,
                                                 uint64_t eLo, uint64_t eHi, uint32_t eVa, const uint64_t* rLo,
                                                 const uint64_t* rHi, const uint32_t* rVa) {
        const auto& st = v.m_state;
        const Vu1Fast::Spare& sp = Vu1Fast::spare(v);
        if (v.m_minReady != ~0ull && !(v.m_fdiv.valid && (qSafe || timedQEnabled()) &&
                                       (v.m_efuMask | v.m_storeMask | v.m_vfMask | v.m_viMask | v.m_accMask) == 0u))
            return false;
        if (st.ebit || st.haltAfterDelaySlot || st.branchPending || c.budgetEnd - v.m_cycle <= cycles)
            return false;
        if (((sp.pending.lo & eLo) | (sp.pending.hi & eHi) | (sp.pending.va & eVa)) == 0u)
            return true;
        return blockReadsReady(v, rLo, rHi, rVa);
    }

    // One pair of a block, issued at the statically known cycle.
    template <bool BatchFmac = false, bool TimedQ = false, uint8_t NormalInputs = 0>
    static PS2_VU_FORCEINLINE void blockPair(VU1Interpreter& v, const Dec& d, Ctx& c, uint64_t cycle, uint32_t pc,
                                             Vu1FlagBatch* batch = nullptr) {
        v.m_cycle = cycle;
        if constexpr (TimedQ) {
            if (v.m_fdiv.valid && v.m_fdiv.readyCycle <= cycle) {
                v.m_cycle = v.m_fdiv.readyCycle;
                v.commitReadyPipelines();
                v.m_cycle = cycle;
            }
        }
        if (d.lowerUsage.pipeline == VU1Interpreter::PipelineBranch)
            v.m_state.pc = pc; // branch targets are relative to the pc of the branch
        execPair<BatchFmac, NormalInputs>(v, d, c, batch);
        resetZeroRegs(v);
    }

    template <size_t N>
    static PS2_VU_FORCEINLINE void blockExit(VU1Interpreter& v, Ctx& c, uint64_t base, uint32_t cycles, uint32_t pairs,
                                             uint32_t nextPc, bool branchEnd, const BlockEvent (&ev)[N],
                                             uint32_t evCount) {
        auto& st = v.m_state;
        Vu1Fast::Spare& sp = Vu1Fast::spare(v);
        v.m_cycle = base + cycles;
        st.cycles = v.m_cycle;
        // the busy registers of the entry state that become ready while the block runs
        Vu1Fast::retireBlockEntry(sp, base, cycles);
        // registers written by the block that are still busy at its end
        for (uint32_t e = 0; e < evCount; ++e) {
            Vu1Fast::installBlockWrite(sp, v.m_cycle, cycles, ev[e].off, ev[e].lo, ev[e].hi, ev[e].va);
        }
        if (v.m_cycle >= v.m_minReady) {
            // Commit a deferred FDIV result as of its own cycle.
            const uint64_t now = v.m_cycle;
            v.m_cycle = v.m_fdiv.readyCycle;
            v.commitReadyPipelines();
            v.m_cycle = now;
        }
        c.pairs += pairs;
        if (branchEnd && st.branchPending) {
            st.branchDelay = 0u;
            st.pc = st.branchTarget & microMask(v);
            st.branchPending = false;
        } else
            st.pc = nextPc >= c.codeSize ? 0u : nextPc;
    }

    // Statistics: why a block was not entered (first failing condition of blockCanEnter).
    static PS2_VU_NOINLINE void blockWhy(VU1Interpreter& v, const Ctx& c, uint32_t cycles, bool qSafe,
                                         const uint64_t* rLo, const uint64_t* rHi, const uint32_t* rVa, uint64_t wLo,
                                         uint64_t wHi, uint32_t wVa) {
        const auto& st = v.m_state;
        Vu1Fast::Spare& sp = Vu1Fast::spare(v);
        uint32_t why;
        const bool pendingBlocked =
            v.m_minReady != ~0ull && !(v.m_fdiv.valid && (qSafe || timedQEnabled()) &&
                                       (v.m_efuMask | v.m_storeMask | v.m_vfMask | v.m_viMask | v.m_accMask) == 0u);
        if (pendingBlocked)
            why = v.m_fdiv.valid ? 2u : 3u; // Q pending / other
        else if (st.ebit || st.haltAfterDelaySlot || st.branchPending)
            why = 4u;
        else if (c.budgetEnd - v.m_cycle <= cycles)
            why = 5u;
        else {
            uint64_t cl = 0, ch = 0;
            uint32_t cv = 0;
            bool readsOk = true;
            for (uint32_t t = 0; t < 3; ++t) {
                if (t > 0) {
                    const Vu1Fast::Spare::Busy& slot = sp.clearAt[(v.m_cycle + t) & 15u];
                    cl |= slot.lo;
                    ch |= slot.hi;
                    cv |= slot.va;
                }
                if (((sp.pending.lo & ~cl & rLo[t]) | (sp.pending.hi & ~ch & rHi[t]) |
                     (sp.pending.va & ~cv & rVa[t])) != 0u)
                    readsOk = false;
            }
            const bool writesOk = ((sp.pending.lo & wLo) | (sp.pending.hi & wHi) | (sp.pending.va & wVa)) == 0u;
            why = !readsOk ? 6u : !writesOk ? 7u : 8u;
        }
        ++sp.why[why];
    }

    static PS2_VU_FORCEINLINE void finishFlagBatch(VU1Interpreter& v, const Vu1FlagBatch& batch) {
        Vu1Fast::settleFlags(v); // older entries precede every batched result
        batch.apply(v.m_state.mac, v.m_state.status);
    }

    static PS2_VU_FORCEINLINE bool nativeContinue(const VU1Interpreter& v, const Ctx& c) {
        return v.m_cycle < c.budgetEnd && !v.m_stopRequested;
    }

    static PS2_VU_FORCEINLINE uint64_t blockBase(const VU1Interpreter& v) { return v.m_cycle; }
    static PS2_VU_FORCEINLINE void blockCount(VU1Interpreter& v, bool fast, uint32_t pairs) {
        Vu1Fast::Spare& sp = Vu1Fast::spare(v);
        if (fast) {
            ++sp.blkFast;
            sp.blkPairs += pairs;
        } else
            ++sp.blkSlow;
    }
};
