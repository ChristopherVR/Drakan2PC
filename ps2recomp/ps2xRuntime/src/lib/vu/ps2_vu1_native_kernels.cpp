#include "aot/ps2_vu1_aot.h"
#include "aot/ps2_vu1_blocks.h"
#include "ps2_vu1_decode.h"
#include "ps2_vu1_normalized_lanes.h"
#include <array>
#include <utility>
#include <cstdlib>
#include "aot/ps2_vu1_image.inc"
#include "ps2_vu1_image_fingerprint.h"
namespace {
using Dec = Vu1Step::Dec;
template <size_t I>
inline constexpr Dec kDec = Vu1Decode::decodePair(uint32_t(kVu1Image[I]), uint32_t(kVu1Image[I] >> 32), true);
const uint32_t kNativeLoopLimit = [] {
    const char* e = std::getenv("DRAKAN_VU_NATIVE_LOOP_LIMIT");
    const long n = e ? std::strtol(e, nullptr, 10) : 32;
    return uint32_t(n < 1 ? 1 : n > 256 ? 256 : n);
}();
bool nativeDataSafe(const VU1Interpreter& v, const Vu1Step::Ctx& c) {
    static constexpr uint32_t zero[4] = {0, 0, 0, 0x3f800000u};
    if (!c.vuData || c.dataSize < 16 || v.state().vi[0] != 0 || std::memcmp(v.state().vf[0], zero, 16))
        return false;
    const auto disjoint = [](const void* a, size_t an, const void* b, size_t bn) {
        if (!an || !bn)
            return true;
        const auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
        return x <= y ? y - x >= an : x - y >= bn;
    };
    return disjoint(&v, sizeof v, &c, sizeof c) && disjoint(c.vuData, c.dataSize, &v, sizeof v) &&
           disjoint(c.vuData, c.dataSize, &c, sizeof c);
}
} // namespace
namespace {
Vu1Aot::Fn g_originalPacket = nullptr;
constexpr uint32_t kPacketPcs[] = {489, 490, 491, 492, 493, 494, 495, 496, 498, 499, 501, 502, 503, 504, 505, 506, 507};
struct PacketImage {
    uint64_t words[17]{};
};
inline constexpr auto kPacketImage = [] {
    PacketImage image;
    for (uint32_t i = 0; i < 17; ++i)
        image.words[i] = kVu1Image[kPacketPcs[i]];
    return image;
}();
template <uint32_t Count>
inline constexpr auto kPacketSchedule = Vu1Blocks::schedule(kPacketImage.words, 0, Count, true, 4u);
constexpr bool packetEligible() {
    for (uint32_t i = 0; i < 17; ++i) {
        const uint32_t pc = kPacketPcs[i];
        const auto d = Vu1Decode::decodePair(uint32_t(kVu1Image[pc]), uint32_t(kVu1Image[pc] >> 32), true);
        if (!Vu1Blocks::blockEligible(d))
            return false;
        if ((d.lowerUsage.pipeline == Vu1Decode::PipelineBranch) != (i == 4 || i == 6 || i == 8 || i == 15))
            return false;
    }
    return true;
}
template <uint32_t Count>
constexpr bool packetPrefixAgrees() {
    for (uint32_t i = 0; i < Count; ++i)
        if (kPacketSchedule<Count>.T[i] != kPacketSchedule<17>.T[i])
            return false;
    return true;
}
static_assert(packetEligible() && kPacketSchedule<17>.qSafe);
static_assert(packetPrefixAgrees<6>() && packetPrefixAgrees<8>() && packetPrefixAgrees<10>());

static_assert(kDec<493>.lowerUsage.pipeline == Vu1Decode::PipelineBranch &&
              kDec<495>.lowerUsage.pipeline == Vu1Decode::PipelineBranch &&
              kDec<498>.lowerUsage.pipeline == Vu1Decode::PipelineBranch &&
              kDec<506>.lowerUsage.pipeline == Vu1Decode::PipelineBranch);

static_assert(Vu1Fingerprint::ofPcs(kVu1Image, kPacketPcs) == 0x7312ec38f65347ceull &&
              kPacketSchedule<17>.evCount == 0);
template <uint32_t Count>
PS2_VU_FORCEINLINE void finishNativePacket(VU1Interpreter& v, Vu1Step::Ctx& c, uint64_t base, uint32_t completed,
                                           uint32_t next) {
    constexpr auto& prefix = kPacketSchedule<Count>;
    Vu1Step::blockExit(v, c, base, completed * kPacketSchedule<17>.cycles + prefix.cycles, completed * 17u + Count,
                       next, false, prefix.ev, prefix.evCount);
    if (Vu1Aot::g_blockStats) {
        for (uint32_t i = 0; i < completed; ++i)
            Vu1Step::blockCount(v, true, 17);
        Vu1Step::blockCount(v, true, Count);
    }
}
} // namespace
bool Vu1Step::packetKernel(VU1Interpreter& v, Ctx& c) {
    constexpr auto& bi = kPacketSchedule<17>;
    if (!nativeDataSafe(v, c) || !traceScalarsIdle(v) ||
        !blockCanEnter(v, c, bi.cycles, false, bi.eLo, bi.eHi, bi.eVa, bi.rLo, bi.rHi, bi.rVa))
        return g_originalPacket(v, c);
    auto& st = v.m_state;
    const uint64_t base = blockBase(v);
    uint32_t done = 0;
    auto viWrite = [&]<uint32_t Pc>(uint8_t reg, int32_t previous) {
        v.m_viBranchBackupValid = false;
        if constexpr (kDec<Pc>.lowerUsage.delaysNextBranchRead) {
            v.m_viBranchBackupValue = previous;
            v.m_viBranchBackupReg = reg;
            v.m_viBranchBackupValid = true;
        }
    };
    auto load = [&]<uint32_t Reg, uint32_t Lanes>(int32_t index) {
        const uint32_t address = (uint32_t(index) * 16u) & (c.dataSize - 1u);
        if (address + 16u <= c.dataSize) {
            if constexpr (Lanes == 15)
                std::memcpy(st.vf[Reg], c.vuData + address, 16);
            else if constexpr (Lanes == 14)
                std::memcpy(st.vf[Reg], c.vuData + address, 12);
        }
        v.m_viBranchBackupValid = false;
    };
    auto branch = [&](bool taken, uint32_t target, uint32_t fallthrough) {
        if (taken)
            st.branchTarget = (target * 8u) & microMask(v);
        st.branchPending = false;
        st.branchDelay = 0;
        st.pc = taken ? st.branchTarget : fallthrough * 8u;
    };
    do {
        const uint64_t iteration = base + uint64_t(done) * bi.cycles;
        const int32_t old6 = st.vi[6];
        uint32_t first;
        std::memcpy(&first, st.vf[19], 4);
        st.vi[6] = int16_t(first & 0xffffu);
        viWrite.operator()<489>(6, old6);
        const __m128 order = _mm_loadu_ps(st.vf[19]);
        _mm_storeu_ps(st.vf[19], _mm_shuffle_ps(order, order, _MM_SHUFFLE(0, 3, 2, 1)));
        v.m_viBranchBackupValid = false;
        const int32_t old1 = st.vi[1];
        st.vi[1] = int16_t(old1 - 1);
        viWrite.operator()<491>(1, old1);
        const int32_t old13 = st.vi[13];
        st.vi[13] = int16_t(old13 + 1);
        viWrite.operator()<492>(13, old13);
        const bool leave = int16_t(st.vi[6]) <= 0;
        load.operator()<12, 15>(st.vi[6] + 65);
        branch(leave, 517, 495);
        if (leave) {
            finishNativePacket<6>(v, c, base, done, st.pc);
            return true;
        }
        const bool material = int16_t(st.vi[3]) > 0;
        load.operator()<13, 14>(st.vi[6] + 515);
        branch(material, 498, 497);
        if (!material) {
            finishNativePacket<8>(v, c, base, done, st.pc);
            return true;
        }
        const bool texture = int16_t(st.vi[4]) > 0;
        load.operator()<17, 15>(st.vi[6] + 290);
        branch(texture, 501, 500);
        if (!texture) {
            finishNativePacket<10>(v, c, base, done, st.pc);
            return true;
        }
        blockPair(v, kDec<501>, c, iteration + bi.T[10], 501u * 8u);
        uint32_t words12[4], words17[4], words13[4];
        std::memcpy(words12, st.vf[12], 16);
        std::memcpy(words17, st.vf[17], 16);
        std::memcpy(words13, st.vf[13], 16);
        const int32_t destination = st.vi[9];
        const auto address = [&](int32_t offset) { return (uint32_t(destination + offset) * 16u) & (c.dataSize - 1u); };
        const auto put = [&](uint32_t at, const uint32_t* words, uint8_t lanes) {
            if (at + 16u <= c.dataSize)
                store(v, at, words, lanes);
        };
        v.m_cycle = iteration + bi.T[11];
        put(address(3), words12, 15);
        v.m_cycle = iteration + bi.T[12];
        put(address(2), words12, 1);
        v.m_cycle = iteration + bi.T[13];
        put(address(1), words17, 15);
        v.m_cycle = iteration + bi.T[14];
        put(address(0), words13, 14);
        v.m_viBranchBackupValid = false;
        st.pc = 506u * 8u;
        const bool again = int16_t(st.vi[1]) > 0;
        if (again) {
            st.branchTarget = 489u * 8u;
            st.branchPending = true;
            st.branchDelay = 1;
        }
        const int32_t old9 = st.vi[9];
        st.vi[9] = int16_t(old9 + 4);
        viWrite.operator()<507>(9, old9);
        // Every register write in a completed iteration is ready at its backedge.
        // Keep only the entry retirement and the final partial prefix, as no pair
        // in this trace observes the intermediate busy masks.
        ++done;
        v.m_cycle = base + uint64_t(done) * bi.cycles;
        if (st.branchPending) {
            st.pc = st.branchTarget;
            st.branchPending = false;
            st.branchDelay = 0;
        } else
            st.pc = 508u * 8u;
        if (!again || done == kNativeLoopLimit || !nativeContinue(v, c) || c.budgetEnd - v.m_cycle <= bi.cycles)
            break;
    } while (true);
    blockExit(v, c, base, done * bi.cycles, done * 17u, st.pc, false, bi.ev, bi.evCount);
    if (Vu1Aot::g_blockStats)
        for (uint32_t i = 0; i < done; ++i)
            blockCount(v, true, 17);
    return true;
}

