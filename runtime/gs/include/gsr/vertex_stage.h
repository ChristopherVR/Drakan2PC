// One description of a vertex stage of the VU1 microprogram, for every evaluator (gsr/vertex_program.h is the
// record such a stage is fed with): a list of VU operations over the run's constants and a vertex's rows, ending in
// the fields of the GS packet. From the one list come
//   * the CPU evaluation (Evaluator below): THE reference every shader is tested against, and the permanent,
//     selectable CPU path of a renderer or adapter without an exact shader;
//   * the HLSL and the WGSL text of the stage (src/vertex_stage_emit.h), over an arithmetic library per backend.
// A new stage is a new list (a function using Builder); no shader text is written by hand.
//
// Values are 32-bit words: float bits, integers, or the 1 / 0 of a comparison. The operations are those of
// gsr/vu_exact.inl (which states the rules). Loops are ordinary C++ loops around Builder calls, so a list is
// straight-line code. Identical operations are stored once.
//
// The motion-vector twin: nothing in a list names a particular set of constants, so the same stage is evaluated a
// second time with the previous frame's constants (Evaluator::bind, or vpEvaluateAt in the shaders), and
// `fieldMask` of the emitters keeps only the operations the wanted fields need (the position alone, for instance).
#pragma once
#include "gsr/vertex_program.h"
#include "gsr/vu_limbs.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace gsr::vp {

// ---- which arithmetic library a shader uses (a run-time choice per adapter; None = evaluate on the CPU) ----
enum class Arithmetic : uint8_t {
    None,    // no exact shader here: the caller evaluates records on the CPU
    Doubles, // Direct3D 11 with double support in shaders
    Limbs,   // 32-bit integers only: every backend
};
enum class ShaderBackend : uint8_t { Direct3D11, WebGpu };
enum class ArithmeticRequest : uint8_t { Auto, Doubles, Limbs, None };

// The text of a diagnosis switch: "doubles", "limbs", "off" / "cpu" / "none"; anything else (or null) is Auto.
inline ArithmeticRequest parseArithmeticRequest(const char* text) {
    const auto is = [text](const char* word) {
        const char* t = text;
        for (; *t && *word; ++t, ++word)
            if ((*t | 0x20) != *word)
                return false;
        return *t == 0 && *word == 0;
    };
    if (!text)
        return ArithmeticRequest::Auto;
    if (is("doubles"))
        return ArithmeticRequest::Doubles;
    if (is("limbs"))
        return ArithmeticRequest::Limbs;
    if (is("off") || is("cpu") || is("none"))
        return ArithmeticRequest::None;
    return ArithmeticRequest::Auto;
}
// Pure: called per decision, never latched. A request that the adapter cannot serve answers None (the caller logs
// one line, counts it and evaluates on the CPU); it is never served by the other library behind the caller's back.
// Auto takes the integer library everywhere: it is the one library every backend shares and it needs no optional
// feature. A shader that fails to compile or to be created is the caller's None as well.
inline Arithmetic chooseArithmetic(ShaderBackend backend, bool doubleSupport, ArithmeticRequest request) {
    switch (request) {
        case ArithmeticRequest::None:
            return Arithmetic::None;
        case ArithmeticRequest::Doubles:
            return backend == ShaderBackend::Direct3D11 && doubleSupport ? Arithmetic::Doubles : Arithmetic::None;
        case ArithmeticRequest::Limbs:
        case ArithmeticRequest::Auto:
            break;
    }
    return Arithmetic::Limbs;
}
inline const char* arithmeticName(Arithmetic arithmetic) {
    return arithmetic == Arithmetic::Doubles ? "doubles" : arithmetic == Arithmetic::Limbs ? "limbs" : "none";
}

