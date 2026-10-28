#include "vu/ps2_vu1_step.h"
#include "vu/ps2_vu1_decode.h"
#include "vu/aot/ps2_vu1_blocks.h"
#include "vu/ref/ps2_vu1_ref.h"
#include "runtime/gs/gs_frontend.h"
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>

static bool same(const VU1State& a, const VU1State& b) {
    return !std::memcmp(a.vf, b.vf, sizeof a.vf) && !std::memcmp(a.vi, b.vi, sizeof a.vi) &&
           !std::memcmp(a.acc, b.acc, sizeof a.acc) && !std::memcmp(&a.q, &b.q, sizeof(float) * 3) && a.r == b.r &&
           a.pc == b.pc && a.mac == b.mac && a.clip == b.clip && a.status == b.status && a.cycles == b.cycles &&
           a.ebit == b.ebit && a.haltAfterDelaySlot == b.haltAfterDelaySlot && a.dBitEnabled == b.dBitEnabled &&
           a.tBitEnabled == b.tBitEnabled && a.stoppedByD == b.stoppedByD && a.stoppedByT == b.stoppedByT &&
           a.top == b.top && a.itop == b.itop && a.branchPending == b.branchPending &&
           a.branchTarget == b.branchTarget && a.branchDelay == b.branchDelay;
}
// The optimized register file writes interlocked VF results at issue, while
// the reference writes at retirement. Compare VF01 through a dependent read
// below; it is intentionally not directly observable at these boundaries.
static bool boundarySame(const VU1State& a, const VU1State& b) {
    VU1State observed = a;
    std::memcpy(observed.vf[1], b.vf[1], sizeof observed.vf[1]);
    return same(observed, b);
}
static uint32_t fsset(uint32_t value) {
    return (0x15u << 25) | ((value & 0x800u) << 10) | (value & 0x7ffu);
}
static uint64_t pair(uint32_t lower, uint32_t upper = 0x000002ffu) {
    return lower | (uint64_t(upper) << 32);
}
int main() {
    std::fesetround(FE_TOWARDZERO);
    auto fast = std::make_unique<VU1Interpreter>();
    auto reference = std::make_unique<VU1InterpreterRef>();
    GS gs;
    std::array<uint8_t, 16384> data{};
    std::mt19937 random(0x719ab3);
    const float values[] = {-4.0f, -1.0f, -0.0f, 0.0f, 1.0f, 4.0f};
    unsigned checks = 0;
    for (unsigned trial = 0; trial < 4096; ++trial) {
        fast->reset();
        reference->reset();
        fast->state().status = random() & 0xfffu;
        fast->state().mac = random() & 0xffffu;
        fast->state().q = values[random() % 6];
        for (unsigned lane = 0; lane < 4; ++lane) {
            fast->state().vf[2][lane] = values[random() % 6];
            fast->state().vf[3][lane] = values[random() % 6];
        }
        reference->state() = fast->state();
        const unsigned kind = trial % 3, latency = kind == 2 ? 13 : 7, phase = 1 + random() % (latency - 1);
        const uint32_t scalar = 0x800313bcu + kind; // DIV / SQRT / RSQRT, VF02.x / VF03.x
        uint64_t prime[16]{};
        for (unsigned i = 0; i < phase; ++i)
            prime[i] = pair(0x8000033cu);
        prime[0] = pair(scalar);
        if (phase > 1)
            prime[phase - 1] = pair(fsset(random() & 0xfc0u), 0x01e01060u); // ADDq plus same-pair FSSET
        fast->execute(reinterpret_cast<uint8_t*>(prime), phase * 8, data.data(), uint32_t(data.size()), gs, nullptr, 0,
                      0, 0, phase);
        reference->execute(reinterpret_cast<uint8_t*>(prime), phase * 8, data.data(), uint32_t(data.size()), gs,
                           nullptr, 0, 0, 0, phase);
        if (!boundarySame(fast->state(), reference->state())) {
            std::printf("prime mismatch %u\n", trial);
            return 1;
        }
        const bool batchMode = (trial & 1u) != 0;
        const unsigned count = batchMode ? 6 + random() % 27 : 1 + random() % 32;
        uint64_t words[32]{};
        for (unsigned i = 0; i < count; ++i) {
            uint32_t lower = 0x8000033cu;
            switch (batchMode ? 5 : random() % 6) {
                case 0:
                    lower = fsset(random() & 0xfc0u);
                    break;
                case 1:
                    lower = (0x14u << 25) | (4u << 16) | 0x7ffu;
                    break; // FSEQ
                default:
                    break;
            }
            words[i] = pair(lower, (batchMode || random() % 4) ? 0x01e01060u : 0x000002ffu);
        }
        const auto block = Vu1Blocks::schedule(words, 0, count, false, 4);
        const uint64_t base = fast->totalCycles();
        Vu1Step::Ctx context{
            base + block.cycles + 1, data.data(), uint32_t(data.size()), &gs, nullptr, count * 8, false, 0};
        const bool enters = Vu1Step::blockCanEnter(*fast, context, block.cycles, block.qSafe, block.eLo, block.eHi,
                                                   block.eVa, block.rLo, block.rHi, block.rVa);
        if (!enters) {
            std::printf("entry rejected %u count=%u\n", trial, count);
            return 2;
        }
        Vu1FlagBatch batch;
        for (unsigned i = 0; i < count; ++i) {
            const auto decoded = Vu1Decode::decodePair(uint32_t(words[i]), uint32_t(words[i] >> 32), true);
            if (batchMode && block.T[i] + 4u <= block.cycles)
                Vu1Step::blockPair<true, true>(*fast, decoded, context, base + block.T[i], i * 8, &batch);
            else
                Vu1Step::blockPair<false, true>(*fast, decoded, context, base + block.T[i], i * 8);
        }
        Vu1Step::blockExit(*fast, context, base, block.cycles, count, count * 8, false, block.ev, block.evCount);
        if (batchMode)
            Vu1Step::finishFlagBatch(*fast, batch);
        else
            Vu1Fast::settleFlags(*fast);
        reference->resume(reinterpret_cast<uint8_t*>(words), count * 8, data.data(), uint32_t(data.size()), gs, nullptr,
                          0, 0, block.cycles);
        if (!boundarySame(fast->state(), reference->state())) {
            std::printf("block mismatch trial=%u kind=%u phase=%u count=%u status=%x/%x mac=%x/%x cycles=%llu/%llu\n",
                        trial, kind, phase, count, fast->state().status, reference->state().status, fast->state().mac,
                        reference->state().mac, (unsigned long long)fast->state().cycles,
                        (unsigned long long)reference->state().cycles);
            return 3;
        }
        ++checks;
        // Read the newest VF01 through its interlock; count issued pairs in VI06
        // so premature register retirement cannot hide behind identical values.
        uint64_t tail = pair(0x10063001u, 0x01e00968u);
        fast->resume(reinterpret_cast<uint8_t*>(&tail), 8, data.data(), uint32_t(data.size()), gs, nullptr, 0, 0, 16);
        reference->resume(reinterpret_cast<uint8_t*>(&tail), 8, data.data(), uint32_t(data.size()), gs, nullptr, 0, 0,
                          16);
        // Drain outstanding VF05 tail writes before comparing register files;
        // VI06 retains the number of interlocked tail instructions issued.
        uint64_t nop = pair(0x8000033cu);
        fast->resume(reinterpret_cast<uint8_t*>(&nop), 8, data.data(), uint32_t(data.size()), gs, nullptr, 0, 0, 8);
        reference->resume(reinterpret_cast<uint8_t*>(&nop), 8, data.data(), uint32_t(data.size()), gs, nullptr, 0, 0,
                          8);
        if (!same(fast->state(), reference->state())) {
            std::printf("tail mismatch trial=%u kind=%u phase=%u count=%u vi6=%d/%d\n", trial, kind, phase, count,
                        fast->state().vi[6], reference->state().vi[6]);
            return 4;
        }
        ++checks;
    }
    std::fesetround(FE_TONEAREST);
    std::printf("%u exact DIV/SQRT/RSQRT, batched-flag and dependent-tail comparisons\n", checks);
}
