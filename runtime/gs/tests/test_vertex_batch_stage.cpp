// The batch path in the stage description (gsr/vertex_batch_stage.h) on the CPU, pinned to gsr/vertex_batch.h, the
// evaluation proven against the microprogram; no GPU.
//  1. Where the helpers of the batch evaluation and the operations of the arithmetic library differ (MAX and MINI on
//     raw registers, the reciprocal length, the square root), as test vectors: the difference exists, and the
//     description reproduces the batch evaluation.
//  2. Every pass kind alone: all twelve lanes of the row after ir::BatchEvaluator against vp::applyPass, with the
//     reference arithmetic and with the integer library.
//  3. Chains (tests/vertex_batch_cases.h): rows and packets against vp::evaluateVertex, both arithmetics; the
//     position of the previous frame's inputs does not depend on the colour and texture rows.
//  4. With GSR_VB_RECORDS: the dumped records, vertex by vertex, and their GIF packets against vp::batchToGif.
//  5. The shape of the generated shader text.
#include "vertex_batch_cases.h"

#include <chrono>

using namespace gsr;
namespace limbs = gsr::vp::limbs;
using vp::ir::BatchEvaluator;
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
bool isNan(uint32_t bits) {
    return (bits & 0x7FFFFFFFu) > 0x7F800000u;
}
const char* kindName(uint32_t kind) {
    const vp::ir::Stage* stage = vp::ir::batchPass(kind);
    return stage ? stage->name : "?";
}

// A stage of the test alone: rawMax and rawMin of the description on two registers (constants 0.x and 0.y), and
// with a first operand that came out of an operation (0.z as an operand).
vp::ir::Stage buildRawProbe() {
    using namespace vp::ir;
    using namespace vp::ir::batchdetail;
    Builder b("RawProbe");
    const RawFloat x = rawRegister(b, b.constant(0, 0)), y = rawRegister(b, b.constant(0, 1));
    const RawFloat z{b.constantFloat(0, 2), {}};
    b.output(S, rawMax(b, x, y).value);
    b.output(T, rawMin(b, x, y).value);
    b.output(Q, rawMax(b, rawMin(b, z, x), y).value); // the depth clamp of the transform
    b.output(Z, rawMin(b, rawMax(b, z, x), y).value); // the height limits of the point light
    return b.finish();
}

