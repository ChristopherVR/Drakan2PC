// Functional execution of the game's fixed VU1 microcode; see ../ps2_vu1_func.h.
//
// The image is split at compile time into blocks: a block starts at a leader (MSCAL entry, branch target, the pair
// after a branch delay slot, the MSCNT resume point after an E bit) and runs to the next leader, or to the delay slot
// of a branch / E bit. Every block is one function in which each pair is specialised on its decoded instruction
// words. Semantics follow the exact engine (Vu1Step::execPair, VU1Interpreter::execLower); only timing differs:
//  * one cycle per pair, plus the stalls of DIV/WAITQ on a pending division and of XGKICK on a running transfer;
//  * Q and P results become visible `latency` cycles after issue, MAC and CLIP results 4 cycles after issue;
//  * XGKICK sends its packet at issue (the image never writes a buffer while it is being transferred: checked by the
//    DRAKAN_VU_BENCH replay against the reference interpreter);
//  * status flags are not maintained.
//
// Loops whose body is one block (a backward branch to the block's own leader) can also run several iterations at once
// (`lanes` below, DRAKAN_VU_LANES): see the comment there.
#include "../ps2_vu1_func.h"
#include "../ps2_vu1_decode.h"
#include "../ps2_vu1_ring.h"
#include "../ps2_vu1_simd.h"
#include "ps2_vu1_lanes.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "ps2_vu1_image.inc"

// Private access to VU1Interpreter (Vu1Fast is its friend; a nested class shares that).
struct Vu1Fast::Func {
    // Gathers the GIF packet at `qword` of the VU data memory ring into the PATH1 buffer and sends it like
    // VU1Interpreter::finishXgkick. Returns its size in bytes (0 for a malformed tag).
    static uint32_t sendPacket(VU1Interpreter& v, const Vu1Step::Ctx& c, const uint8_t* data, uint32_t mask,
                               uint32_t qword);
    static bool canEnter(const VU1Interpreter& v);
    static Vu1Func::Result run(VU1Interpreter& v, Vu1Step::Ctx& c);
};

namespace {
using Dec = Vu1Step::Dec;
constexpr uint32_t kPairs = kVu1ImagePairs;
constexpr uint32_t kStop = 0xFFFFFFFFu;
constexpr uint32_t kFlagLatency = 4; // VU1Interpreter::kFmacLatency

constexpr Dec decodeAt(uint32_t i) {
    return Vu1Decode::decodePair(static_cast<uint32_t>(kVu1Image[i]), static_cast<uint32_t>(kVu1Image[i] >> 32), true);
}

// ---- instruction classes
enum Up : uint8_t { U_NOP, U_FMAC, U_MAX, U_MINI, U_ITOF, U_FTOI, U_ABS, U_CLIP, U_UNSUPPORTED };
enum Lo : uint8_t {
    L_NOP,
    L_LOI,
    L_LQ,
    L_SQ,
    L_ILW,
    L_ISW,
    L_IADDIU,
    L_ISUBIU,
    L_FMAND,
    L_FCGET,
    L_B,
    L_IBEQ,
    L_IBNE,
    L_IBLTZ,
    L_IBGTZ,
    L_IBLEZ,
    L_IBGEZ,
    L_IADD,
    L_ISUB,
    L_IADDI,
    L_IAND,
    L_IOR,
    L_MOVE,
    L_MR32,
    L_LQI,
    L_SQI,
    L_DIV,
    L_WAITQ,
    L_MTIR,
    L_MFIR,
    L_ISWR,
    L_MFP,
    L_XGKICK,
    L_ERLENG,
    L_UNSUPPORTED
};

constexpr Up upClass(const Dec& d) {
    if (d.fmKind == 0xFFu)
        return U_NOP;
    if (d.fmKind != 0u)
        return DEST(d.upper) == 0u ? U_NOP : U_FMAC; // no write, no flags (Vu1Step::upperFmac)
    const uint32_t instr = d.upper;
    const uint8_t op = static_cast<uint8_t>(instr & 0x3Fu);
    if ((op >= 0x10u && op <= 0x13u) || op == 0x1Du || op == 0x2Bu)
        return U_MAX;
    if ((op >= 0x14u && op <= 0x17u) || op == 0x1Fu || op == 0x2Fu)
        return U_MINI;
    if (op >= 0x3Cu) {
        const uint8_t special = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
        if (special >= 0x10u && special <= 0x13u)
            return U_ITOF;
        if (special >= 0x14u && special <= 0x17u)
            return U_FTOI;
        if (special == 0x1Du)
            return U_ABS;
        if (special == 0x1Fu)
            return U_CLIP;
    }
    return U_UNSUPPORTED;
}

constexpr Lo loClass(const Dec& d) {
    if (d.iBit)
        return L_LOI;
    const uint32_t w = d.lower;
    if (w == 0u || w == 0x8000033Cu)
        return L_NOP;
    switch ((w >> 25) & 0x7Fu) {
        case 0x00:
            return L_LQ;
        case 0x01:
            return L_SQ;
        case 0x04:
            return L_ILW;
        case 0x05:
            return L_ISW;
        case 0x08:
            return L_IADDIU;
        case 0x09:
            return L_ISUBIU;
        case 0x1A:
            return L_FMAND;
        case 0x1C:
            return L_FCGET;
        case 0x20:
            return L_B;
        case 0x28:
            return L_IBEQ;
        case 0x29:
            return L_IBNE;
        case 0x2C:
            return L_IBLTZ;
        case 0x2D:
            return L_IBGTZ;
        case 0x2E:
            return L_IBLEZ;
        case 0x2F:
            return L_IBGEZ;
        case 0x40:
            switch (w & 0x3Fu) {
                case 0x30:
                    return L_IADD;
                case 0x31:
                    return L_ISUB;
                case 0x32:
                    return L_IADDI;
                case 0x34:
                    return L_IAND;
                case 0x35:
                    return L_IOR;
                case 0x3C:
                case 0x3D:
                case 0x3E:
                case 0x3F:
                    switch ((w & 0x3u) | ((w >> 4) & 0x7Cu)) {
                        case 0x30:
                            return L_MOVE;
                        case 0x31:
                            return L_MR32;
                        case 0x34:
                            return L_LQI;
                        case 0x35:
                            return L_SQI;
                        case 0x38:
                            return L_DIV;
                        case 0x3B:
                            return L_WAITQ;
                        case 0x3C:
                            return L_MTIR;
                        case 0x3D:
                            return L_MFIR;
                        case 0x3F:
                            return L_ISWR;
                        case 0x64:
                            return L_MFP;
                        case 0x6C:
                            return L_XGKICK;
                        case 0x73:
                            return L_ERLENG;
                        default:
                            return L_UNSUPPORTED;
                    }
                default:
                    return L_UNSUPPORTED;
            }
        default:
            return L_UNSUPPORTED;
    }
}

constexpr bool isBranch(Lo l) {
    return l >= L_B && l <= L_IBGEZ;
}

constexpr bool supported(const Dec& d) {
    return upClass(d) != U_UNSUPPORTED && loClass(d) != L_UNSUPPORTED && !d.dBit && !d.tBit && !d.upperUsage.reserved &&
           !d.lowerUsage.reserved;
}

constexpr uint32_t branchTarget(uint32_t i) {
    return static_cast<uint32_t>(static_cast<int32_t>(i) + 1 + IMM11(static_cast<uint32_t>(kVu1Image[i])));
}

// Everything the run-time code needs from a pair, decoded once (the decoder is expensive to evaluate at compile
// time).
struct PairInfo {
    uint32_t upper = 0, lower = 0;
    Up up = U_NOP;
    Lo lo = L_NOP;
    bool iBit = false, eBit = false, ok = false;
    uint8_t fmKind = 0, fmB = 0;
    bool fmToAcc = false;
    uint8_t upperReg = 0, upperLanes = 0, shadowReg = 0, suppressed = 0;
    uint8_t writtenVi = 0;
    bool delays = false; // a branch in the next pair reads the old value of writtenVi
};
constexpr std::array<PairInfo, kPairs> buildInfo() {
    std::array<PairInfo, kPairs> t{};
    for (uint32_t i = 0; i < kPairs; ++i) {
        const Dec d = decodeAt(i);
        PairInfo& p = t[i];
        p.upper = d.upper;
        p.lower = d.lower;
        p.up = upClass(d);
        p.lo = loClass(d);
        p.iBit = d.iBit;
        p.eBit = d.eBit;
        p.ok = supported(d);
        p.fmKind = d.fmKind;
        p.fmB = d.fmB;
        p.fmToAcc = d.fmToAcc;
        p.upperReg = d.upperUsage.vfWrite.reg;
        p.upperLanes = d.upperUsage.vfWrite.lanes;
        p.shadowReg = d.upperVfShadowReg;
        p.suppressed = d.suppressedLowerVf;
        const uint32_t vi = d.lowerUsage.viWrite & 0xFFFEu;
        p.writtenVi = vi ? static_cast<uint8_t>(std::countr_zero(vi)) : 0u;
        p.delays = vi != 0u && d.lowerUsage.delaysNextBranchRead;
    }
    return t;
}
inline constexpr std::array<PairInfo, kPairs> kInfo = buildInfo();

// ---- block layout
enum Kind : uint8_t { K_NONE, K_FALL, K_BRANCH, K_EBIT };
struct Layout {
    bool leader[kPairs]{};
    bool entry[kPairs]{}; // where a run starts (MSCAL entries, the MSCNT resume points after E bits)
    uint32_t end[kPairs]{};
    uint8_t kind[kPairs]{};
};

constexpr Layout buildLayout() {
    Layout t{};
    t.leader[0] = t.entry[0] = true;
    t.leader[0x26e0u / 8u] = t.entry[0x26e0u / 8u] = true; // MSCAL entry points
    for (uint32_t i = 0; i < kPairs; ++i) {
        const PairInfo& d = kInfo[i];
        if (isBranch(d.lo)) {
            const uint32_t target = branchTarget(i);
            if (target < kPairs)
                t.leader[target] = true;
            if (i + 2 < kPairs)
                t.leader[i + 2] = true;
        }
        if (d.eBit && i + 2 < kPairs)
            t.leader[i + 2] = t.entry[i + 2] = true; // MSCNT resumes here
    }
    for (uint32_t s = 0; s < kPairs; ++s) {
        if (!t.leader[s])
            continue;
        uint32_t i = s;
        uint8_t kind = K_NONE;
        for (;;) {
            const PairInfo& d = kInfo[i];
            if (!d.ok)
                break;
            const bool br = isBranch(d.lo);
            if (br || d.eBit) {
                if (i + 1 >= kPairs)
                    break;
                const PairInfo& slot = kInfo[i + 1];
                if (!slot.ok || slot.eBit || isBranch(slot.lo) || (br && d.eBit))
                    break;
                kind = br ? K_BRANCH : K_EBIT;
                i += 2;
                break;
            }
            ++i;
            if (i >= kPairs || t.leader[i]) {
                kind = K_FALL;
                break;
            }
        }
        t.kind[s] = kind;
        t.end[s] = kind == K_NONE ? s : i;
        if (kind == K_NONE)
            t.entry[s] = false;
    }
    return t;
}
inline constexpr Layout kLayout = buildLayout();

// ---- known-normal VF/ACC lanes (forward dataflow over the pair graph)
// Operand normalization (denormal -> 0, exponent 255 -> FLT_MAX) only changes values that are not normal. FMAC,
// MAX/MINI, ITOF and ABS results are normal; loads, MFIR and FTOI results are not known to be. A lane is known normal
// at a pair when it is on every path from a run entry (where nothing is known). Q, I and P only ever hold normalized
// values (DIV, LOI and EFU results are clamped).
struct Norm {
    uint64_t lo = 0, hi = 0; // bit (reg & 15) * 4 + component; reg < 16 in lo
    uint8_t acc = 0;         // bit = component
    constexpr bool operator==(const Norm&) const = default;
};
constexpr uint32_t laneBit(uint8_t reg, uint32_t comp) {
    return (reg & 15u) * 4u + comp;
}
constexpr bool known(const Norm& n, uint8_t reg, uint32_t comp) {
    if (reg == 0)
        return true;
    const uint64_t word = reg < 16 ? n.lo : n.hi;
    return ((word >> laneBit(reg, comp)) & 1u) != 0;
}
constexpr void setKnown(Norm& n, uint8_t reg, uint32_t comp, bool value) {
    if (reg == 0)
        return;
    uint64_t& word = reg < 16 ? n.lo : n.hi;
    const uint64_t bit = 1ull << laneBit(reg, comp);
    word = value ? word | bit : word & ~bit;
}
// lanes: DEST encoding (bit 3 - c selects component c)
constexpr bool knownLanes(const Norm& n, uint8_t reg, uint8_t lanes) {
    for (uint32_t c = 0; c < 4; ++c)
        if ((lanes & (8u >> c)) && !known(n, reg, c))
            return false;
    return true;
}
constexpr Norm meet(const Norm& a, const Norm& b) {
    return {a.lo & b.lo, a.hi & b.hi, static_cast<uint8_t>(a.acc & b.acc)};
}

constexpr Norm transfer(uint32_t i, const Norm& in) {
    const PairInfo& d = kInfo[i];
    Norm out = in;
    if (!d.iBit) {
        const uint32_t w = d.lower;
        const uint8_t vt = FT(w), vs = FS(w), dest = (w >> 21) & 0xFu;
        if (vt != 0 && vt != d.suppressed)
            for (uint32_t c = 0; c < 4; ++c) {
                if (!(dest & (8u >> c)))
                    continue;
                if (d.lo == L_LQ || d.lo == L_LQI || d.lo == L_MFIR)
                    setKnown(out, vt, c, false);
                else if (d.lo == L_MOVE)
                    setKnown(out, vt, c, known(in, vs, c));
                else if (d.lo == L_MR32)
                    setKnown(out, vt, c, known(in, vs, (c + 1) & 3u));
                else if (d.lo == L_MFP)
                    setKnown(out, vt, c, true);
            }
    }
    if (d.up == U_FMAC && d.fmToAcc) {
        out.acc |= Vu1FmacDetail::kRev4[DEST(d.upper)];
    } else if (d.upperReg != 0 && d.up != U_NOP && d.up != U_CLIP) {
        const bool normal = d.up != U_FTOI;
        for (uint32_t c = 0; c < 4; ++c)
            if (d.upperLanes & (8u >> c))
                setKnown(out, d.upperReg, c, normal);
    }
    return out;
}

// Successors, over-approximated: every pair continues at the next one, and a branch delay slot also at the target.
struct NormTable {
    std::array<Norm, kPairs> in{};
};
constexpr NormTable buildNorm() {
    NormTable t{};
    const Norm top{~0ull, ~0ull, 0xF};
    for (auto& n : t.in)
        n = top;
    for (uint32_t i = 0; i < kPairs; ++i)
        if (kLayout.entry[i])
            t.in[i] = Norm{};
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t i = 0; i < kPairs; ++i) {
            const Norm out = transfer(i, t.in[i]);
            uint32_t succ[2];
            uint32_t count = 0;
            if (i + 1 < kPairs)
                succ[count++] = i + 1;
            if (i > 0 && isBranch(kInfo[i - 1].lo) && branchTarget(i - 1) < kPairs)
                succ[count++] = branchTarget(i - 1);
            for (uint32_t k = 0; k < count; ++k) {
                const Norm merged = meet(t.in[succ[k]], out);
                if (!(merged == t.in[succ[k]])) {
                    t.in[succ[k]] = merged;
                    changed = true;
                }
            }
        }
    }
    return t;
}
inline constexpr NormTable kNorm = buildNorm();

