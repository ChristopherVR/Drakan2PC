// Vertex-program records of the batch path (gsr/vertex_batch.h, vu/ps2_vu1_gpu_batch.h):
//   * the exact-arithmetic helpers of the CPU evaluation against the engine's own arithmetic (host floats with the
//     rounding mode toward zero and denormals flushed, vu/ps2_vu1_simd.h);
//   * the tracker's policy on synthetic VU memory: which stages a flag word selects, the per-family switches, every
//     decline reason, pending passes against passes applied to memory, EE writes, stale lanes, index lists.
//   * the policy of the run-time hook (vu/ps2_vu1_gpu_live.h): the family mask changing at any run boundary, leaving
//     and re-entering the mode, when the tracker may stop following, and the strip stage's question about its inputs.
// No game data: every input is generated here.
#include "vu/ps2_vu1_gpu_batch.h"
#include "vu/ps2_vu1_simd.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

namespace {
using namespace gsr::vp;
using namespace vu1gpu::batch;

int g_failed = 0;
uint64_t g_checks = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            if (++g_failed <= 20)                                                                                      \
                std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                                              \
    } while (0)

float f(uint32_t b) {
    return vu::asFloat(b);
}
uint32_t b(float v) {
    return vu::asBits(v);
}

// Operand bit patterns that exercise every class: ordinary values of any exponent, values next to the largest float,
// zeros, denormals, infinities and NaNs.
uint32_t pattern(std::mt19937& rng) {
    static const uint32_t special[] = {0u,          0x80000000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFEu, 0x7F7FFFFDu,
                                       0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC12345u, 0x00000001u, 0x807FFFFFu,
                                       0x00800000u, 0x80800000u, 0x3F800000u, 0xBF800000u, 0x3F7FFFFFu, 0x43480000u};
    const uint32_t pick = rng() % 16u;
    if (pick == 0u)
        return special[rng() % (sizeof(special) / sizeof(special[0]))];
    if (pick < 6u) { // game-like magnitudes
        const float v = std::ldexp(static_cast<float>(static_cast<int32_t>(rng())) / 2147483648.0f,
                                   static_cast<int>(rng() % 24u) - 4);
        return b(v);
    }
    return rng();
}

void helperTests() {
    // The engine runs VU arithmetic with the host rounding toward zero, results and operands flushed to zero.
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~0x6000u) | 0x6000u | 0x8000u | 0x0040u);
    std::mt19937 rng(20261112u);
    for (int i = 0; i < 400000; ++i) {
        const uint32_t ra = pattern(rng), rb = pattern(rng), rc = pattern(rng);
        const uint32_t a = vu::operand(ra), c = vu::operand(rb), d = vu::operand(rc);
        volatile float x = f(a), y = f(c), z = f(d);
        CHECK(vu::add(a, c) == b(Vu1Simd::add(x, y)));
        CHECK(vu::sub(a, c) == b(Vu1Simd::subtract(x, y)));
        CHECK(vu::mul(a, c) == b(Vu1Simd::multiply(x, y)));
        // multiply-add and multiply-subtract round the product, then the sum
        volatile float product = Vu1Simd::multiply(y, z);
        CHECK(vu::madd(a, c, d) == b(Vu1Simd::add(x, product)));
        CHECK(vu::sub(a, vu::mul(c, d)) == b(Vu1Simd::subtract(x, product)));
        // square root and reciprocal square root of a sum of squares (the E unit's reciprocal length)
        const uint32_t mag = a & 0x7FFFFFFFu;
        volatile float m = f(mag);
        CHECK(vu::sqrtZero(mag) == b(Vu1Simd::squareRoot(m)));
        volatile float sum =
            Vu1Simd::add(Vu1Simd::add(Vu1Simd::multiply(x, x), Vu1Simd::multiply(y, y)), Vu1Simd::multiply(z, z));
        volatile float len = Vu1Simd::squareRoot(sum);
        const float rl = len != 0.0f ? Vu1Simd::divide(1.0f, len) : len;
        CHECK(vu::reciprocalLength(a, c, d) == b(rl));
        // the divider: one over an operand
        if (mag != 0u) {
            float q = Vu1Simd::divide(1.0f, x);
            if (std::isinf(q))
                q = std::signbit(q) ? -3.402823466e38f : 3.402823466e38f;
            CHECK(vu::reciprocal(a) == b(q));
        } else {
            CHECK(vu::reciprocal(a) == ((a & 0x80000000u) | 0x7F7FFFFFu));
        }
        // comparisons pass a raw operand through
        const float fa = f(ra), fb = f(rb);
        CHECK(vu::maxRaw(ra, rb) == ((fa > fb) ? ra : rb));
        CHECK(vu::minRaw(ra, rb) == ((fa < fb) ? ra : rb));
        CHECK(vu::absRaw(ra) == b(std::fabs(f(ra))) || (ra & 0x7FFFFFFFu) > 0x7F800000u);
        // integer conversions
        const int32_t n = static_cast<int32_t>(rng());
        CHECK(vu::fromInt(n) == b(Vu1Simd::fromIntScalar(n)));
        for (uint32_t shift : {0u, 4u}) {
            const double scaled = static_cast<double>(x) * static_cast<double>(1u << shift);
            const int32_t want = scaled >= 2147483647.0    ? 0x7FFFFFFF
                                 : scaled <= -2147483648.0 ? static_cast<int32_t>(0x80000000u)
                                                           : static_cast<int32_t>(scaled);
            CHECK(vu::toFixed(a, shift) == want);
        }
    }
    _mm_setcsr(saved);
    // gridAxis: column i is the origin plus i steps, each sum rounded toward zero
    uint32_t axis[16];
    gridAxis(b(1000.5f), b(0.1f), 16, axis);
    uint32_t acc = b(1000.5f);
    for (int i = 0; i < 16; ++i) {
        CHECK(axis[i] == acc);
        acc = vu::add(acc, b(0.1f));
    }
}

// ---- synthetic VU memory --------------------------------------------------------------------------------------------
struct Machine {
    std::vector<uint8_t> data = std::vector<uint8_t>(16384, 0);
    float vf[32][4] = {};
    uint32_t pc = 0;
    std::mt19937 rng{7u};