void testHelpers() {
    using namespace vp::vu;
    // operands of every class
    std::vector<uint32_t> values = {0u,          0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
                                    0x00800000u, 0x80800000u, 0x3F800000u, 0xBF800000u, 0x3F7FFFFFu, 0x3F800001u,
                                    0x7F7FFFFEu, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7F800001u,
                                    0xFF800001u, 0x7FC00000u, 0xFFC00000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x42C80000u,
                                    0xC2C80000u, 0x43000000u, 0x437F0000u};
    std::mt19937 rng(20261118u);
    for (int i = 0; i < 400; ++i)
        values.push_back(vpcases::edgeBits(rng));

    // MAX and MINI. The engine compares raw registers and passes one through; the library takes operands.
    uint64_t pairs = 0, maxDiffers = 0, minDiffers = 0, differsWithoutNan = 0, probeDiffers = 0;
    const vp::ir::Stage probe = buildRawProbe();
    vp::ir::Evaluator<DoubleArithmetic> probeDoubles(probe);
    vp::ir::Evaluator<LimbArithmetic> probeLimbs(probe);
    for (const uint32_t a : values)
        for (const uint32_t b : values) {
            ++pairs;
            const uint32_t wantMax = operand(maxRaw(a, b)), wantMin = operand(minRaw(a, b));
            const bool maxOther = limbs::vuMax(limbs::vuOperand(a), limbs::vuOperand(b)) != wantMax;
            const bool minOther = limbs::vuMin(limbs::vuOperand(a), limbs::vuOperand(b)) != wantMin;
            maxDiffers += maxOther;
            minDiffers += minOther;
            if ((maxOther || minOther) && !isNan(a) && !isNan(b))
                ++differsWithoutNan;
            for (const uint32_t c : {values[pairs % values.size()], 0x42C80000u, 0x7F7FFFFFu, 0x80000000u}) {
                const uint32_t z = operand(c);
                const uint32_t constants[4] = {a, b, c, 0u};
                const uint32_t want[4] = {wantMax, wantMin, operand(maxRaw(minRaw(z, a), b)),
                                          operand(minRaw(maxRaw(z, a), b))};
                probeDoubles.bind(constants);
                probeLimbs.bind(constants);
                const vp::ir::Raw d = probeDoubles.vertexRaw(static_cast<const uint32_t*>(nullptr));
                const vp::ir::Raw l = probeLimbs.vertexRaw(static_cast<const uint32_t*>(nullptr));
                const uint32_t gotD[4] = {d.stq[0], d.stq[1], d.stq[2], d.place[2]};
                const uint32_t gotL[4] = {l.stq[0], l.stq[1], l.stq[2], l.place[2]};
                if (std::memcmp(gotD, want, 16) != 0 || std::memcmp(gotL, want, 16) != 0)
                    if (++probeDiffers <= 4)
                        std::printf("  [FAIL] raw MAX / MINI of the description: registers %08x %08x, operand %08x\n",
                                    a, b, c);
            }
        }
    std::printf("MAX / MINI on raw registers, %llu register pairs of every class: the library's vuMax on operands "
                "differs from the engine's rule on %llu, vuMin on %llu (%llu of them without a NaN); the "
                "description's raw MAX / MINI (alone and as the two clamps, both arithmetics) differs on %llu\n",
                (unsigned long long)pairs, (unsigned long long)maxDiffers, (unsigned long long)minDiffers,
                (unsigned long long)differsWithoutNan, (unsigned long long)probeDiffers);
    check(maxDiffers > 0 && minDiffers > 0, "the NaN rule of MAX and MINI is a real difference (the vectors hit it)");
    check(differsWithoutNan == 0, "without a NaN, MAX and MINI on operands equal the engine's on raw registers");
    check(probeDiffers == 0, "the description's raw MAX and MINI equal the batch evaluation");
    // the single vectors the journal quotes
    check(operand(maxRaw(0x42C80000u, 0x7FC00000u)) == 0x7F7FFFFFu &&
              limbs::vuMax(0x42C80000u, limbs::vuOperand(0x7FC00000u)) == 0x7F7FFFFFu,
          "max(100, +NaN): the NaN comes out either way");
    check(operand(maxRaw(0x42C80000u, 0xFFC00000u)) == 0xFF7FFFFFu &&
              limbs::vuMax(0x42C80000u, limbs::vuOperand(0xFFC00000u)) == 0x42C80000u,
          "max(100, -NaN): the engine passes the NaN, the library returns 100");
    check(operand(minRaw(0x42C80000u, 0x7FC00000u)) == 0x7F7FFFFFu &&
              limbs::vuMin(0x42C80000u, limbs::vuOperand(0x7FC00000u)) == 0x42C80000u,
          "min(100, +NaN): the engine passes the NaN, the library returns 100");
    check(operand(maxRaw(0x7F800000u, 0x7F7FFFFFu)) == limbs::vuMax(0x7F7FFFFFu, 0x7F7FFFFFu) &&
              maxRaw(0x7F800000u, 0x7F7FFFFFu) == 0x7F800000u,
          "max(+inf, largest): the engine picks the infinity, whose operand form is the library's result");
    check(maxRaw(0x00000001u, 0x80000000u) == 0x80000000u && maxRaw(0x80000000u, 0u) == 0u &&
              maxRaw(0u, 0x80000000u) == 0x80000000u,
          "denormals and signed zeros compare equal: the second register comes out");

    // The reciprocal length: zero for a zero length; the library's reciprocal square root gives the largest value.
    check(reciprocalLength(0u, 0x80000000u, 0u) == 0u, "reciprocalLength of a zero vector is +0");
    check(limbs::vuRsqrt(0x3F800000u, 0u) == 0x7F7FFFFFu, "vuRsqrt of a zero radicand is the largest finite value");
    uint64_t lengths = 0, lengthDiffers = 0, rootDiffers = 0, reciprocalDiffers = 0;
    for (size_t i = 0; i + 2 < values.size(); ++i)
        for (const uint32_t tweak : {0u, 1u, 0x80000000u}) {
            const uint32_t x = operand(values[i] ^ tweak), y = operand(values[i + 1]), z = operand(values[i + 2]);
            ++lengths;
            // the description's form: a select around a division by the root
            const uint32_t sum = limbs::vuAdd(limbs::vuAdd(limbs::vuMul(x, x), limbs::vuMul(y, y)), limbs::vuMul(z, z));
            const uint32_t root = limbs::vuSqrt(sum);
            const uint32_t got = (root & 0x7FFFFFFFu) == 0u ? root : limbs::vuDiv(0x3F800000u, root);
            lengthDiffers += got != reciprocalLength(x, y, z);
            // sqrtZero and vuSqrt agree on what a sum of squares can be (zero or positive, never -0)
            rootDiffers += root != sqrtZero(sum) || (sum >> 31) != 0u;
            rootDiffers += limbs::vuSqrt(x & 0x7FFFFFFFu) != sqrtZero(x & 0x7FFFFFFFu);
            reciprocalDiffers += limbs::vuDiv(0x3F800000u, x) != reciprocal(x);
        }
    std::printf("reciprocal length, square root and reciprocal, %llu operand triples: %llu, %llu, %llu differ\n",
                (unsigned long long)lengths, (unsigned long long)lengthDiffers, (unsigned long long)rootDiffers,
                (unsigned long long)reciprocalDiffers);
    check(lengthDiffers == 0, "select(length == 0, length, 1 / sqrt(sum)) equals reciprocalLength");
    check(rootDiffers == 0, "vuSqrt equals sqrtZero on zero and positive operands");
    check(reciprocalDiffers == 0, "vuDiv(1, w) equals reciprocal(w)");
    check(sqrtZero(0x80000000u) == 0x80000000u && limbs::vuSqrt(0x80000000u) == 0u,
          "sqrt(-0): sqrtZero returns its operand, vuSqrt +0 (the description never takes the root of -0)");

    // The identities the description uses instead of the literal operation.
    uint64_t identityDiffers = 0;
    for (const uint32_t raw : values) {
        const uint32_t x = operand(raw);
        identityDiffers += mul(x, kOne) != x;
        identityDiffers += sub(x, 0u) != x;
        identityDiffers += mul(x, asBits(-1.0f)) != (x ^ 0x80000000u);
        const uint32_t n = fromInt(static_cast<int32_t>(raw));
        identityDiffers += madd(x, n, kOne) != add(x, n);
    }
    check(identityDiffers == 0, "x * 1, x - 0, x * -1 and acc + n * 1 are what the description writes for them");
    check(add(0u, 0x80000000u) == 0u, "0 + -0 is +0 (the transform's 1 / w: kept as a sum)");
}

