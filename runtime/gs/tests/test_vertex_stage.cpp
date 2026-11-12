// The vertex-stage description and its arithmetic on the CPU (gsr/vertex_stage.h, gsr/vu_exact.inl); no GPU.
//  1. The integer-limb library (the source of the HLSL and WGSL libraries, compiled here as C++) against the double
//     arithmetic of the reference and, on x86, against the processor's own arithmetic rounding toward zero with the
//     engine's result rule, over random and edge operands (GSR_VU_PAIRS=<n> operand triples per thread, default
//     2,000,000; eight threads; every operation is checked on every triple).
//  2. The stage evaluator: the reference arithmetic against the integer arithmetic on the standard cases, and the
//     hand-written vp::evaluate against the evaluator (the documented oracle) on the strip programs.
//  3. The arithmetic choice (vp::chooseArithmetic) and the shape of the generated shader text.
#include "vertex_program_cases.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>

#include <emmintrin.h>
#define GSR_HAVE_HARDWARE_ROUNDING 1
#endif

using namespace gsr;
namespace limbs = gsr::vp::limbs;
using vp::ir::DoubleArithmetic;
using vp::ir::LimbArithmetic;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("[FAIL] %s\n", what);
        ++g_failures;
    }
}

constexpr int kOps = 18;
const char* const kOpNames[kOps] = {"operand",  "add",     "sub",     "mul",  "madd",       "msub",
                                    "div",      "sqrt",    "rsqrt",   "max",  "min",        "itof 0/4/12/15",
                                    "ftoi 0/4", "ftoi 12", "ftoi 15", "less", "less-equal", "equal"};
constexpr uint32_t kFractions[4] = {0, 4, 12, 15};

#ifdef GSR_HAVE_HARDWARE_ROUNDING
// The processor's result with the engine's result rule (ps2_vu1_core.cpp: a denormal is a signed zero, an infinity
// or NaN the largest finite value). The caller has set round-toward-zero.
uint32_t hwResult(__m128 value) {
    const uint32_t bits = static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(value)));
    const uint32_t sign = bits & 0x80000000u, exponent = (bits >> 23) & 0xFFu;
    return exponent == 0u ? sign : exponent == 0xFFu ? sign | 0x7F7FFFFFu : bits;
}
__m128 hwFloat(uint32_t bits) {
    return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(bits)));
}
uint32_t hwAdd(uint32_t a, uint32_t b) {
    return hwResult(_mm_add_ss(hwFloat(a), hwFloat(b)));
}
uint32_t hwSub(uint32_t a, uint32_t b) {
    return hwResult(_mm_sub_ss(hwFloat(a), hwFloat(b)));
}
uint32_t hwMul(uint32_t a, uint32_t b) {
    return hwResult(_mm_mul_ss(hwFloat(a), hwFloat(b)));
}
// DIV of the engine (ps2_vu1_lower.cpp): a zero divisor gives the largest finite value with the quotient's sign.
uint32_t hwDiv(uint32_t a, uint32_t b) {
    if ((b & 0x7FFFFFFFu) == 0u)
        return ((a ^ b) & 0x80000000u) | 0x7F7FFFFFu;
    return hwResult(_mm_div_ss(hwFloat(a), hwFloat(b)));
}
uint32_t hwSqrt(uint32_t a) {
    return hwResult(_mm_sqrt_ss(hwFloat(a & 0x7FFFFFFFu)));
}
uint32_t hwItof(uint32_t a, uint32_t fraction) {
    const __m128 f = _mm_cvtepi32_ps(_mm_cvtsi32_si128(static_cast<int>(a)));
    return hwResult(_mm_mul_ss(f, _mm_set_ss(1.0f / static_cast<float>(1u << fraction))));
}
// Vu1Simd::toFixed: scale, truncate, positive overflow saturates (the conversion itself gives INT_MIN).
uint32_t hwFtoi(uint32_t a, uint32_t fraction) {
    const __m128 scaled = hwFloat(hwResult(_mm_mul_ss(hwFloat(a), _mm_set_ss(static_cast<float>(1u << fraction)))));
    const uint32_t converted = static_cast<uint32_t>(_mm_cvttss_si32(scaled));
    return _mm_comige_ss(scaled, _mm_set_ss(2147483648.0f)) ? ~converted : converted;
}
#endif