    void word(uint32_t qw, uint32_t lane, uint32_t v) {
        std::memcpy(data.data() + size_t(qw) * 16u + lane * 4u, &v, 4);
    }
    uint32_t word(uint32_t qw, uint32_t lane) const {
        uint32_t v;
        std::memcpy(&v, data.data() + size_t(qw) * 16u + lane * 4u, 4);
        return v;
    }
    void floats(uint32_t qw, float x, float y, float z, float w) {
        word(qw, 0, b(x));
        word(qw, 1, b(y));
        word(qw, 2, b(z));
        word(qw, 3, b(w));
    }
    void header(int base, int count, int z, int w) {
        word(kQwHeader, 0, static_cast<uint32_t>(base) & 0xFFFFu);
        word(kQwHeader, 1, static_cast<uint32_t>(count) & 0xFFFFu);
        word(kQwHeader, 2, static_cast<uint32_t>(z) & 0xFFFFu);
        word(kQwHeader, 3, static_cast<uint32_t>(w) & 0xFFFFu);
    }
    void flags(uint32_t v) { word(kQwFlags, 0, v); }
    // constants of a plausible view, lights and the strip tag
    void constants() {
        floats(0, 1.2f, 0.0f, 0.0f, 0.0f);
        floats(1, 0.0f, -1.6f, 0.0f, 0.0f);
        floats(2, 0.0f, 0.0f, 1.001f, 1.0f);
        floats(3, 10.0f, -20.0f, 50.0f, 60.0f);
        floats(4, 0.0f, 1.0f, 5000.0f, 0.04f);
        floats(5, 2048.0f, 2048.0f, 8000000.0f, 250.0f);
        floats(6, 320.0f, -224.0f, -7000000.0f, 0.0f);
        for (int r = 0; r < 7; ++r)
            std::memcpy(vf[1 + r], data.data() + size_t(r) * 16u, 16);
        for (uint32_t l = 0; l < 4; ++l) {
            word(kQwColour, l, 40u + l);
            word(kQwFade, l, b(0.004f));
        }
        for (uint32_t r = 0; r < 4; ++r) {
            floats(kQwLightDir + r, 0.002f * (r + 1), -0.001f, 0.003f, 0.0f);
            for (uint32_t l = 0; l < 4; ++l)
                word(kQwLightColour + r, l, 20u * (r + 1) + l);
            floats(kQwTexMatrix + r, 0.01f * (r + 1), 0.02f, 0.5f, 1.0f);
        }
        const uint64_t tagLo = 0x8000ull | (1ull << 46) | (0x0Cull << 47) | (4ull << 60), tagHi = 0x5A12ull;
        std::memcpy(data.data() + size_t(kQwTag) * 16u, &tagLo, 8);
        std::memcpy(data.data() + size_t(kQwTag) * 16u + 8, &tagHi, 8);
    }
    void vertices(uint32_t first, uint32_t count) {
        for (uint32_t s = first; s < first + count; ++s) {
            floats(kPos + s, static_cast<float>(static_cast<int>(rng() % 2000u) - 1000),
                   static_cast<float>(static_cast<int>(rng() % 2000u) - 1000), static_cast<float>(rng() % 3000u + 100u),
                   1.0f);
            for (uint32_t l = 0; l < 4; ++l)
                word(kCol + s, l, static_cast<uint32_t>(static_cast<int32_t>(rng() % 255u) - 127));
            floats(kSt + s, static_cast<float>(rng() % 64u) / 8.0f, static_cast<float>(rng() % 64u) / 8.0f, 1.0f, 0.0f);
        }
    }
    void indexList(uint32_t at, const std::vector<int>& indices) {
        for (size_t i = 0; i < indices.size(); ++i)
            word(at + static_cast<uint32_t>(i / 4), static_cast<uint32_t>(i & 3),
                 static_cast<uint32_t>(indices[i]) & 0xFFFFu);
    }
};

// The stages of a vertex run applied to memory, as the microprogram would (the oracle of the pending passes).
void executeVertexRun(Machine& m) {
    const VertexRun run = classifyVertexRun(m.data.data());
    for (uint32_t k = 0; k < run.count; ++k) {
        const Pass pass = makePass(run.kinds[k], run, m.data.data(), m.vf);
        const bool grid = isGrid(pass.kind);
        static GridAxes axes;
        if (grid)
            axes.set(pass);
        const uint16_t wr = writeLanes(pass.kind);
        for (uint32_t s = pass.first; s < pass.first + pass.count; ++s) {
            Row row;
            loadRow(m.data.data(), s, row);
            const uint32_t cell = s - pass.first;
            applyPass(pass, row, grid ? axes.x[cell % pass.param] : 0u, grid ? axes.z[cell / pass.param] : 0u);
            const uint32_t* words[3] = {row.pos, row.col, row.st};
            const uint32_t base[3] = {kPos, kCol, kSt};
            for (uint32_t l = 0; l < 12u; ++l)
                if ((wr >> l) & 1u)
                    m.word(base[l / 4u] + s, l & 3u, words[l / 4u][l & 3u]);
        }
    }
}

std::vector<std::vector<uint8_t>> packetsOf(const std::vector<uint8_t>& packet) {
    std::vector<std::vector<uint8_t>> out;
    BatchView view;
    CHECK(packet.size() > 16u && parseBatch(packet.data() + 16, static_cast<uint32_t>(packet.size() - 16), view));
    if (packet.size() > 16u && parseBatch(packet.data() + 16, static_cast<uint32_t>(packet.size() - 16), view))
        batchToGif(view, out);
    return out;
}

// One run through a tracker; a run it lets through is "executed" by applying its stages to memory.
struct Stepped {
    Action action;
    Reason why;
    std::vector<uint8_t> packet;
};
Stepped stepRun(Tracker& t, Machine& m, bool resume, uint32_t families, bool engineOk = true) {
    Stepped s;
    uint32_t pc = resume ? m.pc : 0u;
    s.action = t.step(resume, pc, m.vf, m.data.data(), families, engineOk, s.packet, &s.why);
    if (!resume)
        m.pc = kPcAfterSetup;
    else if (s.action == Action::Skip)
        m.pc = pc;
    else if (static_cast<int16_t>(m.word(kQwHeader, 2)) <= 0) {
        executeVertexRun(m);
        m.pc = kPcAfterVertex;
    }
    return s;
}

void classifyTests() {
    Machine m;
    m.constants();
    const auto kinds = [&m](uint32_t flags, int base, int count) {
        m.flags(flags);
        m.header(base, count, 0, 0);
        return classifyVertexRun(m.data.data());
    };
    m.word(kQwGrid, 0, 10);
    m.word(kQwGrid, 1, 11);
    m.word(kQwGrid, 2, 66);
    VertexRun v = kinds(0x106C, 66, 110);
    CHECK(v.why == Reason::None && v.count == 3 && v.kinds[0] == PassGrid && v.kinds[1] == PassTexGenPos &&
          v.kinds[2] == PassTransform && v.gridCols == 10 && v.gridRows == 11);
    CHECK(v.families == (FamGrid | FamTexGen | FamLightTransform));
    m.word(20, 0, 2);
    v = kinds(0x7064, 66, 110);
    CHECK(v.why == Reason::None && v.count == 4 && v.kinds[0] == PassGridFlat && v.kinds[1] == PassPointLight &&
          v.kinds[2] == PassTexGenPos && v.kinds[3] == PassTransform && v.exponent == 2);
    v = kinds(0xF064, 66, 110); // bit 15 makes the point light test negative
    CHECK(v.why == Reason::None && v.count == 3 && v.kinds[1] == PassTexGenPos);
    v = kinds(0x5164, 66, 110); // after the point light the integer generator is tried first
    CHECK(v.why == Reason::None && v.count == 4 && v.kinds[2] == PassTexGenInt);
    v = kinds(0x1164, 66, 110); // without the point light it is not
    CHECK(v.why == Reason::None && v.count == 3 && v.kinds[1] == PassTexGenPos);
    m.word(20, 0, 9);
    CHECK(kinds(0x7064, 66, 110).why == Reason::LightExponent);
    m.word(kQwGrid, 2, 70);
    CHECK(kinds(0x106C, 66, 110).why == Reason::GridShape);
    m.word(kQwGrid, 2, 66);
    m.word(kQwGrid, 0, 21); // 21 x 11 cells leave the window of 225 slots
    CHECK(kinds(0x106C, 66, 110).why == Reason::GridShape);
    m.word(kQwGrid, 0, 12);
    m.word(kQwGrid, 1, 17);
    CHECK(kinds(0x106C, 66, 204).why == Reason::None);
    v = kinds(0x10EC, 70, 20);
    CHECK(v.why == Reason::None && v.count == 1 && v.kinds[0] == PassFade && v.first == 4 && v.slots == 20);
    v = kinds(0x126C, 516, 225);
    CHECK(v.why == Reason::None && v.count == 1 && v.kinds[0] == PassTexMatrix && v.first == 0 && v.slots == 225);
    CHECK(kinds(0x126C, 66, 225).why == Reason::HeaderShape); // the texture matrix on another array
    v = kinds(0x0014, 205, 25);
    CHECK(v.why == Reason::None && v.count == 2 && v.kinds[0] == PassLight && v.kinds[1] == PassTransform &&
          v.first == 139);
    v = kinds(0x0174, 66, 6);
    CHECK(v.why == Reason::None && v.count == 3 && v.kinds[0] == PassTexGenInt && v.kinds[1] == PassLight);
    v = kinds(0x000C, 66, 4);
    CHECK(v.why == Reason::None && v.count == 1 && v.kinds[0] == PassTransform);
    CHECK(kinds(0x000D, 66, 4).why == Reason::ClipCodes);
    CHECK(kinds(0x10ED, 66, 4).why == Reason::ClipCodes);
    CHECK(kinds(0x126D, 516, 225).why == Reason::ClipCodes);
    CHECK(kinds(0x000C, 66, 0).why == Reason::HeaderShape);
    CHECK(kinds(0x000C, 66, 226).why == Reason::HeaderShape);
    CHECK(kinds(0x000C, 60, 4).why == Reason::HeaderShape);
    CHECK(kinds(0x000C, 290, 2).why == Reason::HeaderShape);
    for (uint32_t r = 0; r < static_cast<uint32_t>(Reason::Count); ++r)
        CHECK(reasonName(static_cast<Reason>(r))[0] != '?');
}

