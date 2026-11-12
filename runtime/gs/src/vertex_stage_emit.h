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
    return vuRound(sum, residual);
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

// The stages as functions plus the two dispatchers. `fieldMask` (bits of ir::Field) keeps only the operations the
// wanted packet fields need; the other fields are returned as 0.
inline std::string emitStages(ShaderLanguage language, const StageEntry* entries, size_t entryCount,
                              uint32_t fieldMask = ir::kAllFields, const std::string& suffix = {}) {
    using namespace ir;
    const bool hlsl = language == ShaderLanguage::Hlsl;
    static const char kLane[4] = {'x', 'y', 'z', 'w'};
    std::string out;
    for (size_t e = 0; e < entryCount; ++e) {
        const Stage& stage = *entries[e].stage;
        // the operations the wanted fields reach
        std::vector<uint8_t> live(stage.nodes.size(), 0);
        for (uint32_t f = 0; f < kFieldCount; ++f)
            if ((fieldMask >> f) & 1u)
                if (stage.outputs[f] != kNoValue)
                    live[stage.outputs[f]] = 1;
        std::vector<uint8_t> rowUsed(stage.constantRows, 0);
        for (size_t i = stage.nodes.size(); i-- > 0;) {
            if (!live[i])
                continue;
            const Node& n = stage.nodes[i];
            if (n.op == Op::Constant)
                rowUsed[n.a] = 1;
            const uint16_t inputs[3] = {n.a, n.b, n.c};
            for (uint32_t k = 0; k < inputCount(n.op); ++k)
                live[inputs[k]] = 1;
        }
        const std::string name = "vpStage" + std::to_string(entries[e].program) + suffix;
        out += hlsl ? "VpRaw " + name + "(VpIn i, uint base)\n{\n" : "fn " + name + "(i: VpIn, base: u32) -> VpRaw {\n";
        const std::string declare = hlsl ? "    uint " : "    let ";
        for (uint32_t row = 0; row < stage.constantRows; ++row)
            if (rowUsed[row])
                out += (hlsl ? "    uint4 k" : "    let k") + std::to_string(row) + " = vpConstant(base + " +
                       emit::number(row) + ");\n";
        const auto t = [](uint16_t id) { return "t" + std::to_string(id); };
        for (size_t i = 0; i < stage.nodes.size(); ++i) {
            if (!live[i])
                continue;
            const Node& n = stage.nodes[i];
            const std::string a = t(n.a), b = t(n.b), c = t(n.c);
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
                    else
                        x = "i.a" + std::to_string(n.a) + "." + lane; // further rows: 4 x uint members a3, a4, ...
                    break;
                }
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
            out += declare + t(static_cast<uint16_t>(i)) + " = " + x + ";\n";
        }
        const auto field = [&](Field f, const char* mask) -> std::string {
            if (!((fieldMask >> f) & 1u) || stage.outputs[f] == kNoValue)
                return "0u";
            return mask ? "(" + t(stage.outputs[f]) + " & " + mask + ")" : t(stage.outputs[f]);
        };
        const std::string place = field(X, "0xFFFFu") + ", " + field(Y, "0xFFFFu") + ", " + field(Z, nullptr) + ", " +
                                  field(FogLane, nullptr);
        const std::string colour =
            field(R, "0xFFu") + ", " + field(G, "0xFFu") + ", " + field(B, "0xFFu") + ", " + field(A, "0xFFu");
        const std::string stq = field(S, nullptr) + ", " + field(T, nullptr) + ", " + field(Q, nullptr) + ", 0u";
        if (hlsl)
            out += "    VpRaw o;\n    o.place = uint4(" + place + ");\n    o.colour = uint4(" + colour +
                   ");\n    o.stq = uint4(" + stq + ");\n    return o;\n}\n";
        else
            out += "    return VpRaw(vec4u(" + place + "), vec4u(" + colour + "), vec4u(" + stq + "));\n}\n";
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

// The stages of the game the renderer draws: the two strip programs.
inline std::vector<StageEntry> gameStages() {
    return {{StripsFlat, &ir::stripsFlat()}, {StripsLit, &ir::stripsLit()}};
}

} // namespace gsr::vp
