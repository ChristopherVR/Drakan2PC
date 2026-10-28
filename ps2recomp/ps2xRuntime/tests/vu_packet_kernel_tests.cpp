#include "vu/aot/ps2_vu1_aot.h"
#include "vu/ps2_vu1_decode.h"
#include "vu/ref/ps2_vu1_ref.h"
#include "vu/aot/ps2_vu1_image.inc"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
namespace Vu1Aot {
void registerBlocks0();
void registerBlocks1();
void registerNativeKernels();
} // namespace Vu1Aot
static bool same(const VU1State& a, const VU1State& b) {
    return !std::memcmp(a.vf, b.vf, sizeof a.vf) && !std::memcmp(a.vi, b.vi, sizeof a.vi) &&
           !std::memcmp(a.acc, b.acc, sizeof a.acc) && !std::memcmp(&a.q, &b.q, sizeof(float) * 3) && a.r == b.r &&
           a.pc == b.pc && a.mac == b.mac && a.clip == b.clip && a.status == b.status && a.cycles == b.cycles &&
           a.ebit == b.ebit && a.haltAfterDelaySlot == b.haltAfterDelaySlot && a.dBitEnabled == b.dBitEnabled &&
           a.tBitEnabled == b.tBitEnabled && a.stoppedByD == b.stoppedByD && a.stoppedByT == b.stoppedByT &&
           a.top == b.top && a.itop == b.itop && a.branchPending == b.branchPending &&
           a.branchTarget == b.branchTarget && a.branchDelay == b.branchDelay;
}

