#include "vu/ps2_vu1_step.h"
#include "vu/ps2_vu1_decode.h"
#include "vu/ps2_vu1_normalized.h"
#include "runtime/gs/gs_frontend.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>

template <unsigned Mask>
static void issue(VU1Interpreter& v, const Vu1Step::Dec& d, Vu1Step::Ctx& c, uint64_t cycle) {
    Vu1Step::blockPair<false, false, Mask>(v, d, c, cycle, 0);
}
int main() {
    auto plain = std::make_unique<VU1Interpreter>();
    auto proven = std::make_unique<VU1Interpreter>();
    GS gs;
    std::array<uint8_t, 16384> data{};
    std::mt19937 rng(0x144fa57);
    for (auto& b : data)
        b = uint8_t(rng());
    using Fn = void (*)(VU1Interpreter&, const Vu1Step::Dec&, Vu1Step::Ctx&, uint64_t);
    const Fn functions[] = {issue<0>, issue<1>, issue<2>, issue<3>, issue<4>, issue<5>, issue<6>, issue<7>};
    unsigned checks = 0, skipped = 0;
    for (int mode : {FE_TONEAREST, FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD}) {
        std::fesetround(mode);
        plain->reset();
        proven->reset();
        for (auto& vf : plain->state().vf)
            for (float& f : vf)
                f = std::bit_cast<float>(rng());
        for (float& f : plain->state().acc)
            f = std::bit_cast<float>(rng());
        plain->state().vf[0][0] = plain->state().vf[0][1] = plain->state().vf[0][2] = 0;
        plain->state().vf[0][3] = 1;
        proven->state() = plain->state();
        Vu1Normalized::State known;
        Vu1Step::Ctx a{~0ull, data.data(), uint32_t(data.size()), &gs, nullptr, 16384, false, 0}, b = a;
        for (unsigned n = 0; n < 50000; ++n) {
            const unsigned fs = 1 + rng() % 7, ft = 1 + rng() % 7, fd = 1 + rng() % 7, dest = rng() % 16;
            // Include full writes often enough to prove vector inputs; partial
            // writes and raw loads/conversions must invalidate individual lanes.
            const unsigned lanes = (n % 3 == 0) ? 15 : dest;
            const unsigned ops[] = {0x28, 0x2c, 0x2a, 0x29, 0x2d, 0x00, 0x08, 0x18, 0x1c};
            uint32_t upper = (lanes << 21) | (ft << 16) | (fs << 11) | (fd << 6) | ops[rng() % 9];
            uint32_t lower = 0x8000033cu;
            if (n % 13 == 0)
                lower = (lanes << 21) | (ft << 16); // raw LQ into VF, VI00 base
            if (n % 17 == 0)
                upper = (lanes << 21) | (ft << 16) | (fs << 11) | 0x17cu; // FTOI0 raw integer bits
            if (n % 19 == 0)
                upper = (lanes << 21) | (ft << 16) | (fs << 11) | 0x2bcu; // MUL accumulator
            if (n % 23 == 0) {
                upper |= 0x80000000u;
                lower = rng();
            } // immediate is normalized independently
            const auto d = Vu1Decode::decodePair(lower, upper, true);
            const uint8_t mask = known.inputs(d);
            issue<0>(*plain, d, a, n);
            functions[mask](*proven, d, b, n);
            Vu1Fast::settleFlags(*plain);
            Vu1Fast::settleFlags(*proven);
            const auto& x = plain->state();
            const auto& y = proven->state();
            if (std::memcmp(x.vf, y.vf, sizeof x.vf) || std::memcmp(x.acc, y.acc, sizeof x.acc) || x.mac != y.mac ||
                x.status != y.status || std::memcmp(&x.i, &y.i, sizeof(float))) {
                std::printf("normalized-input mismatch mode=%d pair=%u mask=%u upper=%x lower=%x\n", mode, n, mask,
                            upper, lower);
                return 1;
            }
            known.apply(d);
            ++checks;
            if ((d.upper >> 21) & 15u)
                skipped += std::popcount(unsigned(mask) & (d.fmKind >= 4 ? 7u : 3u));
        }
    }
    std::fesetround(FE_TONEAREST);
    if (skipped < 1000)
        return 2;
    std::printf(
        "%u normalized-input pair comparisons, %u proven operand normalizations skipped; four host rounding modes\n",
        checks, skipped);
}