struct Tally {
    uint64_t vertices = 0, rowDoubles = 0, rowLimbs = 0, rawDoubles = 0, rawLimbs = 0, output = 0;
};

// Rows and packets of every case against the oracle, both arithmetics.
void testCases(const vbcases::Cases& c, const vbcases::Reference& ref) {
    Tally perKind[vp::ir::kBatchKindLimit], perPart[4], perLength[vp::kBatchMaxPasses + 1];
    BatchEvaluator<DoubleArithmetic> doubles;
    BatchEvaluator<LimbArithmetic> integers;
    uint32_t bound = ~0u;
    uint64_t printed = 0, appliedPasses = 0;
    const size_t parts[4] = {c.single, c.single + c.flagChains, c.single + c.flagChains + c.randomChains,
                             c.vertices.size()};
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const vbcases::Record& r = c.records[c.recordOf[i]];
        const vp::Pass* passes = c.passes.data() + r.firstPass;
        if (bound != c.recordOf[i]) {
            doubles.bind(passes, r.passCount);
            integers.bind(passes, r.passCount);
            bound = c.recordOf[i];
        }
        const vp::BatchVertex v = vbcases::batchVertexOf(c.vertices[i]);
        const vp::Row rowD = doubles.row(v), rowL = integers.row(v);
        const vp::ir::Raw rawD = doubles.finishRaw(rowD), rawL = integers.finishRaw(rowL);
        const vp::Output want = vp::evaluateVertex(passes, r.passCount, v);
        const vp::Output o = vp::ir::Evaluator<>::toOutput(rawD);
        Tally one;
        one.vertices = 1;
        one.rowDoubles = std::memcmp(&rowD, &ref.rows[i], 48) != 0;
        one.rowLimbs = std::memcmp(&rowL, &ref.rows[i], 48) != 0;
        one.rawDoubles = std::memcmp(&rawD, &ref.raws[i], 48) != 0;
        one.rawLimbs = std::memcmp(&rawL, &ref.raws[i], 48) != 0;
        one.output = !(o.s == want.s && o.t == want.t && o.q == want.q && o.r == want.r && o.g == want.g &&
                       o.b == want.b && o.a == want.a && o.x == want.x && o.y == want.y && o.z == want.z &&
                       o.fog == want.fog && o.adc == want.adc);
        uint32_t length = 0;
        for (uint32_t p = 0; p < r.passCount; ++p)
            length += (v.passMask >> p) & 1u;
        appliedPasses += length;
        const size_t part = i < parts[0] ? 0 : i < parts[1] ? 1 : i < parts[2] ? 2 : 3;
        Tally* into[3] = {&perPart[part], &perLength[length],
                          part == 0 ? &perKind[passes[0].kind] : static_cast<Tally*>(nullptr)};
        for (Tally* t : into)
            if (t) {
                t->vertices += 1;
                t->rowDoubles += one.rowDoubles;
                t->rowLimbs += one.rowLimbs;
                t->rawDoubles += one.rawDoubles;
                t->rawLimbs += one.rawLimbs;
                t->output += one.output;
            }
        if ((one.rowDoubles || one.rowLimbs || one.rawDoubles || one.rawLimbs || one.output) && ++printed <= 8) {
            std::printf("  [FAIL] vertex %zu (mask %08x, kinds", i, v.passMask);
            for (uint32_t p = 0; p < r.passCount; ++p)
                std::printf(" %u", passes[p].kind);
            std::printf("): row lanes");
            const uint32_t* w = reinterpret_cast<const uint32_t*>(&ref.rows[i]);
            const uint32_t* d = reinterpret_cast<const uint32_t*>(&rowD);
            const uint32_t* l = reinterpret_cast<const uint32_t*>(&rowL);
            for (int k = 0; k < 12; ++k)
                if (d[k] != w[k] || l[k] != w[k])
                    std::printf(" [%d] doubles %08x limbs %08x want %08x", k, d[k], l[k], w[k]);
            std::printf("\n");
        }
    }
    const auto bad = [](const Tally& t) { return t.rowDoubles + t.rowLimbs + t.rawDoubles + t.rawLimbs + t.output; };
    std::printf("one pass alone, all twelve lanes of the row against vp::applyPass (reference arithmetic / integer "
                "library):\n");
    for (const uint32_t kind : vp::ir::kBatchKinds) {
        const Tally& t = perKind[kind];
        std::printf("  %-10s %6llu vertices, %llu / %llu differ\n", kindName(kind), (unsigned long long)t.vertices,
                    (unsigned long long)t.rowDoubles, (unsigned long long)t.rowLimbs);
        check(t.vertices > 0 && bad(t) == 0, "a pass kind alone equals vp::applyPass");
    }
    static const char* const kParts[4] = {"one pass alone", "flag-word chains", "random chains", "recorded"};
    for (int p = 0; p < 4; ++p) {
        const Tally& t = perPart[p];
        std::printf("%s: %llu vertices; rows differ %llu (reference arithmetic), %llu (integer library); packets "
                    "differ %llu, %llu; fields against vp::evaluateVertex differ %llu\n",
                    kParts[p], (unsigned long long)t.vertices, (unsigned long long)t.rowDoubles,
                    (unsigned long long)t.rowLimbs, (unsigned long long)t.rawDoubles, (unsigned long long)t.rawLimbs,
                    (unsigned long long)t.output);
        check(bad(t) == 0, "the batch description equals vp::evaluateVertex");
    }
    std::printf("vertices by passes applied:");
    uint32_t longest = 0;
    for (uint32_t n = 0; n <= vp::kBatchMaxPasses; ++n) {
        if (perLength[n].vertices) {
            std::printf(" %u:%llu", n, (unsigned long long)perLength[n].vertices);
            longest = n;
        }
        check(bad(perLength[n]) == 0, "chains of every length");
    }
    std::printf(" (%.2f per vertex)\n", c.vertices.empty() ? 0.0 : double(appliedPasses) / c.vertices.size());
    check(longest == vp::kBatchMaxPasses && perLength[1].vertices > 0, "chains from 1 to kBatchMaxPasses passes occur");
}