// A terrain-like batch (grid, texture coordinates from positions, transform; then texture matrix and fade per layer)
// through a tracker with `families`; returns the packets of every index-list run.
std::vector<std::vector<uint8_t>> terrainBatch(uint32_t families, Counters* counters, bool rewriteColours) {
    Machine m;
    m.constants();
    Tracker t;
    std::vector<std::vector<uint8_t>> all;
    const std::vector<int> listA = {1, 11, 2, 12, 3, 13, 0, 21, 31, 22, 32, 0, 0, 5, 6, 7};
    const std::vector<int> listB = {41, 51, 42, 52, 43, 0, 61, 62, 63, 64, 0, 9, 9, 0, 0, 0};
    const auto draw = [&](const std::vector<int>& list, uint32_t at, int z) {
        m.indexList(at, list);
        for (uint32_t q = 0; q < 4u; ++q)
            for (uint32_t l = 0; l < 4u; ++l)
                t.noteWrite(at + q, 15u);
        m.flags(0x106C);
        m.header(static_cast<int>(at), 4, z, 0);
        const Stepped s = stepRun(t, m, true, families);
        if (s.action == Action::Skip) {
            for (auto& pk : packetsOf(s.packet))
                all.push_back(pk);
        } else {
            // executed: the record of the processed rows is what the microprogram would send
            Tracker plain;
            std::vector<uint8_t> packet;
            uint32_t pc = kPcAfterVertex;
            CHECK(plain.step(true, pc, m.vf, m.data.data(), FamPrimitive, true, packet) == Action::Skip);
            for (auto& pk : packetsOf(packet))
                all.push_back(pk);
        }
    };
    // set-up with flag 8: the colour rows are filled with the constant colour
    m.flags(0x106C);
    for (uint32_t s = 0; s < kSlots; ++s)
        for (uint32_t l = 0; l < 4u; ++l)
            m.word(kCol + s, l, m.word(kQwColour, l));
    stepRun(t, m, false, families);
    // heights
    m.word(kQwGrid, 0, 10);
    m.word(kQwGrid, 1, 11);
    m.word(kQwGrid, 2, 66);
    m.floats(kQwGrid + 1, -500.0f, 30.0f, 200.0f, 1.0f);
    m.floats(kQwGrid + 2, 100.0f, 0.0f, 250.0f, 0.0f);
    for (uint32_t s = 0; s < 110u; ++s) {
        m.word(kPos + s, 1, 32768u + (s * 37u) % 200u);
        t.noteWrite(kPos + s, 15u);
    }
    m.header(66, 110, 0, 0);
    stepRun(t, m, true, families);
    draw(listA, 741, 1);
    draw(listB, 757, 65);
    for (int layer = 0; layer < 3; ++layer) {
        m.floats(kQwTexMatrix, 0.5f + layer, 0.1f, 0.0f, 0.0f);
        m.floats(kQwTexMatrix + 1, -0.1f, 0.5f + layer, 0.0f, 0.0f);
        m.floats(kQwTexMatrix + 3, 0.25f, 0.75f, 0.0f, 0.0f);
        m.flags(0x126C);
        m.header(516, 225, 0, 0);
        stepRun(t, m, true, families);
        if (rewriteColours)
            for (uint32_t s = 0; s < 110u; ++s) {
                for (uint32_t l = 0; l < 4u; ++l)
                    m.word(kCol + s, l, m.word(kQwColour, l) + static_cast<uint32_t>(layer));
                t.noteWrite(kCol + s, 15u);
            }
        m.word(kQwFade, 3, b(0.002f * static_cast<float>(layer + 1)));
        m.flags(0x10EC);
        m.header(70, 100, 0, 0);
        stepRun(t, m, true, families);
        draw(listA, 741, 1);
        draw(listB, 757, 65);
    }
    if (counters)
        *counters = t.counters;
    return all;
}

void deferralTests() {
    // Everything deferred against everything applied to memory, and each family switched off alone: same packets.
    Counters all, none;
    for (int rewrite = 0; rewrite < 2; ++rewrite) {
        const auto reference = terrainBatch(FamPrimitive, &none, rewrite != 0);
        const auto deferred = terrainBatch(FamAll, &all, rewrite != 0);
        CHECK(!reference.empty() && reference == deferred);
        CHECK(none.passesDeferred == 0 && none.taken[static_cast<uint32_t>(Stage::Vertex)] == 0);
        CHECK(none.declined[static_cast<uint32_t>(Reason::Disabled)] == 7);
        CHECK(all.taken[static_cast<uint32_t>(Stage::Vertex)] == 7 &&
              all.taken[static_cast<uint32_t>(Stage::List)] == 8);
        CHECK(all.slotsMaterialised == 0 && all.staleDraws == 0);
        // with the colours rewritten before each fade, the fade of the layer before is dropped, not kept
        CHECK(rewrite == 0 ? all.passesDropped == 0 : all.passesDropped == 200);
        for (uint32_t family : {FamFade, FamTexMatrix, FamGrid, FamTexGen, FamLightTransform, FamPrimitive}) {
            Counters c;
            const auto mixed = terrainBatch(FamAll & ~family, &c, rewrite != 0);
            CHECK(mixed == reference);
            CHECK(c.declined[static_cast<uint32_t>(Reason::Disabled)] != 0);
            CHECK(c.staleDraws == 0);
        }
        Counters c;
        CHECK(terrainBatch(0, &c, rewrite != 0) == reference); // nothing enabled: nothing taken
        CHECK(c.taken[static_cast<uint32_t>(Stage::Vertex)] == 0 && c.taken[static_cast<uint32_t>(Stage::List)] == 0);
    }
}

