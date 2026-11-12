// The batch path of the VU1 microprogram in the one stage description (gsr/vertex_stage.h): every pass kind of
// gsr/vertex_batch.h as a pass stage (a list of operations from the carried row and the pass constants to the next
// row), the stage that ends a chain (the packet fields from the row), and the loop over a record's pass list. From
// these lists come the CPU evaluation below (BatchEvaluator) and the HLSL and WGSL text (src/vertex_stage_emit.h:
// emitBatch); no pass is written a second time for a shader.
//
// vp::evaluateVertex / vp::batchToGif of gsr/vertex_batch.h stay the oracle (they are proven against the
// microprogram); tests/test_vertex_batch_stage.cpp pins this description to them, pass kind by pass kind and chain by
// chain.
//
// How a pass reads its inputs:
//   * constants: the thirteen rows of its vp::Pass as they lie in the record. Row 0 is the header (kind, param,
//     first, count), rows 1..12 are k[0..11]; a shader reaches them through vpConstant(base + row) with `base` the
//     row of that header in the constants table;
//   * the carried row: Op::State, rows StatePos, StateCol, StateSt (raw bits, as VU memory holds them);
//   * the vertex: attribute row kBatchSelRow = (passMask, gridX, gridZ, first row of the record's pass table), which
//     is the fourth row of a vp::BatchVertex with its `slot` word replaced by the table row.
//
// Where the description departs from a word-for-word copy of gsr/vertex_batch.h (each case is in the tests):
//   * x * 1.0 and x - 0 of a value that already is an operand are left out (both return x, also for -0);
//     x * -1.0 is a sign flip; acc + integer * 1.0 is a plain sum;
//   * MAX and MINI on a raw register (the depth clamp of the transform, the height limits of the point light) are
//     not Op::Max / Op::Min: the engine compares raw bits, where a NaN is greater and smaller than nothing, so its
//     second operand comes out; Op::Max and Op::Min take operands, where a NaN is the largest finite value.
//     rawMax / rawMin below reproduce the engine: the operand form of the result, and whether the register is a NaN;
//   * the reciprocal length of the point light is zero for a zero length (the E unit), not the largest finite value
//     Op::Rsqrt gives for a zero radicand: a select around a division by the root.
#pragma once
#include "gsr/vertex_batch.h"
#include "gsr/vertex_stage.h"

#include <memory>