struct Tally {
    uint64_t triples = 0;
    uint64_t againstDoubles[kOps] = {}, againstHardware[kOps] = {};
    std::string firstFailure;
};

void note(Tally& t, uint64_t* counter, int op, const char* against, uint32_t a, uint32_t b, uint32_t c, uint32_t got,
          uint32_t want) {
    if (++counter[op] == 1 && t.firstFailure.size() < 2000) {
        char line[200];
        std::snprintf(line, sizeof(line), "  [FAIL] %s against %s: operands %08x %08x %08x: limbs %08x, want %08x\n",
                      kOpNames[op], against, a, b, c, got, want);
        t.firstFailure += line;
    }
}

void runTriples(uint32_t seed, uint64_t count, Tally& t) {
#ifdef GSR_HAVE_HARDWARE_ROUNDING
    const unsigned priorCsr = _mm_getcsr();
    _mm_setcsr((priorCsr & ~0xE040u) | 0x6000u | 0x1F80u); // toward zero; no flush-to-zero, no denormals-are-zero
#endif
    std::mt19937 rng(seed);
    for (uint64_t i = 0; i < count; ++i) {
        const uint32_t ra = vpcases::edgeBits(rng);
        uint32_t rb = vpcases::edgeBits(rng), rc = vpcases::edgeBits(rng);
        const uint32_t how = static_cast<uint32_t>(rng());
        if (how % 4u == 0u) // b beside a (either sign, the same or a neighbouring exponent): cancellation, ties
            rb = (ra ^ ((how >> 2) & 1u ? 0x80000000u : 0u)) + ((how >> 3) % 9u) - 4u + (((how >> 8) % 3u) << 23) -
                 (1u << 23);
        if (how % 8u == 1u) // c beside -(a * b)
            rc = DoubleArithmetic::mul(DoubleArithmetic::operand(ra), DoubleArithmetic::operand(rb)) ^ 0x80000000u ^
                 ((how >> 4) % 4u);
        if (how % 16u == 2u) // a = b * b, exactly or nearly: roots that are exact or just below
            rb = ra, rc = DoubleArithmetic::mul(DoubleArithmetic::operand(ra), DoubleArithmetic::operand(ra)) +
                          ((how >> 4) % 3u) - 1u;
        const uint32_t a = limbs::vuOperand(ra), b = limbs::vuOperand(rb), c = limbs::vuOperand(rc);
        const auto cmp = [&](int op, uint32_t got, uint32_t wantDoubles) {
            if (got != wantDoubles)
                note(t, t.againstDoubles, op, "doubles", ra, rb, rc, got, wantDoubles);
        };
        cmp(0, a, vp::vu::operand(ra));
        cmp(0, b, vp::vu::operand(rb));
        const uint32_t add = limbs::vuAdd(a, b), sub = limbs::vuSub(a, b), mul = limbs::vuMul(a, b);
        const uint32_t madd = limbs::vuMadd(c, a, b), msub = limbs::vuMsub(c, a, b);
        const uint32_t div = limbs::vuDiv(a, b), root = limbs::vuSqrt(c), rsqrt = limbs::vuRsqrt(a, c);
        const uint32_t mx = limbs::vuMax(a, b), mn = limbs::vuMin(a, b);
        cmp(1, add, DoubleArithmetic::add(a, b));
        cmp(2, sub, DoubleArithmetic::add(a, b ^ 0x80000000u));
        cmp(3, mul, DoubleArithmetic::mul(a, b));
        cmp(4, madd, DoubleArithmetic::add(c, DoubleArithmetic::mul(a, b)));
        cmp(5, msub, DoubleArithmetic::add(c, DoubleArithmetic::mul(a, b) ^ 0x80000000u));
        cmp(6, div, DoubleArithmetic::div(a, b));
        cmp(6, limbs::vuDiv(0x3F800000u, a), vp::vu::reciprocal(a));
        cmp(7, root, DoubleArithmetic::sqrt(c));
        cmp(8, rsqrt, DoubleArithmetic::div(a, DoubleArithmetic::sqrt(c)));
        cmp(9, mx, DoubleArithmetic::max(a, b));
        cmp(10, mn, DoubleArithmetic::min(a, b));
        for (const uint32_t fraction : kFractions) {
            cmp(11, limbs::vuItof(ra, fraction), DoubleArithmetic::itof(ra, fraction));
            cmp(fraction <= 4    ? 12
                : fraction == 12 ? 13
                                 : 14,
                limbs::vuFtoi(a, fraction), DoubleArithmetic::ftoi(a, fraction));
        }
        cmp(11, limbs::vuItof(ra, 0), vp::vu::fromInt(static_cast<int32_t>(ra)));
        cmp(15, limbs::vuLess(a, b), DoubleArithmetic::less(a, b));
        cmp(16, limbs::vuLessEqual(a, b), DoubleArithmetic::lessEqual(a, b));
        cmp(17, limbs::vuEqual(a, b), DoubleArithmetic::equal(a, b));
#ifdef GSR_HAVE_HARDWARE_ROUNDING
        const auto hw = [&](int op, uint32_t got, uint32_t want) {
            if (got != want)
                note(t, t.againstHardware, op, "the processor", ra, rb, rc, got, want);
        };
        hw(1, add, hwAdd(a, b));
        hw(2, sub, hwSub(a, b));
        hw(3, mul, hwMul(a, b));
        hw(4, madd, hwAdd(c, hwMul(a, b)));
        hw(5, msub, hwSub(c, hwMul(a, b)));
        hw(6, div, hwDiv(a, b));
        hw(7, root, hwSqrt(c));
        hw(8, rsqrt, hwDiv(a, hwSqrt(c)));
        hw(9, mx, hwResult(_mm_max_ss(hwFloat(a), hwFloat(b))));
        hw(10, mn, hwResult(_mm_min_ss(hwFloat(a), hwFloat(b))));
        for (const uint32_t fraction : kFractions) {
            hw(11, limbs::vuItof(ra, fraction), hwItof(ra, fraction));
            hw(fraction <= 4 ? 12 : fraction == 12 ? 13 : 14, limbs::vuFtoi(a, fraction), hwFtoi(a, fraction));
        }
        hw(15, limbs::vuLess(a, b), _mm_comilt_ss(hwFloat(a), hwFloat(b)) ? 1u : 0u);
        hw(16, limbs::vuLessEqual(a, b), _mm_comile_ss(hwFloat(a), hwFloat(b)) ? 1u : 0u);
        hw(17, limbs::vuEqual(a, b), _mm_comieq_ss(hwFloat(a), hwFloat(b)) ? 1u : 0u);
#endif
    }
    t.triples = count;
#ifdef GSR_HAVE_HARDWARE_ROUNDING
    _mm_setcsr(priorCsr);
#endif
}