void trackerTests() {
    // Lit batch in two chunks, colours per index quadword and texture coordinates per corner.
    {
        Machine m;
        m.constants();
        m.vertices(0, 60);
        Tracker t;
        m.flags(0x0010);
        CHECK(stepRun(t, m, false, FamAll).action == Action::Run);
        m.header(66, 40, 0, 0);
        Stepped s = stepRun(t, m, true, FamAll);
        CHECK(s.action == Action::Skip && s.packet.empty() && m.pc == kPcAfterVertex);
        CHECK(t.virtualLanes(0) == (kLanePos | kLaneStZ | kLaneCol) && t.pendingPasses(0) == 2);
        CHECK(t.virtualLanes(40) == 0);
        m.header(66 + 40, 20, 0, 0);
        CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
        // per-corner texture coordinates at slots 0..: real lanes, written by the EE
        for (uint32_t q = 0; q < 16u; ++q) {
            m.word(kSt + q, 0, b(0.125f * static_cast<float>(q)));
            m.word(kSt + q, 1, b(1.0f - 0.125f * static_cast<float>(q)));
            t.noteWrite(kSt + q, 3u);
        }
        CHECK(t.virtualLanes(3) == (kLanePos | kLaneStZ | kLaneCol)); // the texture row's z is still pending
        const std::vector<int> list = {5, 45, 6, 46, 7, 0, 50, 51, 52, 0, 0, 0};
        m.indexList(741, list);
        m.header(741, 3, 1, 20);
        Machine physical = m;
        s = stepRun(t, m, true, FamAll);
        CHECK(s.action == Action::Skip && m.pc == kPcAfterList);
        BatchView view;
        CHECK(parseBatch(s.packet.data() + 16, static_cast<uint32_t>(s.packet.size() - 16), view));
        CHECK(view.header.stripCount == 2 && view.header.indexCount == 8 && view.header.passCount == 2);
        CHECK(view.strips[0] == 5 && view.strips[1] == 3 && view.header.prim == 0x0C);
        // the oracle: both vertex runs applied to memory, then the list of processed rows
        physical.header(66, 40, 0, 0);
        executeVertexRun(physical);
        physical.header(66 + 40, 20, 0, 0);
        executeVertexRun(physical);
        physical.header(741, 3, 1, 20);
        Tracker plain;
        std::vector<uint8_t> packet;
        uint32_t pc = kPcAfterVertex;
        CHECK(plain.step(true, pc, physical.vf, physical.data.data(), FamPrimitive, true, packet) == Action::Skip);
        CHECK(packetsOf(packet) == packetsOf(s.packet));
        // a run that is let through gets the same memory
        s = stepRun(t, m, true, FamAll & ~FamPrimitive);
        CHECK(s.action == Action::Run && s.why == Reason::Disabled && t.virtualLanes(0) == 0);
        CHECK(std::memcmp(m.data.data() + size_t(kPos) * 16u, physical.data.data() + size_t(kPos) * 16u,
                          size_t(kSlots) * 3u * 16u) == 0);
        CHECK(t.counters.slotsMaterialised == 60 && t.counters.staleDraws == 0);
        // engine state
        s = stepRun(t, m, true, FamAll, false);
        CHECK(s.action == Action::Run && s.why == Reason::EngineState);
    }
    // A batch in many vertex chunks: the chunks repeat the same stages with the same constants, so a list that
    // draws from all of them still needs two passes; with other constants per chunk the record runs out of passes.
    for (int vary = 0; vary < 2; ++vary) {
        Machine m;
        m.constants();
        m.vertices(0, 60);
        Tracker t;
        m.flags(0x0034);
        stepRun(t, m, false, FamAll);
        std::vector<int> list;
        for (int chunk = 0; chunk < 15; ++chunk) {
            if (vary)
                m.word(kQwLightColour, 0, 20u + static_cast<uint32_t>(chunk));
            m.header(66 + chunk * 4, 4, 0, 0);
            CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
            list.push_back(1 + chunk * 4);
            list.push_back(2 + chunk * 4);
            list.push_back(3 + chunk * 4);
        }
        list.push_back(0);
        m.indexList(741, list);
        m.header(741, 12, 1, 0);
        const Stepped s = stepRun(t, m, true, FamAll);
        if (vary) {
            CHECK(s.action == Action::Run && s.why == Reason::ChainLength);
        } else {
            BatchView view;
            CHECK(s.action == Action::Skip &&
                  parseBatch(s.packet.data() + 16, static_cast<uint32_t>(s.packet.size() - 16), view) &&
                  view.header.passCount == 2 && view.header.vertexCount == 45);
        }
    }
    // A corner whose colour is another slot's: lighting alone is carried over, a fade is not.
    {
        Machine m;
        m.constants();
        m.vertices(0, 30);
        Tracker t;
        m.flags(0x0090);
        stepRun(t, m, false, FamAll);
        m.flags(0x0010);
        m.header(66, 30, 0, 0);
        stepRun(t, m, true, FamAll);
        m.flags(0x0090);
        stepRun(t, m, true, FamAll);
        m.indexList(741, {1, 2, 3, 0});
        m.flags(0x0030);
        m.header(741, 1, 1, 7);
        const Stepped s = stepRun(t, m, true, FamAll);
        CHECK(s.action == Action::Run && s.why == Reason::CornerSource);
    }
    // EE writes: a rewritten input makes the results stale, and a draw from them is refused and counted.
    {
        Machine m;
        m.constants();
        m.vertices(0, 8);
        Tracker t;
        m.flags(0x000C);
        stepRun(t, m, false, FamAll);
        m.header(66, 8, 0, 0);
        CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
        t.noteWrite(kPos + 2, 15u); // the EE replaces a position the pending transform has not read yet
        CHECK(t.staleLanes(2) == kLaneStZ && t.virtualLanes(2) == 0 && t.pendingPasses(2) == 0);
        t.noteWrite(kSt + 3, 4u); // the EE replaces a result: only that lane is real again
        CHECK(t.staleLanes(3) == 0 && t.virtualLanes(3) == kLanePos);
        m.indexList(741, {1, 2, 3, 0});
        m.header(741, 1, 1, 0);
        m.flags(0x002C);
        Stepped s = stepRun(t, m, true, FamAll);
        CHECK(s.action == Action::Run && s.why == Reason::StaleInput && t.counters.staleDraws == 1);
        // the next set-up leaves what was pending stale; an upload and a new pass make the slots good again
        m.vertices(0, 8);
        Tracker u;
        m.flags(0x000C);
        stepRun(u, m, false, FamAll);
        m.header(66, 8, 0, 0);
        stepRun(u, m, true, FamAll);
        stepRun(u, m, false, FamAll);
        CHECK(u.staleLanes(1) == (kLanePos | kLaneStZ) && u.counters.staleLanes == 8 * 5);
        m.header(741, 1, 1, 0);
        m.flags(0x002C);
        CHECK(stepRun(u, m, true, FamAll).why == Reason::StaleInput);
        for (uint32_t sl = 0; sl < 8u; ++sl)
            u.noteWrite(kPos + sl, 15u);
        m.flags(0x000C);
        m.header(66, 8, 0, 0);
        CHECK(stepRun(u, m, true, FamAll).action == Action::Skip);
        m.header(741, 1, 1, 0);
        m.flags(0x002C);
        CHECK(stepRun(u, m, true, FamAll).action == Action::Skip);
    }
    // Index lists.
    {
        Machine m;
        m.constants();
        m.vertices(0, 225);
        m.flags(0x002C);
        const auto walk = [&m](const std::vector<int>& list, int quads) {
            for (uint32_t q = 741; q < 773u; ++q)
                for (uint32_t l = 0; l < 4u; ++l)
                    m.word(q, l, 0);
            m.indexList(741, list);
            m.header(741, quads, 1, 0);
            return walkIndexList(m.data.data());
        };
        IndexRun r = walk({1, 2, 3, 0, 4, 5, 6, 7, -1, 8, 9, 10}, 3);
        CHECK(r.why == Reason::None && r.stripCount == 2 && r.strips[0] == 3 && r.strips[1] == 4 && r.cornerCount == 7);
        r = walk({0, 1, 2, 3, 0}, 2); // an empty strip ends the run
        CHECK(r.why == Reason::None && r.stripCount == 0 && r.cornerCount == 0);
        r = walk({1, 2, 3, 0, 0, 4, 5, 6, 0}, 3);
        CHECK(r.why == Reason::None && r.stripCount == 1 && r.cornerCount == 3);
        CHECK(walk({1, 226, 3, 0}, 1).why == Reason::IndexList);
        CHECK(walk({1, 2, 3, 0}, 0).why == Reason::IndexList);
        std::vector<int> big;
        for (int i = 0; i < 63; ++i)
            big.push_back(1 + i);
        big.push_back(0);
        CHECK(walk(big, 16).why == Reason::BufferOverflow); // 1 + 4 * 63 quadwords do not fit above 773
        big.assign(62, 5);
        big.push_back(0);
        CHECK(walk(big, 16).why == Reason::None);
        m.word(kQwTag, 2, 0x5A13u);
        CHECK(walk({1, 2, 3, 0}, 1).why == Reason::TagTemplate);
        m.constants();
        // without flag 0x20 and 4: texture coordinates per corner, a colour per index quadword
        m.flags(0x0008);
        m.indexList(741, {1, 2, 3, 4, 5, 6, 0, 0});
        m.header(741, 2, 65, 30);
        r = walkIndexList(m.data.data());
        CHECK(r.why == Reason::None && r.cornerCount == 6 && r.corners[0].stSlot == 64 && r.corners[5].stSlot == 69 &&
              r.corners[0].colSlot == 30 && r.corners[3].colSlot == 30 && r.corners[4].colSlot == 31);
        m.header(741, 2, 222, 30);
        CHECK(walkIndexList(m.data.data()).why == Reason::IndexList);
        // clip codes: a vertex outside sends the run through the clipper, all inside gives a record with the
        // projection and the closing empty tag
        m.flags(0x002D);
        m.header(741, 1, 1, 0);
        m.indexList(741, {1, 2, 3, 0});
        for (uint32_t s = 0; s < 8u; ++s)
            m.word(kSt + s, 3, 0);
        r = walkIndexList(m.data.data());
        CHECK(r.why == Reason::None && r.clip && r.emptyTag && r.stripCount == 1);
        Tracker t;
        std::vector<uint8_t> packet;
        uint32_t pc = kPcAfterVertex;
        Reason why = Reason::None;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll, true, packet, &why) == Action::Skip &&
              pc == kPcAfterClipList);
        BatchView view;
        CHECK(parseBatch(packet.data() + 16, static_cast<uint32_t>(packet.size() - 16), view) &&
              view.header.passCount == 1 && view.passes[0].kind == PassProject &&
              (view.header.flags & kBatchEmptyTag) != 0u);
        CHECK(packetsOf(packet).size() == 2 && packetsOf(packet)[1].size() == 16);
        pc = kPcAfterVertex;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll & ~FamClipInside, true, packet, &why) == Action::Run &&
              why == Reason::Disabled);
        m.word(kSt + 1, 3, 4);
        pc = kPcAfterVertex;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll, true, packet, &why) == Action::Run &&
              why == Reason::Clipper);
        CHECK(t.counters.declined[static_cast<uint32_t>(Reason::Clipper)] == 1 &&
              t.counters.declined[static_cast<uint32_t>(Reason::Disabled)] == 1);
        // A list of nothing but ends: every end index contributes lane w of quadword 515 as a code to the next
        // entry. With a positive value all entries share an "outside" bit and the run sends nothing; with zero the
        // run sends the empty tag alone. Either way the entries are stored, the seventeenth in quadword 43.
        m.word(kSt + 1, 3, 0);
        for (uint32_t q = 741; q < 757u; ++q)
            for (uint32_t l = 0; l < 4u; ++l)
                m.word(q, l, 0);
        m.header(741, 5, 1, 0);
        m.word(515, 3, 0x80);
        pc = kPcAfterVertex;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll, true, packet, &why) == Action::Skip && packet.empty());
        CHECK(m.word(27, 0) == 0 && m.word(27, 1) == 0xFFF && m.word(28, 0) == 0x80 && m.word(28, 1) == 0x80 &&
              m.word(43, 0) == 0x80 && m.word(43, 1) == 0x80 && m.word(65, 0) == 0x80);
        m.word(515, 3, 0);
        pc = kPcAfterVertex;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll, true, packet, &why) == Action::Skip);
        CHECK(packetsOf(packet).size() == 1 && packetsOf(packet)[0].size() == 16 && m.word(43, 1) == 0);
        // the code of an end index must be real: a pending pass on the last colour slot refuses the run
        {
            Tracker u;
            Machine k = m;
            k.flags(0x0010);
            k.header(66 + 200, 25, 0, 0);
            uint32_t at = kPcAfterVertex;
            CHECK(u.step(true, at, k.vf, k.data.data(), FamAll, true, packet, &why) == Action::Skip);
            k.flags(0x002D);
            k.header(741, 5, 1, 0);
            at = kPcAfterVertex;
            CHECK(u.step(true, at, k.vf, k.data.data(), FamAll, true, packet, &why) == Action::Run &&
                  why == Reason::StaleInput);
        }
    }
    // A run that is not the batch dispatcher is never taken.
    {
        Machine m;
        m.constants();
        Tracker t;
        std::vector<uint8_t> packet;
        uint32_t pc = 0x2A60;
        CHECK(t.step(true, pc, m.vf, m.data.data(), FamAll, true, packet) == Action::Run && pc == 0x2A60);
        CHECK(t.counters.runs[static_cast<uint32_t>(Stage::Other)] == 1);
    }
}