namespace {
Vu1Aot::Fn g_originalUv = nullptr;
static_assert(Vu1Fingerprint::ofRange(kVu1Image, 155, 7) == 0x57e52eab7a014eddull);
constexpr auto kUvSchedule = Vu1Blocks::schedule(kVu1Image, 155, 7, true, 4);
static_assert(kUvSchedule.cycles >= 3 && kUvSchedule.evCount == 0 && kUvSchedule.qSafe &&
              kUvSchedule.T[2] + 4u <= kUvSchedule.cycles);
constexpr auto kUvInputs = [] {
    std::array<uint8_t, 7> inputs{};
    Vu1SelectedNormalized::State known;
    for (uint32_t i = 0; i < 7; ++i) {
        const auto word = kVu1Image[155 + i];
        const auto d = Vu1Decode::decodePair(uint32_t(word), uint32_t(word >> 32), true);
        inputs[i] = known.inputs(d);
        known.apply(d);
    }
    return inputs;
}();
} // namespace
// Fixed UV transform loop 155..161. Every output and MAC/status result is
// ready at the backedge, so retire the entry once and apply the due flag batch
// once. Stores still observe their original issue cycles and PATH1 hazards.
bool Vu1Step::uvKernel(VU1Interpreter& v, Ctx& c) {
    constexpr auto& bi = kUvSchedule;
    if (!nativeDataSafe(v, c) || !traceScalarsIdle(v) ||
        !blockCanEnter(v, c, bi.cycles, true, bi.eLo, bi.eHi, bi.eVa, bi.rLo, bi.rHi, bi.rVa))
        return g_originalUv(v, c);
    auto& st = v.m_state;
    const uint64_t base = blockBase(v);
    uint32_t done = 0;
    Vu1FlagBatch batch;
    v.m_upperAccDst = st.acc;
    do {
        const uint64_t iteration = base + uint64_t(done) * bi.cycles;
        v.m_cycle = iteration + bi.T[0];
        upperFmac<true, kUvInputs[0]>(v, kDec<155>, &batch);
        v.m_cycle = iteration + bi.T[1];
        upperFmac<true, kUvInputs[1]>(v, kDec<156>, &batch);
        v.m_upperDst = st.vf[15];
        v.m_cycle = iteration + bi.T[2];
        upperFmac<true, kUvInputs[2]>(v, kDec<157>, &batch);
        const uint32_t input = (uint32_t(st.vi[12] + 1) * 16u) & (c.dataSize - 1u);
        if (input + 16u <= c.dataSize)
            std::memcpy(st.vf[14], c.vuData + input, 16);
        const int32_t old11 = st.vi[11];
        st.vi[11] = int16_t(old11 - 1);
        v.m_viBranchBackupValid = false;
        if constexpr (kDec<158>.lowerUsage.delaysNextBranchRead) {
            v.m_viBranchBackupValue = old11;
            v.m_viBranchBackupReg = 11;
        }
        st.pc = 160u * 8u;
        const bool again = int16_t(st.vi[11]) > 0;
        if (again) {
            st.branchTarget = 155u * 8u;
            st.branchPending = true;
            st.branchDelay = 1;
        }
        const int32_t old12 = st.vi[12];
        const uint32_t output = (uint32_t(uint16_t(old12)) * 16u) & (c.dataSize - 1u);
        uint32_t words[4];
        std::memcpy(words, st.vf[15], 16);
        v.m_cycle = iteration + bi.T[6];
        if (output + 16u <= c.dataSize)
            store(v, output, words, 12);
        st.vi[12] = int16_t(old12 + 1);
        v.m_viBranchBackupValid = false;
        if constexpr (kDec<161>.lowerUsage.delaysNextBranchRead) {
            v.m_viBranchBackupValue = old12;
            v.m_viBranchBackupReg = 12;
            v.m_viBranchBackupValid = true;
        }
        ++done;
        v.m_cycle = base + uint64_t(done) * bi.cycles;
        if (st.branchPending) {
            st.pc = st.branchTarget;
            st.branchPending = false;
            st.branchDelay = 0;
        } else
            st.pc = 162u * 8u;
        if (!again || done == kNativeLoopLimit || !nativeContinue(v, c) || c.budgetEnd - v.m_cycle <= bi.cycles)
            break;
    } while (true);
    blockExit(v, c, base, done * bi.cycles, done * 7u, st.pc, false, bi.ev, bi.evCount);
    finishFlagBatch(v, batch);
    if (Vu1Aot::g_blockStats)
        for (uint32_t i = 0; i < done; ++i)
            blockCount(v, true, 7);
    return true;
}
namespace {
// Fixed transform loop: four arithmetic pairs and a delayed backedge.
constexpr auto kTransformSchedule = Vu1Blocks::schedule(kVu1Image, 334, 7, true, 4);
static_assert(Vu1Fingerprint::ofRange(kVu1Image, 334, 7) == 0x623d454e800ae7c2ull, "transform loop code");
static_assert(kTransformSchedule.evCount == 0, "events");
static_assert(kTransformSchedule.qSafe, "Q");
static_assert(kTransformSchedule.T[3] + 4u <= kTransformSchedule.cycles, "flags");
template <unsigned Pc>
inline constexpr auto kTransformPairDec = [] {
    auto d = kDec<Pc>;
    d.upper = (d.upper & ~(15u << 21)) | (15u << 21);
    return d;
}();
constexpr auto kTransformInputs = [] {
    std::array<uint8_t, 7> inputs{};
    Vu1SelectedNormalized::State known;
    for (unsigned i = 0; i < 7; ++i) {
        const auto word = kVu1Image[334 + i];
        const auto d = Vu1Decode::decodePair(uint32_t(word), uint32_t(word >> 32), true);
        inputs[i] = known.inputs(d);
        known.apply(d);
    }
    return inputs;
}();
} // namespace
bool Vu1Step::transformKernel(VU1Interpreter& v, Ctx& c) {
    constexpr auto& bi = kTransformSchedule;
    if (!nativeDataSafe(v, c) || !traceScalarsIdle(v) ||
        !blockCanEnter(v, c, bi.cycles, true, bi.eLo, bi.eHi, bi.eVa, bi.rLo, bi.rHi, bi.rVa))
        return Vu1Aot::g_table[334](v, c);
    auto& st = v.m_state;
    const uint64_t base = blockBase(v);
    uint32_t done = 0;
    Vu1FlagBatch batch;
    v.m_upperAccDst = st.acc;
    const __m128 matrixX = readOperand(st, 11, false);
    const __m128 matrixY = readOperand(st, 13, false);
    const __m128 matrixZ = readOperand(st, 19, false);
    const __m128 matrixW = readOperand(st, 14, false);
    const __m128 pairX = _mm_movelh_ps(matrixX, matrixX), pairY = _mm_movelh_ps(matrixY, matrixY);
    const __m128 pairZ = _mm_movelh_ps(matrixZ, matrixZ), pairW = _mm_movelh_ps(matrixW, matrixW);

    do {
        const uint64_t iteration = base + uint64_t(done) * bi.cycles;
        const __m128 inputVector = readOperand(st, 20, false);
        // Two independent XY transforms fill all four SIMD lanes. The
        // look-ahead load may move before SQI only when it cannot read SQI's
        // destination. Other aliases retain the original ordered loads/stores.
        const uint32_t output0 = (uint32_t(uint16_t(st.vi[13])) * 16u) & (c.dataSize - 1u);
        const uint32_t input1 = (uint32_t(uint16_t(st.vi[9])) * 16u) & (c.dataSize - 1u);
        if (int16_t(st.vi[10]) >= 2 && done + 2 <= kNativeLoopLimit && c.budgetEnd - iteration > 2u * bi.cycles &&
            input1 + 16u <= c.dataSize && output0 != input1) {
            const __m128 next = Vu1Simd::normalize(_mm_loadu_ps(reinterpret_cast<const float*>(c.vuData + input1)));
            const __m128 x = _mm_shuffle_ps(inputVector, next, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 y = _mm_shuffle_ps(inputVector, next, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 z = _mm_shuffle_ps(inputVector, next, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 w = _mm_shuffle_ps(inputVector, next, _MM_SHUFFLE(3, 3, 3, 3));
            const auto f0 = calculateFmac(kTransformPairDec<334>, pairX, x, _mm_setzero_ps());
            const auto f1 = calculateFmac(kTransformPairDec<335>, pairY, y, f0.value);
            const auto f2 = calculateFmac(kTransformPairDec<336>, pairZ, z, f1.value);
            const auto f3 = calculateFmac(kTransformPairDec<337>, pairW, w, f2.value);
            const __m128 r0 = f0.value, r2 = f2.value, r3 = f3.value;
            uint32_t previous[4], first[4];
            std::memcpy(previous, st.vf[18], 16);
            _mm_storeu_ps(reinterpret_cast<float*>(first), Vu1Simd::mergeLanes(_mm_loadu_ps(st.vf[18]), r3, 3));
            v.m_cycle = iteration + bi.T[0];
            v.m_currentUpperInstruction = kDec<334>.upper;
            _mm_storeu_ps(st.acc, Vu1Simd::mergeLanes(_mm_loadu_ps(st.acc), r0, 3));
            if (output0 + 16u <= c.dataSize)
                store(v, output0, previous, 15);
            const uint32_t output1 = (uint32_t(uint16_t(st.vi[13] + 1)) * 16u) & (c.dataSize - 1u);
            v.m_cycle = iteration + bi.cycles + bi.T[0];
            _mm_storeu_ps(st.acc, Vu1Simd::mergeLanes(_mm_loadu_ps(st.acc), _mm_movehl_ps(r0, r0), 3));
            if (output1 + 16u <= c.dataSize)
                store(v, output1, first, 15);
            _mm_storeu_ps(st.acc, Vu1Simd::mergeLanes(_mm_loadu_ps(st.acc), _mm_movehl_ps(r2, r2), 3));
            _mm_storeu_ps(st.vf[18], Vu1Simd::mergeLanes(_mm_loadu_ps(st.vf[18]), _mm_movehl_ps(r3, r3), 3));
            // MAC/current STATUS retain the last vertex. Sticky STATUS
            // accumulates every lane and every intermediate product.
            const uint32_t mac = (f3.mac & 0x3333u) << 2;
            const uint32_t status = ((mac & 0xfu) ? 1u : 0u) | ((mac & 0xf0u) ? 2u : 0u) | ((mac & 0xf00u) ? 4u : 0u) |
                                    ((mac & 0xf000u) ? 8u : 0u);
            batch.add(mac, status,
                      f0.status | f0.extraSticky | f1.status | f1.extraSticky | f2.status | f2.extraSticky | f3.status |
                          f3.extraSticky);
            const uint32_t input2 = (uint32_t(uint16_t(st.vi[9] + 1)) * 16u) & (c.dataSize - 1u);
            if (input2 + 16u <= c.dataSize)
                std::memcpy(st.vf[20], c.vuData + input2, 16);
            st.vi[13] = int16_t(st.vi[13] + 2);
            st.vi[9] = int16_t(st.vi[9] + 2);
            st.vi[10] = int16_t(st.vi[10] - 2);
            v.m_currentUpperInstruction = kDec<337>.upper;
            v.m_viBranchBackupValid = false;
            st.branchTarget = (334u * 8u) & microMask(v);
            st.branchPending = false;
            st.branchDelay = 0;
            const bool again = int16_t(st.vi[10]) > 0;
            st.pc = again ? st.branchTarget : 341u * 8u;
            done += 2;
            v.m_cycle = base + uint64_t(done) * bi.cycles;
            if (!again || done == kNativeLoopLimit || !nativeContinue(v, c) || c.budgetEnd - v.m_cycle <= bi.cycles)
                break;
            continue;
        }
        // SQI stores the previous vertex before the MADD replaces VF18.
        const int32_t output = st.vi[13];
        const uint32_t at = (uint32_t(uint16_t(output)) * 16u) & (c.dataSize - 1u);
        uint32_t words[4];
        std::memcpy(words, st.vf[18], sizeof words);
        v.m_cycle = iteration + bi.T[0];
        upperFmacOperands<true, kTransformInputs[0]>(
            v, kDec<334>, matrixX, _mm_shuffle_ps(inputVector, inputVector, _MM_SHUFFLE(0, 0, 0, 0)), &batch);
        if (at + 16u <= c.dataSize)
            store(v, at, words, 15);
        st.vi[13] = int16_t(output + 1);
        v.m_cycle = iteration + bi.T[1];
        upperFmacOperands<true, kTransformInputs[1]>(
            v, kDec<335>, matrixY, _mm_shuffle_ps(inputVector, inputVector, _MM_SHUFFLE(1, 1, 1, 1)), &batch);
        v.m_cycle = iteration + bi.T[2];
        upperFmacOperands<true, kTransformInputs[2]>(
            v, kDec<336>, matrixZ, _mm_shuffle_ps(inputVector, inputVector, _MM_SHUFFLE(2, 2, 2, 2)), &batch);
        st.vi[10] = int16_t(st.vi[10] - 1);
        v.m_upperDst = st.vf[18];
        v.m_cycle = iteration + bi.T[3];
        upperFmacOperands<true, kTransformInputs[3]>(
            v, kDec<337>, matrixW, _mm_shuffle_ps(inputVector, inputVector, _MM_SHUFFLE(3, 3, 3, 3)), &batch);
        // The upper unit consumes the old VF20; the paired LQI replaces it.
        const int32_t input = st.vi[9];
        const uint32_t from = (uint32_t(uint16_t(input)) * 16u) & (c.dataSize - 1u);
        if (from + 16u <= c.dataSize)
            std::memcpy(st.vf[20], c.vuData + from, 16);
        st.vi[9] = int16_t(input + 1);
        v.m_viBranchBackupValid = false;
        const bool again = int16_t(st.vi[10]) > 0;
        if (again)
            st.branchTarget = (334u * 8u) & microMask(v);
        st.branchPending = false;
        st.branchDelay = 0;
        st.pc = again ? st.branchTarget : 341u * 8u;
        ++done;
        v.m_cycle = base + uint64_t(done) * bi.cycles;
        if (!again || done == kNativeLoopLimit || !nativeContinue(v, c) || c.budgetEnd - v.m_cycle <= bi.cycles)
            break;
    } while (true);
    blockExit(v, c, base, done * bi.cycles, done * 7u, st.pc, false, bi.ev, bi.evCount);
    finishFlagBatch(v, batch);
    if (Vu1Aot::g_blockStats)
        for (uint32_t i = 0; i < done; ++i)
            blockCount(v, true, 7);
    return true;
}
namespace Vu1Aot {
void registerNativeKernels() {
    const char* setting = std::getenv("DRAKAN_VU_NATIVE_KERNELS");
    const unsigned mode = setting ? unsigned(std::strtoul(setting, nullptr, 0)) & 3u : 3u;
    if ((mode & 1u) && recoveryEnabled()) {
        const char* trace = std::getenv("DRAKAN_VU_TRACE");
        if (!(trace && trace[0] == '0')) {
            if (g_dispatch[489] != &Vu1Step::packetKernel)
                g_originalPacket = g_dispatch[489];
            g_dispatch[489] = &Vu1Step::packetKernel;
        }
    }
    if (mode & 2u) {
        if (g_dispatch[155] != &Vu1Step::uvKernel)
            g_originalUv = g_dispatch[155];
        g_dispatch[155] = &Vu1Step::uvKernel;
    }
    const char* transform = std::getenv("DRAKAN_VU_TRANSFORM_KERNEL");
    if (transform && transform[0] == '1')
        g_dispatch[334] = &Vu1Step::transformKernel;
}
} // namespace Vu1Aot