// The previous frame's position: the same for any colour and texture rows (what lets a shader evaluate the position
// passes alone), and the plan the emitter derives for it.
void testPrevious(const vbcases::Cases& c, const vbcases::Reference& ref) {
    uint64_t differ = 0;
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const vbcases::Record& p = c.records[c.previousRecordOf[i]];
        vp::BatchVertex v = vbcases::previousVertexOf(c.vertices[i]);
        std::memcpy(v.row.col, c.vertices[i].row.col, 16);
        std::memcpy(v.row.st, c.vertices[i].row.st, 16);
        const vp::Output o = vp::evaluateVertex(c.passes.data() + p.firstPass, p.passCount, v);
        differ += o.x != ref.previous[i].place[0] || o.y != ref.previous[i].place[1] || o.z != ref.previous[i].place[2];
    }
    std::printf("the position of the previous frame's inputs with other colour and texture rows: %zu vertices, %llu "
                "differ\n",
                c.vertices.size(), (unsigned long long)differ);
    check(differ == 0, "x, y and z depend on the position row and the position passes alone");
    const vp::BatchPlan position = vp::planBatch(vp::ir::kPositionFields), all = vp::planBatch(vp::ir::kAllFields);
    const std::vector<uint32_t> wantKinds = {vp::PassGrid, vp::PassGridFlat, vp::PassTransform, vp::PassProject};
    check(position.stateMask == vp::ir::kPositionState && position.kinds == wantKinds,
          "the position plan: the four lanes of the position row; grid, flat grid, transform, project");
    check(all.stateMask == 0x7FFu && all.kinds.size() == 10, "the full plan: every kind, every lane but texture w");
}

