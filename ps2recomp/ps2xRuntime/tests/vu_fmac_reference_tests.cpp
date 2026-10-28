#include "vu/ps2_vu1_step.h"
#include "vu/ps2_vu1_decode.h"
#include "vu/ref/ps2_vu1_ref.h"
#include "vu/aot/ps2_vu1_image.inc"
#include "runtime/gs/gs_frontend.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

static bool same(const VU1State& a, const VU1State& b) {
    return !std::memcmp(a.vf, b.vf, sizeof a.vf) && !std::memcmp(a.vi, b.vi, sizeof a.vi) &&
           !std::memcmp(a.acc, b.acc, sizeof a.acc) && !std::memcmp(&a.q, &b.q, sizeof(float) * 3) && a.r == b.r &&
           a.pc == b.pc && a.mac == b.mac && a.status == b.status && a.clip == b.clip && a.cycles == b.cycles &&
           a.ebit == b.ebit && a.haltAfterDelaySlot == b.haltAfterDelaySlot && a.branchPending == b.branchPending &&
           a.branchTarget == b.branchTarget && a.branchDelay == b.branchDelay;
}
int main() {
    auto fast = std::make_unique<VU1Interpreter>();
    auto reference = std::make_unique<VU1InterpreterRef>();
    GS gs;
    std::array<uint8_t, 16384> data{};
    std::vector<uint32_t> operations;
    for (auto word : kVu1Image) {
        const uint32_t upper = uint32_t(word >> 32) & 0x01ffffffu;
        const auto d = Vu1Decode::decodePair(0x8000033cu, upper, true);
        if (d.fmKind > 0 && d.fmKind <= 5)
            operations.push_back(upper);
    }
    const uint32_t edges[] = {0,          0x80000000, 1,          0x80000001, 0x007fffff, 0x00800000, 0x03ffffff,
                              0x04000000, 0x3f000000, 0x3f800000, 0xbf800000, 0x7b7fffff, 0x7b800000, 0x7f7fffff,
                              0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345, 0x7fa00001};
    std::mt19937 random(0xc01df1a6);
    unsigned checks = 0;
    for (int mode : {FE_TONEAREST, FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD}) {
        for (unsigned trial = 0; trial < 32768; ++trial) {
            fast->reset();
            reference->reset();
            auto& st = fast->state();
            auto value = [&] {
                return std::bit_cast<float>(trial % 3 ? edges[random() % std::size(edges)] : uint32_t(random()));
            };
            for (unsigned reg = 1; reg < 32; ++reg)
                for (float& f : st.vf[reg])
                    f = value();
            for (float& f : st.acc)
                f = value();
            st.q = value();
            st.i = value();
            st.mac = random() & 0xffffu;
            st.status = random() & 0xfffu;
            st.clip = random() & 0xffffffu;
            reference->state() = st;
            uint32_t upper = operations[random() % operations.size()];
            upper = (upper & ~((31u << 11) | (31u << 16) | (15u << 21))) | ((random() % 4 ? 2u : 0u) << 11) |
                    ((random() % 4 ? 3u : 0u) << 16) | ((random() % 16) << 21);
            const auto d = Vu1Decode::decodePair(0x8000033cu, upper, true);
            if (!d.fmToAcc)
                upper = (upper & ~(31u << 6)) | (4u << 6);
            std::array<uint64_t, 8> code;
            code.fill(0x000002ff8000033cull);
            code[0] = (uint64_t(upper) << 32) | 0x8000033cu;
            code[6] = 0x400002ff8000033cull; // E bit and original delay pair flush the pipelines.
            std::fesetround(mode);
            fast->execute(reinterpret_cast<uint8_t*>(code.data()), sizeof code, data.data(), data.size(), gs, nullptr,
                          0, 0, 0, 64);
            reference->execute(reinterpret_cast<uint8_t*>(code.data()), sizeof code, data.data(), data.size(), gs,
                               nullptr, 0, 0, 0, 64);
            if (std::fegetround() != mode || !same(fast->state(), reference->state())) {
                std::printf("FAIL FMAC trial=%u mode=%d upper=%08x mac=%x/%x status=%x/%x cycles=%llu/%llu\n", trial,
                            mode, upper, fast->state().mac, reference->state().mac, fast->state().status,
                            reference->state().status, (unsigned long long)fast->state().cycles,
                            (unsigned long long)reference->state().cycles);
                return 1;
            }
            ++checks;
        }
    }
    std::fesetround(FE_TONEAREST);
    std::printf("PASS: %u frozen-reference FMAC state comparisons; all image arithmetic forms, random/edge bits, "
                "masks, VF/ACC destinations, four caller rounding modes\n",
                checks);
}