namespace gsr::vp {
namespace ir {

constexpr uint32_t kPassRows = 13;   // rows of one vp::Pass in the constants table
constexpr uint32_t kBatchSelRow = 3; // attribute row: passMask, gridX, gridZ, first row of the pass table
enum StateRow : uint32_t { StatePos = 0, StateCol = 1, StateSt = 2 };
constexpr uint32_t kPositionState = 0xFu; // the state lanes the position fields of the packet depend on

// Every pass kind, in the order the generated dispatch tests them.
inline constexpr uint32_t kBatchKinds[] = {PassGrid,  PassGridFlat,  PassPointLight, PassTexGenInt, PassTexGenPos,
                                           PassLight, PassTransform, PassProject,    PassFade,      PassTexMatrix};
constexpr uint32_t kBatchKindLimit = PassGridFlat + 1; // kinds are below this

namespace batchdetail {
// A float register as MAX and MINI of the engine see it: `value` is its operand form, `nan` is 1 when the raw bits
// are a NaN (no node: never, the value came out of an operation).
struct RawFloat {
    Value value;
    Value nan;
};
inline RawFloat rawRegister(Builder& b, Value bits) {
    return {b.operand(bits), b.intLess(b.imm(0x7F800000u), b.bitAnd(bits, b.imm(0x7FFFFFFFu)))};
}
// first > second ? first : second on raw registers (`greater` is that comparison on the operand forms). A NaN on
// either side compares false, so the second register comes out. An infinity beside the largest finite value picks
// the other register than the engine does; both have the same operand form, which is all that is read afterwards.
inline RawFloat rawPick(Builder& b, Value greater, RawFloat first, RawFloat second) {
    Value nan = first.nan;
    if (nan.id == kNoValue)
        nan = second.nan;
    else if (second.nan.id != kNoValue)
        nan = b.bitOr(first.nan, second.nan);
    if (nan.id != kNoValue)
        greater = b.bitAnd(greater, b.bitXor(nan, b.imm(1u)));
    return {b.select(greater, first.value, second.value), second.nan};
}
inline RawFloat rawMax(Builder& b, RawFloat first, RawFloat second) {
    return rawPick(b, b.less(second.value, first.value), first, second);
}
inline RawFloat rawMin(Builder& b, RawFloat first, RawFloat second) {
    return rawPick(b, b.less(first.value, second.value), first, second);
}

// k[row][lane] of the pass: raw, as a float operand, as an integer converted to float.
inline Value k(Builder& b, uint32_t row, uint32_t lane) {
    return b.constant(1u + row, lane);
}
inline Value kFloat(Builder& b, uint32_t row, uint32_t lane) {
    return b.operand(k(b, row, lane));
}
inline Value kInt(Builder& b, uint32_t row, uint32_t lane) {
    return b.itof(k(b, row, lane), 0);
}
inline Value param(Builder& b) {
    return b.constant(0, 1);
}
inline Value stateFloat(Builder& b, uint32_t row, uint32_t lane) {
    return b.operand(b.state(row, lane));
}
inline Value stateInt(Builder& b, uint32_t row, uint32_t lane) {
    return b.itof(b.state(row, lane), 0);
}

inline void grid(Builder& b, bool flat) {
    const Value height = flat ? b.imm(0u) : stateInt(b, StatePos, 1);
    const Value y = b.add(kFloat(b, 0, 1), b.mul(height, b.immFloat(2.0f)));
    b.setState(StatePos, 0, b.attribute(kBatchSelRow, 1));
    b.setState(StatePos, 1, b.sub(y, b.immFloat(65536.0f)));
    b.setState(StatePos, 2, b.attribute(kBatchSelRow, 2));
    b.setState(StatePos, 3, kFloat(b, 0, 3));
}

inline void pointLight(Builder& b) {
    Value d[3], n[3], along[3];
    for (uint32_t l = 0; l < 3; ++l)
        d[l] = b.sub(kFloat(b, 0, l), stateFloat(b, StatePos, l));
    // the E unit: the root, toward zero, of the rounded sum of squares, then one over it; zero for a zero length
    const Value length = b.sqrt(b.add(b.add(b.mul(d[0], d[0]), b.mul(d[1], d[1])), b.mul(d[2], d[2])));
    const Value reciprocalLength =
        b.select(b.intEqual(b.abs(length), b.imm(0u)), length, b.div(b.immFloat(1.0f), length));
    n[0] = b.neg(d[0]);
    n[1] = d[1];
    n[2] = b.neg(d[2]);
    RawFloat height = rawMax(b, {d[1], {}}, rawRegister(b, k(b, 3, 1)));
    height = rawMin(b, height, rawRegister(b, k(b, 3, 2)));
    for (uint32_t l = 0; l < 3; ++l)
        along[l] = b.mul(n[l], kFloat(b, 1, l));
    Value cosine = b.add(b.add(along[0], along[1]), along[2]);
    Value alpha = b.msub(b.immFloat(128.0f), kFloat(b, 4, 3), kFloat(b, 3, 1));
    alpha = b.madd(alpha, kFloat(b, 4, 3), height.value);
    cosine = b.max(cosine, b.imm(0u));
    const Value steep = b.mul(b.abs(n[1]), reciprocalLength);
    cosine = b.mul(cosine, reciprocalLength);
    alpha = b.msub(alpha, kFloat(b, 3, 3), steep);
    // the squarings: a count from the pass, at most kBatchMaxExponent
    for (uint32_t i = 0; i < kBatchMaxExponent; ++i)
        cosine = b.select(b.intLess(b.imm(i), param(b)), b.mul(cosine, cosine), cosine);
    cosine = b.select(b.intEqual(param(b), b.imm(0u)), b.imm(0u), cosine);
    alpha = b.min(alpha, b.immFloat(128.0f));
    for (uint32_t l = 0; l < 3; ++l) {
        const Value light = b.mul(kInt(b, 2, l), b.immFloat(2.0f));
        const Value c = b.min(b.madd(stateInt(b, StateCol, l), light, cosine), b.immFloat(255.0f));
        b.setState(StateCol, l, b.ftoi(c, 0));
    }
    b.setState(StateCol, 3, b.ftoi(alpha, 0));
}

inline void texGen(Builder& b, bool fromIntegers) {
    Value v[4];
    for (uint32_t l = 0; l < 4; ++l)
        v[l] = fromIntegers ? stateInt(b, StateCol, l) : stateFloat(b, StatePos, l);
    for (uint32_t l = 0; l < 2; ++l) {
        Value acc = b.mul(kFloat(b, 0, l), v[0]);
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, kFloat(b, r, l), v[r]);
        b.setState(StateSt, l, acc);
    }
    b.setState(StateSt, 2, b.immFloat(1.0f));
    b.setState(StateSt, 3, b.imm(0u));
}

inline void light(Builder& b) {
    Value n[4], term[4];
    for (uint32_t l = 0; l < 4; ++l)
        n[l] = stateInt(b, StateCol, l);
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = b.mul(kFloat(b, 1, l), n[0]);
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, kFloat(b, 1 + r, l), n[r]);
        term[l] = b.max(acc, b.imm(0u));
    }
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = b.imm(0u);
        if (l < 3) {
            acc = b.mul(kInt(b, 5, l), term[0]);
            for (uint32_t r = 1; r < 4; ++r)
                acc = b.madd(acc, kInt(b, 5 + r, l), term[r]);
        }
        acc = b.add(acc, kInt(b, 0, l));
        b.setState(StateCol, l, b.ftoi(b.min(acc, b.immFloat(200.0f)), 0));
    }
}