void testLibrary() {
    uint64_t perThread = 2000000;
    if (const char* text = std::getenv("GSR_VU_PAIRS"))
        perThread = std::strtoull(text, nullptr, 10);
    constexpr unsigned kThreads = 8;
    std::vector<Tally> tallies(kThreads);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < kThreads; ++i)
        threads.emplace_back(runTriples, 20261112u + i * 7919u, perThread, std::ref(tallies[i]));
    for (auto& thread : threads)
        thread.join();
    uint64_t triples = 0, doubles[kOps] = {}, hardware[kOps] = {}, bad = 0;
    for (const Tally& t : tallies) {
        triples += t.triples;
        for (int op = 0; op < kOps; ++op) {
            doubles[op] += t.againstDoubles[op];
            hardware[op] += t.againstHardware[op];
        }
        if (!t.firstFailure.empty())
            std::fputs(t.firstFailure.c_str(), stdout);
    }
    for (int op = 0; op < kOps; ++op) {
        bad += doubles[op] + hardware[op];
        if (doubles[op] || hardware[op])
            std::printf("[FAIL] %s: %llu differ from the double arithmetic, %llu from the processor\n", kOpNames[op],
                        (unsigned long long)doubles[op], (unsigned long long)hardware[op]);
    }
#ifdef GSR_HAVE_HARDWARE_ROUNDING
    const char* hardwareNote = "and the processor rounding toward zero";
#else
    const char* hardwareNote = "(no processor comparison on this target)";