// FMAC results an FMAND may observe (the MAC history only holds these; others only update the final MAC).
constexpr std::array<bool, kPairs> buildMacObserved() {
    std::array<bool, kPairs> t{};
    for (uint32_t r = 0; r < kPairs; ++r) {
        if (kInfo[r].lo != L_FMAND)
            continue;
        // every pair within 16 pairs before r on any path (predecessors: p - 1, and branch delay slots targeting p);
        // breadth first, so each pair gets its shortest distance once
        std::array<uint8_t, kPairs> depth{};
        std::array<uint32_t, kPairs> queue{};
        uint32_t head = 0, tail = 0;
        queue[tail++] = r;
        depth[r] = 1;
        while (head < tail) {
            const uint32_t p = queue[head++];
            if (depth[p] >= 17)
                continue;
            const auto visit = [&](uint32_t q) {
                if (depth[q] == 0) {
                    depth[q] = static_cast<uint8_t>(depth[p] + 1);
                    queue[tail++] = q;
                }
            };
            if (p > 0)
                visit(p - 1);
            for (uint32_t b = 0; b + 1 < kPairs; ++b)
                if (isBranch(kInfo[b].lo) && branchTarget(b) == p)
                    visit(b + 1);
        }
        for (uint32_t i = 0; i < kPairs; ++i)
            if (depth[i] && kInfo[i].up == U_FMAC)
                t[i] = true;
    }
    return t;
}
inline constexpr std::array<bool, kPairs> kMacObserved = buildMacObserved();

// VU1Interpreter::normalizeOperand / normalizeResult (the value part): denormals -> signed zero, exponent 255 ->
// signed largest finite value.
PS2_VU_FORCEINLINE float clampVu(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    if (exponent == 0u)
        bits &= 0x80000000u;
    else if (exponent == 0xFFu)
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    std::memcpy(&value, &bits, 4);
    return value;
}

// ---- run state
struct State {
    VU1State& st;
    uint8_t* data;
    uint32_t mask;      // data size - 1 (power of two)
    uint64_t now;       // cycle of the first pair of the current block (pair K issues at now + K)
    uint64_t budgetEnd; // stop at a loop branch once reached
    uint64_t pairs = 0;
    uint32_t exitPc = 0; // pair index where the run left the compiled blocks
    bool finished = false, stopped = false;
    bool taken = false;        // branch decision of the block's branch pair
    uint32_t branchTarget = 0; // VU1State::branchTarget keeps the last taken target
    // VI value a branch in the next pair reads (the previous pair wrote the register)
    uint8_t bkReg = 0;
    int32_t bkVal = 0;
    // Q / P (FDIV / EFU) pending results
    bool qValid = false, pValid = false;
    float qValue = 0, pValue = 0;
    uint64_t qReady = 0, pReady = 0;
    // MAC / CLIP results become visible 4 cycles after issue: the last 4 per register and the value before them
    uint32_t mac[4]{}, clip[4]{};
    uint64_t macReady[4]{}, clipReady[4]{};
    uint32_t macCount = 0, clipCount = 0, macBase = 0, clipBase = 0;
    uint32_t lastMac = 0; // MAC of the most recent FMAC op (the final MAC register)
    uint32_t workingClip = 0;
    uint64_t kickDone = 0; // PATH1 busy until this cycle
    VU1Interpreter* v = nullptr;
    Vu1Step::Ctx* ctx = nullptr;
    // multi-iteration loops: scalar iterations to run before the next attempt, statistics
    uint32_t laneHold = 0;
    uint64_t laneIters = 0, laneSteps = 0, laneAborts = 0;
};

// Pair K of a block issues at s.now + K; a stall moves the block's base so that this stays true.
template <uint32_t K>
PS2_VU_FORCEINLINE uint64_t at(const State& s) {
    return s.now + K;
}
template <uint32_t K>
PS2_VU_FORCEINLINE void stallUntil(State& s, uint64_t ready) {
    if (s.now + K < ready)
        s.now = ready - K;
}

PS2_VU_FORCEINLINE void commitQ(State& s) {
    s.st.q = s.qValue;
    s.qValid = false;
}

// ---- several loop iterations at once
// A loop whose body is one block (pairs L..E-1, the last pair being the delay slot of a branch back to L) runs W
// consecutive iterations per step (ps2_vu1_lanes.h: W = 2 with AVX2, 4 with AVX-512); 128-bit lane j of every value
// belongs to iteration i + j. The body is analysed at compile time from the decoded image:
//  * Every operand component has a reaching definition: an earlier pair of the same iteration (distance 0), the last
//    writer of the previous iteration (distance 1: lane j reads lane j - 1 of that writer, lane 0 the register file at
//    the start of the step), or nothing in the body (invariant). Software-pipelined loops become plain lane-parallel
//    code this way. The operations are ordered topologically over these edges; a cycle (a real recurrence) rejects the
//    loop. VI registers may only be advanced by constants (IADDIU/ISUBIU/IADDI on themselves, LQI/SQI), so their value
//    in any iteration is affine.
//  * Q: one DIV; every reader sees the DIV of its own iteration (>= 7 pairs later) or of the previous one. The body is
//    at least 7 pairs long, so no DIV stalls; the step starts only if a pending Q is ready before the first Q access.
//  * MAC: FMAC results no FMAND observes. The final MAC is that of the body's last FMAC in the last iteration; when
//    the loop's exit iteration runs in lanes, the scalar fmac computes it for that lane. No CLIP, flag reads, XGKICK,
//    P, I writes, integer loads/stores or other VI arithmetic.
//  * FMAC results are the plain SSE results unless a component is not ordinary (Vu1Step::calculateFmac's fast path and
//    flagsKnownClasses, Vu1Lanes::fmacBad). Lanes that fail are recomputed by the scalar fmac.
//  * Memory: at run time, every load and store address of all iterations about to run must stay inside VU memory
//    without wrapping; a quadword stored by these iterations may only be loaded before its store in program order, and
//    no quadword is stored twice (same-stride streams are compared by residue). Stores are performed after each step.
// Run time: the iterations up to the loop exit (or the cycle budget), in steps of W and one masked partial step.
// Registers, VI, Q, MAC, branch state, cycle and pair counts end exactly as on the scalar blocks.
template <uint8_t Kind, uint8_t Dest>
__m128 fmac(__m128 vs, __m128 b, __m128 acc, uint32_t& mac);