// ---- the run-time policy: the family mask of every run comes from a control word that can change at any time -------
// One VU memory with its tracker, and the packets of every index-list run it saw.
struct Follow {
    Machine m;
    Tracker t;
    std::vector<std::vector<uint8_t>> packets;
    uint64_t staleDeclines = 0;
    // The hook's rest (vu1gpu::live::beforeRun): under a word without a batch stage a clean tracker stops following at
    // a call; then no run steps it and no write is reported to it, until a word with a batch stage comes.
    bool resting = false;
    uint64_t rests = 0, wakes = 0, unfollowedRuns = 0;
};

// The set-up stage executes always: with flag 8 it fills the colour rows with the constant colour.
void executeSetup(Machine& m) {
    if (m.word(kQwFlags, 0) & 8u)
        for (uint32_t s = 0; s < kSlots; ++s)
            for (uint32_t l = 0; l < 4u; ++l)
                m.word(kCol + s, l, m.word(kQwColour, l));
}

// A run executed without the tracker (it rests): what the microprogram does on that memory.
void executeUnfollowed(Follow& f, bool resume, uint32_t startPc) {
    ++f.unfollowedRuns;
    if (!resume) {
        if (startPc == kPcSetup) {
            executeSetup(f.m);
            f.m.pc = kPcAfterSetup;
        } else {
            f.m.pc = 0x2A60;
        }
        return;
    }
    if (f.m.pc == 0x2A60)
        return;
    if (static_cast<int16_t>(f.m.word(kQwHeader, 2)) > 0) {
        Tracker plain;
        std::vector<uint8_t> packet;
        uint32_t at = kPcAfterVertex;
        CHECK(plain.step(true, at, f.m.vf, f.m.data.data(), FamPrimitive, true, packet) == Action::Skip);
        for (auto& pk : packetsOf(packet))
            f.packets.push_back(pk);
        f.m.pc = kPcAfterList;
    } else {
        executeVertexRun(f.m);
        f.m.pc = kPcAfterVertex;
    }
}

// One run the way the hook drives the tracker. mask == kMicroprogram stands for the evaluation mode `microprogram`:
// what was deferred is written before the run and nothing is taken. A mask without a batch family (FamStrips alone,
// or none) does the same under `records`. Under either a clean tracker rests from the next call on.
constexpr uint32_t kMicroprogram = 0x80000000u;
void hookRun(Follow& f, bool resume, uint32_t startPc, uint32_t mask) {
    const bool micro = mask == kMicroprogram;
    const bool batchFamilies = !micro && (mask & ~static_cast<uint32_t>(FamStrips)) != 0u;
    if (f.resting) {
        if (!batchFamilies) {
            executeUnfollowed(f, resume, startPc);
            return;
        }
        f.resting = false; // a batch stage is on again: follow from the clean state the tracker rested in
        ++f.wakes;
    }
    if (!batchFamilies)
        f.t.materialiseAll(f.m.data.data());
    const uint32_t families = micro ? 0u : mask;
    Stepped s;
    uint32_t pc = resume ? f.m.pc : startPc;
    const bool list = resume && static_cast<int16_t>(f.m.word(kQwHeader, 2)) > 0;
    s.action = f.t.step(resume, pc, f.m.vf, f.m.data.data(), families, true, s.packet, &s.why);
    CHECK(!micro || s.action == Action::Run);
    f.staleDeclines += s.why == Reason::StaleInput;
    if (!resume) {
        CHECK(s.action == Action::Run);
        if (startPc == kPcSetup) {
            executeSetup(f.m);
            f.m.pc = kPcAfterSetup;
        } else {
            f.m.pc = 0x2A60; // another program: it stops somewhere outside the batch dispatcher
        }
        if (!batchFamilies && f.t.clean()) {
            f.resting = true;
            ++f.rests;
        }
        return;
    }
    if (f.m.pc == 0x2A60) { // a run of another program (the strip stage): executed, the tracker follows nothing
        CHECK(s.action == Action::Run);
        return;
    }
    if (s.action == Action::Skip) {
        f.m.pc = pc;
        if (list)
            for (auto& pk : packetsOf(s.packet))
                f.packets.push_back(pk);
        else
            CHECK(s.packet.empty());
        return;
    }
    if (list) {
        // executed: what the microprogram sends from this memory is the record of the processed rows
        Tracker plain;
        std::vector<uint8_t> packet;
        uint32_t at = kPcAfterVertex;
        CHECK(plain.step(true, at, f.m.vf, f.m.data.data(), FamPrimitive, true, packet) == Action::Skip);
        for (auto& pk : packetsOf(packet))
            f.packets.push_back(pk);
        f.m.pc = kPcAfterList;
    } else {
        executeVertexRun(f.m);
        f.m.pc = kPcAfterVertex;
    }
}