// Clip coordinates to the fixed point position and 1 / w (Transform and Project).
inline void project(Builder& b, const Value clip[4]) {
    const Value q = b.div(b.immFloat(1.0f), clip[3]);
    RawFloat depth = rawMin(b, {clip[3], {}}, rawRegister(b, k(b, 4, 2)));
    depth = rawMax(b, depth, rawRegister(b, k(b, 4, 1)));
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = kFloat(b, 5, l);
        if (l == 3)
            acc = b.msub(acc, kFloat(b, 4, 3), depth.value);
        b.setState(StatePos, l, b.ftoi(b.madd(acc, b.mul(clip[l], kFloat(b, 6, l)), q), 4));
    }
    b.setState(StateSt, 2, b.add(b.imm(0u), q)); // a sum with +0: a negative zero becomes +0
}

inline void transform(Builder& b, bool matrix) {
    Value clip[4];
    for (uint32_t l = 0; l < 4; ++l) {
        if (!matrix) {
            clip[l] = stateFloat(b, StatePos, l);
            continue;
        }
        Value acc = b.mul(kFloat(b, 0, l), stateFloat(b, StatePos, 0));
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, kFloat(b, r, l), stateFloat(b, StatePos, r));
        clip[l] = acc;
    }
    project(b, clip);
}

inline void fade(Builder& b) {
    const Value w = b.div(b.immFloat(1.0f), stateFloat(b, StateSt, 2));
    const Value f = b.max(b.msub(b.immFloat(1.0f), kFloat(b, 0, 3), w), b.imm(0u));
    b.setState(StateCol, 3, b.ftoi(b.mul(f, stateInt(b, StateCol, 3)), 0));
}

inline void texMatrix(Builder& b) {
    const Value s = stateFloat(b, StateSt, 0), t = stateFloat(b, StateSt, 1);
    for (uint32_t l = 0; l < 2; ++l) {
        const Value acc = b.madd(b.mul(kFloat(b, 0, l), s), kFloat(b, 1, l), t);
        b.setState(StateSt, l, b.add(acc, kFloat(b, 2, l)));
    }
}
} // namespace batchdetail

// The pass stage of a kind; an empty list (no state output) for a value that is no kind.
inline Stage buildBatchPass(uint32_t kind) {
    using namespace batchdetail;
    static const char* const kNames[kBatchKindLimit] = {"",          "Grid",    "PointLight", "TexGenInt",
                                                        "TexGenPos", "Light",   "Transform",  "Fade",
                                                        "TexMatrix", "Project", "GridFlat"};
    Builder b(kind < kBatchKindLimit ? kNames[kind] : "");
    switch (kind) {
        case PassGrid:
        case PassGridFlat:
            grid(b, kind == PassGridFlat);
            break;
        case PassPointLight:
            pointLight(b);
            break;
        case PassTexGenInt:
        case PassTexGenPos:
            texGen(b, kind == PassTexGenInt);
            break;
        case PassLight:
            light(b);
            break;
        case PassTransform:
        case PassProject:
            transform(b, kind == PassTransform);
            break;
        case PassFade:
            fade(b);
            break;
        case PassTexMatrix:
            texMatrix(b);
            break;
        default:
            break;
    }
    return b.finish();
}
// Null for a value that is no kind (such a pass changes nothing, as in vp::applyPass).
inline const Stage* batchPass(uint32_t kind) {
    static const std::array<Stage, kBatchKindLimit> stages = [] {
        std::array<Stage, kBatchKindLimit> all;
        for (uint32_t i = 0; i < kBatchKindLimit; ++i)
            all[i] = buildBatchPass(i);
        return all;
    }();
    return kind >= PassGrid && kind < kBatchKindLimit ? &stages[kind] : nullptr;
}

