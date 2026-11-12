// Shader text of vertex stages (gsr/vertex_stage.h), generated at start-up from the one description: HLSL over the
// double library, HLSL over the integer library, WGSL over the integer library. The integer library itself is
// generated from gsr/vu_exact.inl, so no arithmetic is written twice; only the four double functions below exist
// in one language alone (Direct3D 11 adapters with double support), and the same suite tests them
// (tests/test_vertex_program.cpp, tests/test_vertex_stage_webgpu.cpp).
//
// The text expects from its surroundings (src/vertex_program_hlsl.h, src/vertex_program_wgsl.h):
//   VpIn  { pos, st : 4 x uint; nrm : 4 x int; sel : 4 x uint (x = constants index, y = program) }
//   VpRaw { place, colour, stq : 4 x uint }
//   vpConstant(row) -> 4 x uint          a row of the constants table
// and defines, per call of emitStages with a `suffix`:
//   VpRaw vpEvaluateAt<suffix>(VpIn i, uint constantsIndex)   the stage of i.sel.y with any set of constants (the
//                                                             previous frame's, for motion vectors)
//   VpRaw vpEvaluate<suffix>(VpIn i)                          the same with i.sel.x
// Portable: standard C++ only.
#pragma once
#include "gsr/vertex_batch_stage.h"
#include "gsr/vertex_stage.h"

#include <cstdio>
#include <string>
#include <vector>

namespace gsr::vp {

enum class ShaderLanguage : uint8_t { Hlsl, Wgsl };

struct StageEntry {
    uint32_t program; // the value of VpIn.sel.y that selects the stage
    const ir::Stage* stage;
};

constexpr uint32_t kConstantRowsPerRun = 16; // rows of vp::Constants

// clang-format off
// Add, multiply, divide and square root with doubles (vp::vu::roundZero, add, mul and ir::DoubleArithmetic in HLSL).
// A double holds the product of two floats exactly and their sum up to a residual whose sign two more sums give.
// Quotient and root start from any value within a few float steps and are settled by exact products, so neither
// the precision of the adapter's division nor of its square root matters.
inline const char* kHlslDoubleCore = R"HLSL(
uint vuRound(double value, double residual)
{
    float nearest = (float)value;
    uint bits = asuint(nearest);
    if (value == 0.0) return bits & 0x80000000u;
    precise double back = (double)nearest;
    if ((value > 0.0 && (back > value || (back == value && residual < 0.0))) ||
        (value < 0.0 && (back < value || (back == value && residual > 0.0))))
        bits -= 1u;
    if ((bits & 0x7F800000u) == 0u) bits &= 0x80000000u;
    return bits;
}
uint vuAdd(uint a, uint b)
{
    precise double x = (double)asfloat(a);
    precise double y = (double)asfloat(b);
    precise double sum = x + y;
    precise double back = sum - x;
    precise double residual = (x - (sum - back)) + (y - back);
    // Two zeros in integers (-0 only for -0 + -0): FXC folds the sum of a literal zero and y to y, also under
    // `precise` and IEEE strictness, which would keep the sign of a -0.
    return ((a | b) & 0x7FFFFFFFu) == 0u ? (a & b) : vuRound(sum, residual);
}
uint vuMul(uint a, uint b)
{
    precise double p = (double)asfloat(a) * (double)asfloat(b);
    return vuRound(p, 0.0);
}
uint vuDiv(uint a, uint b)
{
    uint sign = (a ^ b) & 0x80000000u;
    uint ma = a & 0x7FFFFFFFu;
    uint mb = b & 0x7FFFFFFFu;
    precise double x = (double)asfloat(ma);
    precise double y = (double)asfloat(max(mb, 0x00800000u));
    precise double d = x / y;
    uint q = clamp(asuint((float)d), 0x00800000u, 0x7F7FFFFFu);
    if ((double)asfloat(q) * y > x) q -= 1u;
    if ((double)asfloat(q) * y > x) q -= 1u;
    if (q < 0x7F7FFFFFu && (double)asfloat(q + 1u) * y <= x) q += 1u;
    if (q < 0x7F7FFFFFu && (double)asfloat(q + 1u) * y <= x) q += 1u;
    if ((double)asfloat(0x00800000u) * y > x) q = 0u;
    if (ma == 0u) q = 0u;
    if (mb == 0u) q = 0x7F7FFFFFu;
    return sign | q;
}
uint vuSqrt(uint a)
{
    uint mag = a & 0x7FFFFFFFu;
    precise double x = (double)asfloat(mag);
    uint q = clamp(asuint(sqrt(asfloat(mag))), 0x00800000u, 0x7F7FFFFEu);
    if ((double)asfloat(q) * (double)asfloat(q) > x) q -= 1u;
    if ((double)asfloat(q) * (double)asfloat(q) > x) q -= 1u;
    if ((double)asfloat(q + 1u) * (double)asfloat(q + 1u) <= x) q += 1u;
    if ((double)asfloat(q + 1u) * (double)asfloat(q + 1u) <= x) q += 1u;
    return mag == 0u ? 0u : q;
}
)HLSL";
// clang-format on