namespace lanes {
constexpr uint32_t kMaxBody = 24;
constexpr uint32_t kMaxNodes = 2 * kMaxBody; // node id = 2 * pair + (0 upper, 1 lower)
constexpr uint8_t kAcc = 32, kQ = 33, kI = 34, kRegs = 35;
constexpr uint8_t kNone = 0xFF;

enum SrcKind : uint8_t { SK_NONE, SK_NODE, SK_INV, SK_VF0 };
struct Src {
    uint8_t kind = SK_NONE, node = 0, d = 0;
    constexpr bool operator==(const Src&) const = default;
};
struct Operand {
    uint8_t reg = 0;   // 0..31 VF, kAcc, kQ, kI
    uint8_t comps = 0; // components read (bit c = component c)
    bool norm = false; // normalized like the scalar engine's operand
    Src src[4]{};
};
enum NodeOp : uint8_t { N_NONE, N_FMAC, N_MAX, N_MINI, N_ITOF, N_FTOI, N_ABS, N_LQ, N_SQ, N_DIV, N_MOVE, N_MR32 };
struct Node {
    NodeOp op = N_NONE;
    uint8_t pair = 0;
    uint8_t nops = 0;
    Operand ops[3]{};
    uint8_t wreg = kNone, wcomps = 0; // register / components written
    uint8_t kind = 0, fmB = 0;        // FMAC kind; broadcast component of the second operand (0-3), else 4+
    uint8_t special = 0;              // ITOF / FTOI variant
    uint8_t vi = 0, dest = 0;         // memory: base VI register, stored / loaded components
    int16_t offset = 0;               // memory: immediate + increments of vi earlier in the iteration
    bool carry = false;               // last lane kept for the next step (distance-1 source or last writer)
};
enum Reason : uint8_t {
    R_ACCEPTED, // R_OK is a POSIX access() macro from unistd.h.
    R_NOT_LOOP,
    R_LENGTH,
    R_PAIR,
    R_UPPER,
    R_LOWER,
    R_MAC,
    R_VI,
    R_BRANCH,
    R_Q,
    R_CYCLE,
};
struct Plan {
    bool ok = false;
    uint8_t reason = R_NOT_LOOP;
    uint32_t n = 0;
    Node nodes[kMaxNodes]{};
    uint8_t order[kMaxNodes]{};
    uint32_t orderCount = 0;
    int16_t viStep[16]{};
    uint16_t viWritten = 0;
    uint8_t brOp = L_NOP, brS = 0, brT = 0;
    int16_t brPreS = 0, brPreT = 0;
    uint8_t slotVi = 0; // VI register the delay slot advances (0: none)
    int16_t slotPre = 0;
    uint8_t divNode = kNone, divPair = 0, firstQ = 0xFF;
    bool readsQ = false, qCommit = false;
    uint8_t last[kRegs][4]{};
    uint64_t invRegs = 0; // invariant registers read (bit = register id)
    // the final MAC comes from the last FMAC of the body: the not-taken iteration can run in lanes when that FMAC is a
    // node (it does not write VF00), its flags being computed for the last lane only
    uint8_t macNode = kNone;
    bool finalOk = true;
    uint8_t loads[kMaxNodes]{}, stores[kMaxNodes]{};
    uint32_t loadCount = 0, storeCount = 0;
};

constexpr bool selfLoop(uint32_t l) {
    return l < kPairs && kLayout.leader[l] && kLayout.kind[l] == K_BRANCH && kLayout.end[l] >= l + 3 &&
           branchTarget(kLayout.end[l] - 2) == l;
}

constexpr uint8_t firstComp(uint8_t comps) {
    for (uint8_t c = 0; c < 4; ++c)
        if (comps & (1u << c))
            return c;
    return 0;
}

constexpr Plan reject(Plan p, uint8_t why) {
    p.ok = false;
    p.reason = why;
    return p;
}

constexpr Plan buildPlan(uint32_t l) {
    Plan p{};
    if (!selfLoop(l))
        return p;
    const uint32_t n = kLayout.end[l] - l;
    p.n = n;
    if (n > kMaxBody)
        return reject(p, R_LENGTH);
    const uint32_t br = n - 2;
    uint32_t divCount = 0;
    // pass 1: operations, writes, VI increments
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t i = l + k;
        const PairInfo& d = kInfo[i];
        if (!d.ok || d.eBit || d.iBit || d.suppressed != 0)
            return reject(p, R_PAIR);
        Node& up = p.nodes[2 * k];
        Node& lo = p.nodes[2 * k + 1];
        up.pair = lo.pair = static_cast<uint8_t>(k);
        const uint32_t instr = d.upper;
        const uint8_t dest = DEST(instr), comps = Vu1FmacDetail::kRev4[dest];
        switch (d.up) {
            case U_NOP:
                break;
            case U_FMAC:
                if (kMacObserved[i])
                    return reject(p, R_MAC);
                if (d.fmToAcc || d.upperReg != 0) {
                    up.op = N_FMAC;
                    up.kind = d.fmKind;
                    up.fmB = d.fmB;
                    up.wreg = d.fmToAcc ? kAcc : d.upperReg;
                    up.wcomps = comps;
                }
                break;
            case U_MAX:
            case U_MINI:
            case U_ITOF:
            case U_FTOI:
            case U_ABS:
                if (d.upperReg != 0) {
                    up.op = d.up == U_MAX    ? N_MAX
                            : d.up == U_MINI ? N_MINI
                            : d.up == U_ITOF ? N_ITOF
                            : d.up == U_FTOI ? N_FTOI
                                             : N_ABS;
                    up.wreg = d.upperReg;
                    up.wcomps = Vu1FmacDetail::kRev4[d.upperLanes];
                    up.special = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
                }
                break;
            default:
                return reject(p, R_UPPER);
        }
        const uint32_t w = d.lower;
        const uint8_t vt = FT(w), vs = FS(w), it = VIT(w), is = VIS(w);
        const uint8_t ldest = Vu1FmacDetail::kRev4[(w >> 21) & 0xFu];
        if (isBranch(d.lo) != (k == br))
            return reject(p, R_BRANCH);
        switch (d.lo) {
            case L_NOP:
                break;
            case L_LQ:
            case L_LQI:
                lo.vi = is;
                lo.offset = d.lo == L_LQ ? IMM11(w) : 0;
                lo.dest = ldest;
                if (vt != 0) {
                    lo.op = N_LQ;
                    lo.wreg = vt;
                    lo.wcomps = ldest;
                }
                if (d.lo == L_LQI && is != 0) {
                    p.viStep[is] = static_cast<int16_t>(p.viStep[is] + 1);
                    p.viWritten |= static_cast<uint16_t>(1u << is);
                }
                break;
            case L_SQ:
            case L_SQI:
                lo.op = N_SQ;
                lo.vi = it;
                lo.offset = d.lo == L_SQ ? IMM11(w) : 0;
                lo.dest = ldest;
                if (d.lo == L_SQI && it != 0) {
                    p.viStep[it] = static_cast<int16_t>(p.viStep[it] + 1);
                    p.viWritten |= static_cast<uint16_t>(1u << it);
                }
                break;
            case L_DIV:
                lo.op = N_DIV;
                lo.wreg = kQ;
                lo.wcomps = 15;
                ++divCount;
                p.divNode = static_cast<uint8_t>(2 * k + 1);
                p.divPair = static_cast<uint8_t>(k);
                break;
            case L_MOVE:
            case L_MR32:
                if (vt != 0) {
                    lo.op = d.lo == L_MOVE ? N_MOVE : N_MR32;
                    lo.wreg = vt;
                    lo.wcomps = ldest;
                }
                break;
            case L_IADDIU:
            case L_ISUBIU:
            case L_IADDI: {
                if (it == 0)
                    break;
                if (it != is)
                    return reject(p, R_VI);
                const int16_t imm15 = static_cast<int16_t>((w & 0x7FFu) | ((w >> 10) & 0x7800u));
                const int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>(((w >> 6) & 0x1Fu) << 27) >> 27);
                const int16_t delta = d.lo == L_IADDIU ? imm15 : d.lo == L_ISUBIU ? static_cast<int16_t>(-imm15) : imm5;
                p.viStep[it] = static_cast<int16_t>(p.viStep[it] + delta);
                p.viWritten |= static_cast<uint16_t>(1u << it);
                break;
            }
            case L_B:
            case L_IBEQ:
            case L_IBNE:
            case L_IBLTZ:
            case L_IBGTZ:
            case L_IBLEZ:
            case L_IBGEZ:
                p.brOp = d.lo;
                p.brS = d.lo == L_B ? 0 : is;
                p.brT = (d.lo == L_IBEQ || d.lo == L_IBNE) ? it : 0;
                break;
            default:
                return reject(p, R_LOWER);
        }
        (void)vs;
    }
    if (divCount > 1 || (divCount == 1 && n < 7))
        return reject(p, R_Q);
    // the branch must read the current VI values (no VI write in the pair before it, none in the delay slot)
    if (kInfo[l + br - 1].delays &&
        (kInfo[l + br - 1].writtenVi == p.brS || (p.brT != 0 && kInfo[l + br - 1].writtenVi == p.brT)))
        return reject(p, R_BRANCH);
    if (kInfo[l + n - 1].delays) {
        // a VI write in the delay slot leaves the old value for a branch in the next block (State::bkReg)
        p.slotVi = kInfo[l + n - 1].writtenVi;
        if (!(p.viWritten & (1u << p.slotVi)))
            return reject(p, R_VI);
    }
    // VI value seen at pair k: start of the iteration + increments by earlier pairs
    const auto viPre = [&](uint8_t reg, uint32_t k) {
        int32_t sum = 0;
        for (uint32_t q = 0; q < k; ++q) {
            const PairInfo& d = kInfo[l + q];
            const uint32_t w = d.lower;
            if ((d.lo == L_LQI && VIS(w) == reg) || (d.lo == L_SQI && VIT(w) == reg))
                sum += 1;
            else if ((d.lo == L_IADDIU || d.lo == L_ISUBIU || d.lo == L_IADDI) && VIT(w) == reg && reg != 0) {
                const int16_t imm15 = static_cast<int16_t>((w & 0x7FFu) | ((w >> 10) & 0x7800u));
                const int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>(((w >> 6) & 0x1Fu) << 27) >> 27);
                sum += d.lo == L_IADDIU ? imm15 : d.lo == L_ISUBIU ? -imm15 : imm5;
            }
        }
        return static_cast<int16_t>(sum);
    };
    p.brPreS = viPre(p.brS, br);
    p.brPreT = viPre(p.brT, br);
    p.slotPre = viPre(p.slotVi, n - 1);
    for (uint32_t id = 0; id < 2 * n; ++id) {
        Node& nd = p.nodes[id];
        if (nd.op == N_LQ || nd.op == N_SQ)
            nd.offset = static_cast<int16_t>(nd.offset + viPre(nd.vi, nd.pair));
        if (nd.op == N_LQ)
            p.loads[p.loadCount++] = static_cast<uint8_t>(id);
        else if (nd.op == N_SQ)
            p.stores[p.storeCount++] = static_cast<uint8_t>(id);
    }
    // pass 2: operands (reaching definitions)
    const auto writes = [&](uint32_t id, uint8_t reg, uint8_t c) {
        const Node& w = p.nodes[id];
        return w.op != N_NONE && w.op != N_SQ && w.wreg == reg && (w.wcomps & (1u << c)) != 0;
    };
    const auto source = [&](uint8_t reg, uint8_t c, uint32_t k) {
        Src s{};
        if (reg == 0) {
            s.kind = SK_VF0;
            return s;
        }
        if (reg == kI) {
            s.kind = SK_INV;
            return s;
        }
        for (uint32_t id = 0; id < 2 * k; ++id)
            if (writes(id, reg, c)) {
                s.kind = SK_NODE;
                s.node = static_cast<uint8_t>(id);
                s.d = 0;
            }
        if (s.kind == SK_NODE)
            return s;
        for (uint32_t id = 2 * k; id < 2 * n; ++id)
            if (writes(id, reg, c)) {
                s.kind = SK_NODE;
                s.node = static_cast<uint8_t>(id);
                s.d = 1;
            }
        if (s.kind == SK_NONE)
            s.kind = SK_INV;
        return s;
    };
    bool qBad = false;
    const auto makeOperand = [&](uint32_t k, uint8_t reg, uint8_t comps, bool norm) {
        Operand o{};
        o.reg = reg;
        o.comps = comps;
        o.norm = norm;
        if (reg == kQ) {
            p.readsQ = true;
            if (p.firstQ > k)
                p.firstQ = static_cast<uint8_t>(k);
            Src s{};
            if (divCount == 0)
                s.kind = SK_INV;
            else if (k >= p.divPair + 7u) {
                s = Src{SK_NODE, p.divNode, 0};
                p.qCommit = true;
            } else if (k + n >= p.divPair + 7u)
                s = Src{SK_NODE, p.divNode, 1};
            else
                qBad = true;
            for (uint8_t c = 0; c < 4; ++c)
                o.src[c] = s;
            return o;
        }
        for (uint8_t c = 0; c < 4; ++c)
            if (comps & (1u << c))
                o.src[c] = source(reg, c, k);
        return o;
    };
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t i = l + k;
        const PairInfo& d = kInfo[i];
        const Norm& in = kNorm.in[i];
        Node& up = p.nodes[2 * k];
        Node& lo = p.nodes[2 * k + 1];
        const uint32_t instr = d.upper;
        const uint8_t dest = DEST(instr), comps = Vu1FmacDetail::kRev4[dest];
        const uint8_t ft = FT(instr), fs = FS(instr);
        if (up.op == N_FMAC) {
            up.ops[0] = makeOperand(k, fs, comps, !knownLanes(in, fs, dest));
            if (d.fmB <= 3)
                up.ops[1] = makeOperand(k, ft, static_cast<uint8_t>(1u << d.fmB), !known(in, ft, d.fmB));
            else if (d.fmB == 4)
                up.ops[1] = makeOperand(k, kQ, 15, false);
            else if (d.fmB == 5)
                up.ops[1] = makeOperand(k, kI, 15, false);
            else
                up.ops[1] = makeOperand(k, ft, comps, !knownLanes(in, ft, dest));
            up.nops = 2;
            if (d.fmKind >= 4) {
                const uint8_t want = Vu1FmacDetail::kRev4[dest];
                up.ops[2] = makeOperand(k, kAcc, comps, (in.acc & want) != want);
                up.nops = 3;
            }
        } else if (up.op == N_MAX || up.op == N_MINI) {
            const uint8_t op = static_cast<uint8_t>(instr & 0x3Fu);
            up.ops[0] = makeOperand(k, fs, comps, !knownLanes(in, fs, dest));
            if (op < 0x18u) {
                up.fmB = op & 3u;
                up.ops[1] = makeOperand(k, ft, static_cast<uint8_t>(1u << up.fmB), !known(in, ft, up.fmB));
            } else if (op == 0x1Du || op == 0x1Fu) {
                up.fmB = 5;
                up.ops[1] = makeOperand(k, kI, 15, false);
            } else {
                up.fmB = 6;
                up.ops[1] = makeOperand(k, ft, comps, !knownLanes(in, ft, dest));
            }
            up.nops = 2;
        } else if (up.op == N_ITOF) {
            up.ops[0] = makeOperand(k, fs, comps, false);
            up.nops = 1;
        } else if (up.op == N_FTOI || up.op == N_ABS) {
            up.ops[0] = makeOperand(k, fs, comps, !knownLanes(in, fs, dest));
            up.nops = 1;
        }
        const uint32_t w = d.lower;
        const uint8_t vs = FS(w), vt = FT(w);
        if (lo.op == N_SQ || lo.op == N_MOVE) {
            lo.ops[0] = makeOperand(k, vs, Vu1FmacDetail::kRev4[(w >> 21) & 0xFu], false);
            lo.nops = 1;
        } else if (lo.op == N_MR32) {
            const uint8_t ld = Vu1FmacDetail::kRev4[(w >> 21) & 0xFu];
            uint8_t rot = 0;
            for (uint8_t c = 0; c < 4; ++c)
                if (ld & (1u << c))
                    rot |= static_cast<uint8_t>(1u << ((c + 1) & 3u));
            lo.ops[0] = makeOperand(k, vs, rot, false);
            lo.nops = 1;
        } else if (lo.op == N_DIV) {
            const uint8_t fsf = (w >> 21) & 0x3, ftf = (w >> 23) & 0x3;
            lo.ops[0] = makeOperand(k, vs, static_cast<uint8_t>(1u << fsf), false);
            lo.ops[1] = makeOperand(k, vt, static_cast<uint8_t>(1u << ftf), false);
            lo.fmB = fsf;
            lo.special = ftf;
            lo.nops = 2;
            if (p.firstQ > k)
                p.firstQ = static_cast<uint8_t>(k);
        }
    }
    if (qBad)
        return reject(p, R_Q);
    // last writers (committed at the end), invariants, carried values
    for (uint8_t r = 0; r < kRegs; ++r)
        for (uint8_t c = 0; c < 4; ++c) {
            p.last[r][c] = kNone;
            for (uint32_t id = 0; id < 2 * n; ++id)
                if (writes(id, r, c))
                    p.last[r][c] = static_cast<uint8_t>(id);
            if (p.last[r][c] != kNone)
                p.nodes[p.last[r][c]].carry = true;
        }
    uint32_t indeg[kMaxNodes]{};
    for (uint32_t id = 0; id < 2 * n; ++id) {
        Node& nd = p.nodes[id];
        for (uint32_t o = 0; o < nd.nops; ++o)
            for (uint8_t c = 0; c < 4; ++c) {
                if (!(nd.ops[o].comps & (1u << c)))
                    continue;
                const Src s = nd.ops[o].src[c];
                if (s.kind == SK_INV)
                    p.invRegs |= 1ull << nd.ops[o].reg;
                else if (s.kind == SK_NODE) {
                    ++indeg[id];
                    if (s.d == 1)
                        p.nodes[s.node].carry = true;
                }
            }
    }
    if (p.divNode != kNone)
        p.nodes[p.divNode].carry = true;
    // topological order (Kahn, earliest body position first)
    bool done[kMaxNodes]{};
    for (;;) {
        uint32_t pick = kNone;
        for (uint32_t id = 0; id < 2 * n && pick == kNone; ++id)
            if (!done[id] && p.nodes[id].op != N_NONE && indeg[id] == 0)
                pick = id;
        if (pick == kNone)
            break;
        done[pick] = true;
        p.order[p.orderCount++] = static_cast<uint8_t>(pick);
        for (uint32_t id = 0; id < 2 * n; ++id) {
            const Node& nd = p.nodes[id];
            for (uint32_t o = 0; o < nd.nops; ++o)
                for (uint8_t c = 0; c < 4; ++c)
                    if ((nd.ops[o].comps & (1u << c)) && nd.ops[o].src[c].kind == SK_NODE &&
                        nd.ops[o].src[c].node == pick)
                        --indeg[id];
        }
    }
    for (uint32_t id = 0; id < 2 * n; ++id)
        if (p.nodes[id].op != N_NONE && !done[id])
            return reject(p, R_CYCLE);
    for (uint32_t k = 0; k < n; ++k)
        if (kInfo[l + k].up == U_FMAC) {
            p.finalOk = p.nodes[2 * k].op == N_FMAC;
            p.macNode = p.finalOk ? static_cast<uint8_t>(2 * k) : kNone;
        }
    p.ok = true;
    p.reason = R_ACCEPTED;
    return p;
}