#endif
    std::printf("integer-limb library against the double arithmetic %s: %llu operand triples, %d operations each, "
                "%llu results differ\n",
                hardwareNote, (unsigned long long)triples, kOps, (unsigned long long)bad);
    check(bad == 0, "the integer-limb library equals the reference arithmetic");
}

void testStages() {
    const vpcases::StandardCases s = vpcases::standardCases();
    const vpcases::Cases& c = s.cases;
    const auto time = [](auto&& work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    };
    std::vector<vp::ir::Raw> doubles, integers;
    const double nsDoubles = time([&] { doubles = vpcases::reference<DoubleArithmetic>(c); });
    const double nsIntegers = time([&] { integers = vpcases::reference<LimbArithmetic>(c); });
    const uint64_t differ =
        vpcases::compareRaw("integer evaluator", c, doubles, reinterpret_cast<const uint32_t*>(integers.data()));
    std::printf("stage evaluator, integer arithmetic against the reference arithmetic: %zu vertices (%zu generated, "
                "%zu probes, %zu recorded), %llu differ\n",
                c.vertices.size(), s.generated, s.probes, s.recorded, (unsigned long long)differ);
    check(differ == 0, "the two CPU arithmetics give the same packets");

    // the hand-written twin against the oracle, on the strip programs
    uint64_t strips = 0, twinDiffers = 0;
    std::vector<vp::Output> hand(c.vertices.size());
    const double nsHand = time([&] {
        for (size_t i = 0; i < c.vertices.size(); ++i)
            if (c.vertices[i].program < vpcases::kProbeFirst)
                hand[i] = vp::evaluate(c.vertices[i].program, c.constants[c.vertices[i].constants], c.vertices[i].in);
    });
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        if (c.vertices[i].program >= vpcases::kProbeFirst)
            continue;
        ++strips;
        const vp::Output o = vp::ir::Evaluator<>::toOutput(doubles[i]);
        const vp::Output& h = hand[i];
        const bool same = o.s == h.s && o.t == h.t && o.q == h.q && o.r == h.r && o.g == h.g && o.b == h.b &&
                          o.a == h.a && o.x == h.x && o.y == h.y && o.z == h.z && o.fog == h.fog && o.adc == h.adc;
        if (!same && ++twinDiffers <= 4)
            std::printf("  [FAIL] vp::evaluate differs from the stage evaluator at vertex %zu (program %u)\n", i,
                        c.vertices[i].program);
    }
    std::printf("vp::evaluate (hand-written twin) against the stage evaluator: %llu strip vertices, %llu differ\n",
                (unsigned long long)strips, (unsigned long long)twinDiffers);
    check(twinDiffers == 0, "vp::evaluate equals the stage evaluator");
    {
        const vpcases::GpuVertex& v = c.vertices[1];
        const vp::Output a = vp::evaluateStage(v.program, c.constants[v.constants], v.in);
        const vp::Output b = vp::evaluate(v.program, c.constants[v.constants], v.in);
        check(a.x == b.x && a.y == b.y && a.z == b.z && a.s == b.s && a.r == b.r, "vp::evaluateStage");
    }
    std::printf("for information, CPU cost per vertex on this machine (one thread, whole standard set): reference "
                "evaluator %.0f ns, integer evaluator %.0f ns; vp::evaluate on the %llu strip vertices %.0f ns\n",
                nsDoubles / c.vertices.size(), nsIntegers / c.vertices.size(), (unsigned long long)strips,
                nsHand / (strips ? strips : 1));
    std::printf("stage sizes: StripsFlat %zu operations, StripsLit %zu\n", vp::ir::stripsFlat().nodes.size(),
                vp::ir::stripsLit().nodes.size());
}