namespace emit {
// Replaces the whole word `from` in `text`.
inline std::string replaceWord(const std::string& text, const std::string& from, const std::string& to) {
    const auto wordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    std::string out;
    size_t at = 0;
    for (;;) {
        const size_t hit = text.find(from, at);
        if (hit == std::string::npos)
            break;
        const size_t end = hit + from.size();
        const bool whole = (hit == 0 || !wordChar(text[hit - 1])) && (end >= text.size() || !wordChar(text[end]));
        out.append(text, at, hit - at);
        out += whole ? to : from;
        at = end;
    }
    out.append(text, at, std::string::npos);
    return out;
}
inline std::string hex(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08Xu", value);
    return buffer;
}
inline std::string number(uint32_t value) {
    return std::to_string(value) + "u";
}
} // namespace emit

// The arithmetic library: every function of gsr/vu_exact.inl (its Primitive group as the language's intrinsics),
// or with Arithmetic::Doubles (HLSL only) its Limb group replaced by kHlslDoubleCore. Empty when the combination does
// not exist.
inline std::string emitLibrary(ShaderLanguage language, Arithmetic arithmetic) {
    const bool hlsl = language == ShaderLanguage::Hlsl;
    if (arithmetic == Arithmetic::None || (arithmetic == Arithmetic::Doubles && !hlsl))
        return {};
    // The primitives: a comparison as 1 or 0, a select on such a value, the leading zeros of a non-zero value.
    std::string out = hlsl ? "uint ubit(bool c) { return c ? 1u : 0u; }\n"
                             "uint vuSelect(uint c, uint a, uint b) { return c != 0u ? a : b; }\n"
                             "uint vuClz(uint value) { return 31u - firstbithigh(value); }\n"
                           : "fn ubit(c: bool) -> u32 { return u32(c); }\n"
                             "fn vuSelect(c: u32, a: u32, b: u32) -> u32 { return select(b, a, c != 0u); }\n"
                             "fn vuClz(value: u32) -> u32 { return countLeadingZeros(value); }\n";
    bool coreDone = false;
    for (const limbs::LibraryFunction& f : limbs::kLibrary) {
        if (f.group == limbs::Group::Primitive)
            continue;
        if (f.group == limbs::Group::Limb && arithmetic == Arithmetic::Doubles) {
            if (!coreDone)
                out += kHlslDoubleCore;
            coreDone = true;
            continue;
        }
        out += hlsl ? "uint " : "fn ";
        out += f.name;
        out += "(";
        for (uint32_t p = 0; p < f.parameterCount; ++p) {
            if (p)
                out += ", ";
            out += hlsl ? std::string("uint ") + f.parameters[p] : std::string(f.parameters[p]) + ": u32";
        }
        out += hlsl ? ") " : ") -> u32 ";
        out += emit::replaceWord(f.body, "uvar", hlsl ? "uint" : "var");
        out += "\n";
    }
    return out;
}