// An EE write: the same in both memories, reported to both trackers unless one rests (the VIF does not report then).
void eeWrite(Follow& a, Follow& b, uint32_t qw, uint32_t lanes, const uint32_t value[4]) {
    for (Follow* f : {&a, &b}) {
        for (uint32_t l = 0; l < 4u; ++l)
            if ((lanes >> l) & 1u)
                f->m.word(qw, l, value[l]);
        if (!f->resting)
            f->t.noteWrite(qw, lanes);
    }
}

// Wherever the tracker of `test` says VU memory is real, it is the memory of `ref`, in which every run executed.
void expectSameMemory(const Follow& test, const Follow& ref) {
    uint64_t differing = 0;
    for (uint32_t qw = 0; qw < kBuffer; ++qw) {
        const uint32_t marked = test.t.lanesOf(qw, 15u);
        for (uint32_t l = 0; l < 4u; ++l)
            if (((marked >> l) & 1u) == 0u && test.m.word(qw, l) != ref.m.word(qw, l))
                ++differing;
    }
    CHECK(differing == 0);
    CHECK(ref.t.clean());
}

// Terrain-like batches (grid, texture coordinates from positions, transform; per layer a texture matrix run over all
// slots, new alphas and a depth fade; index lists after each) with a run of another program between them, through
// two memories side by side: in `ref` every run executes, in `test` the mask of each run comes from `mask(run)`.
void switchingSequence(const std::function<uint32_t(uint32_t)>& mask, uint32_t batches, Follow& test, Follow& ref) {
    std::mt19937 rng(99u);
    uint32_t run = 0;
    const auto both = [&](bool resume, uint32_t startPc) {
        hookRun(ref, resume, startPc, kMicroprogram);
        hookRun(test, resume, startPc, mask(run++));
        expectSameMemory(test, ref);
    };
    const auto set = [&](uint32_t qw, uint32_t lane, uint32_t value) {
        for (Follow* f : {&test, &ref})
            f->m.word(qw, lane, value);
    };
    const auto draw = [&](const std::vector<int>& list, uint32_t at, int z) {
        for (size_t i = 0; i < list.size(); ++i)
            set(at + static_cast<uint32_t>(i / 4), static_cast<uint32_t>(i & 3),
                static_cast<uint32_t>(list[i]) & 0xFFFFu);
        set(kQwFlags, 0, 0x106C);
        for (Follow* f : {&test, &ref})
            f->m.header(static_cast<int>(at), 4, z, 0);
        both(true, 0);
    };
    test.m.constants();
    ref.m.constants();
    const std::vector<int> listA = {1, 11, 2, 12, 3, 13, 0, 21, 31, 22, 32, 0, 0, 5, 6, 7};
    const std::vector<int> listB = {41, 51, 42, 52, 43, 0, 61, 62, 63, 64, 0, 9, 9, 0, 0, 0};
    for (uint32_t batch = 0; batch < batches; ++batch) {
        // another program between two batches: a call and a continuation (the object path with its strip stage)
        if (batch % 3u == 1u) {
            both(false, 0x26E0);
            both(true, 0);
        }
        const uint32_t cols = 8u + rng() % 5u, rows = 9u + rng() % 4u, n = cols * rows;
        set(kQwFlags, 0, 0x106C);
        for (uint32_t l = 0; l < 4u; ++l)
            set(kQwColour, l, 30u + batch + l);
        both(false, kPcSetup);
        set(kQwGrid, 0, cols);
        set(kQwGrid, 1, rows);
        set(kQwGrid, 2, 66);
        for (Follow* f : {&test, &ref}) {
            f->m.floats(kQwGrid + 1, -500.0f + static_cast<float>(batch), 30.0f, 200.0f, 1.0f);
            f->m.floats(kQwGrid + 2, 100.0f, 0.0f, 250.0f, 0.0f);
        }
        for (uint32_t s = 0; s < n; ++s) { // the heights, as whole quadwords
            const uint32_t value[4] = {rng(), 32768u + rng() % 200u, rng(), rng()};
            eeWrite(test, ref, kPos + s, 15u, value);
        }
        for (Follow* f : {&test, &ref})
            f->m.header(66, static_cast<int>(n), 0, 0);
        both(true, 0);
        draw(listA, 741, 1);
        draw(listB, 757, 65);
        const uint32_t layers = 1u + rng() % 3u;
        for (uint32_t layer = 0; layer < layers; ++layer) {
            for (Follow* f : {&test, &ref}) {
                f->m.floats(kQwTexMatrix, 0.5f + static_cast<float>(layer), 0.1f, 0.0f, 0.0f);
                f->m.floats(kQwTexMatrix + 1, -0.1f, 0.5f + static_cast<float>(batch % 4u), 0.0f, 0.0f);
                f->m.floats(kQwTexMatrix + 3, 0.25f, 0.75f, 0.0f, 0.0f);
                f->m.header(516, 225, 0, 0);
            }
            set(kQwFlags, 0, 0x126C);
            both(true, 0);
            for (uint32_t s = 0; s < n; ++s) { // only the alpha lane before a fade
                const uint32_t value[4] = {0, 0, 0, 60u + (rng() % 60u)};
                eeWrite(test, ref, kCol + s, 8u, value);
            }
            set(kQwFade, 3, b(0.002f * static_cast<float>(layer + 1u)));
            set(kQwFlags, 0, 0x10EC);
            for (Follow* f : {&test, &ref})
                f->m.header(70, static_cast<int>(n - 4u), 0, 0);
            both(true, 0);
            draw(listA, 741, 1);
            draw(listB, 757, 65);
        }
    }
}