// The dumped records as GIF packets: vp::batchToGif against the same packets built from the description.
void testDumps() {
    BatchEvaluator<DoubleArithmetic> doubles;
    BatchEvaluator<LimbArithmetic> integers;
    for (const std::string& file : vbcases::dumpFiles()) {
        uint64_t packets = 0, vertices = 0, differD = 0, differL = 0, longest = 0, passSum = 0;
        const uint32_t records = vbcases::forEachDumped(file.c_str(), [&](const vp::BatchView& rec) {
            std::vector<std::vector<uint8_t>> want;
            vp::batchToGif(rec, want);
            doubles.bind(rec.passes, rec.header.passCount);
            integers.bind(rec.passes, rec.header.passCount);
            longest = std::max<uint64_t>(longest, rec.header.passCount);
            const uint16_t* index = rec.indices;
            for (uint32_t s = 0; s < rec.header.stripCount; ++s, ++packets) {
                const std::vector<uint8_t>& packet = want[s];
                for (uint32_t i = 0; i < rec.strips[s]; ++i, ++index, ++vertices) {
                    const vp::BatchVertex& v = rec.vertices[*index];
                    for (uint32_t p = 0; p < rec.header.passCount; ++p)
                        passSum += (v.passMask >> p) & 1u;
                    for (int which = 0; which < 2; ++which) {
                        const vp::ir::Raw raw = which ? integers.vertexRaw(v) : doubles.vertexRaw(v);
                        // the sixteen words of a packet vertex as vp::batchToGif lays them out
                        uint32_t w[16] = {};
                        w[0] = raw.stq[0], w[1] = raw.stq[1], w[2] = raw.stq[2];
                        for (int l = 0; l < 4; ++l)
                            w[4 + l] = raw.colour[l];
                        w[11] = ((raw.place[3] >> 4) & 0xFFu) << 4;
                        w[12] = raw.place[0], w[13] = raw.place[1], w[14] = raw.place[2];
                        w[15] = raw.place[3] & 0x8000u;
                        const bool same = packet.size() == 16u + size_t(rec.strips[s]) * 64u &&
                                          std::memcmp(packet.data() + 16u + size_t(i) * 64u, w, 64) == 0;
                        (which ? differL : differD) += !same;
                    }
                }
            }
        });
        std::printf("%s: %u records, %llu packets, %llu packet vertices (%.2f passes each, records of up to %llu): "
                    "%llu differ from vp::batchToGif with the reference arithmetic, %llu with the integer library\n",
                    file.c_str(), records, (unsigned long long)packets, (unsigned long long)vertices,
                    vertices ? double(passSum) / vertices : 0.0, (unsigned long long)longest,
                    (unsigned long long)differD, (unsigned long long)differL);
        check(records > 0 && differD == 0 && differL == 0, "the description gives the packets of vp::batchToGif");
    }
}