namespace emit {
// The operations of `stage` that the wanted packet fields (bits of ir::Field) and the wanted state lanes (bits of
// the twelve lanes of a pass stage) reach, and the constant rows those read.
struct Live {
    std::vector<uint8_t> node, row;
};
inline Live liveOf(const ir::Stage& stage, uint32_t fieldMask, uint32_t stateMask) {
    using namespace ir;
    Live live{std::vector<uint8_t>(stage.nodes.size(), 0), std::vector<uint8_t>(stage.constantRows, 0)};
    for (uint32_t f = 0; f < kFieldCount; ++f)
        if (((fieldMask >> f) & 1u) && stage.outputs[f] != kNoValue)
            live.node[stage.outputs[f]] = 1;
    for (uint32_t lane = 0; lane < kStateLanes; ++lane)
        if (((stateMask >> lane) & 1u) && stage.state[lane] != kNoValue)
            live.node[stage.state[lane]] = 1;
    for (size_t i = stage.nodes.size(); i-- > 0;) {
        if (!live.node[i])
            continue;
        const Node& n = stage.nodes[i];
        if (n.op == Op::Constant)
            live.row[n.a] = 1;
        const uint16_t inputs[3] = {n.a, n.b, n.c};
        for (uint32_t k = 0; k < inputCount(n.op); ++k)
            live.node[inputs[k]] = 1;
    }
    return live;
}
// The state lanes (bits) the live operations of a stage read.
inline uint32_t stateRead(const ir::Stage& stage, const Live& live) {
    uint32_t mask = 0;
    for (size_t i = 0; i < stage.nodes.size(); ++i)
        if (live.node[i] && stage.nodes[i].op == ir::Op::State)
            mask |= 1u << (stage.nodes[i].a * 4u + stage.nodes[i].b);
    return mask;
}
inline std::string temporary(uint16_t id) {
    return "t" + std::to_string(id);
}
inline constexpr char kLane[4] = {'x', 'y', 'z', 'w'};
inline constexpr const char* kStateRow[ir::kStateRows] = {"r.pos", "r.col", "r.st"};
// The body of a stage function: its constant rows as k<row>, then one line per live operation. The function has
// `base` (the first row of its constants) and, as far as the stage reads them, `i` (the vertex: pos, st, nrm),
// `r` (the carried row of a pass stage: pos, col, st) and `sel` (attribute row 3 of a pass stage).
inline std::string body(bool hlsl, const ir::Stage& stage, const Live& live) {
    using namespace ir;
    std::string out;
    const std::string declare = hlsl ? "    uint " : "    let ";
    for (uint32_t row = 0; row < stage.constantRows; ++row)
        if (live.row[row])
            out += (hlsl ? "    uint4 k" : "    let k") + std::to_string(row) + " = vpConstant(base + " +
                   emit::number(row) + ");\n";
    for (size_t i = 0; i < stage.nodes.size(); ++i) {
        if (!live.node[i])
            continue;
        const Node& n = stage.nodes[i];
        const std::string a = temporary(n.a), b = temporary(n.b), c = temporary(n.c);
        const auto call1 = [&](const char* f) { return std::string(f) + "(" + a + ")"; };
        const auto call2 = [&](const char* f) { return std::string(f) + "(" + a + ", " + b + ")"; };
        const auto call3 = [&](const char* f) { return std::string(f) + "(" + a + ", " + b + ", " + c + ")"; };
        const auto infix = [&](const char* op) { return "(" + a + " " + op + " " + b + ")"; };
        std::string x;
        switch (n.op) {
            case Op::Imm:
                x = emit::hex(n.imm);
                break;
            case Op::Constant:
                x = "k" + std::to_string(n.a) + "." + kLane[n.b & 3u];
                break;
            case Op::Attribute: {
                const std::string lane(1, kLane[n.b & 3u]);
                if (n.a == 0)
                    x = "i.pos." + lane;
                else if (n.a == 1)
                    x = "i.st." + lane;
                else if (n.a == 2)
                    x = (hlsl ? "(uint)i.nrm." : "bitcast<u32>(i.nrm.") + lane + (hlsl ? "" : ")");
                else if (n.a == 3)
                    x = "sel." + lane;
                else
                    x = "i.a" + std::to_string(n.a) + "." + lane; // further rows: 4 x uint members a4, a5, ...
                break;
            }
            case Op::State:
                x = std::string(kStateRow[n.a % kStateRows]) + "." + kLane[n.b & 3u];
                break;
            case Op::Operand:
                x = call1("vuOperand");
                break;
            case Op::Neg:
                x = call1("vuNeg");
                break;
            case Op::Abs:
                x = call1("vuAbs");
                break;
            case Op::Add:
                x = call2("vuAdd");
                break;
            case Op::Sub:
                x = call2("vuSub");
                break;
            case Op::Mul:
                x = call2("vuMul");
                break;
            case Op::Madd:
                x = call3("vuMadd");
                break;
            case Op::Msub:
                x = call3("vuMsub");
                break;
            case Op::Div:
                x = call2("vuDiv");
                break;
            case Op::Sqrt:
                x = call1("vuSqrt");
                break;
            case Op::Rsqrt:
                x = call2("vuRsqrt");
                break;
            case Op::Max:
                x = call2("vuMax");
                break;
            case Op::Min:
                x = call2("vuMin");
                break;
            case Op::Itof:
                x = "vuItof(" + a + ", " + emit::number(n.imm) + ")";
                break;
            case Op::Ftoi:
                x = "vuFtoi(" + a + ", " + emit::number(n.imm) + ")";
                break;
            case Op::Less:
                x = call2("vuLess");
                break;
            case Op::LessEqual:
                x = call2("vuLessEqual");
                break;
            case Op::Equal:
                x = call2("vuEqual");
                break;
            case Op::Select:
                x = "vuSelect(ubit(" + a + " != 0u), " + b + ", " + c + ")";
                break;
            case Op::And:
                x = infix("&");
                break;
            case Op::Or:
                x = infix("|");
                break;
            case Op::Xor:
                x = infix("^");
                break;
            case Op::IAdd:
                x = infix("+");
                break;
            case Op::ISub:
                x = infix("-");
                break;
            case Op::Shl:
                x = "(" + a + " << " + emit::number(n.imm) + ")";
                break;
            case Op::Shr:
                x = "(" + a + " >> " + emit::number(n.imm) + ")";
                break;
            case Op::IEqual:
                x = "ubit(" + a + " == " + b + ")";
                break;
            case Op::ILess:
                x = "ubit(" + a + " < " + b + ")";
                break;
        }
        out += declare + temporary(static_cast<uint16_t>(i)) + " = " + x + ";\n";
    }
    return out;
}
// The end of a function that returns the packet fields.
inline std::string returnFields(bool hlsl, const ir::Stage& stage, uint32_t fieldMask) {
    using namespace ir;
    const auto field = [&](Field f, const char* mask) -> std::string {
        if (!((fieldMask >> f) & 1u) || stage.outputs[f] == kNoValue)
            return "0u";
        return mask ? "(" + temporary(stage.outputs[f]) + " & " + mask + ")" : temporary(stage.outputs[f]);
    };
    const std::string place =
        field(X, "0xFFFFu") + ", " + field(Y, "0xFFFFu") + ", " + field(Z, nullptr) + ", " + field(FogLane, nullptr);
    const std::string colour =
        field(R, "0xFFu") + ", " + field(G, "0xFFu") + ", " + field(B, "0xFFu") + ", " + field(A, "0xFFu");
    const std::string stq = field(S, nullptr) + ", " + field(T, nullptr) + ", " + field(Q, nullptr) + ", 0u";
    if (hlsl)
        return "    VpRaw o;\n    o.place = uint4(" + place + ");\n    o.colour = uint4(" + colour +
               ");\n    o.stq = uint4(" + stq + ");\n    return o;\n}\n";
    return "    return VpRaw(vec4u(" + place + "), vec4u(" + colour + "), vec4u(" + stq + "));\n}\n";
}
} // namespace emit