namespace ir {

enum class Op : uint8_t {
    Imm,       // imm
    Constant,  // word of the run's constants: a = row, b = lane (raw bits)
    Attribute, // word of the vertex: a = row (0 position, 1 texture coordinate, 2 normal), b = lane (raw bits)
    // VU float operations (operands as vuOperand leaves them)
    Operand,
    Neg,
    Abs,
    Add,
    Sub,
    Mul,
    Madd, // a + b * c, rounded twice
    Msub, // a - b * c, rounded twice
    Div,
    Sqrt,
    Rsqrt,
    Max,
    Min,
    Itof, // imm = fraction bits (0, 4, 12, 15)
    Ftoi, // imm = fraction bits
    // comparisons of floats: 1 or 0
    Less,
    LessEqual,
    Equal,
    Select, // a (1 or 0) ? b : c
    // integers
    And,
    Or,
    Xor,
    IAdd,
    ISub,
    Shl, // by imm (below 32)
    Shr, // by imm (below 32), unsigned
    IEqual,
    ILess, // unsigned
    // word of the carried state of a pass stage (gsr/vertex_batch_stage.h): a = row, b = lane (raw bits)
    State,
};

struct Node {
    Op op;
    uint16_t a, b, c;
    uint32_t imm;
};

// The GS packet of a vertex. X and Y keep their low 16 bits, the colours their low 8; the fog lane is the whole
// fixed-point word (bits 4 to 11 fog, bit 15 "do not draw").
enum Field : uint32_t { S, T, Q, R, G, B, A, X, Y, Z, FogLane, kFieldCount };
constexpr uint32_t kAllFields = (1u << kFieldCount) - 1u;
constexpr uint32_t kPositionFields = (1u << X) | (1u << Y) | (1u << Z);
constexpr uint16_t kNoValue = 0xFFFFu;
constexpr uint32_t kStateRows = 3, kStateLanes = kStateRows * 4; // the carried state of a pass stage: three rows

struct Value {
    uint16_t id = kNoValue;
};

struct Stage {
    const char* name = "";
    std::vector<Node> nodes;
    std::vector<uint8_t> perVertex;              // per node: depends on a vertex word (else on the constants alone)
    std::array<uint16_t, kFieldCount> outputs{}; // node per field; kNoValue = the field is 0
    uint32_t constantRows = 0, attributeRows = 0;
    // A pass stage works on a carried state (Op::State) and leaves the next one: node per lane, kNoValue = the lane
    // keeps its value. A stage that ends a chain reads the state and has `outputs`.
    std::array<uint16_t, kStateLanes> state{};
};

class Builder {
public:
    explicit Builder(const char* name) {
        stage_.name = name;
        stage_.outputs.fill(kNoValue);
        stage_.state.fill(kNoValue);
    }
    Value imm(uint32_t bits) { return node(Op::Imm, {}, {}, {}, bits); }
    Value immFloat(float value) { return imm(vu::asBits(value)); }
    Value constant(uint32_t row, uint32_t lane) {
        if (row + 1 > stage_.constantRows)
            stage_.constantRows = row + 1;
        return node(Op::Constant, {static_cast<uint16_t>(row)}, {static_cast<uint16_t>(lane)}, {}, 0);
    }
    Value attribute(uint32_t row, uint32_t lane) {
        if (row + 1 > stage_.attributeRows)
            stage_.attributeRows = row + 1;
        return node(Op::Attribute, {static_cast<uint16_t>(row)}, {static_cast<uint16_t>(lane)}, {}, 0);
    }
    Value state(uint32_t row, uint32_t lane) {
        return node(Op::State, {static_cast<uint16_t>(row)}, {static_cast<uint16_t>(lane)}, {}, 0);
    }
    void setState(uint32_t row, uint32_t lane, Value value) { stage_.state[row * 4u + lane] = value.id; }
    // the usual reads: a float as an instruction sees it
    Value constantFloat(uint32_t row, uint32_t lane) { return operand(constant(row, lane)); }
    Value attributeFloat(uint32_t row, uint32_t lane) { return operand(attribute(row, lane)); }