static void selectMode(const char* mode) {
#if defined(_WIN32)
    _putenv_s("DRAKAN_VU_NATIVE_LOOPS", "0");
    _putenv_s("DRAKAN_VU_NATIVE_KERNELS", mode[0] == '0' ? "0" : "1");
    _putenv_s("DRAKAN_VU_MATRIX_LOOP", mode[0] == '4' ? "1" : "0");
#else
    setenv("DRAKAN_VU_NATIVE_LOOPS", "0", 1);
    setenv("DRAKAN_VU_NATIVE_KERNELS", mode[0] == '0' ? "0" : "1", 1);
    setenv("DRAKAN_VU_MATRIX_LOOP", mode[0] == '4' ? "1" : "0", 1);
#endif
}
int main(int argc, char** argv) {
    GS gs;
    std::vector<uint64_t> code(std::begin(kVu1Image), std::end(kVu1Image));
    const uint32_t size = uint32_t(code.size() * 8);
    PS2Memory trackedMemory;
    if (!trackedMemory.initialize())
        return 7;
    auto* bytes = trackedMemory.getVU1Code();
    std::memcpy(bytes, code.data(), size);
    trackedMemory.markVU1CodeModified();
    if (!Vu1Aot::imageMatches(bytes, size))
        return 1;
    selectMode("0");
    Vu1Aot::registerBlocks0();
    Vu1Aot::registerBlocks1();
    Vu1Aot::registerNativeKernels();
    std::vector<Vu1Aot::Fn> ordinary(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    selectMode(argc > 1 ? argv[1] : "4");
    Vu1Aot::registerBlocks0();
    Vu1Aot::registerBlocks1();
    Vu1Aot::registerNativeKernels();
    std::vector<Vu1Aot::Fn> chained(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    const uint32_t entries[] = {489, 489, 489, 489};
    for (auto pc : entries)
        if (ordinary[pc] == chained[pc])
            return 2;
    auto a = std::make_unique<VU1Interpreter>(), b = std::make_unique<VU1Interpreter>();
    std::array<uint8_t, 16384> da{}, db{};
    std::vector<std::vector<uint8_t>> pa, pb;
    a->setPacketTap(&pa);
    b->setPacketTap(&pb);
    std::mt19937 rng(0x1951ec);
    unsigned checks = 0;
    for (uint32_t trial = 0; trial < 16384; ++trial) {
        a->reset();
        b->reset();
        pa.clear();
        pb.clear();
        Vu1Fast::spare(*a).sinkNone = Vu1Fast::spare(*b).sinkNone = 1;
        da.fill(0);
        for (unsigned i = 0; i < 32; ++i)
            for (float& lane : a->state().vf[i])
                lane = float(int(rng() % 33) - 16) / 4.0f;
        for (unsigned i = 1; i < 16; ++i)
            a->state().vi[i] = rng() % 9;
        for (float& lane : a->state().acc)
            lane = float(int(rng() % 33) - 16) / 4.0f;
        a->state().mac = rng() & 0xffff;
        a->state().status = rng() & 0xfff;
        a->state().clip = rng() & 0xffffff;
        if (trial % 4 == 0) {
            const int counters[] = {-32768, -1, 0, 1, 2, 31, 32, 33, 255, 256, 32767};
            const int pointers[] = {0, 1, 1023, 1024, 32767, -32768, -1};
            a->state().vi[2] = counters[(trial / 4) % 11];
            a->state().vi[7] = pointers[(trial / 4) % 7];
        }
        b->state() = a->state();
        db = da;
        if (trial % 8 == 1)
            for (unsigned reg = 1; reg < 32; ++reg)
                for (float& lane : a->state().vf[reg]) {
                    const uint32_t bits = rng();
                    std::memcpy(&lane, &bits, 4);
                }
        if (trial % 8 == 2)
            for (unsigned reg = 1; reg < 32; ++reg)
                for (float& lane : a->state().vf[reg]) {
                    const uint32_t special[] = {0u,          0x80000000u, 1u,          0x80000001u,
                                                0x7f800000u, 0x7fc10001u, 0xffc10001u, 0x7f7fffffu};
                    const uint32_t bits = special[rng() % 8];
                    std::memcpy(&lane, &bits, 4);
                }
        for (unsigned lane = 0; lane < 4; ++lane) {
            const int indices[] = {0, 1, 63, 64, 1023, 1024, 32767, -1, -32768};
            const uint32_t bits = (rng() & 0xffff0000u) | uint16_t(indices[(trial / 4 + lane) % 9]);
            std::memcpy(&a->state().vf[19][lane], &bits, 4);
        }
        a->state().vi[3] = (trial % 3) ? 1 : 0;
        a->state().vi[4] = (trial % 5) ? 1 : -1;
        a->state().vi[14] = 1 + trial % 4;
        const int outputs[] = {0, 1, 1023, 1024, 32767, -32768, -1};
        a->state().vi[9] = outputs[(trial / 4) % 7];
        for (auto& byte : da)
            byte = uint8_t(rng());
        const uint64_t tag = 32 | 0x8000ull | (2ull << 58);
        std::memcpy(da.data(), &tag, 8);
        db = da;
        const unsigned counterReg = 1;
        const int counts[] = {-32768, -1, 0, 1, 2, 3, 4, 31, 32, 33, 255, 256, 32767};
        a->state().vi[counterReg] = counts[(trial / 4) % 13];
        if (trial % 33 == 0)
            a->state().vi[0] = 2;
        // Overwrite the earlier copy after installing exceptional register bits.
        b->state() = a->state();
        const uint32_t pc = entries[trial % 4] * 8u, budget = 1u + (trial / 4) % 1024;
        if (trial & 1024u) {
            uint64_t prime = 0x000002ff800313bcull;
            switch ((trial / 4) % 7) {
                case 1:
                    prime = 0x000002ff800313bdull;
                    break;
                case 2:
                    prime = 0x000002ff800313beull;
                    break;
                case 3:
                    prime = uint64_t((15u << 21) | (3u << 16) | (2u << 11) | (10u << 6) | 0x28u) << 32 | 0x8000033cull;
                    break;
                case 4:
                    prime = uint64_t((15u << 21) | (3u << 16) | (2u << 11) | 0x2bcu) << 32 | 0x8000033cull;
                    break;
                case 5:
                    prime = 0x000002ff100b5801ull;
                    break;
                case 6:
                    prime = 0x000002ff80007efcull;
                    a->state().vi[15] = b->state().vi[15] = (trial & 8) ? 1023 : 0;
                    std::memcpy(da.data() + a->state().vi[15] * 16, &tag, 8);
                    std::memcpy(db.data() + b->state().vi[15] * 16, &tag, 8);
                    break;
            }
            a->execute(reinterpret_cast<uint8_t*>(&prime), 8, da.data(), da.size(), gs, nullptr, 0, 0, 0, 1);
            b->execute(reinterpret_cast<uint8_t*>(&prime), 8, db.data(), db.size(), gs, nullptr, 0, 0, 0, 1);
            a->state().pc = b->state().pc = pc;
            std::copy(ordinary.begin(), ordinary.end(), Vu1Aot::g_dispatch);
            a->resume(bytes, size, da.data(), da.size(), gs, &trackedMemory, 0, 0, budget);
            std::copy(chained.begin(), chained.end(), Vu1Aot::g_dispatch);
            b->resume(bytes, size, db.data(), db.size(), gs, &trackedMemory, 0, 0, budget);
        } else {
            std::copy(ordinary.begin(), ordinary.end(), Vu1Aot::g_dispatch);
            a->execute(bytes, size, da.data(), da.size(), gs, &trackedMemory, pc, 0, 0, budget);
            std::copy(chained.begin(), chained.end(), Vu1Aot::g_dispatch);
            b->execute(bytes, size, db.data(), db.size(), gs, &trackedMemory, pc, 0, 0, budget);
        }
        if (Vu1Fast::spare(*a).aotValid != 1 || Vu1Fast::spare(*b).aotValid != 1)
            return 6;
        if (!same(a->state(), b->state()) || da != db || pa != pb) {
            std::printf("native loop mismatch trial=%u pc=%u budget=%u cycles=%llu/%llu endpc=%u/%u\n", trial, pc / 8,
                        budget, (unsigned long long)a->state().cycles, (unsigned long long)b->state().cycles,
                        a->state().pc, b->state().pc);
            return 3;
        }
        // Resume across the boundary to expose any disagreement in pending writes.
        a->state().pc = b->state().pc = 0;
        a->state().branchPending = b->state().branchPending = false;
        const auto pair = [](uint32_t lower, uint32_t upper = 0x2ffu) {
            return uint64_t(lower) | (uint64_t(upper) << 32);
        };
        std::array<uint64_t, 4> probe = {pair(0x80000030u | (13u << 16) | (6u << 11) | (15u << 6),
                                              (15u << 21) | (17u << 16) | (12u << 11) | (30u << 6) | 0x28u),
                                         pair((0x16u << 25) | (1u << 21) | (14u << 16) | 0x7ffu),
                                         pair(0x80000030u | (8u << 16) | (9u << 11) | (12u << 6),
                                              (15u << 21) | (19u << 16) | (13u << 11) | (29u << 6) | 0x28u),
                                         pair((0x1au << 25) | (11u << 16) | (6u << 11))};
        for (uint32_t tail : {1u, 2u, 3u, 4u, 16u, 256u}) {
            a->resume(reinterpret_cast<uint8_t*>(probe.data()), sizeof(probe), da.data(), da.size(), gs, nullptr, 0, 0,
                      tail);
            b->resume(reinterpret_cast<uint8_t*>(probe.data()), sizeof(probe), db.data(), db.size(), gs, nullptr, 0, 0,
                      tail);
            if (!same(a->state(), b->state()) || da != db || pa != pb) {
                std::printf("packet dependent-tail mismatch trial=%u tail=%u\n", trial, tail);
                return 4;
            }
        }
        checks += 7;
    }
    std::printf(
        "PASS: %u fused-packet state/data/packet comparisons, budgets 1..1024, packet entry 489, all branch-prefix "
        "exits, exceptional IEEE bits, wrapping stores/transfers, pending DIV/SQRT/RSQRT/VF/ACC/VI and resumed tails\n",
        checks);
}