// The stages as functions plus the two dispatchers. `fieldMask` (bits of ir::Field) keeps only the operations the
// wanted packet fields need; the other fields are returned as 0.
inline std::string emitStages(ShaderLanguage language, const StageEntry* entries, size_t entryCount,
                              uint32_t fieldMask = ir::kAllFields, const std::string& suffix = {}) {
    const bool hlsl = language == ShaderLanguage::Hlsl;
    std::string out;
    for (size_t e = 0; e < entryCount; ++e) {
        const ir::Stage& stage = *entries[e].stage;
        const std::string name = "vpStage" + std::to_string(entries[e].program) + suffix;
        out += hlsl ? "VpRaw " + name + "(VpIn i, uint base)\n{\n" : "fn " + name + "(i: VpIn, base: u32) -> VpRaw {\n";
        out += emit::body(hlsl, stage, emit::liveOf(stage, fieldMask, 0u));
        out += emit::returnFields(hlsl, stage, fieldMask);
    }
    const std::string at = "vpEvaluateAt" + suffix;
    out += hlsl ? "VpRaw " + at + "(VpIn i, uint constantsIndex)\n{\n    uint base = constantsIndex * "
                : "fn " + at + "(i: VpIn, constantsIndex: u32) -> VpRaw {\n    let base = constantsIndex * ";
    out += emit::number(kConstantRowsPerRun) + ";\n";
    for (size_t e = 0; e < entryCount; ++e) {
        const std::string call = "return vpStage" + std::to_string(entries[e].program) + suffix + "(i, base);";
        if (e + 1 < entryCount)
            out += "    if (i.sel.y == " + emit::number(entries[e].program) + ") { " + call + " }\n";
        else
            out += "    " + call + "\n";
    }
    out += "}\n";
    out += hlsl ? "VpRaw vpEvaluate" + suffix + "(VpIn i) { return " + at + "(i, i.sel.x); }\n"
                : "fn vpEvaluate" + suffix + "(i: VpIn) -> VpRaw { return " + at + "(i, i.sel.x); }\n";
    return out;
}