template <uint32_t L>
inline constexpr Plan kPlan = buildPlan(L);

// results that are normal numbers or zero (FMAC: when the step is kept)
constexpr bool normalOut(NodeOp op) {
    return op == N_FMAC || op == N_MAX || op == N_MINI || op == N_ITOF || op == N_ABS || op == N_DIV;
}
// how each invariant register / carried node is read: bit 0 raw, bit 1 normalized
template <uint32_t L>
constexpr uint32_t invUse(uint32_t reg) {
    const Plan& p = kPlan<L>;
    uint32_t use = 0;
    for (uint32_t id = 0; id < 2 * p.n; ++id)
        for (uint32_t o = 0; o < p.nodes[id].nops; ++o) {
            const Operand& op = p.nodes[id].ops[o];
            for (uint32_t c = 0; c < 4; ++c)
                if ((op.comps & (1u << c)) && op.src[c].kind == SK_INV && op.reg == reg)
                    use |= op.norm ? 2u : 1u;
        }
    return use;
}
template <uint32_t L>
constexpr bool prevNormUsed(uint32_t node) {
    const Plan& p = kPlan<L>;
    if (!normalOut(p.nodes[node].op))
        return false;
    for (uint32_t id = 0; id < 2 * p.n; ++id)
        for (uint32_t o = 0; o < p.nodes[id].nops; ++o) {
            const Operand& op = p.nodes[id].ops[o];
            for (uint32_t c = 0; c < 4; ++c)
                if ((op.comps & (1u << c)) && op.norm && op.src[c].kind == SK_NODE && op.src[c].d == 1 &&
                    op.src[c].node == node)
                    return true;
        }
    return false;
}

uint32_t width() {
    static const uint32_t w = [] {
        const uint32_t best = Vu1Lanes::detect();
        const char* e = std::getenv("DRAKAN_VU_LANES");
        if (e && e[0] == '0')
            return 0u;
        if (e && (e[0] == '2' || e[0] == 'a' || e[0] == 'A') && best >= 2) // "2" / "avx2"
            return 2u;
        return best;
    }();
    return w;
}
std::atomic<uint64_t> g_iters{0}, g_steps{0}, g_aborts{0}, g_pairs{0};
// per loop (DRAKAN_VU_LANES_STATS): attempts, pending Q, budget, fewer than 2 iterations, memory, FMAC lane fix-ups,
// iterations run in lanes
enum Count : uint32_t { C_CALLS, C_Q, C_BUDGET, C_SHORT, C_MEMORY, C_FIXED, C_ITERS, C_COUNT };
std::atomic<uint64_t> g_loop[kPairs][C_COUNT];
bool statsEnabled();
PS2_VU_FORCEINLINE void note(uint32_t loop, Count what, uint64_t n = 1) {
    if (statsEnabled())
        g_loop[loop][what].fetch_add(n, std::memory_order_relaxed);
}

template <class B, uint32_t L>
struct Exec {
    static constexpr const Plan& P = kPlan<L>;
    static constexpr uint32_t W = B::W;
    static constexpr uint32_t N = P.n;
    using V = typename B::V;
    using M = typename B::M;
    struct Ctx {
        V out[kMaxNodes];
        V prev[kMaxNodes];  // carried: every lane holds the last lane of the previous step (initially the registers)
        V prevN[kMaxNodes]; // normalized prev, for nodes whose results are always normal
        V inv[kRegs];
        V invN[kRegs];
        uint32_t fixed = 0; // FMAC nodes with lanes recomputed by the scalar path
        int32_t vi[16];
        float qPrev;
        uint32_t vlen;         // iterations in this step
        V macVs, macB, macAcc; // operands of the last FMAC
    };
    // MAC flags of the last FMAC in iteration `lane` of the step (the scalar engine's fmac on that lane)
    static PS2_VU_FORCEINLINE uint32_t finalMac(const Ctx& c, uint32_t lane) {
        constexpr Node nd = P.nodes[P.macNode];
        constexpr uint8_t dest = DEST(kInfo[L + nd.pair].upper);
        const __m128 vs = B::template lane<0>(B::laneOf(c.macVs, lane));
        const __m128 b = B::template lane<0>(B::laneOf(c.macB, lane));
        const __m128 acc = nd.kind >= 4 ? B::template lane<0>(B::laneOf(c.macAcc, lane)) : _mm_setzero_ps();
        uint32_t mac = 0;
        fmac<nd.kind, dest>(vs, b, acc, mac);
        return mac;
    }

    // Component C's source, normalized if the operand is (normalization is per component, so it can be applied to
    // each source; invariants and carried values are normalized once per run).
    template <uint32_t Nd, uint32_t Op, uint32_t C>
    static PS2_VU_FORCEINLINE V comp(Ctx& c) {
        constexpr Operand o = P.nodes[Nd].ops[Op];
        constexpr Src s = o.src[C];
        if constexpr (s.kind == SK_VF0)
            return B::constant(0.0f, 0.0f, 0.0f, 1.0f);
        else if constexpr (s.kind == SK_INV)
            return o.norm ? c.invN[o.reg] : c.inv[o.reg];
        else if constexpr (s.d == 0) {
            if constexpr (o.norm && !normalOut(P.nodes[s.node].op))
                return B::normalize(c.out[s.node]);
            else
                return c.out[s.node];
        } else {
            if constexpr (!o.norm)
                return B::shiftIn(c.out[s.node], c.prev[s.node]);
            else if constexpr (normalOut(P.nodes[s.node].op))
                return B::shiftIn(c.out[s.node], c.prevN[s.node]);
            else
                return B::normalize(B::shiftIn(c.out[s.node], c.prev[s.node]));
        }
    }
    // components of the operand with the same source as component C (only for the first such component)
    template <uint32_t Nd, uint32_t Op, uint32_t C>
    static constexpr uint32_t group() {
        constexpr Operand o = P.nodes[Nd].ops[Op];
        if (!(o.comps & (1u << C)))
            return 0;
        for (uint32_t e = 0; e < C; ++e)
            if ((o.comps & (1u << e)) && o.src[e] == o.src[C])
                return 0;
        uint32_t m = 0;
        for (uint32_t e = C; e < 4; ++e)
            if ((o.comps & (1u << e)) && o.src[e] == o.src[C])
                m |= 1u << e;
        return m;
    }
    template <uint32_t Nd, uint32_t Op, uint32_t C>
    static PS2_VU_FORCEINLINE V merge(Ctx& c, V r) {
        constexpr uint32_t first = firstComp(P.nodes[Nd].ops[Op].comps);
        constexpr uint32_t m = group<Nd, Op, C>();
        if constexpr (C == first || m == 0)
            return r;
        else
            return B::template blend<m>(r, comp<Nd, Op, C>(c));
    }
    template <uint32_t Nd, uint32_t Op>
    static PS2_VU_FORCEINLINE V operand(Ctx& c) {
        constexpr Operand o = P.nodes[Nd].ops[Op];
        constexpr uint32_t first = firstComp(o.comps);
        V r = comp<Nd, Op, first>(c);
        r = merge<Nd, Op, 0>(c, r);
        r = merge<Nd, Op, 1>(c, r);
        r = merge<Nd, Op, 2>(c, r);
        r = merge<Nd, Op, 3>(c, r);
        return r;
    }
    template <uint32_t Nd>
    static PS2_VU_FORCEINLINE V second(Ctx& c) {
        constexpr Node nd = P.nodes[Nd];
        if constexpr (nd.fmB <= 3)
            return B::template permute<static_cast<int>(nd.fmB * 0x55u)>(operand<Nd, 1>(c));
        else
            return operand<Nd, 1>(c);
    }
    // the second operand is the constant 0 (VF00 x/y/z broadcast) / never zero (VF00 w broadcast)
    template <uint32_t Nd>
    static constexpr int constantB() {
        constexpr Node nd = P.nodes[Nd];
        if constexpr (nd.fmB <= 3 && nd.ops[1].src[nd.fmB].kind == SK_VF0)
            return nd.fmB == 3 ? 1 : 0;
        else
            return -1;
    }

    // the scalar engine's FMAC for the lanes with a component in `bad` (rare: exact zero / underflow / overflow)
    template <uint32_t Nd>
    static VU_LANES_NOINLINE V fixLanes(Ctx& c, M bad, V vs, V b, V result) {
        constexpr Node nd = P.nodes[Nd];
        constexpr uint8_t dest = DEST(kInfo[L + nd.pair].upper);
        V acc = B::set1(0.0f);
        if constexpr (nd.kind >= 4)
            acc = operand<Nd, 2>(c);
        for (uint32_t j = 0; j < W; ++j) {
            if (!B::laneAny(bad, j))
                continue;
            uint32_t mac;
            const __m128 r =
                fmac<nd.kind, dest>(B::template lane<0>(B::laneOf(vs, j)), B::template lane<0>(B::laneOf(b, j)),
                                    B::template lane<0>(B::laneOf(acc, j)), mac);
            result = B::insertLane(result, j, r);
        }
        return result;
    }

