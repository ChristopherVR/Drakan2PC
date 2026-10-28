#include "runtime/ps2_vu1.h"
#include "ps2_vu1_detail.h"

#include <bit>
#include "ps2_vu1_fast.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <smmintrin.h>
#include "ps2_vu1_step.h"

namespace {
int32_t vuFloatToInt(float value, float scale) {
    const double scaled = static_cast<double>(value) * static_cast<double>(scale);
    if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
        return std::numeric_limits<int32_t>::max();
    if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(scaled);
}
} // namespace

// ============================================================================
// Upper instructions (FMAC pipeline)
// ============================================================================
void VU1Interpreter::execUpper(uint32_t instr) {
    m_currentUpperInstruction = instr;
    uint8_t dest = DEST(instr);
    uint8_t ft = FT(instr);
    uint8_t fs = FS(instr);
    uint8_t fd = FD(instr);
    uint8_t op = instr & 0x3F;

    float* vd = m_upperDst;
    alignas(16) float normalizedVs[4];
    alignas(16) float normalizedVt[4];
    alignas(16) float normalizedAcc[4];
    _mm_store_ps(normalizedVs, vuNormalizeVec(_mm_loadu_ps(m_state.vf[fs])));
    _mm_store_ps(normalizedVt, vuNormalizeVec(_mm_loadu_ps(m_state.vf[ft])));
    _mm_store_ps(normalizedAcc, vuNormalizeVec(_mm_loadu_ps(m_state.acc)));
    const float* vs = normalizedVs;
    const float* vt = normalizedVt;
    const float* acc = normalizedAcc;
    const float q = normalizeOperand(m_state.q);
    const float i = normalizeOperand(m_state.i);
    float result[4];

    // Upper opcode decoding (bits 5:0 of upper word)
    switch (op) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03: // ADDbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(vs[c], bc);
            applyFmacDest(vd, result, dest);
            return;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07: // SUBbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(vs[c], bc);
            applyFmacDest(vd, result, dest);
            return;
        }
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: // MADDbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], bc));
            applyFmacDest(vd, result, dest);
            return;
        }
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F: // MSUBbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], bc));
            applyFmacDest(vd, result, dest);
            return;
        }
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13: // MAXbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] > bc) ? vs[c] : bc;
            applyDest(vd, result, dest);
            return;
        }
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17: // MINIbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] < bc) ? vs[c] : bc;
            applyDest(vd, result, dest);
            return;
        }
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B: // MULbc
        {
            float bc = broadcast(vt, op & 3);
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::multiply(vs[c], bc);
            applyFmacDest(vd, result, dest);
            return;
        }
        case 0x1C: // MULq
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::multiply(vs[c], q);
            applyFmacDest(vd, result, dest);
            return;
        case 0x1D: // MAXi
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] > i) ? vs[c] : i;
            applyDest(vd, result, dest);
            return;
        case 0x1E: // MULi
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::multiply(vs[c], i);
            applyFmacDest(vd, result, dest);
            return;
        case 0x1F: // MINIi
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] < i) ? vs[c] : i;
            applyDest(vd, result, dest);
            return;
        case 0x20: // ADDq
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(vs[c], q);
            applyFmacDest(vd, result, dest);
            return;
        case 0x21: // MADDq
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], q));
            applyFmacDest(vd, result, dest);
            return;
        case 0x22: // ADDi
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(vs[c], i);
            applyFmacDest(vd, result, dest);
            return;
        case 0x23: // MADDi
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], i));
            applyFmacDest(vd, result, dest);
            return;
        case 0x24: // SUBq
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(vs[c], q);
            applyFmacDest(vd, result, dest);
            return;
        case 0x25: // MSUBq
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], q));
            applyFmacDest(vd, result, dest);
            return;
        case 0x26: // SUBi
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(vs[c], i);
            applyFmacDest(vd, result, dest);
            return;
        case 0x27: // MSUBi
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], i));
            applyFmacDest(vd, result, dest);
            return;
        case 0x28: // ADD
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(vs[c], vt[c]);
            applyFmacDest(vd, result, dest);
            return;
        case 0x29: // MADD
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], vt[c]));
            applyFmacDest(vd, result, dest);
            return;
        case 0x2A: // MUL
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::multiply(vs[c], vt[c]);
            applyFmacDest(vd, result, dest);
            return;
        case 0x2B: // MAX
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] > vt[c]) ? vs[c] : vt[c];
            applyDest(vd, result, dest);
            return;
        case 0x2C: // SUB
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(vs[c], vt[c]);
            applyFmacDest(vd, result, dest);
            return;
        case 0x2D: // MSUB
            for (int c = 0; c < 4; c++)
                result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], vt[c]));
            applyFmacDest(vd, result, dest);
            return;
        case 0x2E: // OPMSUB
            result[0] = Vu1Simd::subtract(acc[0], Vu1Simd::multiply(vs[1], vt[2]));
            result[1] = Vu1Simd::subtract(acc[1], Vu1Simd::multiply(vs[2], vt[0]));
            result[2] = Vu1Simd::subtract(acc[2], Vu1Simd::multiply(vs[0], vt[1]));
            result[3] = 0.0f;
            applyFmacDest(vd, result, dest);
            return;
        case 0x2F: // MINI
            for (int c = 0; c < 4; c++)
                result[c] = (vs[c] < vt[c]) ? vs[c] : vt[c];
            applyDest(vd, result, dest);
            return;

        // Upper special group (low op 0x3C..0x3F).
        // Like lower1 special, the real selector is not just bits 5:0.  Dobie decodes:
        //   op = (instr & 0x3) | ((instr >> 4) & 0x7C)
        // Several instructions in this group also use FT as the destination, not FD.
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F: {
            const uint8_t specialOp = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
            float* vtDest = m_upperDst;

            switch (specialOp) {
                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03: // ADDAbc
                {
                    float bc = broadcast(vt, specialOp & 3);
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(vs[c], bc);
                    applyFmacDestAcc(result, dest);
                    return;
                }
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07: // SUBAbc
                {
                    float bc = broadcast(vt, specialOp & 3);
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(vs[c], bc);
                    applyFmacDestAcc(result, dest);
                    return;
                }
                case 0x08:
                case 0x09:
                case 0x0A:
                case 0x0B: // MADDAbc
                {
                    float bc = broadcast(vt, specialOp & 3);
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], bc));
                    applyFmacDestAcc(result, dest);
                    return;
                }
                case 0x0C:
                case 0x0D:
                case 0x0E:
                case 0x0F: // MSUBAbc
                {
                    float bc = broadcast(vt, specialOp & 3);
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], bc));
                    applyFmacDestAcc(result, dest);
                    return;
                }
                case 0x10: // ITOF0
                    for (int c = 0; c < 4; c++) {
                        int32_t iv;
                        std::memcpy(&iv, &m_state.vf[fs][c], 4);
                        result[c] = Vu1Simd::fromIntScalar(iv);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x11: // ITOF4
                    for (int c = 0; c < 4; c++) {
                        int32_t iv;
                        std::memcpy(&iv, &m_state.vf[fs][c], 4);
                        result[c] = Vu1Simd::divide(Vu1Simd::fromIntScalar(iv), 16.0f);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x12: // ITOF12
                    for (int c = 0; c < 4; c++) {
                        int32_t iv;
                        std::memcpy(&iv, &m_state.vf[fs][c], 4);
                        result[c] = Vu1Simd::divide(Vu1Simd::fromIntScalar(iv), 4096.0f);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x13: // ITOF15
                    for (int c = 0; c < 4; c++) {
                        int32_t iv;
                        std::memcpy(&iv, &m_state.vf[fs][c], 4);
                        result[c] = Vu1Simd::divide(Vu1Simd::fromIntScalar(iv), 32768.0f);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x14: // FTOI0
                    for (int c = 0; c < 4; c++) {
                        int32_t iv = vuFloatToInt(vs[c], 1.0f);
                        std::memcpy(&result[c], &iv, 4);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x15: // FTOI4
                    for (int c = 0; c < 4; c++) {
                        int32_t iv = vuFloatToInt(vs[c], 16.0f);
                        std::memcpy(&result[c], &iv, 4);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x16: // FTOI12
                    for (int c = 0; c < 4; c++) {
                        int32_t iv = vuFloatToInt(vs[c], 4096.0f);
                        std::memcpy(&result[c], &iv, 4);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x17: // FTOI15
                    for (int c = 0; c < 4; c++) {
                        int32_t iv = vuFloatToInt(vs[c], 32768.0f);
                        std::memcpy(&result[c], &iv, 4);
                    }
                    applyDest(vtDest, result, dest);
                    return;
                case 0x18:
                case 0x19:
                case 0x1A:
                case 0x1B: // MULAbc
                {
                    float bc = broadcast(vt, specialOp & 3);
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::multiply(vs[c], bc);
                    applyFmacDestAcc(result, dest);
                    return;
                }
                case 0x1C: // MULAq
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::multiply(vs[c], q);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x1D: // ABS
                    for (int c = 0; c < 4; c++)
                        result[c] = std::fabs(vs[c]);
                    applyDest(vtDest, result, dest);
                    return;
                case 0x1E: // MULAi
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::multiply(vs[c], i);
                    applyFmacDestAcc(result, dest);
                    return;
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
                    queueClip(flags);
                    return;
                }
                case 0x20: // ADDAq
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(vs[c], q);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x21: // MADDAq
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], q));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x22: // ADDAi
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(vs[c], i);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x23: // MADDAi
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], i));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x24: // SUBAq
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(vs[c], q);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x25: // MSUBAq
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], q));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x26: // SUBAi
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(vs[c], i);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x27: // MSUBAi
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], i));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x28: // ADDA
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(vs[c], vt[c]);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x29: // MADDA
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::add(acc[c], Vu1Simd::multiply(vs[c], vt[c]));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x2A: // MULA
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::multiply(vs[c], vt[c]);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x2C: // SUBA
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(vs[c], vt[c]);
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x2D: // MSUBA
                    for (int c = 0; c < 4; c++)
                        result[c] = Vu1Simd::subtract(acc[c], Vu1Simd::multiply(vs[c], vt[c]));
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x2E: // OPMULA
                    result[0] = Vu1Simd::multiply(vs[1], vt[2]);
                    result[1] = Vu1Simd::multiply(vs[2], vt[0]);
                    result[2] = Vu1Simd::multiply(vs[0], vt[1]);
                    result[3] = 0.0f;
                    applyFmacDestAcc(result, dest);
                    return;
                case 0x2F:
                case 0x30: // NOP
                    return;
                default:
                    reportReservedInstruction(true, instr);
                    return;
            }
        }

        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        default:
            reportReservedInstruction(true, instr);
            return;
    }
}