// What emitBatch keeps for a set of wanted packet fields: the state lanes that matter and the pass kinds that write
// one of them. Found from the description alone: start from the lanes the finishing stage reads for those fields,
// then add what the kept passes read, until nothing is added.
struct BatchPlan {
    uint32_t stateMask = 0;
    std::vector<uint32_t> kinds;
};
inline BatchPlan planBatch(uint32_t fieldMask) {
    using namespace ir;
    BatchPlan plan;
    const Stage& finish = batchFinish();
    plan.stateMask = emit::stateRead(finish, emit::liveOf(finish, fieldMask, 0u));
    for (bool grown = true; grown;) {
        grown = false;
        plan.kinds.clear();
        for (const uint32_t kind : kBatchKinds) {
            const Stage& stage = *batchPass(kind);
            if ((stateWritten(stage) & plan.stateMask) == 0u)
                continue;
            plan.kinds.push_back(kind);
            const uint32_t reads = emit::stateRead(stage, emit::liveOf(stage, 0u, plan.stateMask));
            grown = grown || (reads & ~plan.stateMask) != 0u;
            plan.stateMask |= reads;
        }
    }
    return plan;
}

// The batch path (gsr/vertex_batch_stage.h): one function per pass kind, the loop over a vertex's pass list, the
// function that ends the chain. Expects, besides VpRaw and vpConstant:
//   VbRow { pos, col, st : 4 x uint }      the carried row
// and defines, per call with a `suffix`:
//   VbRow vbPass<kind><suffix>(VbRow r, uint4 sel, uint base)   one pass; base = the row of its header
//   VbRow vbChain<suffix>(VbRow row, uint4 sel)                 the passes sel.x names, from the table at row sel.w
//   VpRaw vbFinish<suffix>(VbRow r)                             the packet fields of a row
//   VpRaw vbEvaluate<suffix>(VbRow row, uint4 sel)              both
// sel = (passMask, gridX, gridZ, first row of the record's pass table in the constants table).
// `fieldMask` keeps the passes and operations the wanted packet fields need (planBatch): with ir::kPositionFields
// the chain is the position part alone (the motion-vector twin, fed the previous frame's row, sel and pass table).
inline std::string emitBatch(ShaderLanguage language, uint32_t fieldMask = ir::kAllFields,
                             const std::string& suffix = {}) {
    using namespace ir;
    const bool hlsl = language == ShaderLanguage::Hlsl;
    const BatchPlan plan = planBatch(fieldMask);
    std::string out;
    for (const uint32_t kind : plan.kinds) {
        const Stage& stage = *batchPass(kind);
        const std::string name = "vbPass" + std::to_string(kind) + suffix;
        out += hlsl ? "VbRow " + name + "(VbRow r, uint4 sel, uint base)\n{\n"
                    : "fn " + name + "(r: VbRow, sel: vec4u, base: u32) -> VbRow {\n";
        out += emit::body(hlsl, stage, emit::liveOf(stage, 0u, plan.stateMask));
        std::string rows[kStateRows];
        for (uint32_t row = 0; row < kStateRows; ++row) {
            bool changed = false;
            std::string lanes;
            for (uint32_t l = 0; l < 4; ++l) {
                const uint32_t lane = row * 4u + l;
                const bool written = ((plan.stateMask >> lane) & 1u) && stage.state[lane] != kNoValue;
                changed = changed || written;
                lanes += l ? ", " : "";
                lanes += written ? emit::temporary(stage.state[lane])
                                 : std::string(emit::kStateRow[row]) + "." + emit::kLane[l];
            }
            rows[row] = changed ? (hlsl ? "uint4(" : "vec4u(") + lanes + ")" : std::string(emit::kStateRow[row]);
        }
        if (hlsl)
            out += "    VbRow o;\n    o.pos = " + rows[0] + ";\n    o.col = " + rows[1] + ";\n    o.st = " + rows[2] +
                   ";\n    return o;\n}\n";
        else
            out += "    return VbRow(" + rows[0] + ", " + rows[1] + ", " + rows[2] + ");\n}\n";
    }
    // The loop (ir::forEachPass is its CPU twin): the set bits of the mask from the lowest up, at most
    // kBatchMaxPasses of them; the kind of a pass is the first word of its header row.
    const std::string bound = emit::number(kBatchMaxPasses), rowsPerPass = emit::number(kPassRows);
    if (hlsl)
        out += "VbRow vbChain" + suffix + "(VbRow row, uint4 sel)\n{\n    VbRow r = row;\n    uint todo = sel.x;\n" +
               "    [loop] for (uint n = 0u; n < " + bound + " && todo != 0u; n += 1u)\n    {\n" +
               "        uint base = sel.w + firstbitlow(todo) * " + rowsPerPass + ";\n" +
               "        todo &= todo - 1u;\n        uint kind = vpConstant(base).x;\n";
    else
        out += "fn vbChain" + suffix +
               "(row: VbRow, sel: vec4u) -> VbRow {\n    var r = row;\n    var todo = sel.x;\n" +
               "    for (var n = 0u; n < " + bound + " && todo != 0u; n += 1u) {\n" +
               "        let base = sel.w + firstTrailingBit(todo) * " + rowsPerPass + ";\n" +
               "        todo &= todo - 1u;\n        let kind = vpConstant(base).x;\n";
    for (size_t k = 0; k < plan.kinds.size(); ++k)
        out += std::string(k ? "        else if (kind == " : "        if (kind == ") + emit::number(plan.kinds[k]) +
               ") { r = vbPass" + std::to_string(plan.kinds[k]) + suffix + "(r, sel, base); }\n";
    out += "    }\n    return r;\n}\n";
    const Stage& finish = batchFinish();
    out += hlsl ? "VpRaw vbFinish" + suffix + "(VbRow r)\n{\n" : "fn vbFinish" + suffix + "(r: VbRow) -> VpRaw {\n";
    out += emit::body(hlsl, finish, emit::liveOf(finish, fieldMask, 0u));
    out += emit::returnFields(hlsl, finish, fieldMask);
    const std::string call = "return vbFinish" + suffix + "(vbChain" + suffix + "(row, sel)); }\n";
    out += hlsl ? "VpRaw vbEvaluate" + suffix + "(VbRow row, uint4 sel) { " + call
                : "fn vbEvaluate" + suffix + "(row: VbRow, sel: vec4u) -> VpRaw { " + call;
    return out;
}

// The stages of the game the renderer draws: the two strip programs.
inline std::vector<StageEntry> gameStages() {
    return {{StripsFlat, &ir::stripsFlat()}, {StripsLit, &ir::stripsLit()}};
}

} // namespace gsr::vp