    template <uint32_t Nd, bool Partial>
    static PS2_VU_FORCEINLINE void exec(Ctx& c, const uint8_t* data) {
        constexpr Node nd = P.nodes[Nd];
        if constexpr (nd.op == N_FMAC) {
            const V vs = operand<Nd, 0>(c);
            const V b = second<Nd>(c);
            V result, product = B::set1(0.0f);
            if constexpr (nd.kind == 1)
                result = B::add(vs, b);
            else if constexpr (nd.kind == 2)
                result = B::sub(vs, b);
            else if constexpr (nd.kind == 3)
                result = product = B::mul(vs, b);
            else {
                const V acc = operand<Nd, 2>(c);
                product = B::mul(vs, b);
                result = nd.kind == 4 ? B::add(acc, product) : B::sub(acc, product);
            }
            // components where Vu1Step::calculateFmac would not return the plain result drop the step
            // lanes where Vu1Step::calculateFmac would not return the plain result (zero / underflow / overflow of
            // the exact result) are recomputed by the scalar engine's fmac
            if constexpr (!(nd.kind == 3 && constantB<Nd>() == 0)) {
                const M bad = Vu1Lanes::fmacBad<B, nd.kind, constantB<Nd>()>(B::template comps<nd.wcomps>(), vs, b,
                                                                             product, result);
                if (B::any(bad)) {
                    ++c.fixed;
                    result = fixLanes<Nd>(c, bad, vs, b, result);
                }
            }
            c.out[Nd] = result;
            if constexpr (Nd == P.macNode) {
                c.macVs = vs;
                c.macB = b;
                if constexpr (nd.kind >= 4)
                    c.macAcc = operand<Nd, 2>(c);
            }
        } else if constexpr (nd.op == N_MAX || nd.op == N_MINI) {
            const V vs = operand<Nd, 0>(c);
            const V b = second<Nd>(c);
            c.out[Nd] = nd.op == N_MAX ? B::max(vs, b) : B::min(vs, b);
        } else if constexpr (nd.op == N_ITOF) {
            V r = B::itof(operand<Nd, 0>(c));
            if constexpr (nd.special == 0x11)
                r = B::mul(r, B::set1(1.0f / 16.0f));
            else if constexpr (nd.special == 0x12)
                r = B::mul(r, B::set1(1.0f / 4096.0f));
            else if constexpr (nd.special == 0x13)
                r = B::mul(r, B::set1(1.0f / 32768.0f));
            c.out[Nd] = r;
        } else if constexpr (nd.op == N_FTOI) {
            constexpr float scale = nd.special == 0x14   ? 1.0f
                                    : nd.special == 0x15 ? 16.0f
                                    : nd.special == 0x16 ? 4096.0f
                                                         : 32768.0f;
            c.out[Nd] = B::toFixed(operand<Nd, 0>(c), scale);
        } else if constexpr (nd.op == N_ABS) {
            c.out[Nd] = B::abs(operand<Nd, 0>(c));
        } else if constexpr (nd.op == N_LQ) {
            constexpr int32_t step = P.viStep[nd.vi];
            const int32_t q0 = (nd.vi == 0 ? 0 : c.vi[nd.vi]) + nd.offset;
            if constexpr (step == 1 && !Partial)
                c.out[Nd] = B::load(data + static_cast<uint32_t>(q0) * 16u);
            else {
                // a partial step loads lane 0's quadword into the unused lanes (only checked addresses are read)
                uint32_t off[W];
                for (uint32_t j = 0; j < W; ++j)
                    off[j] =
                        static_cast<uint32_t>(q0 + static_cast<int32_t>(Partial && j >= c.vlen ? 0 : j) * step) * 16u;
                c.out[Nd] = B::loadLanes(data, off);
            }
        } else if constexpr (nd.op == N_SQ || nd.op == N_MOVE) {
            c.out[Nd] = operand<Nd, 0>(c);
        } else if constexpr (nd.op == N_MR32) {
            c.out[Nd] = B::template permute<_MM_SHUFFLE(0, 3, 2, 1)>(operand<Nd, 0>(c));
        } else if constexpr (nd.op == N_DIV) {
            const V num = B::normalize(B::template permute<static_cast<int>(nd.fmB * 0x55u)>(operand<Nd, 0>(c)));
            const V den = B::normalize(B::template permute<static_cast<int>(nd.special * 0x55u)>(operand<Nd, 1>(c)));
            c.out[Nd] = B::normalize(B::divide(num, den));
        }
    }

    template <uint32_t J, uint32_t Nd, bool Partial>
    static PS2_VU_FORCEINLINE void storeLane(Ctx& c, uint8_t* base) {
        if constexpr (Partial) {
            if (J >= c.vlen)
                return;
        }
        constexpr Node nd = P.nodes[Nd];
        constexpr int32_t offset = (static_cast<int32_t>(nd.offset) + static_cast<int32_t>(J) * P.viStep[nd.vi]) * 16;
        B::template storeLane<nd.dest>(base + offset, B::template lane<J>(c.out[Nd]));
    }
    template <uint32_t Nd, bool Partial>
    static PS2_VU_FORCEINLINE void store(Ctx& c, uint8_t* data) {
        constexpr Node nd = P.nodes[Nd];
        if constexpr (nd.op == N_SQ) {
            uint8_t* base = data + (nd.vi == 0 ? 0 : static_cast<ptrdiff_t>(c.vi[nd.vi]) * 16);
            storeLane<0, Nd, Partial>(c, base);
            storeLane<1, Nd, Partial>(c, base);
            if constexpr (W == 4) {
                storeLane<2, Nd, Partial>(c, base);
                storeLane<3, Nd, Partial>(c, base);
            }
        }
    }
    template <uint32_t Nd, bool Partial>
    static PS2_VU_FORCEINLINE void carry(Ctx& c) {
        constexpr Node nd = P.nodes[Nd];
        if constexpr (nd.carry && nd.op != N_SQ && nd.op != N_NONE) {
            c.prev[Nd] = Partial ? B::laneOf(c.out[Nd], c.vlen - 1) : B::last(c.out[Nd]);
            if constexpr (prevNormUsed<L>(Nd))
                c.prevN[Nd] = c.prev[Nd]; // a normal result
        }
    }
    template <uint32_t R>
    static PS2_VU_FORCEINLINE void advance(Ctx& c) {
        if constexpr (R != 0 && ((P.viWritten >> R) & 1u))
            c.vi[R] += static_cast<int32_t>(c.vlen) * P.viStep[R];
    }
    template <bool Partial, uint32_t... K>
    static PS2_VU_FORCEINLINE void body(Ctx& c, const uint8_t* data, std::integer_sequence<uint32_t, K...>) {
        (exec<P.order[K], Partial>(c, data), ...);
    }
    template <bool Partial, uint32_t... Id>
    static PS2_VU_FORCEINLINE void finish(Ctx& c, uint8_t* data, std::integer_sequence<uint32_t, Id...>) {
        (store<Id, Partial>(c, data), ...);
        (carry<Id, Partial>(c), ...);
    }
    // One step of c.vlen iterations (W, or fewer for the last one).
    template <bool Partial>
    static PS2_VU_FORCEINLINE void step(Ctx& c, State& s) {
        body<Partial>(c, s.data, std::make_integer_sequence<uint32_t, P.orderCount>{});
        if constexpr (P.divNode != kNone && !P.qCommit) // Q of the second-to-last iteration
            c.qPrev = _mm_cvtss_f32(
                B::template lane<0>(c.vlen >= 2 ? B::laneOf(c.out[P.divNode], c.vlen - 2) : c.prev[P.divNode]));
        finish<Partial>(c, s.data, std::make_integer_sequence<uint32_t, 2 * N>{});
        advanceAll(c, std::make_integer_sequence<uint32_t, 16>{});
    }
    template <uint32_t... R>
    static PS2_VU_FORCEINLINE void advanceAll(Ctx& c, std::integer_sequence<uint32_t, R...>) {
        (advance<R>(c), ...);
    }

    // value of VI register Reg at the branch of iteration k
    template <uint8_t Reg, int16_t Pre>
    static PS2_VU_FORCEINLINE int16_t branchValue(const int32_t (&v0)[16], uint32_t k) {
        if constexpr (Reg == 0)
            return 0;
        else if constexpr (!((P.viWritten >> Reg) & 1u))
            return static_cast<int16_t>(v0[Reg]);
        else
            return static_cast<int16_t>(v0[Reg] + static_cast<int32_t>(k) * P.viStep[Reg] + Pre);
    }
    static PS2_VU_FORCEINLINE bool taken(const int32_t (&v0)[16], uint32_t k) {
        const int16_t a = branchValue<P.brS, P.brPreS>(v0, k);
        const int16_t b = branchValue<P.brT, P.brPreT>(v0, k);
        if constexpr (P.brOp == L_B)
            return true;
        else if constexpr (P.brOp == L_IBEQ)
            return a == b;
        else if constexpr (P.brOp == L_IBNE)
            return a != b;
        else if constexpr (P.brOp == L_IBLTZ)
            return a < 0;
        else if constexpr (P.brOp == L_IBGTZ)
            return a > 0;
        else if constexpr (P.brOp == L_IBLEZ)
            return a <= 0;
        else
            return a >= 0;
    }

    // first / last quadword index touched by memory node Id over `total` iterations
    template <uint32_t Id>
    static PS2_VU_FORCEINLINE void span(const int32_t (&v0)[16], uint32_t total, int32_t& lo, int32_t& hi) {
        constexpr Node nd = P.nodes[Id];
        const int32_t first = (nd.vi == 0 ? 0 : v0[nd.vi]) + nd.offset;
        const int32_t lastQ = first + static_cast<int32_t>(total - 1) * P.viStep[nd.vi];
        lo = first < lastQ ? first : lastQ;
        hi = first < lastQ ? lastQ : first;
    }
    // Load and store streams with the same stride: the store of iteration b hits the load of iteration a when
    // b - a = (load - store) / stride. Stores are performed after each step, so this is exact if every such load
    // runs before its store in program order, or in an earlier step.
    template <uint32_t Load, uint32_t Store>
    static PS2_VU_FORCEINLINE bool loadsFirst(const int32_t (&v0)[16]) {
        constexpr Node ld = P.nodes[Load];
        constexpr Node sd = P.nodes[Store];
        constexpr int32_t stride = P.viStep[ld.vi];
        if constexpr (stride == 0 || P.viStep[sd.vi] != stride)
            return false;
        else {
            const int32_t diff =
                ((ld.vi == 0 ? 0 : v0[ld.vi]) + ld.offset) - ((sd.vi == 0 ? 0 : v0[sd.vi]) + sd.offset);
            if (diff % stride != 0)
                return true; // never the same quadword
            const int32_t delta = diff / stride;
            return delta > 0 || (delta == 0 && ld.pair < sd.pair) || delta <= -static_cast<int32_t>(W);
        }
    }
    struct Spans {
        int32_t lo[kMaxNodes], hi[kMaxNodes];
    };
    // two store streams with the same stride whose start addresses differ by a non-multiple of it never write the
    // same quadword (e.g. the interleaved vertex outputs at 0..3(viN) with viN += 4)
    template <uint32_t A, uint32_t Bn>
    static PS2_VU_FORCEINLINE bool interleaved(const int32_t (&v0)[16]) {
        constexpr Node a = P.nodes[A];
        constexpr Node b = P.nodes[Bn];
        constexpr int32_t stride = P.viStep[a.vi];
        if constexpr (stride == 0 || P.viStep[b.vi] != stride)
            return false;
        else {
            const int32_t diff = ((a.vi == 0 ? 0 : v0[a.vi]) + a.offset) - ((b.vi == 0 ? 0 : v0[b.vi]) + b.offset);
            return diff % stride != 0;
        }
    }
    template <uint32_t K, uint32_t... E>
    static PS2_VU_FORCEINLINE bool storePairsOk(const int32_t (&v0)[16], const Spans& sp,
                                                std::integer_sequence<uint32_t, E...>) {
        return ((E >= K || sp.hi[E] < sp.lo[K] || sp.hi[K] < sp.lo[E] ||
                 interleaved<P.stores[K], P.stores[E < K ? E : 0]>(v0)) &&
                ...);
    }
    template <uint32_t K>
    static PS2_VU_FORCEINLINE bool storeOk(const int32_t (&v0)[16], uint32_t total, int32_t qmask, Spans& sp) {
        span<P.stores[K]>(v0, total, sp.lo[K], sp.hi[K]);
        if (sp.lo[K] < 0 || sp.hi[K] > qmask)
            return false;
        return storePairsOk<K>(v0, sp, std::make_integer_sequence<uint32_t, K>{});
    }
    template <uint32_t K, uint32_t E>
    static PS2_VU_FORCEINLINE bool loadStoreOk(const int32_t (&v0)[16], int32_t lo, int32_t hi, const Spans& sp) {
        return sp.hi[E] < lo || hi < sp.lo[E] || loadsFirst<P.loads[K], P.stores[E]>(v0);
    }
    template <uint32_t K, uint32_t... E>
    static PS2_VU_FORCEINLINE bool loadOk(const int32_t (&v0)[16], uint32_t total, int32_t qmask, const Spans& sp,
                                          std::integer_sequence<uint32_t, E...>) {
        int32_t lo, hi;
        span<P.loads[K]>(v0, total, lo, hi);
        return lo >= 0 && hi <= qmask && (loadStoreOk<K, E>(v0, lo, hi, sp) && ...);
    }
    template <uint32_t... S, uint32_t... K>
    static PS2_VU_FORCEINLINE bool memoryOk(const int32_t (&v0)[16], uint32_t total, int32_t qmask,
                                            std::integer_sequence<uint32_t, S...>,
                                            std::integer_sequence<uint32_t, K...>) {
        Spans sp;
        return (storeOk<S>(v0, total, qmask, sp) && ...) &&
               (loadOk<K>(v0, total, qmask, sp, std::make_integer_sequence<uint32_t, P.storeCount>{}) && ...);
    }