void VU1Interpreter::execUpperFmac(const DecodedInstructionPair& decoded) {
    Vu1Step::upperFmac(*this, decoded);
}

void Vu1Step::fmacSlow(uint8_t kind, const float* vs4, const float* b4, const float* acc4, const float* product4,
                       float* result4, int comps, int ordR, int ordP, uint32_t* flags3) {
    using namespace Vu1FmacDetail;
    const __m128 vs = _mm_load_ps(vs4);
    const __m128 b = _mm_load_ps(b4);
    const __m128 acc = _mm_load_ps(acc4);
    const __m128 product = _mm_load_ps(product4);
    __m128 result = _mm_load_ps(result4);
    const bool productSum = kind >= 4;
    uint32_t mac, status, extraSticky;
    if (flagsKnownClasses(kind, vs, b, product, result, comps, ordR, ordP, mac, status, extraSticky)) {
    } else {
        const __m128d vsLo = _mm_cvtps_pd(vs);
        const __m128d vsHi = _mm_cvtps_pd(_mm_movehl_ps(vs, vs));
        const __m128d bLo = _mm_cvtps_pd(b);
        const __m128d bHi = _mm_cvtps_pd(_mm_movehl_ps(b, b));
        __m128d exLo, exHi;
        __m128d pLo = _mm_setzero_pd(), pHi = _mm_setzero_pd();
        switch (kind) {
            case 1:
                exLo = _mm_add_pd(vsLo, bLo);
                exHi = _mm_add_pd(vsHi, bHi);
                break;
            case 2:
                exLo = _mm_sub_pd(vsLo, bLo);
                exHi = _mm_sub_pd(vsHi, bHi);
                break;
            case 3:
                exLo = _mm_mul_pd(vsLo, bLo);
                exHi = _mm_mul_pd(vsHi, bHi);
                break;
            case 4:
                pLo = _mm_mul_pd(vsLo, bLo);
                pHi = _mm_mul_pd(vsHi, bHi);
                exLo = _mm_add_pd(_mm_cvtps_pd(acc), pLo);
                exHi = _mm_add_pd(_mm_cvtps_pd(_mm_movehl_ps(acc, acc)), pHi);
                break;
            default:
                pLo = _mm_mul_pd(vsLo, bLo);
                pHi = _mm_mul_pd(vsHi, bHi);
                exLo = _mm_sub_pd(_mm_cvtps_pd(acc), pLo);
                exHi = _mm_sub_pd(_mm_cvtps_pd(_mm_movehl_ps(acc, acc)), pHi);
                break;
        }
        const Classes r = classifyExact(exLo, exHi);
        const int zeroFlag = (r.zero | r.under) & comps; // flag 0x1 (underflow reports zero as well)
        const int negFlag = r.neg & comps;
        const int underFlag = r.under & comps;
        const int overFlag = r.over & comps;
        mac = static_cast<uint32_t>(kRev4[zeroFlag]) | (static_cast<uint32_t>(kRev4[negFlag]) << 4) |
              (static_cast<uint32_t>(kRev4[underFlag]) << 8) | (static_cast<uint32_t>(kRev4[overFlag]) << 12);
        status = (zeroFlag != 0 ? 0x1u : 0u) | (negFlag != 0 ? 0x2u : 0u) | (underFlag != 0 ? 0x4u : 0u) |
                 (overFlag != 0 ? 0x8u : 0u);
        extraSticky = 0u;
        if (productSum) {
            const Classes pc = classifyExact(pLo, pHi);
            extraSticky = (((pc.zero | pc.under) & comps) != 0 ? 0x1u : 0u) | ((pc.neg & comps) != 0 ? 0x2u : 0u) |
                          ((pc.under & comps) != 0 ? 0x4u : 0u) | ((pc.over & comps) != 0 ? 0x8u : 0u);
        }
        // zero / underflow lanes become +-0, overflow lanes +-FLT_MAX (sign of the exact result)
        const int fix = (r.zero | r.under | r.over) & comps;
        if (fix != 0) {
            const __m128i sign =
                _mm_and_si128(_mm_castps_si128(compMask(r.neg)), _mm_set1_epi32(static_cast<int>(0x80000000u)));
            const __m128i max = _mm_and_si128(_mm_castps_si128(compMask(r.over)), _mm_set1_epi32(0x7F7FFFFF));
            result = _mm_blendv_ps(result, _mm_castsi128_ps(_mm_or_si128(sign, max)), compMask(fix));
        }
    }

    _mm_store_ps(result4, result);
    flags3[0] = mac;
    flags3[1] = status;
    flags3[2] = extraSticky;
}