    Value operand(Value a) { return node(Op::Operand, a, {}, {}, 0); }
    Value neg(Value a) { return node(Op::Neg, a, {}, {}, 0); }
    Value abs(Value a) { return node(Op::Abs, a, {}, {}, 0); }
    Value add(Value a, Value b) { return node(Op::Add, a, b, {}, 0); }
    Value sub(Value a, Value b) { return node(Op::Sub, a, b, {}, 0); }
    Value mul(Value a, Value b) { return node(Op::Mul, a, b, {}, 0); }
    Value madd(Value acc, Value a, Value b) { return node(Op::Madd, acc, a, b, 0); }
    Value msub(Value acc, Value a, Value b) { return node(Op::Msub, acc, a, b, 0); }
    Value div(Value a, Value b) { return node(Op::Div, a, b, {}, 0); }
    Value sqrt(Value a) { return node(Op::Sqrt, a, {}, {}, 0); }
    Value rsqrt(Value num, Value radicand) { return node(Op::Rsqrt, num, radicand, {}, 0); }
    Value max(Value a, Value b) { return node(Op::Max, a, b, {}, 0); }
    Value min(Value a, Value b) { return node(Op::Min, a, b, {}, 0); }
    Value itof(Value a, uint32_t fractionBits) { return node(Op::Itof, a, {}, {}, fractionBits); }
    Value ftoi(Value a, uint32_t fractionBits) { return node(Op::Ftoi, a, {}, {}, fractionBits); }
    Value less(Value a, Value b) { return node(Op::Less, a, b, {}, 0); }
    Value lessEqual(Value a, Value b) { return node(Op::LessEqual, a, b, {}, 0); }
    Value equal(Value a, Value b) { return node(Op::Equal, a, b, {}, 0); }
    Value select(Value condition, Value yes, Value no) { return node(Op::Select, condition, yes, no, 0); }
    Value bitAnd(Value a, Value b) { return node(Op::And, a, b, {}, 0); }
    Value bitOr(Value a, Value b) { return node(Op::Or, a, b, {}, 0); }
    Value bitXor(Value a, Value b) { return node(Op::Xor, a, b, {}, 0); }
    Value intAdd(Value a, Value b) { return node(Op::IAdd, a, b, {}, 0); }
    Value intSub(Value a, Value b) { return node(Op::ISub, a, b, {}, 0); }
    Value shl(Value a, uint32_t count) { return node(Op::Shl, a, {}, {}, count & 31u); }
    Value shr(Value a, uint32_t count) { return node(Op::Shr, a, {}, {}, count & 31u); }
    Value intEqual(Value a, Value b) { return node(Op::IEqual, a, b, {}, 0); }
    Value intLess(Value a, Value b) { return node(Op::ILess, a, b, {}, 0); }

