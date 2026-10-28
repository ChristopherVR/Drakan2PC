#include "vu/aot/ps2_vu1_aot.h"
#include "vu/ref/ps2_vu1_ref.h"
#include "vu/aot/ps2_vu1_image.inc"
#include "runtime/gs/gs_frontend.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
namespace Vu1Aot {
void registerBlocks0();
}
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
    _putenv_s("DRAKAN_VU_NATIVE_LOOPS", mode);
#else
    setenv("DRAKAN_VU_NATIVE_LOOPS", mode, 1);
#endif
}
int main(int argc, char** argv) {
    GS gs;
    std::vector<uint64_t> code(std::begin(kVu1Image), std::end(kVu1Image));
    auto* bytes = reinterpret_cast<uint8_t*>(code.data());
    const uint32_t size = uint32_t(code.size() * 8);
    if (!Vu1Aot::imageMatches(bytes, size))
        return 1;
    selectMode("0");
    Vu1Aot::registerBlocks0();
    std::vector<Vu1Aot::Fn> ordinary(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    selectMode(argc > 1 ? argv[1] : "1");
    Vu1Aot::registerBlocks0();
    std::vector<Vu1Aot::Fn> chained(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    const uint32_t entries[] = {55, 155, 188, 334};
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
    for (uint32_t trial = 0; trial < 2048; ++trial) {
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
        const uint32_t pc = entries[trial % 4] * 8u, budget = 1u + (trial / 4) % 256;
        if (trial & 1024u) {
            uint64_t prime = 0x000002ff800313bcull;
            switch ((trial / 4) % 6) {
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
            }
            a->execute(reinterpret_cast<uint8_t*>(&prime), 8, da.data(), da.size(), gs, nullptr, 0, 0, 0, 1);
            b->execute(reinterpret_cast<uint8_t*>(&prime), 8, db.data(), db.size(), gs, nullptr, 0, 0, 0, 1);
            a->state().pc = b->state().pc = pc;
            std::copy(ordinary.begin(), ordinary.end(), Vu1Aot::g_dispatch);
            a->resume(bytes, size, da.data(), da.size(), gs, nullptr, 0, 0, budget);
            std::copy(chained.begin(), chained.end(), Vu1Aot::g_dispatch);
            b->resume(bytes, size, db.data(), db.size(), gs, nullptr, 0, 0, budget);
        } else {
            std::copy(ordinary.begin(), ordinary.end(), Vu1Aot::g_dispatch);
            a->execute(bytes, size, da.data(), da.size(), gs, nullptr, pc, 0, 0, budget);
            std::copy(chained.begin(), chained.end(), Vu1Aot::g_dispatch);
            b->execute(bytes, size, db.data(), db.size(), gs, nullptr, pc, 0, 0, budget);
        }
        if (!same(a->state(), b->state()) || da != db || pa != pb) {
            std::printf("native loop mismatch trial=%u pc=%u budget=%u cycles=%llu/%llu endpc=%u/%u\n", trial, pc / 8,
                        budget, (unsigned long long)a->state().cycles, (unsigned long long)b->state().cycles,
                        a->state().pc, b->state().pc);
            return 3;
        }
        // Resume across the boundary to expose any disagreement in pending writes.
        a->state().pc = b->state().pc = 0;
        a->state().branchPending = b->state().branchPending = false;
        uint64_t nop = 0x000002ff8000033cull;
        a->resume(reinterpret_cast<uint8_t*>(&nop), 8, da.data(), da.size(), gs, nullptr, 0, 0, 16);
        b->resume(reinterpret_cast<uint8_t*>(&nop), 8, db.data(), db.size(), gs, nullptr, 0, 0, 16);
        if (!same(a->state(), b->state()) || da != db || pa != pb)
            return 4;
        checks += 2;
    }
    std::printf("PASS: %u native-loop state/data/packet comparisons, budgets 1..256, four entry PCs, pending "
                "DIV/SQRT/RSQRT/VF/ACC/VI and resumed tails\n",
                checks);
}