    template <uint32_t R>
    static PS2_VU_FORCEINLINE void loadInvariant(Ctx& c, const State& s) {
        constexpr uint32_t use = invUse<L>(R);
        if constexpr (use != 0) {
            V v;
            if constexpr (R < 32)
                v = B::bcast(_mm_loadu_ps(s.st.vf[R]));
            else if constexpr (R == kAcc)
                v = B::bcast(_mm_loadu_ps(s.st.acc));
            else if constexpr (R == kQ)
                v = B::set1(s.st.q);
            else
                v = B::set1(s.st.i);
            if constexpr (use & 1u)
                c.inv[R] = v;
            if constexpr (use & 2u)
                c.invN[R] = B::normalize(v);
        }
    }
    template <uint32_t Id>
    static PS2_VU_FORCEINLINE void loadPrev(Ctx& c, const State& s) {
        constexpr Node nd = P.nodes[Id];
        if constexpr (nd.carry && nd.op != N_SQ && nd.op != N_NONE) {
            if constexpr (nd.wreg < 32)
                c.prev[Id] = B::bcast(_mm_loadu_ps(s.st.vf[nd.wreg]));
            else if constexpr (nd.wreg == kAcc)
                c.prev[Id] = B::bcast(_mm_loadu_ps(s.st.acc));
            else
                c.prev[Id] = B::set1(s.qValid ? s.qValue : s.st.q); // DIV: Q of the previous iteration
            if constexpr (prevNormUsed<L>(Id))
                c.prevN[Id] = B::normalize(c.prev[Id]);
        }
    }
    template <uint32_t... R, uint32_t... Id>
    static PS2_VU_FORCEINLINE void enter(Ctx& c, const State& s, std::integer_sequence<uint32_t, R...>,
                                         std::integer_sequence<uint32_t, Id...>) {
        (loadInvariant<R>(c, s), ...);
        (loadPrev<Id>(c, s), ...);
    }
    // registers after the last completed step (lane 0 of a carried value is its last lane)
    template <uint32_t R>
    static PS2_VU_FORCEINLINE void commit(const Ctx& c, VU1State& st) {
        if constexpr (R < kAcc + 1) {
            float* dst = R < 32 ? st.vf[R] : st.acc;
            constexpr uint8_t lx = P.last[R][0], ly = P.last[R][1], lz = P.last[R][2], lw = P.last[R][3];
            if constexpr (lx != kNone)
                dst[0] = _mm_cvtss_f32(B::template lane<0>(c.prev[lx]));
            if constexpr (ly != kNone)
                _mm_store_ss(dst + 1, _mm_shuffle_ps(B::template lane<0>(c.prev[ly]), B::template lane<0>(c.prev[ly]),
                                                     _MM_SHUFFLE(1, 1, 1, 1)));
            if constexpr (lz != kNone)
                _mm_store_ss(dst + 2, _mm_shuffle_ps(B::template lane<0>(c.prev[lz]), B::template lane<0>(c.prev[lz]),
                                                     _MM_SHUFFLE(2, 2, 2, 2)));
            if constexpr (lw != kNone)
                _mm_store_ss(dst + 3, _mm_shuffle_ps(B::template lane<0>(c.prev[lw]), B::template lane<0>(c.prev[lw]),
                                                     _MM_SHUFFLE(3, 3, 3, 3)));
        }
    }
    template <uint32_t... R>
    static PS2_VU_FORCEINLINE void commitAll(const Ctx& c, VU1State& st, std::integer_sequence<uint32_t, R...>) {
        (commit<R>(c, st), ...);
    }

    // Runs the loop from its head in steps. True: the loop's last iteration (branch not taken) ran too and execution
    // continues after the loop; false: the scalar block continues with the next iteration.
    static bool run(State& s) {
        VU1State& st = s.st;
        note(L, C_CALLS);
        if constexpr (P.readsQ || P.divNode != kNone) {
            if (s.qValid && s.qReady > s.now + P.firstQ) {
                note(L, C_Q);
                return false; // a pending division would be read (or stall the next one) inside the first iteration
            }
        }
        if (s.budgetEnd <= s.now + N) {
            note(L, C_BUDGET);
            return false;
        }
        const uint64_t room = (s.budgetEnd - s.now - 1) / N; // iterations whose budget check still continues
        const uint32_t cap = room < 4096 ? static_cast<uint32_t>(room) : 4096u;
        int32_t v0[16];
        for (uint32_t r = 0; r < 16; ++r)
            v0[r] = st.vi[r];
        uint32_t takenCount = 0;
        while (takenCount < cap && taken(v0, takenCount))
            ++takenCount;
        // the iteration whose branch is not taken (unless the budget ends the run first)
        const bool withFinal = P.finalOk && takenCount < cap;
        const uint32_t iters = takenCount + (withFinal ? 1u : 0u);
        // whole steps, then one partial step for the rest when it has at least two iterations
        const uint32_t total = iters % W >= 2 ? iters : iters - iters % W;
        // the iterations left after this run go to the scalar blocks without another attempt
        s.laneHold = iters - total + (withFinal ? 0u : 1u);
        if (total == 0) {
            note(L, C_SHORT);
            return false;
        }
        // memory: inside VU memory, no wrap, loads not stored by these iterations, stores disjoint
        if (!memoryOk(v0, total, static_cast<int32_t>(s.mask >> 4),
                      std::make_integer_sequence<uint32_t, P.storeCount>{},
                      std::make_integer_sequence<uint32_t, P.loadCount>{})) {
            s.laneHold = takenCount + 1;
            note(L, C_MEMORY);
            return false;
        }

        Ctx c;
        if constexpr (P.divNode == kNone) {
            if (P.readsQ && s.qValid)
                commitQ(s); // ready before the first reader (checked above), which would commit it
        }
        enter(c, s, std::make_integer_sequence<uint32_t, kRegs>{}, std::make_integer_sequence<uint32_t, 2 * N>{});
        for (uint32_t r = 0; r < 16; ++r)
            c.vi[r] = v0[r];
        c.qPrev = st.q;

        uint32_t done = 0;
        while (done < total) {
            c.vlen = total - done < W ? total - done : W;
            if (c.vlen == W)
                step<false>(c, s);
            else
                step<true>(c, s);
            done += c.vlen;
            ++s.laneSteps;
        }
        s.laneAborts += c.fixed;
        note(L, C_FIXED, c.fixed);
        if (done == 0)
            return false;
        const bool finished = withFinal && done == iters;
        // the state after iteration done - 1, as the scalar blocks leave it
        commitAll(c, st, std::make_integer_sequence<uint32_t, kAcc + 1>{});
        for (uint32_t r = 1; r < 16; ++r)
            if (P.viWritten & (1u << r))
                st.vi[r] = static_cast<int16_t>(v0[r] + static_cast<int32_t>(done) * P.viStep[r]);
        s.now += static_cast<uint64_t>(done) * N;
        s.pairs += static_cast<uint64_t>(done) * N;
        if constexpr (P.divNode != kNone) {
            const float q = _mm_cvtss_f32(B::template lane<0>(c.prev[P.divNode]));
            if constexpr (P.qCommit) {
                st.q = q;
                s.qValid = false;
            } else {
                st.q = c.qPrev;
                s.qValid = true;
                s.qValue = q;
                s.qReady = s.now - N + P.divPair + 7u;
            }
        }
        s.taken = !finished;
        if (done > (finished ? 1u : 0u)) // a taken iteration ran
            s.branchTarget = (L * 8u) & 0x3FFFu;
        if constexpr (P.macNode != kNone) {
            if (finished)
                s.lastMac = finalMac(c, c.vlen - 1);
        }
        if constexpr (P.slotVi != 0) {
            s.bkReg = P.slotVi;
            s.bkVal =
                static_cast<int16_t>(v0[P.slotVi] + static_cast<int32_t>(done - 1) * P.viStep[P.slotVi] + P.slotPre);
        } else
            s.bkReg = 0;
        s.laneIters += done;
        note(L, C_ITERS, done);
        return finished;
    }
};

// True: the loop ran to its exit in lanes.
template <uint32_t L>
PS2_VU_FORCEINLINE bool tryLoop(State& s) {
    if constexpr (selfLoop(L) && kPlan<L>.ok) {
        if (s.laneHold != 0) {
            --s.laneHold;
            return false;
        }
        switch (width()) {
#if defined(VU_LANES_AVX512)
            case 4:
                return Exec<Vu1Lanes::Avx512, L>::run(s);
#endif
#if defined(VU_LANES_AVX2)
            case 2:
                return Exec<Vu1Lanes::Avx2, L>::run(s);
#endif
            default:
                return false;
        }
    } else
        return false;
}

// why each one-block loop of the image does (R_ACCEPTED) or does not run in lanes (DRAKAN_VU_LANES_STATS)
constexpr std::array<uint8_t, kPairs> buildReasons() {
    std::array<uint8_t, kPairs> t{};
    for (uint32_t i = 0; i < kPairs; ++i)
        t[i] = selfLoop(i) ? buildPlan(i).reason : static_cast<uint8_t>(0xFF);
    return t;
}

void printStats() {
    static constexpr std::array<uint8_t, kPairs> kReasons = buildReasons();
    static const char* const kNames[] = {"ok",       "not a loop", "length", "pair", "upper op",  "lower op",
                                         "MAC read", "VI",         "branch", "Q",    "recurrence"};
    std::fprintf(
        stderr, "[vulanes] width %u: %llu iterations in %llu steps, %llu FMAC lane fix-ups, %llu pairs total\n",
        width(), static_cast<unsigned long long>(g_iters.load()), static_cast<unsigned long long>(g_steps.load()),
        static_cast<unsigned long long>(g_aborts.load()), static_cast<unsigned long long>(g_pairs.load()));
    for (uint32_t i = 0; i < kPairs; ++i)
        if (kReasons[i] != 0xFF)
            std::fprintf(
                stderr,
                "[vulanes] loop 0x%04x (%u pairs): %s; attempts %llu, pending Q %llu, budget %llu, short %llu, "
                "memory %llu, fixed FMACs %llu, lane iterations %llu\n",
                i * 8u, kLayout.end[i] - i, kNames[kReasons[i] < 11 ? kReasons[i] : 0],
                static_cast<unsigned long long>(g_loop[i][C_CALLS].load()),
                static_cast<unsigned long long>(g_loop[i][C_Q].load()),
                static_cast<unsigned long long>(g_loop[i][C_BUDGET].load()),
                static_cast<unsigned long long>(g_loop[i][C_SHORT].load()),
                static_cast<unsigned long long>(g_loop[i][C_MEMORY].load()),
                static_cast<unsigned long long>(g_loop[i][C_FIXED].load()),
                static_cast<unsigned long long>(g_loop[i][C_ITERS].load()));
}
bool statsEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_LANES_STATS");
        const bool enabled = e && e[0] && e[0] != '0';
        if (enabled)
            std::atexit(printStats);
        return enabled;
    }();
    return on;
}
} // namespace lanes

PS2_VU_FORCEINLINE void pushFlag(uint32_t (&ring)[4], uint64_t (&ready)[4], uint32_t& count, uint32_t& base,
                                 uint32_t value, uint64_t now) {
    const uint32_t slot = count & 3u;
    if (count >= 4u)
        base = ring[slot]; // the oldest result leaves the window: it is visible by now
    ring[slot] = value;
    ready[slot] = now + kFlagLatency;
    ++count;
}
PS2_VU_FORCEINLINE uint32_t visibleFlag(const uint32_t (&ring)[4], const uint64_t (&ready)[4], uint32_t count,
                                        uint32_t base, uint64_t now) {
    const uint32_t n = count < 4u ? count : 4u;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t slot = (count - 1u - k) & 3u;
        if (ready[slot] <= now)
            return ring[slot];
    }
    return base;
}

// Operands: normalized unless the lanes the instruction writes are known to hold normal values already.
template <uint32_t I, uint8_t Reg, uint8_t Lanes>
PS2_VU_FORCEINLINE __m128 operand(const VU1State& st) {
    if constexpr (Reg == 0)
        return _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
    else if constexpr (knownLanes(kNorm.in[I], Reg, Lanes))
        return _mm_loadu_ps(st.vf[Reg]);
    else
        return Vu1Simd::normalize(_mm_loadu_ps(st.vf[Reg]));
}
template <uint32_t I, uint8_t Reg, uint8_t C>
PS2_VU_FORCEINLINE __m128 broadcastOperand(const VU1State& st) {
    if constexpr (Reg == 0)
        return _mm_set1_ps(C == 3 ? 1.0f : 0.0f);
    else if constexpr (known(kNorm.in[I], Reg, C))
        return _mm_set1_ps(st.vf[Reg][C]);
    else
        return Vu1Simd::normalize(_mm_set1_ps(st.vf[Reg][C]));
}
template <uint32_t I, uint8_t Lanes>
PS2_VU_FORCEINLINE __m128 accOperand(const VU1State& st) {
    if constexpr ((kNorm.in[I].acc & Vu1FmacDetail::kRev4[Lanes]) == Vu1FmacDetail::kRev4[Lanes])
        return _mm_loadu_ps(st.acc);
    else
        return Vu1Simd::normalize(_mm_loadu_ps(st.acc));
}