    void output(Field field, Value value) { stage_.outputs[field] = value.id; }
    Stage finish() { return std::move(stage_); }

private:
    Value node(Op op, Value a, Value b, Value c, uint32_t imm) {
        const auto key = std::make_tuple(static_cast<uint8_t>(op), a.id, b.id, c.id, imm);
        const auto found = known_.find(key);
        if (found != known_.end())
            return {found->second};
        const uint16_t id = static_cast<uint16_t>(stage_.nodes.size());
        stage_.nodes.push_back({op, a.id, b.id, c.id, imm});
        bool vertex = op == Op::Attribute || op == Op::State;
        if (op != Op::Imm && op != Op::Constant && op != Op::Attribute && op != Op::State)
            for (const uint16_t input : {a.id, b.id, c.id})
                vertex = vertex || (input != kNoValue && stage_.perVertex[input] != 0);
        stage_.perVertex.push_back(vertex ? 1 : 0);
        known_.emplace(key, id);
        return {id};
    }
    Stage stage_;
    std::map<std::tuple<uint8_t, uint16_t, uint16_t, uint16_t, uint32_t>, uint16_t> known_;
};

// Number of inputs of an operation that are values (the emitters and the evaluator share it).
constexpr uint32_t inputCount(Op op) {
    switch (op) {
        case Op::Imm:
        case Op::Constant:
        case Op::Attribute:
        case Op::State:
            return 0;
        case Op::Operand:
        case Op::Neg:
        case Op::Abs:
        case Op::Sqrt:
        case Op::Itof:
        case Op::Ftoi:
        case Op::Shl:
        case Op::Shr:
            return 1;
        case Op::Madd:
        case Op::Msub:
        case Op::Select:
            return 3;
        default:
            return 2;
    }
}

// ---- the arithmetic of the reference: gsr/vertex_program.h's double arithmetic (no rounding-mode switch) ----
struct DoubleArithmetic {
    static uint32_t operand(uint32_t a) { return vu::operand(a); }
    static uint32_t add(uint32_t a, uint32_t b) { return vu::add(a, b); }
    static uint32_t mul(uint32_t a, uint32_t b) { return vu::mul(a, b); }
    // A double quotient of two floats is never close enough to a float for its own rounding to cross it.
    static uint32_t div(uint32_t a, uint32_t b) {
        const uint32_t sign = (a ^ b) & 0x80000000u;
        if ((b & 0x7FFFFFFFu) == 0u)
            return sign | 0x7F7FFFFFu;
        if ((a & 0x7FFFFFFFu) == 0u)
            return sign;
        return vu::roundZero(static_cast<double>(vu::asFloat(a)) / static_cast<double>(vu::asFloat(b)));
    }
    static uint32_t sqrt(uint32_t a) {
        const uint32_t mag = a & 0x7FFFFFFFu;
        if (mag == 0u)
            return 0u;
        const double x = vu::asFloat(mag);
        uint32_t q = vu::asBits(static_cast<float>(std::sqrt(x)));
        for (int i = 0; i < 2; ++i)
            if (static_cast<double>(vu::asFloat(q)) * static_cast<double>(vu::asFloat(q)) > x)
                --q;
        for (int i = 0; i < 2; ++i)
            if (static_cast<double>(vu::asFloat(q + 1u)) * static_cast<double>(vu::asFloat(q + 1u)) <= x)
                ++q;
        return q;
    }
    static uint32_t max(uint32_t a, uint32_t b) { return vu::less(b, a) ? a : b; }
    static uint32_t min(uint32_t a, uint32_t b) { return vu::less(a, b) ? a : b; }
    static uint32_t itof(uint32_t a, uint32_t fraction) {
        return vu::mul(vu::fromInt(static_cast<int32_t>(a)), vu::asBits(1.0f / static_cast<float>(1u << fraction)));
    }
    static uint32_t ftoi(uint32_t a, uint32_t fraction) { return static_cast<uint32_t>(vu::toFixed(a, fraction)); }
    static uint32_t less(uint32_t a, uint32_t b) { return vu::less(a, b) ? 1u : 0u; }
    static uint32_t lessEqual(uint32_t a, uint32_t b) { return vu::asFloat(a) <= vu::asFloat(b) ? 1u : 0u; }
    static uint32_t equal(uint32_t a, uint32_t b) { return vu::asFloat(a) == vu::asFloat(b) ? 1u : 0u; }
};
// The same in 32-bit integers (gsr/vu_exact.inl compiled as C++): no floating point at all.
struct LimbArithmetic {
    static uint32_t operand(uint32_t a) { return limbs::vuOperand(a); }
    static uint32_t add(uint32_t a, uint32_t b) { return limbs::vuAdd(a, b); }
    static uint32_t mul(uint32_t a, uint32_t b) { return limbs::vuMul(a, b); }
    static uint32_t div(uint32_t a, uint32_t b) { return limbs::vuDiv(a, b); }
    static uint32_t sqrt(uint32_t a) { return limbs::vuSqrt(a); }
    static uint32_t max(uint32_t a, uint32_t b) { return limbs::vuMax(a, b); }
    static uint32_t min(uint32_t a, uint32_t b) { return limbs::vuMin(a, b); }
    static uint32_t itof(uint32_t a, uint32_t fraction) { return limbs::vuItof(a, fraction); }
    static uint32_t ftoi(uint32_t a, uint32_t fraction) { return limbs::vuFtoi(a, fraction); }
    static uint32_t less(uint32_t a, uint32_t b) { return limbs::vuLess(a, b); }
    static uint32_t lessEqual(uint32_t a, uint32_t b) { return limbs::vuLessEqual(a, b); }
    static uint32_t equal(uint32_t a, uint32_t b) { return limbs::vuEqual(a, b); }
};

// What a shader returns for a vertex (VpRaw): the packet fields as integers.
struct Raw {
    uint32_t place[4];  // x, y (16 bits each), z, fog lane
    uint32_t colour[4]; // r g b a (0..255)
    uint32_t stq[4];    // float bits of s, t, q; 0
};

// Evaluates a stage on the CPU. bind() does the work that depends on the constants alone, once per run; vertex()
// the rest. Not thread-safe per object (it owns the value table); make one per thread.
template <class Arith = DoubleArithmetic>
class Evaluator {
public:
    explicit Evaluator(const Stage& stage) : stage_(&stage), values_(stage.nodes.size()) {}
    // `constants`: rows of four words (at least stage.constantRows of them)
    void bind(const uint32_t* constants) {
        constants_ = constants;
        run(0, nullptr);
    }
    void bind(const Constants& constants) { bind(reinterpret_cast<const uint32_t*>(&constants)); }
    // `vertex`: rows of four words (at least stage.attributeRows of them)
    Raw vertexRaw(const uint32_t* vertex) {
        run(1, vertex);
        const auto field = [this](Field f) {
            const uint16_t id = stage_->outputs[f];
            return id == kNoValue ? 0u : values_[id];
        };
        return {{field(X) & 0xFFFFu, field(Y) & 0xFFFFu, field(Z), field(FogLane)},
                {field(R) & 0xFFu, field(G) & 0xFFu, field(B) & 0xFFu, field(A) & 0xFFu},
                {field(S), field(T), field(Q), 0u}};
    }
    Raw vertexRaw(const Vertex& vertex) { return vertexRaw(reinterpret_cast<const uint32_t*>(&vertex)); }
    Output vertex(const Vertex& vertex) { return toOutput(vertexRaw(vertex)); }
    // A pass stage on the carried state (kStateLanes words, updated in place); `vertex` as for vertexRaw.
    void apply(const uint32_t* vertex, uint32_t* state) {
        state_ = state;
        run(1, vertex);
        for (uint32_t lane = 0; lane < kStateLanes; ++lane)
            if (stage_->state[lane] != kNoValue)
                next_[lane] = values_[stage_->state[lane]];
            else
                next_[lane] = state[lane];
        for (uint32_t lane = 0; lane < kStateLanes; ++lane)
            state[lane] = next_[lane];
        state_ = nullptr;
    }
    // The packet fields of a stage that reads a carried state.
    Raw stateRaw(const uint32_t* vertex, const uint32_t* state) {
        state_ = state;
        const Raw raw = vertexRaw(vertex);
        state_ = nullptr;
        return raw;
    }
    const Stage& stage() const { return *stage_; }
    static Output toOutput(const Raw& r) {
        Output o{};
        o.s = r.stq[0];
        o.t = r.stq[1];
        o.q = r.stq[2];
        o.r = static_cast<uint8_t>(r.colour[0]);
        o.g = static_cast<uint8_t>(r.colour[1]);
        o.b = static_cast<uint8_t>(r.colour[2]);
        o.a = static_cast<uint8_t>(r.colour[3]);
        o.x = static_cast<uint16_t>(r.place[0]);
        o.y = static_cast<uint16_t>(r.place[1]);
        o.z = r.place[2];
        o.fog = static_cast<uint8_t>(r.place[3] >> 4);
        o.adc = ((r.place[3] >> 15) & 1u) != 0u;
        return o;
    }

private:
    void run(uint8_t pass, const uint32_t* vertex) {
        const Node* nodes = stage_->nodes.data();
        const uint8_t* perVertex = stage_->perVertex.data();
        uint32_t* v = values_.data();
        const size_t count = stage_->nodes.size();
        for (size_t i = 0; i < count; ++i) {
            if (perVertex[i] != pass)
                continue;
            const Node& n = nodes[i];
            uint32_t r = 0;
            switch (n.op) {
                case Op::Imm:
                    r = n.imm;
                    break;
                case Op::Constant:
                    r = constants_[n.a * 4u + n.b];
                    break;
                case Op::Attribute:
                    r = vertex[n.a * 4u + n.b];
                    break;
                case Op::Operand:
                    r = Arith::operand(v[n.a]);
                    break;
                case Op::Neg:
                    r = v[n.a] ^ 0x80000000u;
                    break;
                case Op::Abs:
                    r = v[n.a] & 0x7FFFFFFFu;
                    break;
                case Op::Add:
                    r = Arith::add(v[n.a], v[n.b]);
                    break;
                case Op::Sub:
                    r = Arith::add(v[n.a], v[n.b] ^ 0x80000000u);
                    break;
                case Op::Mul:
                    r = Arith::mul(v[n.a], v[n.b]);
                    break;
                case Op::Madd:
                    r = Arith::add(v[n.a], Arith::mul(v[n.b], v[n.c]));
                    break;
                case Op::Msub:
                    r = Arith::add(v[n.a], Arith::mul(v[n.b], v[n.c]) ^ 0x80000000u);
                    break;
                case Op::Div:
                    r = Arith::div(v[n.a], v[n.b]);
                    break;
                case Op::Sqrt:
                    r = Arith::sqrt(v[n.a]);
                    break;
                case Op::Rsqrt:
                    r = Arith::div(v[n.a], Arith::sqrt(v[n.b]));
                    break;
                case Op::Max:
                    r = Arith::max(v[n.a], v[n.b]);
                    break;
                case Op::Min:
                    r = Arith::min(v[n.a], v[n.b]);
                    break;
                case Op::Itof:
                    r = Arith::itof(v[n.a], n.imm);
                    break;
                case Op::Ftoi:
                    r = Arith::ftoi(v[n.a], n.imm);
                    break;
                case Op::Less:
                    r = Arith::less(v[n.a], v[n.b]);
                    break;
                case Op::LessEqual:
                    r = Arith::lessEqual(v[n.a], v[n.b]);
                    break;
                case Op::Equal:
                    r = Arith::equal(v[n.a], v[n.b]);
                    break;
                case Op::Select:
                    r = v[n.a] != 0u ? v[n.b] : v[n.c];
                    break;
                case Op::And:
                    r = v[n.a] & v[n.b];
                    break;
                case Op::Or:
                    r = v[n.a] | v[n.b];
                    break;
                case Op::Xor:
                    r = v[n.a] ^ v[n.b];
                    break;
                case Op::IAdd:
                    r = v[n.a] + v[n.b];
                    break;
                case Op::ISub:
                    r = v[n.a] - v[n.b];
                    break;
                case Op::Shl:
                    r = v[n.a] << n.imm;
                    break;
                case Op::Shr:
                    r = v[n.a] >> n.imm;
                    break;
                case Op::IEqual:
                    r = v[n.a] == v[n.b] ? 1u : 0u;
                    break;
                case Op::ILess:
                    r = v[n.a] < v[n.b] ? 1u : 0u;
                    break;
                case Op::State:
                    r = state_[n.a * 4u + n.b];
                    break;
            }
            v[i] = r;
        }
    }
    const Stage* stage_;
    const uint32_t* constants_ = nullptr;
    const uint32_t* state_ = nullptr;
    uint32_t next_[kStateLanes] = {};
    std::vector<uint32_t> values_;
};

// ---- the strip stage of the microprogram (constant colour, and lit by four directional terms) ----
// Constants rows as vp::Constants lays them out: 0..3 matrix, 4 viewport scale, 5 viewport offset, 6 fog (y, z =
// depth clamp, w = slope), 7 colour / ambient as integers, 8..11 light directions, 12..15 light colours as integers.
inline Stage buildStrips(bool lit) {
    Builder b(lit ? "StripsLit" : "StripsFlat");
    Value clip[4];
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = b.mul(b.constantFloat(0, l), b.attributeFloat(0, 0));
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, b.constantFloat(r, l), b.attributeFloat(0, r));
        clip[l] = acc;
    }
    const Value q = b.div(b.immFloat(1.0f), clip[3]);
    const Value depth = b.max(b.min(clip[3], b.constantFloat(6, 2)), b.constantFloat(6, 1));
    Value fixedPoint[4];
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = b.constantFloat(5, l);
        if (l == 3)
            acc = b.msub(acc, b.constantFloat(6, 3), depth);
        fixedPoint[l] = b.ftoi(b.madd(acc, b.mul(clip[l], b.constantFloat(4, l)), q), 4);
    }
    b.output(S, b.mul(b.attributeFloat(1, 0), q));
    b.output(T, b.mul(b.attributeFloat(1, 1), q));
    b.output(Q, b.mul(b.attributeFloat(1, 2), q));
    b.output(X, fixedPoint[0]);
    b.output(Y, fixedPoint[1]);
    b.output(Z, fixedPoint[2]);
    b.output(FogLane, fixedPoint[3]);
    b.output(A, b.constant(7, 3));
    static constexpr Field kColour[3] = {R, G, B};
    if (!lit) {
        for (uint32_t l = 0; l < 3; ++l)
            b.output(kColour[l], b.constant(7, l));
        return b.finish();
    }
    Value term[4];
    for (uint32_t l = 0; l < 4; ++l) {
        Value acc = b.mul(b.constantFloat(8, l), b.itof(b.attribute(2, 0), 0));
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, b.constantFloat(8 + r, l), b.itof(b.attribute(2, r), 0));
        term[l] = b.max(acc, b.imm(0u));
    }
    for (uint32_t l = 0; l < 3; ++l) {
        Value acc = b.mul(b.itof(b.constant(12, l), 0), term[0]);
        for (uint32_t r = 1; r < 4; ++r)
            acc = b.madd(acc, b.itof(b.constant(12 + r, l), 0), term[r]);
        acc = b.add(acc, b.itof(b.constant(7, l), 0));
        b.output(kColour[l], b.ftoi(b.min(acc, b.immFloat(200.0f)), 0));
    }
    return b.finish();
}
inline const Stage& stripsFlat() {
    static const Stage stage = buildStrips(false);
    return stage;
}
inline const Stage& stripsLit() {
    static const Stage stage = buildStrips(true);
    return stage;
}
// The stage of a record's program id; null for an unknown id.
inline const Stage* stageOf(uint32_t program) {
    return program == StripsFlat ? &stripsFlat() : program == StripsLit ? &stripsLit() : nullptr;
}

} // namespace ir

// The documented oracle: a record's vertex through the stage description with the reference arithmetic.
// (vp::evaluate of gsr/vertex_program.h is its hand-written twin; tests/test_vertex_stage.cpp pins the two.)
inline Output evaluateStage(uint32_t program, const Constants& k, const Vertex& v) {
    ir::Evaluator<ir::DoubleArithmetic> evaluator(*ir::stageOf(program));
    evaluator.bind(k);
    return evaluator.vertex(v);
}

} // namespace gsr::vp
