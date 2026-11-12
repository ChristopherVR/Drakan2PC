// Vertex-program records of the batch path (gsr/vertex_batch.h, vu/ps2_vu1_gpu_batch.h):
//   * the exact-arithmetic helpers of the CPU evaluation against the engine's own arithmetic (host floats with the
//     rounding mode toward zero and denormals flushed, vu/ps2_vu1_simd.h);
//   * the tracker's policy on synthetic VU memory: which stages a flag word selects, the per-family switches, every
//     decline reason, pending passes against passes applied to memory, EE writes, stale lanes, index lists.
// No game data: every input is generated here.
#include "vu/ps2_vu1_gpu_batch.h"
#include "vu/ps2_vu1_simd.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
} // namespace

int main() {
    helperTests();
    classifyTests();
    deferralTests();
    trackerTests();
    std::printf("vu_gpu_batch_tests: %llu checks, %d failed\n", static_cast<unsigned long long>(g_checks), g_failed);
    return g_failed ? 1 : 0;
}