// Vu1Step::calculateFmac with compile-time operation and destination lanes.
template <uint8_t Kind, uint8_t Dest>
PS2_VU_FORCEINLINE __m128 fmac(__m128 vs, __m128 b, __m128 acc, uint32_t& mac) {
    using namespace Vu1FmacDetail;
    __m128 result, product = _mm_setzero_ps();
    if constexpr (Kind == 1)
        result = _mm_add_ps(vs, b);
    else if constexpr (Kind == 2)
        result = _mm_sub_ps(vs, b);
    else if constexpr (Kind == 3)
        result = _mm_mul_ps(vs, b);
    else if constexpr (Kind == 4) {
        product = _mm_mul_ps(vs, b);
        result = _mm_add_ps(acc, product);
    } else {
        product = _mm_mul_ps(vs, b);
        result = _mm_sub_ps(acc, product);
    }
    constexpr int comps = kRev4[Dest];
    const int ordR = ordinaryLanes(result);
    const int ordP = Kind >= 4 ? ordinaryLanes(product) : 0xF;
    if (((ordR & ordP) & comps) == comps) {
        mac = static_cast<uint32_t>(kRev4[_mm_movemask_ps(result) & comps]) << 4;
        return result;
    }
    uint32_t status, extraSticky;
    if (flagsKnownClasses(Kind, vs, b, product, result, comps, ordR, ordP, mac, status, extraSticky))
        return result;
    alignas(16) float vs4[4], b4[4], acc4[4], product4[4], result4[4];
    _mm_store_ps(vs4, vs);
    _mm_store_ps(b4, b);
    _mm_store_ps(acc4, acc);
    _mm_store_ps(product4, product);
    _mm_store_ps(result4, result);
    uint32_t flags3[3];
    Vu1Step::fmacSlow(Kind, vs4, b4, acc4, product4, result4, comps, ordR, ordP, flags3);
    mac = flags3[0];
    return _mm_load_ps(result4);
}

// ---- upper unit (Vu1Step::upperFmac / upperOther)
template <uint32_t I, uint32_t K>
PS2_VU_FORCEINLINE void upper(State& s, float* dst) {
    constexpr PairInfo d = kInfo[I];
    constexpr Up u = d.up;
    VU1State& st = s.st;
    constexpr uint32_t instr = d.upper;
    constexpr uint8_t dest = DEST(instr), ft = FT(instr), fs = FS(instr);
    constexpr int comps = Vu1FmacDetail::kRev4[dest];
    if constexpr (u == U_FMAC) {
        const __m128 vs = operand<I, fs, dest>(st);
        __m128 b;
        if constexpr (d.fmB <= 3)
            b = broadcastOperand<I, ft, d.fmB>(st);
        else if constexpr (d.fmB == 4) {
            if (s.qValid && at<K>(s) >= s.qReady)
                commitQ(s);
            b = _mm_set1_ps(st.q);
        } else if constexpr (d.fmB == 5)
            b = _mm_set1_ps(st.i);
        else
            b = operand<I, ft, dest>(st);
        __m128 acc;
        if constexpr (d.fmKind >= 4)
            acc = accOperand<I, dest>(st);
        else
            acc = _mm_setzero_ps();
        uint32_t mac;
        const __m128 value = fmac<d.fmKind, dest>(vs, b, acc, mac);
        float* target = d.fmToAcc ? st.acc : dst;
        _mm_storeu_ps(target, Vu1Simd::mergeLanes(_mm_loadu_ps(target), value, comps));
        s.lastMac = mac;
        if constexpr (kMacObserved[I])
            pushFlag(s.mac, s.macReady, s.macCount, s.macBase, mac, at<K>(s));
    } else if constexpr (u == U_MAX || u == U_MINI) {
        constexpr uint8_t op = instr & 0x3Fu;
        const __m128 vs = operand<I, fs, dest>(st);
        __m128 b;
        if constexpr (op < 0x18u)
            b = broadcastOperand<I, ft, (op & 3u)>(st);
        else if constexpr (op == 0x1Du || op == 0x1Fu)
            b = _mm_set1_ps(st.i);
        else
            b = operand<I, ft, dest>(st);
        const __m128 result = u == U_MAX ? _mm_max_ps(vs, b) : _mm_min_ps(vs, b);
        _mm_storeu_ps(dst, Vu1Simd::mergeLanes(_mm_loadu_ps(dst), result, comps));
    } else if constexpr (u == U_ITOF) {
        constexpr uint8_t special = (instr & 0x3u) | ((instr >> 4) & 0x7Cu);
        __m128 result = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(st.vf[fs])));
        if constexpr (special == 0x11)
            result = _mm_mul_ps(result, _mm_set1_ps(1.0f / 16.0f));
        else if constexpr (special == 0x12)
            result = _mm_mul_ps(result, _mm_set1_ps(1.0f / 4096.0f));
        else if constexpr (special == 0x13)
            result = _mm_mul_ps(result, _mm_set1_ps(1.0f / 32768.0f));
        _mm_storeu_ps(dst, Vu1Simd::mergeLanes(_mm_loadu_ps(dst), result, comps));
    } else if constexpr (u == U_FTOI) {
        constexpr uint8_t special = (instr & 0x3u) | ((instr >> 4) & 0x7Cu);
        constexpr float scale = special == 0x14 ? 1.0f : special == 0x15 ? 16.0f : special == 0x16 ? 4096.0f : 32768.0f;
        const __m128 result = _mm_castsi128_ps(Vu1Simd::toFixed(operand<I, fs, dest>(st), scale));
        _mm_storeu_ps(dst, Vu1Simd::mergeLanes(_mm_loadu_ps(dst), result, comps));
    } else if constexpr (u == U_ABS) {
        const __m128 result =
            _mm_andnot_ps(_mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(0x80000000u))), operand<I, fs, dest>(st));
        _mm_storeu_ps(dst, Vu1Simd::mergeLanes(_mm_loadu_ps(dst), result, comps));
    } else if constexpr (u == U_CLIP) {
        uint32_t wBits;
        std::memcpy(&wBits, &st.vf[ft][3], 4);
        const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;
        uint32_t flags = 0;
        for (uint32_t c = 0; c < 3; ++c) {
            uint32_t bits;
            std::memcpy(&bits, &st.vf[fs][c], 4);
            if (static_cast<int32_t>(bits) > limit)
                flags |= 1u << (2 * c);
            if (static_cast<int32_t>(bits ^ 0x80000000u) > limit)
                flags |= 2u << (2 * c);
        }
        s.workingClip = ((s.workingClip << 6) | flags) & 0xFFFFFFu;
        pushFlag(s.clip, s.clipReady, s.clipCount, s.clipBase, s.workingClip, at<K>(s));
    }
}

// ---- lower unit (Vu1Step::lower / VU1Interpreter::execLower)
template <bool UseBackup>
PS2_VU_FORCEINLINE int16_t branchVi(const State& s, uint8_t reg) {
    if (reg == 0)
        return 0;
    if constexpr (UseBackup)
        if (s.bkReg == reg)
            return static_cast<int16_t>(s.bkVal);
    return static_cast<int16_t>(s.st.vi[reg]);
}

PS2_VU_FORCEINLINE void storeWords(State& s, uint32_t addr, const uint32_t (&words)[4], uint8_t lanes) {
    auto* dst = reinterpret_cast<uint32_t*>(s.data + addr);
    if (lanes == 15u)
        std::memcpy(dst, words, 16u);
    else
        for (uint32_t c = 0; c < 4u; ++c)
            if (lanes & (8u >> c))
                dst[c] = words[c];
}

// First: the pair starts its block, so its predecessor (whose VI write a branch may see as the old value) is only
// known at run time.
template <uint32_t I, uint32_t K, bool First>
PS2_VU_FORCEINLINE void lower(State& s) {
    constexpr PairInfo d = kInfo[I];
    constexpr Lo l = d.lo;
    constexpr uint32_t w = d.lower;
    VU1State& st = s.st;
    constexpr uint8_t it = VIT(w), is = VIS(w), id = VID(w), vt = FT(w), vs = FS(w);
    constexpr uint8_t dest = (w >> 21) & 0xFu;
    constexpr int16_t imm11 = IMM11(w);
    constexpr int16_t imm15 = static_cast<int16_t>((w & 0x7FFu) | ((w >> 10) & 0x7800u));
    constexpr bool useBackup = First || (I > 0 && kInfo[I > 0 ? I - 1 : 0].delays);
    if constexpr (l == L_LQ) {
        const uint32_t addr = (static_cast<uint32_t>(st.vi[is] + imm11) * 16u) & s.mask;
        if constexpr (vt != 0)
            Vu1Simd::writeLanes(st.vf[vt], reinterpret_cast<const float*>(s.data + addr), dest);
    } else if constexpr (l == L_SQ) {
        const uint32_t addr = (static_cast<uint32_t>(st.vi[it] + imm11) * 16u) & s.mask;
        uint32_t words[4];
        std::memcpy(words, st.vf[vs], 16);
        storeWords(s, addr, words, dest);
    } else if constexpr (l == L_ILW) {
        const uint32_t addr = (static_cast<uint32_t>(st.vi[is] + imm11) * 16u) & s.mask;
        constexpr int comp = (dest & 8) ? 0 : (dest & 4) ? 1 : (dest & 2) ? 2 : 3;
        uint32_t val;
        std::memcpy(&val, s.data + addr + comp * 4, 4);
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(val & 0xFFFFu);
    } else if constexpr (l == L_ISW) {
        const uint32_t addr = (static_cast<uint32_t>(st.vi[is] + imm11) * 16u) & s.mask;
        const uint32_t val = static_cast<uint16_t>(st.vi[it] & 0xFFFF);
        const uint32_t words[4] = {val, val, val, val};
        storeWords(s, addr, words, dest);
    } else if constexpr (l == L_ISWR) {
        const uint32_t addr = (static_cast<uint32_t>(static_cast<uint16_t>(st.vi[is])) * 16u) & s.mask;
        const uint32_t val = static_cast<uint16_t>(st.vi[it] & 0xFFFF);
        const uint32_t words[4] = {val, val, val, val};
        storeWords(s, addr, words, dest);
    } else if constexpr (l == L_IADDIU) {
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(st.vi[is] + imm15);
    } else if constexpr (l == L_ISUBIU) {
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(st.vi[is] - imm15);
    } else if constexpr (l == L_FMAND) {
        const uint32_t mac = visibleFlag(s.mac, s.macReady, s.macCount, s.macBase, at<K>(s));
        if constexpr (it != 0)
            st.vi[it] = static_cast<int32_t>(mac & static_cast<uint16_t>(st.vi[is]));
    } else if constexpr (l == L_FCGET) {
        const uint32_t clip = visibleFlag(s.clip, s.clipReady, s.clipCount, s.clipBase, at<K>(s));
        if constexpr (it != 0)
            st.vi[it] = static_cast<int32_t>(clip & 0x0FFFu);
    } else if constexpr (l == L_B) {
        s.taken = true;
    } else if constexpr (l == L_IBEQ) {
        s.taken = branchVi<useBackup>(s, is) == branchVi<useBackup>(s, it);
    } else if constexpr (l == L_IBNE) {
        s.taken = branchVi<useBackup>(s, is) != branchVi<useBackup>(s, it);
    } else if constexpr (l == L_IBLTZ) {
        s.taken = branchVi<useBackup>(s, is) < 0;
    } else if constexpr (l == L_IBGTZ) {
        s.taken = branchVi<useBackup>(s, is) > 0;
    } else if constexpr (l == L_IBLEZ) {
        s.taken = branchVi<useBackup>(s, is) <= 0;
    } else if constexpr (l == L_IBGEZ) {
        s.taken = branchVi<useBackup>(s, is) >= 0;
    } else if constexpr (l == L_IADD) {
        if constexpr (id != 0)
            st.vi[id] = static_cast<int16_t>(st.vi[is] + st.vi[it]);
    } else if constexpr (l == L_ISUB) {
        if constexpr (id != 0)
            st.vi[id] = static_cast<int16_t>(st.vi[is] - st.vi[it]);
    } else if constexpr (l == L_IADDI) {
        constexpr int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>(((w >> 6) & 0x1Fu) << 27) >> 27);
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(st.vi[is] + imm5);
    } else if constexpr (l == L_IAND) {
        if constexpr (id != 0)
            st.vi[id] = st.vi[is] & st.vi[it];
    } else if constexpr (l == L_IOR) {
        if constexpr (id != 0)
            st.vi[id] = st.vi[is] | st.vi[it];
    } else if constexpr (l == L_MOVE) {
        if constexpr (vt != 0) {
            float tmp[4];
            std::memcpy(tmp, st.vf[vs], 16);
            Vu1Simd::writeLanes(st.vf[vt], tmp, dest);
        }
    } else if constexpr (l == L_MR32) {
        if constexpr (vt != 0) {
            const float tmp[4] = {st.vf[vs][1], st.vf[vs][2], st.vf[vs][3], st.vf[vs][0]};
            Vu1Simd::writeLanes(st.vf[vt], tmp, dest);
        }
    } else if constexpr (l == L_LQI) {
        const uint32_t addr = (static_cast<uint32_t>(static_cast<uint16_t>(st.vi[is])) * 16u) & s.mask;
        if constexpr (vt != 0)
            Vu1Simd::writeLanes(st.vf[vt], reinterpret_cast<const float*>(s.data + addr), dest);
        if constexpr (is != 0)
            st.vi[is] = static_cast<int16_t>(st.vi[is] + 1);
    } else if constexpr (l == L_SQI) {
        const uint32_t addr = (static_cast<uint32_t>(static_cast<uint16_t>(st.vi[it])) * 16u) & s.mask;
        uint32_t words[4];
        std::memcpy(words, st.vf[vs], 16);
        storeWords(s, addr, words, dest);
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(st.vi[it] + 1);
    } else if constexpr (l == L_DIV) {
        if (s.qValid) { // the divider is busy until the previous result is out
            stallUntil<K>(s, s.qReady);
            commitQ(s);
        }
        constexpr int fsf = (w >> 21) & 0x3, ftf = (w >> 23) & 0x3;
        const float num = clampVu(st.vf[vs][fsf]);
        const float den = clampVu(st.vf[vt][ftf]);
        float result;
        if (den == 0.0f)
            result = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max()
                                                            : std::numeric_limits<float>::max();
        else
            result = num / den;
        s.qValue = clampVu(result);
        s.qReady = at<K>(s) + 7u;
        s.qValid = true;
    } else if constexpr (l == L_WAITQ) {
        if (s.qValid) {
            stallUntil<K>(s, s.qReady);
            commitQ(s);
        }
    } else if constexpr (l == L_MTIR) {
        constexpr uint32_t component = (w >> 21) & 3u;
        uint32_t bits;
        std::memcpy(&bits, &st.vf[vs][component], 4);
        if constexpr (it != 0)
            st.vi[it] = static_cast<int16_t>(bits & 0xFFFFu);
    } else if constexpr (l == L_MFIR) {
        if constexpr (vt != 0) {
            const int32_t value = static_cast<int16_t>(st.vi[is] & 0xFFFF);
            float result[4];
            for (unsigned lane = 0; lane < 4; ++lane)
                std::memcpy(&result[lane], &value, 4);
            Vu1Simd::writeLanes(st.vf[vt], result, dest);
        }
    } else if constexpr (l == L_MFP) {
        if (s.pValid && at<K>(s) >= s.pReady) {
            st.p = s.pValue;
            s.pValid = false;
        }
        if constexpr (vt != 0) {
            const float result[4] = {st.p, st.p, st.p, st.p};
            Vu1Simd::writeLanes(st.vf[vt], result, dest);
        }
    } else if constexpr (l == L_ERLENG) {
        const float x = clampVu(st.vf[vs][0]);
        const float y = clampVu(st.vf[vs][1]);
        const float z = clampVu(st.vf[vs][2]);
        const float len = std::sqrt(x * x + y * y + z * z);
        if (s.pValid && at<K>(s) >= s.pReady)
            st.p = s.pValue;
        s.pValue = clampVu(len != 0.0f ? 1.0f / len : len);
        s.pReady = at<K>(s) + 24u;
        s.pValid = true;
    } else if constexpr (l == L_XGKICK) {
        stallUntil<K>(s, s.kickDone); // a new transfer waits for the previous one
        const uint32_t bytes =
            Vu1Fast::Func::sendPacket(*s.v, *s.ctx, s.data, s.mask, static_cast<uint16_t>(st.vi[is]));
        s.kickDone = at<K>(s) + 2ull * (bytes / 16u);
    }
}