void testEmitters() {
    using namespace vp;
    const auto count = [](const std::string& text, const std::string& what) {
        size_t n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
            ++n;
        return n;
    };
    for (const ShaderLanguage language : {ShaderLanguage::Hlsl, ShaderLanguage::Wgsl}) {
        const std::string all = emitBatch(language);
        const std::string position = emitBatch(language, ir::kPositionFields, "Previous");
        for (const uint32_t kind : ir::kBatchKinds) {
            const std::string name = "vbPass" + std::to_string(kind) + "(";
            check(count(all, name) == 2, "every pass kind is one function, called from one place (no unrolled chain)");
        }
        check(count(all, "vbChain(") == 2 && count(all, "vbFinish(") == 2 && count(all, "vbEvaluate(") == 1,
              "chain, finish and their composition");
        check(count(position, "vbPass6Previous(") == 2 && count(position, "vbPass9Previous(") == 2 &&
                  count(position, "vbPass1Previous(") == 2 && count(position, "vbPass10Previous(") == 2 &&
                  count(position, "vbPass2Previous(") == 0 && count(position, "vbPass8Previous(") == 0 &&
                  count(position, "vbPass7Previous(") == 0 && count(position, "vbPass5Previous(") == 0,
              "the position twin holds the position passes alone");
        check(count(position, "vuSqrt") == 0 && position.size() * 2 < all.size(),
              "the position twin drops what the position does not need");
        check(count(all, language == ShaderLanguage::Hlsl ? "firstbitlow(todo) * 13u"
                                                          : "firstTrailingBit(todo) * 13u") == 1 &&
                  count(all, "n < 24u") == 1,
              "the loop takes the passes of the mask, thirteen rows each, at most 24");
    }
    size_t operations = 0;
    std::printf("pass stages, operations:");
    for (const uint32_t kind : ir::kBatchKinds) {
        std::printf(" %s %zu,", kindName(kind), ir::batchPass(kind)->nodes.size());
        operations += ir::batchPass(kind)->nodes.size();
    }
    std::printf(" finish %zu; %zu in all\n", ir::batchFinish().nodes.size(),
                operations + ir::batchFinish().nodes.size());
    check(ir::batchPass(0) == nullptr && ir::batchPass(11) == nullptr && ir::batchPass(PassGridFlat) != nullptr,
          "no stage for a value that is no kind");
}
} // namespace

int main() {
    testHelpers();
    testEmitters();
    const vbcases::Cases cases = vbcases::standardCases();
    vbcases::describe(cases);
    const auto start = std::chrono::steady_clock::now();
    const vbcases::Reference ref = vbcases::reference(cases);
    const double oracleNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    testCases(cases, ref);
    testPrevious(cases, ref);
    testDumps();
    {
        const vbcases::GpuVertex& g = cases.vertices[cases.single + 3];
        const vbcases::Record& r = cases.records[cases.recordOf[cases.single + 3]];
        const vp::BatchVertex v = vbcases::batchVertexOf(g);
        const vp::Output a = vp::evaluateBatchStage(cases.passes.data() + r.firstPass, r.passCount, v);
        const vp::Output b = vp::evaluateVertex(cases.passes.data() + r.firstPass, r.passCount, v);
        check(a.x == b.x && a.y == b.y && a.z == b.z && a.s == b.s && a.t == b.t && a.a == b.a,
              "vp::evaluateBatchStage");
    }
    std::printf("for information, the oracle (vp::applyPass chain, the packet, and the previous position) took %.0f ns "
                "per vertex on this machine\n",
                oracleNs / cases.vertices.size());
    std::printf(g_failures ? "vertex batch stage: %d checks FAILED\n" : "vertex batch stage: all checks passed\n",
                g_failures);
    return g_failures ? 1 : 0;
}