void testPolicy() {
    using namespace vp;
    const auto d3d = ShaderBackend::Direct3D11, web = ShaderBackend::WebGpu;
    check(chooseArithmetic(d3d, true, ArithmeticRequest::Auto) == Arithmetic::Limbs, "auto on Direct3D 11");
    check(chooseArithmetic(d3d, false, ArithmeticRequest::Auto) == Arithmetic::Limbs, "auto without doubles");
    check(chooseArithmetic(web, false, ArithmeticRequest::Auto) == Arithmetic::Limbs, "auto on WebGPU");
    check(chooseArithmetic(d3d, true, ArithmeticRequest::Doubles) == Arithmetic::Doubles, "doubles when supported");
    check(chooseArithmetic(d3d, false, ArithmeticRequest::Doubles) == Arithmetic::None, "doubles unsupported: CPU");
    check(chooseArithmetic(web, true, ArithmeticRequest::Doubles) == Arithmetic::None, "no doubles in WGSL: CPU");
    check(chooseArithmetic(d3d, true, ArithmeticRequest::Limbs) == Arithmetic::Limbs, "limbs forced");
    check(chooseArithmetic(web, false, ArithmeticRequest::Limbs) == Arithmetic::Limbs, "limbs forced on WebGPU");
    for (const bool doubles : {false, true})
        for (const ShaderBackend backend : {d3d, web})
            check(chooseArithmetic(backend, doubles, ArithmeticRequest::None) == Arithmetic::None, "forced off");
    check(parseArithmeticRequest(nullptr) == ArithmeticRequest::Auto, "no text");
    check(parseArithmeticRequest("") == ArithmeticRequest::Auto, "empty text");
    check(parseArithmeticRequest("Doubles") == ArithmeticRequest::Doubles, "doubles text");
    check(parseArithmeticRequest("limbs") == ArithmeticRequest::Limbs, "limbs text");
    check(parseArithmeticRequest("off") == ArithmeticRequest::None, "off text");
    check(parseArithmeticRequest("CPU") == ArithmeticRequest::None, "cpu text");
    check(parseArithmeticRequest("limbsx") == ArithmeticRequest::Auto, "unknown text");
}

void testEmitters() {
    using namespace vp;
    const std::vector<StageEntry> stages = gameStages();
    const auto has = [](const std::string& text, const char* what) { return text.find(what) != std::string::npos; };
    check(emitLibrary(ShaderLanguage::Wgsl, Arithmetic::Doubles).empty(), "no double library in WGSL");
    check(emitLibrary(ShaderLanguage::Hlsl, Arithmetic::None).empty(), "no library without arithmetic");
    const std::string hlslLimbs = emitLibrary(ShaderLanguage::Hlsl, Arithmetic::Limbs);
    const std::string hlslDoubles = emitLibrary(ShaderLanguage::Hlsl, Arithmetic::Doubles);
    const std::string wgsl = emitLibrary(ShaderLanguage::Wgsl, Arithmetic::Limbs);
    check(!has(hlslLimbs, "double") && !has(hlslLimbs, "uvar") && has(hlslLimbs, "uint vuAdd(uint a, uint b)"),
          "integer HLSL library");
    check(has(hlslDoubles, "precise double") && has(hlslDoubles, "uint vuFtoi(") && has(hlslDoubles, "uint vuMadd("),
          "double HLSL library keeps the shared functions");
    check(!has(wgsl, "uvar") && !has(wgsl, "uint") && has(wgsl, "fn vuAdd(a: u32, b: u32) -> u32"), "WGSL library");
    const std::string all = emitStages(ShaderLanguage::Wgsl, stages.data(), stages.size());
    const std::string position =
        emitStages(ShaderLanguage::Wgsl, stages.data(), stages.size(), ir::kPositionFields, "Previous");
    check(has(all, "fn vpEvaluate(i: VpIn) -> VpRaw") && has(all, "fn vpEvaluateAt(i: VpIn, constantsIndex: u32)"),
          "dispatchers");
    check(has(all, "vuItof") && !has(position, "vuItof") && has(position, "fn vpEvaluateAtPrevious(") &&
              position.size() < all.size(),
          "a field mask drops the operations of the other fields");
    const std::string hlsl = emitStages(ShaderLanguage::Hlsl, stages.data(), stages.size());
    check(has(hlsl, "VpRaw vpEvaluate(VpIn i)") && has(hlsl, "VpRaw vpStage2(VpIn i, uint base)"), "HLSL stages");
}
} // namespace

int main() {
    testPolicy();
    testEmitters();
    testLibrary();
    testStages();
    std::printf(g_failures ? "vertex stage: %d checks FAILED\n" : "vertex stage: all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