// What the index-list run copies into the packet from a processed row (vp::finishRow).
inline Stage buildBatchFinish() {
    using namespace batchdetail;
    Builder b("BatchFinish");
    const Value q = stateFloat(b, StateSt, 2);
    b.output(S, b.mul(stateFloat(b, StateSt, 0), q));
    b.output(T, b.mul(stateFloat(b, StateSt, 1), q));
    b.output(Q, b.state(StateSt, 2));
    static constexpr Field kColour[4] = {R, G, B, A};
    for (uint32_t l = 0; l < 4; ++l)
        b.output(kColour[l], b.state(StateCol, l));
    b.output(X, b.state(StatePos, 0));
    b.output(Y, b.state(StatePos, 1));
    b.output(Z, b.state(StatePos, 2));
    b.output(FogLane, b.state(StatePos, 3));
    return b.finish();
}
inline const Stage& batchFinish() {
    static const Stage stage = buildBatchFinish();
    return stage;
}

// Bit `lane` set: the pass stage writes that state lane.
inline uint32_t stateWritten(const Stage& stage) {
    uint32_t mask = 0;
    for (uint32_t lane = 0; lane < kStateLanes; ++lane)
        if (stage.state[lane] != kNoValue)
            mask |= 1u << lane;
    return mask;
}

// The loop over a record's pass list, as the generated shaders run it (emitBatch writes the same loop as text):
// the set bits of the pass mask from the lowest up, at most kBatchMaxPasses of them. `apply(pass index)`.
template <class Apply>
void forEachPass(uint32_t passMask, Apply&& apply) {
    uint32_t todo = passMask;
    for (uint32_t n = 0; n < kBatchMaxPasses && todo != 0u; ++n) {
        uint32_t lowest = 0;
        while (((todo >> lowest) & 1u) == 0u)
            ++lowest;
        todo &= todo - 1u;
        apply(lowest);
    }
}

// The batch description on the CPU. bind() takes the passes of one record (the part of every pass that depends on
// its constants alone runs there, once); vertices follow. The passes must stay alive while vertices are evaluated.
// Not thread-safe per object.
template <class Arith = DoubleArithmetic>
class BatchEvaluator {
public:
    BatchEvaluator() : finish_(batchFinish()) {}
    void bind(const Pass* passes, uint32_t passCount) {
        count_ = passCount < kBatchMaxPasses ? passCount : kBatchMaxPasses;
        for (uint32_t i = 0; i < count_; ++i) {
            const Stage* stage = batchPass(passes[i].kind);
            if (!stage)
                evaluators_[i].reset();
            else if (!evaluators_[i] || &evaluators_[i]->stage() != stage)
                evaluators_[i] = std::make_unique<Evaluator<Arith>>(*stage);
            if (stage)
                evaluators_[i]->bind(reinterpret_cast<const uint32_t*>(&passes[i]));
        }
    }
    // The row after the passes of the vertex.
    Row row(const BatchVertex& vertex) {
        Row r = vertex.row;
        const uint32_t* words = reinterpret_cast<const uint32_t*>(&vertex);
        forEachPass(vertex.passMask, [&](uint32_t pass) {
            if (pass < count_ && evaluators_[pass])
                evaluators_[pass]->apply(words, reinterpret_cast<uint32_t*>(&r));
        });
        return r;
    }
    Raw finishRaw(const Row& r) { return finish_.stateRaw(nullptr, reinterpret_cast<const uint32_t*>(&r)); }
    Raw vertexRaw(const BatchVertex& vertex) { return finishRaw(row(vertex)); }
    Output vertex(const BatchVertex& vertex) { return Evaluator<Arith>::toOutput(vertexRaw(vertex)); }

private:
    std::array<std::unique_ptr<Evaluator<Arith>>, kBatchMaxPasses> evaluators_;
    Evaluator<Arith> finish_;
    uint32_t count_ = 0;
};

} // namespace ir

// A record's vertex through the batch description with the reference arithmetic (vp::evaluateVertex is the oracle
// it is pinned to).
inline Output evaluateBatchStage(const Pass* passes, uint32_t passCount, const BatchVertex& v) {
    ir::BatchEvaluator<ir::DoubleArithmetic> evaluator;
    evaluator.bind(passes, passCount);
    return evaluator.vertex(v);
}

} // namespace gsr::vp