constexpr bool nextIsBranch(uint32_t i) {
    return i + 1 < kPairs && isBranch(kInfo[i + 1].lo);
}

// One instruction pair (Vu1Step::execPair), pair K of its block. First / Last: the first / last pair of its block
// (the VI branch backup crosses block boundaries at run time; inside a block it is decided statically).
template <uint32_t I, uint32_t K, bool First, bool Last>
PS2_VU_FORCEINLINE void pair(State& s) {
    constexpr PairInfo d = kInfo[I];
    VU1State& st = s.st;
    constexpr uint8_t writtenVi = d.writtenVi;
    constexpr bool recordsBackup = d.delays && (Last || nextIsBranch(I));
    int32_t oldVi = 0;
    if constexpr (recordsBackup)
        oldVi = st.vi[writtenVi];

    constexpr uint8_t upperReg = d.upperReg;
    constexpr bool shadow = upperReg != 0u && d.shadowReg != 0u;
    alignas(16) float shadowOut[4];
    float* dst = shadowOut;
    if constexpr (shadow)
        std::memcpy(shadowOut, st.vf[upperReg], 16);
    else if constexpr (upperReg != 0u)
        dst = st.vf[upperReg];
    upper<I, K>(s, dst);
    if constexpr (d.iBit) {
        constexpr uint32_t bits = d.lower;
        float immediate;
        std::memcpy(&immediate, &bits, 4);
        st.i = clampVu(immediate);
    } else if constexpr (d.suppressed != 0u) {
        float kept[4];
        std::memcpy(kept, st.vf[d.suppressed], 16);
        lower<I, K, First>(s);
        std::memcpy(st.vf[d.suppressed], kept, 16);
    } else
        lower<I, K, First>(s);
    if constexpr (shadow)
        Vu1Simd::writeLanes(st.vf[upperReg], shadowOut, d.upperLanes);
    if constexpr (recordsBackup) {
        s.bkReg = writtenVi;
        s.bkVal = oldVi;
    } else if constexpr (Last)
        s.bkReg = 0;
}

template <uint32_t L, uint32_t... K>
PS2_VU_FORCEINLINE void pairs(State& s, std::integer_sequence<uint32_t, K...>) {
    constexpr uint32_t n = sizeof...(K);
    (pair < L + K, K, K == 0, K + 1 == n > (s), ...);
}

// Blocks return their successor (known at compile time), so the dispatcher is a plain indirect-call loop.
struct Next;
using Fn = Next (*)(State&);
struct Next {
    Fn fn;
};

template <uint32_t P>
Next exitAt(State& s) {
    s.exitPc = P; // not compiled: the exact engine continues here
    return {nullptr};
}
template <uint32_t L>
Next blockFn(State& s);
template <uint32_t P>
constexpr Fn fnFor() {
    if constexpr (P < kPairs && kLayout.kind[P] != K_NONE && kLayout.leader[P])
        return &blockFn<P>;
    else
        return &exitAt<P>;
}

template <uint32_t L>
Next blockFn(State& s) {
    constexpr uint32_t E = kLayout.end[L];
    constexpr uint8_t kind = kLayout.kind[L];
    static_assert(E > L);
    if constexpr (kind == K_BRANCH) {
        if (lanes::tryLoop<L>(s))
            return {fnFor<E>()};
    }
    pairs<L>(s, std::make_integer_sequence<uint32_t, E - L>{});
    s.now += E - L;
    s.pairs += E - L;
    if constexpr (kind == K_BRANCH) {
        constexpr uint32_t target = branchTarget(E - 2) & 2047u;
        constexpr bool loop = target <= L;
        if constexpr (kInfo[E - 2].lo != L_B) {
            if (!s.taken)
                return {fnFor<E>()};
        }
        s.branchTarget = (target * 8u) & 0x3FFFu;
        if constexpr (loop) {
            if (s.now >= s.budgetEnd) {
                s.exitPc = target;
                s.stopped = true;
                return {nullptr};
            }
        }
        return {fnFor<target>()};
    } else if constexpr (kind == K_EBIT) {
        s.exitPc = E;
        s.finished = true;
        return {nullptr};
    } else
        return {fnFor<E>()};
}

template <uint32_t I>
constexpr Fn entryFn() {
    if constexpr (kLayout.entry[I])
        return &blockFn<I>;
    else
        return nullptr;
}
template <uint32_t... Is>
constexpr std::array<Fn, sizeof...(Is)> makeEntries(std::integer_sequence<uint32_t, Is...>) {
    return {entryFn<Is>()...};
}
const std::array<Fn, kPairs> kEntries = makeEntries(std::make_integer_sequence<uint32_t, kPairs>{});
} // namespace

uint32_t Vu1Fast::Func::sendPacket(VU1Interpreter& v, const Vu1Step::Ctx& c, const uint8_t* data, uint32_t mask,
                                   uint32_t qword) {
    static_assert(VU1Interpreter::XgkickPipeline::kBufferSize == 0x10000u);
    const uint32_t source = (qword * 16u) & mask;
    uint32_t total = 0;
    for (;;) {
        uint64_t tag;
        std::memcpy(&tag, data + ((source + total) & mask), 8);
        const uint32_t nloop = static_cast<uint32_t>(tag & 0x7FFFu);
        const uint32_t format = static_cast<uint32_t>((tag >> 58) & 0x3u);
        uint32_t nreg = static_cast<uint32_t>((tag >> 60) & 0xFu);
        if (nreg == 0u)
            nreg = 16u;
        uint64_t bytes = 16u;
        if (format == 0u)
            bytes += static_cast<uint64_t>(nloop) * nreg * 16u;
        else if (format == 1u)
            bytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
        else if (format == 2u)
            bytes += static_cast<uint64_t>(nloop) * 16u;
        else {
            v.reportReservedInstruction(false, 0xFFFFFFF8u);
            return 0;
        }
        if (total + bytes > VU1Interpreter::XgkickPipeline::kBufferSize) {
            v.reportReservedInstruction(false, 0xFFFFFFFBu);
            return 0;
        }
        total += static_cast<uint32_t>(bytes);
        if ((tag >> 15) & 1u)
            break;
    }
    // A packet that does not wrap around the end of VU memory is passed in place (the receivers copy it before VU
    // memory can change); only a wrapping one is assembled in the PATH1 buffer.
    const uint8_t* packet = data + source;
    if (source + total > mask + 1u) {
        Vu1Ring::copyPayload(v.m_xgkick.packet.data(), data, mask + 1u, source, total);
        packet = v.m_xgkick.packet.data();
    }
    if (v.m_packetTap)
        v.m_packetTap->emplace_back(packet, packet + total);
    if (Vu1Fast::spare(v).sinkNone) {
    } else if (c.memory)
        c.memory->submitGifPacket(GifPathId::Path1, packet, total);
    else if (c.gs)
        c.gs->processGIFPacket(packet, total);
    return total;
}

bool Vu1Fast::Func::canEnter(const VU1Interpreter& v) {
    const VU1State& st = v.m_state;
    const uint32_t pc = st.pc;
    return v.m_unit == VU1Interpreter::Unit::VU1 && (pc & 7u) == 0u && (pc >> 3) < kPairs &&
           kEntries[pc >> 3] != nullptr && v.m_minReady == ~0ull && !v.m_xgkick.active &&
           Vu1Fast::spare(v).flagCount == 0u && !st.ebit && !st.haltAfterDelaySlot && !st.branchPending;
}

Vu1Func::Result Vu1Fast::Func::run(VU1Interpreter& v, Vu1Step::Ctx& c) {
    VU1State& st = v.m_state;
    State s{st, c.vuData, c.dataSize - 1u, v.m_cycle, c.budgetEnd};
    s.v = &v;
    s.ctx = &c;
    s.bkReg = v.m_viBranchBackupValid ? v.m_viBranchBackupReg : 0;
    s.bkVal = v.m_viBranchBackupValue;
    s.macBase = s.lastMac = st.mac;
    s.clipBase = st.clip;
    s.workingClip = v.m_workingClip;
    s.branchTarget = st.branchTarget;
    for (Next n{kEntries[st.pc >> 3]}; n.fn;)
        n = n.fn(s);
    Vu1Func::Result result = Vu1Func::Result::Fallback;
    uint32_t pc = s.exitPc * 8u;
    if (s.finished) {
        result = Vu1Func::Result::Finished;
        if (pc >= c.codeSize)
            pc = 0;
    } else if (s.stopped)
        result = Vu1Func::Result::Stopped;
    st.pc = pc;
    // Everything pending retires: the exact engine flushes at the end of a program, and a stopped or abandoned run
    // continues from committed state.
    if (s.qValid)
        st.q = s.qValue;
    if (s.pValid)
        st.p = s.pValue;
    st.mac = s.lastMac;
    st.clip = s.workingClip;
    v.m_workingClip = s.workingClip;
    v.m_viBranchBackupValid = s.bkReg != 0;
    v.m_viBranchBackupReg = s.bkReg;
    v.m_viBranchBackupValue = s.bkVal;
    st.ebit = false;
    st.haltAfterDelaySlot = false;
    st.branchPending = false;
    st.branchDelay = 0;
    st.branchTarget = s.branchTarget;
    v.m_cycle = s.now;
    st.cycles = v.m_cycle;
    c.pairs += s.pairs;
    c.programEnded = result == Vu1Func::Result::Finished;
    if (lanes::statsEnabled()) {
        lanes::g_iters += s.laneIters;
        lanes::g_steps += s.laneSteps;
        lanes::g_aborts += s.laneAborts;
        lanes::g_pairs += s.pairs;
        static std::atomic<uint64_t> runs{0};
        if ((runs.fetch_add(1, std::memory_order_relaxed) & ((1u << 22) - 1u)) == (1u << 22) - 1u)
            lanes::printStats(); // the game does not always leave through atexit
    }
    return result;
}

namespace Vu1Func {
bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_FUNC");
        return !(e && e[0] == '0');
    }();
    return on;
}
bool canEnter(const VU1Interpreter& v) {
    return Vu1Fast::Func::canEnter(v);
}
Result run(VU1Interpreter& v, Vu1Step::Ctx& c) {
    return Vu1Fast::Func::run(v, c);
}
} // namespace Vu1Func