void modeSwitchTests() {
    struct Schedule {
        const char* name;
        std::function<uint32_t(uint32_t)> mask;
    };
    std::mt19937 rng(4242u);
    std::vector<uint32_t> random(4096);
    for (uint32_t& r : random)
        r = rng();
    std::vector<Schedule> schedules;
    schedules.push_back({"records", [](uint32_t) { return static_cast<uint32_t>(FamAll); }});
    schedules.push_back({"microprogram", [](uint32_t) { return kMicroprogram; }});
    for (uint32_t period : {1u, 2u, 3u, 7u, 13u, 64u})
        schedules.push_back({"toggle", [period](uint32_t run) {
                                 return ((run / period) & 1u) ? kMicroprogram : static_cast<uint32_t>(FamAll);
                             }});
    // the mode at random, held for a random number of runs
    schedules.push_back({"random mode", [&random](uint32_t run) {
                             return (random[(run / (1u + random[run / 16u % 4096u] % 5u)) % 4096u] & 1u)
                                        ? kMicroprogram
                                        : static_cast<uint32_t>(FamAll);
                         }});
    // any stage families at random under `records`, with the microprogram now and then
    schedules.push_back({"random families", [&random](uint32_t run) {
                             const uint32_t r = random[run % 4096u];
                             return (r % 7u) == 0u ? kMicroprogram : (r >> 8) & FamAll;
                         }});
    // the strip stage alone (the tracker rests), and switching between every family and the strips alone, as a
    // stage toggle (DRAKAN_VERTEX_TOGGLE_STAGES) does at frame boundaries
    schedules.push_back({"strips only", [](uint32_t) { return static_cast<uint32_t>(FamStrips); }});
    for (uint32_t period : {1u, 2u, 5u, 11u, 40u})
        schedules.push_back({"stage toggle", [period](uint32_t run) {
                                 return ((run / period) & 1u) ? static_cast<uint32_t>(FamStrips)
                                                              : static_cast<uint32_t>(FamAll);
                             }});
    schedules.push_back({"random stage sets", [&random](uint32_t run) {
                             const uint32_t r = random[(run / (1u + random[run / 8u % 4096u] % 9u)) % 4096u];
                             switch (r % 4u) {
                                 case 0:
                                     return static_cast<uint32_t>(FamStrips);
                                 case 1:
                                     return kMicroprogram;
                                 case 2:
                                     return static_cast<uint32_t>(FamAll);
                                 default:
                                     return (r >> 8) & FamAll;
                             }
                         }});
    uint64_t switchesSeen = 0, takenUnderSwitching = 0, rests = 0, wakes = 0, unfollowed = 0;
    for (const Schedule& q : schedules) {
        Follow test, ref;
        uint32_t last = 0, switches = 0;
        const auto takesBatch = [](uint32_t m) {
            return m != kMicroprogram && (m & ~static_cast<uint32_t>(FamStrips)) != 0u;
        };
        const auto mask = [&](uint32_t run) {
            const uint32_t now = q.mask(run);
            switches += run != 0u && takesBatch(now) != takesBatch(last);
            last = now;
            return now;
        };
        switchingSequence(mask, 24, test, ref);
        // the same vertices reach the renderer, in the same packets, whatever the mode was at each run
        CHECK(!ref.packets.empty() && test.packets == ref.packets);
        // every draw found its inputs: nothing had to be refused, nothing was drawn from an unknown lane
        CHECK(test.staleDeclines == 0 && test.t.counters.staleDraws == 0);
        CHECK(test.t.counters.declined[static_cast<uint32_t>(Reason::StaleInput)] == 0);
        const Counters& c = test.t.counters;
        const uint64_t taken =
            c.taken[static_cast<uint32_t>(Stage::Vertex)] + c.taken[static_cast<uint32_t>(Stage::List)];
        if (std::strcmp(q.name, "microprogram") == 0 || std::strcmp(q.name, "strips only") == 0) {
            CHECK(taken == 0 && c.passesDeferred == 0 && c.records == 0 && test.t.clean());
            // it rests at the first call and never follows again: the hook costs one test per run from there on
            CHECK(test.rests == 1 && test.wakes == 0 && test.unfollowedRuns != 0);
        } else {
            CHECK(taken != 0);
        }
        if (std::strcmp(q.name, "records") == 0)
            CHECK(test.rests == 0 && test.unfollowedRuns == 0);
        if (std::strcmp(q.name, "random stage sets") == 0)
            CHECK(test.wakes != 0); // it rested and followed again (a short stage toggle may never find it clean)
        if (switches) {
            switchesSeen += switches;
            takenUnderSwitching += taken;
        }
        rests += test.rests;
        wakes += test.wakes;
        unfollowed += test.unfollowedRuns;
    }
    CHECK(switchesSeen > 500 && takenUnderSwitching > 1000);
    CHECK(rests > 20 && wakes > 20 && unfollowed > 500);
    std::printf("mode and stage switches: %llu switches, %llu runs taken while switching; the tracker rested %llu "
                "times and followed again %llu times, %llu runs not followed\n",
                static_cast<unsigned long long>(switchesSeen), static_cast<unsigned long long>(takenUnderSwitching),
                static_cast<unsigned long long>(rests), static_cast<unsigned long long>(wakes),
                static_cast<unsigned long long>(unfollowed));

    // Leaving in the middle of a batch: the deferred stages are written, memory is the microprogram's from then on.
    {
        Follow test, ref;
        uint32_t leaveAt = 0;
        uint64_t written = 0;
        const auto mask = [&](uint32_t run) { return run >= leaveAt ? kMicroprogram : static_cast<uint32_t>(FamAll); };
        for (leaveAt = 1; leaveAt < 40; leaveAt += 3) {
            test = Follow();
            ref = Follow();
            switchingSequence(mask, 6, test, ref);
            CHECK(test.packets == ref.packets);
            written += test.t.counters.slotsMaterialised;
        }
        CHECK(written != 0);
    }

    // When the tracker may stop following: only when no lane is pending or unknown.
    {
        Follow test, ref;
        CHECK(test.t.clean());
        switchingSequence([](uint32_t) { return static_cast<uint32_t>(FamAll); }, 2, test, ref);
        CHECK(!test.t.clean()); // the last batch's deferred stages
        hookRun(test, false, kPcSetup, kMicroprogram);
        hookRun(ref, false, kPcSetup, kMicroprogram);
        CHECK(!test.t.clean()); // lanes earlier batches left unknown stay unknown ...
        expectSameMemory(test, ref);
        uint32_t unknownSlots = 0;
        for (uint32_t s = 0; s < kSlots; ++s)
            unknownSlots += test.t.staleLanes(s) != 0u;
        CHECK(unknownSlots != 0);
        // ... until the EE has written them again: a whole upload of the three arrays
        std::mt19937 fill(5u);
        for (uint32_t qw = kPos; qw < kSt + kSlots; ++qw) {
            const uint32_t value[4] = {fill(), fill(), fill(), fill()};
            eeWrite(test, ref, qw, 15u, value);
        }
        CHECK(test.t.clean());
        // from the clean state the next entry into `records` is the initial state again
        Follow again, againRef;
        switchingSequence([](uint32_t) { return static_cast<uint32_t>(FamAll); }, 3, again, againRef);
        CHECK(again.packets == againRef.packets);
    }
}

void writeNoteTests() {
    // noteWrites over a range is noteWrite per quadword.
    for (uint32_t lanes : {15u, 7u, 8u, 3u}) {
        Follow a, b, ref;
        switchingSequence([](uint32_t) { return static_cast<uint32_t>(FamAll); }, 2, a, ref);
        ref = Follow();
        switchingSequence([](uint32_t) { return static_cast<uint32_t>(FamAll); }, 2, b, ref);
        // start a third batch so that lanes are pending as well as unknown
        for (Follow* f : {&a, &b}) {
            f->m.flags(0x000C);
            hookRun(*f, false, kPcSetup, FamAll);
            f->m.header(66, 150, 0, 0);
            hookRun(*f, true, 0, FamAll);
        }
        a.t.noteWrites(40, 720, lanes); // from below the arrays to the index lists
        for (uint32_t qw = 40; qw < 760; ++qw)
            b.t.noteWrite(qw, lanes);
        bool same = true;
        for (uint32_t s = 0; s < kSlots; ++s)
            same = same && a.t.virtualLanes(s) == b.t.virtualLanes(s) && a.t.staleLanes(s) == b.t.staleLanes(s) &&
                   a.t.pendingPasses(s) == b.t.pendingPasses(s);
        CHECK(same);
        CHECK(a.t.counters.passesDropped == b.t.counters.passesDropped);
    }
    // A lane written with a value made from its old content is unknown, and what was pending on it is dropped.
    {
        Machine m;
        m.constants();
        m.vertices(0, 20);
        Tracker t;
        m.flags(0x000C);
        CHECK(stepRun(t, m, false, FamAll).action == Action::Run);
        m.header(66, 20, 0, 0);
        CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
        CHECK(t.lastFamilies() == FamLightTransform);
        CHECK(t.pendingPasses(3) == 1 && t.virtualLanes(3) == (kLanePos | kLaneStZ));
        t.noteUnknown(kPos + 3, 1u);
        CHECK(t.pendingPasses(3) == 0 && (t.staleLanes(3) & 1u) != 0u && !t.unknownLanes(0, kPos));
        CHECK(t.unknownLanes(kPos + 3, 1) && t.lanesOf(kPos + 3, 15u) != 0u);
        m.indexList(741, {1, 2, 3, 4, 0, 0, 0, 0});
        m.header(741, 2, 1, 0);
        m.flags(0x002C);
        const Stepped s = stepRun(t, m, true, FamAll);
        CHECK(s.action == Action::Run && s.why == Reason::StaleInput);
        CHECK(t.lastFamilies() == FamPrimitive);
        t.noteUnknown(20, 15u); // outside the arrays: nothing to mark
        t.noteUnknown(900, 15u);
    }
}

// The alpha lane of the last colour slot is read by every clip-code index list as the "code" of an end index: the
// tracker keeps that slot what the microprogram would have left, whatever was deferred on it.
void lastSlotTests() {
    Machine test, ref;
    Tracker t;
    for (Machine* m : {&test, &ref}) {
        m->constants();
        m->vertices(0, 225);
        // a water batch over the whole window: flat grid, point light (which replaces alpha), texture coordinates
        // from positions, transform
        m->word(kQwGrid, 0, 15);
        m->word(kQwGrid, 1, 15);
        m->word(kQwGrid, 2, 66);
        m->floats(kQwGrid + 1, -700.0f, 10.0f, 300.0f, 1.0f);
        m->floats(kQwGrid + 2, 100.0f, 0.0f, 100.0f, 0.0f);
        m->word(20, 0, 3);
        m->floats(13, 0.0f, 200.0f, 900.0f, 0.0f);
        m->floats(14, 0.0f, 1.0f, 0.0f, 0.0f);
        m->floats(18, 0.0f, 20.0f, 150.0f, 25.0f);
        m->floats(19, 0.0f, 0.0f, 0.0f, 0.3f);
        m->flags(0x7064);
        m->header(66, 225, 0, 0);
    }
    CHECK(stepRun(t, test, false, FamAll).action == Action::Run);
    CHECK(stepRun(t, test, true, FamAll).action == Action::Skip);
    executeVertexRun(ref);
    CHECK(t.pendingPasses(224) == 4 && (t.virtualLanes(224) & kLaneAlpha) != 0u);
    CHECK(test.word(kCol + 224, 3) != ref.word(kCol + 224, 3)); // deferred: memory still holds the old alpha
    // the next call: the batch ends; slot 224 is written, the other slots are left unknown
    test.flags(0x0001);
    CHECK(stepRun(t, test, false, FamAll).action == Action::Run);
    CHECK(t.pendingPasses(224) == 0 && t.virtualLanes(224) == 0 && t.staleLanes(224) == 0);
    CHECK(t.staleLanes(223) != 0 && t.staleLanes(0) != 0);
    bool same = true;
    for (uint32_t l = 0; l < 4u; ++l)
        same = same && test.word(kPos + 224, l) == ref.word(kPos + 224, l) &&
               test.word(kCol + 224, l) == ref.word(kCol + 224, l) && test.word(kSt + 224, l) == ref.word(kSt + 224, l);
    CHECK(same);
    CHECK(t.counters.slotsMaterialised == 1);
    // A clip-code list that is executed with a code lane of unknown content is counted like a draw from one.
    test.vertices(0, 8);
    for (uint32_t s = 0; s < 8; ++s) {
        t.noteWrite(kPos + s, 15u);
        t.noteWrite(kCol + s, 15u);
        t.noteWrite(kSt + s, 15u);
        test.word(kSt + s, 3, s == 2 ? 4u : 0u); // one vertex outside: the clipper
    }
    test.indexList(741, {1, 2, 3, 4, 0, 5, 6, 7, 0, 0, 0, 0});
    for (uint32_t q = 0; q < 3; ++q)
        t.noteWrite(741 + q, 15u);
    test.flags(0x0025);
    test.header(741, 3, 1, 0);
    Stepped s = stepRun(t, test, true, FamAll);
    CHECK(s.action == Action::Run && s.why == Reason::Clipper && t.counters.staleDraws == 0);
    t.noteUnknown(kCol + 224, 8u);
    test.pc = kPcAfterClipList;
    s = stepRun(t, test, true, FamAll);
    CHECK(s.action == Action::Run && s.why == Reason::Clipper && t.counters.staleDraws == 1);
}

// The strip stage's question before it builds a record from VU memory.
void stripInputTests() {
    // a strip record's payload: header, constants, strip lengths (the vertices are not looked at)
    const auto record = [](uint32_t program, const std::vector<uint32_t>& strips) {
        std::vector<uint8_t> r(sizeof(Header) + sizeof(Constants) + ((strips.size() + 3u) / 4u) * 16u, 0);
        uint32_t vertices = 0;
        for (uint32_t n : strips)
            vertices += n;
        const Header h{program, 0x0Cu, static_cast<uint32_t>(strips.size()), vertices};
        std::memcpy(r.data(), &h, sizeof(h));
        std::memcpy(r.data() + sizeof(Header) + sizeof(Constants), strips.data(), strips.size() * 4u);
        return r;
    };
    constexpr uint32_t kFirst = 54, kAux = 240, kStripSt = 480;
    Tracker fresh;
    CHECK(stripInputsKnown(fresh, record(StripsLit, {3, 31, 31}).data()));

    // A lit batch over slots 0..59, then the next set-up: positions, colours and the 1 / w of those slots are unknown.
    Machine m;
    m.constants();
    m.vertices(0, 60);
    Tracker t;
    m.flags(0x0010);
    CHECK(stepRun(t, m, false, FamAll).action == Action::Run);
    m.header(66, 60, 0, 0);
    CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
    CHECK(stepRun(t, m, false, FamAll).action == Action::Run);
    CHECK(t.unknownLanes(54, 719) && t.staleLanes(0) == (kLanePos | kLaneCol | kLaneStZ));
    // strip table {3}: header slot 54, vertices in slots 55..57, their normals 240 higher (colour rows of the batch
    // slots 4..6), their texture coordinates 480 higher (texture rows of the batch slots 19..21, of which the third
    // lane is unknown), the end of the table in the length word of slot 58
    const std::vector<uint8_t> lit = record(StripsLit, {3}), flat = record(StripsFlat, {3});
    CHECK(!stripInputsKnown(t, lit.data()) && !stripInputsKnown(t, flat.data()));
    t.noteWrites(kFirst + 1 + kStripSt, 3, 3u); // S and T only: the third lane is still unknown
    CHECK(!stripInputsKnown(t, flat.data()));
    t.noteWrites(kFirst + 1 + kStripSt, 3, 7u);
    CHECK(!stripInputsKnown(t, flat.data())); // the two length words lie in colour rows
    t.noteWrite(kFirst + kAux, 1u);
    CHECK(!stripInputsKnown(t, flat.data()));
    t.noteWrite(kFirst + 4 + kAux, 1u);
    CHECK(stripInputsKnown(t, flat.data())); // a constant-colour run reads no normals
    CHECK(!stripInputsKnown(t, lit.data()));
    t.noteWrites(kFirst + 1 + kAux, 3, 7u);
    CHECK(!stripInputsKnown(t, lit.data())); // the lit stage reads all four lanes of a normal
    t.noteWrites(kFirst + 1 + kAux, 3, 15u);
    CHECK(stripInputsKnown(t, lit.data()));
    // a longer table reaches the batch positions (quadword 66 is the vertex of strip slot 66)
    const std::vector<uint8_t> longer = record(StripsFlat, {3, 12});
    CHECK(!stripInputsKnown(t, longer.data()));
    t.noteWrites(kFirst, 20, 15u);           // positions of slots 54..73
    t.noteWrites(kFirst + kStripSt, 20, 7u); // texture coordinates
    t.noteWrite(kFirst + 4 + 13 + kAux, 1u); // the length word that ends the table
    CHECK(stripInputsKnown(t, longer.data()));
    // what was deferred and not yet written counts as unknown too
    m.flags(0x000C);
    m.header(66, 30, 0, 0);
    for (uint32_t s = 0; s < 30; ++s)
        t.noteWrite(kPos + s, 15u);
    CHECK(stepRun(t, m, true, FamAll).action == Action::Skip);
    CHECK(!stripInputsKnown(t, longer.data()));
    t.materialiseAll(m.data.data());
    CHECK(stripInputsKnown(t, longer.data()));
}
} // namespace

int main() {
    helperTests();
    classifyTests();
    deferralTests();
    trackerTests();
    modeSwitchTests();
    writeNoteTests();
    lastSlotTests();
    stripInputTests();
    std::printf("vu_gpu_batch_tests: %llu checks, %d failed\n", static_cast<unsigned long long>(g_checks), g_failed);
    return g_failed ? 1 : 0;
}
