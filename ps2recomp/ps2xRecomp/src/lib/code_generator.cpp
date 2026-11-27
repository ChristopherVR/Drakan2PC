#include "ps2recomp/code_generator.h"
#include "ps2recomp/control_flow_analyzer.h"
#include "ps2recomp/Emitters/function_emitter.h"
#include "ps2recomp/Emitters/function_table_emitter.h"
#include "ps2recomp/Translators/instruction_translator.h"
#include "ps2recomp/Translators/special_translator.h"
#include "ps2recomp/Translators/regimm_translator.h"
#include "ps2recomp/Translators/cop0_translator.h"
#include "ps2recomp/Translators/fpu_translator.h"
#include "ps2recomp/Translators/mmi_translator.h"
#include "ps2recomp/Translators/vu_translator.h"
#include "ps2recomp/Emitters/control_flow_emitter.h"
#include "ps2recomp/control_flow_utils.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/recompiler_reporter.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_calls.h"

#include <fmt/format.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <iostream>
#include <cctype>
#include <cmath>
#include <vector>

namespace ps2recomp {
const std::unordered_set<std::string> kKeywords = {
    "alignas",     "alignof",   "and",        "and_eq",    "asm",      "auto",         "bitand",
    "bitor",       "bool",      "break",      "case",      "catch",    "char",         "char8_t",
    "char16_t",    "char32_t",  "class",      "compl",     "concept",  "const",        "consteval",
    "constexpr",   "constinit", "const_cast", "continue",  "co_await", "co_return",    "co_yield",
    "decltype",    "default",   "delete",     "do",        "double",   "dynamic_cast", "else",
    "enum",        "explicit",  "export",     "extern",    "false",    "float",        "for",
    "friend",      "goto",      "if",         "inline",    "int",      "long",         "mutable",
    "namespace",   "new",       "noexcept",   "not",       "not_eq",   "nullptr",      "operator",
    "or",          "or_eq",     "private",    "protected", "public",   "register",     "reinterpret_cast",
    "requires",    "return",    "short",      "signed",    "sizeof",   "static",       "static_assert",
    "static_cast", "struct",    "switch",     "template",  "this",     "thread_local", "throw",
    "true",        "try",       "typedef",    "typeid",    "typename", "union",        "unsigned",
    "using",       "virtual",   "void",       "volatile",  "wchar_t",  "while",        "xor",
    "xor_eq"};
}

namespace ps2recomp {

static std::string sanitizeIdentifierBody(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size() + 1);

    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_') {
            sanitized.push_back(c);
        } else {
            sanitized.push_back('_');
        }
    }

    if (sanitized.empty()) {
        return sanitized;
    }

    const unsigned char first = static_cast<unsigned char>(sanitized.front());
    if (!(std::isalpha(first) || sanitized.front() == '_')) {
        sanitized.insert(sanitized.begin(), '_');
    }

    return sanitized;
}

static bool isReservedCxxIdentifier(const std::string& name) {
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
        return true;
    if (name.size() >= 2 && name[0] == '_' && std::isupper(static_cast<unsigned char>(name[1])))
        return true;
    return false;
}

static bool isReservedCxxKeyword(const std::string& name) {
    return kKeywords.contains(name);
}

CodeGenerator::CodeGenerator(const std::vector<Symbol>& symbols, const std::vector<Section>& sections)
    : m_sections(sections) {
    for (auto& symbol : symbols) {
        m_symbols.emplace(symbol.address, symbol);
    }
}

void CodeGenerator::setRenamedFunctions(const std::unordered_map<uint32_t, std::string>& renames) {
    m_renamedFunctions = renames;
}

void CodeGenerator::setBootstrapInfo(const BootstrapInfo& info) {
    m_bootstrapInfo = info;
}

void CodeGenerator::setRelocationCallNames(const std::unordered_map<uint32_t, std::string>& callNames) {
    m_relocationCallNames = callNames;
}

void CodeGenerator::setConfiguredJumpTables(const std::vector<JumpTable>& jumpTables) {
    m_configJumpTableTargetsByAddress.clear();
    for (const auto& table : jumpTables) {
        auto& targets = m_configJumpTableTargetsByAddress[table.address];
        for (const auto& entry : table.entries) {
            targets.push_back(entry.target);
        }
    }

    for (auto& [address, targets] : m_configJumpTableTargetsByAddress) {
        (void)address;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
}

void CodeGenerator::setResumeEntryTargets(
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& resumeTargetsByOwner) {
    m_resumeEntryTargetsByOwner = resumeTargetsByOwner;
    for (auto& [owner, targets] : m_resumeEntryTargetsByOwner) {
        (void)owner;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
}

void CodeGenerator::setEmitInstructionComments(bool emitInstructionComments) {
    m_emitInstructionComments = emitInstructionComments;
}

void CodeGenerator::setReporter(RecompilerReporter* reporter) {
    m_reporter = reporter;
}

std::string CodeGenerator::getFunctionName(uint32_t address) const {
    auto it = m_renamedFunctions.find(address);
    if (it != m_renamedFunctions.end()) {
        return it->second;
    }

    const Symbol* sym = findSymbolByAddress(address);
    if (sym && sym->isFunction) {
        return CodeGenerator::sanitizeFunctionName(sym->name);
    }

    return "";
}

std::string CodeGenerator::sanitizeFunctionName(const std::string& name) const {
    std::string sanitized = sanitizeIdentifierBody(name);
    if (sanitized.empty())
        return sanitized;

    // ugly but will do for now
    if (sanitized == "main")
        return "ps2_main";

    if (isReservedCxxKeyword(sanitized))
        return "ps2_" + sanitized;

    if (sanitized[0] == '_')
        return "ps2" + sanitized;

    if (!isReservedCxxIdentifier(sanitized))
        return sanitized;

    return "ps2_" + sanitized;
}

std::string CodeGenerator::handleBranchDelaySlots(const Instruction& branchInst, const Instruction& delaySlot,
                                                  const Function& function, const AnalysisResult& analysisResult) {
    ControlFlowEmitter emitter(*this, branchInst, delaySlot, function, analysisResult);
    return emitter.emit();
}

std::string CodeGenerator::handleBranchDelaySlots(const Instruction& branchInst, const Instruction& delaySlot,
                                                  const Function& function, const AnalysisResult& analysisResult,
                                                  std::string delaySlotOverride) {
    ControlFlowEmitter emitter(*this, branchInst, delaySlot, function, analysisResult, std::move(delaySlotOverride));
    return emitter.emit();
}

CodeGenerator::~CodeGenerator() = default;

TranslatedInstruction CodeGenerator::translate(const Instruction& inst) {
    return translate(inst, {});
}

TranslatedInstruction CodeGenerator::translate(const Instruction& inst, const MemoryAccessHint& memoryHint) {
    m_translationEntersRuntime = false;
    TranslatedInstruction translated;
    translated.code = translateInstruction(inst, memoryHint);
    translated.entersRuntime = m_translationEntersRuntime;
    m_translationEntersRuntime = false;
    return translated;
}

// READn_AT / WRITEn_AT are READn / WRITEn of ps2_runtime_macros.h plus the guest pc of the access. Guest RAM is
// accessed inline exactly as before; only the slow path (MMIO, scratchpad, anything outside RAM) stores ctx->pc first,
// so MMIO hooks keyed on the pc and address-error exceptions see the accessing instruction at no cost to RAM accesses.
std::string CodeGenerator::memoryAccessHeader() {
    return R"PS2X(#pragma once
// Generated by ps2xRecomp: guest loads and stores whose address is only known at run time.
#include "ps2_runtime_macros.h"

#define PS2_RD_AT_FN(NAME, T, SLOW)                                                                                    \
    static PS2_FORCEINLINE T NAME(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, uint32_t pc) {        \
        if (Ps2InFastRam(a)) {                                                                                         \
            T v;                                                                                                       \
            std::memcpy(&v, rdram + (a & PS2_RAM_MASK), sizeof(T));                                                    \
            return v;                                                                                                  \
        }                                                                                                              \
        ctx->pc = pc;                                                                                                  \
        return SLOW(rdram, rt, ctx, a);                                                                                \
    }
PS2_RD_AT_FN(Ps2Rd8At, uint8_t, Ps2Rd8Slow)
PS2_RD_AT_FN(Ps2Rd16At, uint16_t, Ps2Rd16Slow)
PS2_RD_AT_FN(Ps2Rd32At, uint32_t, Ps2Rd32Slow)
PS2_RD_AT_FN(Ps2Rd64At, uint64_t, Ps2Rd64Slow)
PS2_RD_AT_FN(Ps2Rd128At, __m128i, Ps2Rd128Slow)
#undef PS2_RD_AT_FN

#define PS2_WR_AT_FN(NAME, T, SLOW)                                                                                    \
    static PS2_FORCEINLINE void NAME(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, T v,               \
                                     uint32_t pc) {                                                                    \
        if (Ps2InFastRam(a)) {                                                                                         \
            std::memcpy(rdram + (a & PS2_RAM_MASK), &v, sizeof(T));                                                    \
            return;                                                                                                    \
        }                                                                                                              \
        ctx->pc = pc;                                                                                                  \
        SLOW(rdram, rt, ctx, a, v);                                                                                    \
    }
PS2_WR_AT_FN(Ps2Wr8At, uint8_t, Ps2Wr8Slow)
PS2_WR_AT_FN(Ps2Wr16At, uint16_t, Ps2Wr16Slow)
PS2_WR_AT_FN(Ps2Wr32At, uint32_t, Ps2Wr32Slow)
PS2_WR_AT_FN(Ps2Wr64At, uint64_t, Ps2Wr64Slow)
PS2_WR_AT_FN(Ps2Wr128At, __m128i, Ps2Wr128Slow)
#undef PS2_WR_AT_FN

#define READ8_AT(addr, pc) Ps2Rd8At(rdram, runtime, ctx, (uint32_t)(addr), (pc))
#define READ16_AT(addr, pc) Ps2Rd16At(rdram, runtime, ctx, (uint32_t)(addr), (pc))
#define READ32_AT(addr, pc) Ps2Rd32At(rdram, runtime, ctx, (uint32_t)(addr), (pc))
#define READ64_AT(addr, pc) Ps2Rd64At(rdram, runtime, ctx, (uint32_t)(addr), (pc))
#define READ128_AT(addr, pc) Ps2Rd128At(rdram, runtime, ctx, (uint32_t)(addr), (pc))
#define WRITE8_AT(addr, val, pc) Ps2Wr8At(rdram, runtime, ctx, (uint32_t)(addr), (uint8_t)(val), (pc))
#define WRITE16_AT(addr, val, pc) Ps2Wr16At(rdram, runtime, ctx, (uint32_t)(addr), (uint16_t)(val), (pc))
#define WRITE32_AT(addr, val, pc) Ps2Wr32At(rdram, runtime, ctx, (uint32_t)(addr), (uint32_t)(val), (pc))
#define WRITE64_AT(addr, val, pc) Ps2Wr64At(rdram, runtime, ctx, (uint32_t)(addr), (uint64_t)(val), (pc))
#define WRITE128_AT(addr, val, pc) Ps2Wr128At(rdram, runtime, ctx, (uint32_t)(addr), (val), (pc))
)PS2X";
}

std::string CodeGenerator::translateInstruction(const Instruction& inst) {
    return InstructionTranslator(*this).translate(inst, {});
}

std::string CodeGenerator::translateInstruction(const Instruction& inst, const MemoryAccessHint& memoryHint) {
    return InstructionTranslator(*this).translate(inst, memoryHint);
}

std::string CodeGenerator::translateSpecialInstruction(const Instruction& inst) {
    return SpecialTranslator(*this).translate(inst);
}

std::string CodeGenerator::translateRegimmInstruction(const Instruction& inst) {
    return RegimmTranslator(*this).translate(inst);
}

std::string CodeGenerator::translateCOP0Instruction(const Instruction& inst) {
    return Cop0Translator(*this).translate(inst);
}

std::string CodeGenerator::translateFPUInstruction(const Instruction& inst) {
    return FpuTranslator(*this).translate(inst);
}

std::string CodeGenerator::translateMMIInstruction(const Instruction& inst) {
    return MmiTranslator(*this).translate(inst);
}

std::string CodeGenerator::translateVUInstruction(const Instruction& inst) {
    std::string code = VuTranslator(*this).translate(inst);
    // VF0 is the constant (0, 0, 0, 1) and VI0 is 0 on the VU: a write to them is discarded by the hardware (assemblers
    // use vf0 as a "result not needed" destination). The translated statements write the register file directly, so
    // restore the constant.
    bool fixVf0 = false, fixVi0 = false;
    if (inst.rs == 0x05 && inst.rd == 0) // QMTC2 vf0
        fixVf0 = true;
    if (inst.rs >= 0x10) {
        const uint32_t f6 = inst.function & 0x3Fu;
        if (f6 < 0x30u)
            fixVf0 = fixVf0 || inst.sa == 0;
        else if (f6 <= 0x35u && f6 != 0x33u)
            fixVi0 = fixVi0 || (f6 == 0x32u ? inst.rt : inst.sa) == 0;
        else if (f6 >= 0x3Cu) {
            const uint32_t op2 = (((inst.raw >> 6) & 0x1Fu) << 2) | (inst.raw & 3u);
            if ((op2 >= 0x10 && op2 <= 0x17) || op2 == 0x1D || op2 == 0x30 || op2 == 0x31 || op2 == 0x3D)
                fixVf0 = fixVf0 || inst.rt == 0;
            if (op2 == 0x3C || op2 == 0x3E)
                fixVi0 = fixVi0 || inst.rt == 0;
        }
    }
    if (fixVf0)
        code += "\n    ctx->vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);";
    if (fixVi0)
        code += "\n    ctx->vi[0] = 0;";
    return code;
}

CodeGenerator::AnalysisResult CodeGenerator::collectInternalBranchTargets(const Function& function,
                                                                          const std::vector<Instruction>& instructions,
                                                                          const std::vector<Function>* allFunctions) {
    ControlFlowAnalyzer analyzer(m_sections, m_configJumpTableTargetsByAddress, m_reporter, &m_jumpTargetsByJr,
                                 m_indirectFallback);
    return analyzer.analyze(function, instructions, allFunctions);
}

std::string ps2recomp::CodeGenerator::generateFunction(const Function& function,
                                                       const std::vector<Instruction>& instructions,
                                                       const bool& useHeaders) {
    FunctionEmitter emitter(*this);
    return emitter.emit(function, instructions, useHeaders);
}

std::string CodeGenerator::emitUnhandledInstruction(const Instruction& inst, const std::string& message) {
    if (m_reporter) {
        m_reporter->recordUnhandledInstruction(m_currentFunctionName, inst.address, inst.raw, message);
    }

    m_translationEntersRuntime = true; // the throw unwinds into the runtime
    return fmt::format("throw std::runtime_error(\"{} at 0x{:X} raw=0x{:08X}\");", message, inst.address, inst.raw);
}

std::string CodeGenerator::generateFunctionRegistration(const std::vector<Function>& functions,
                                                        const std::map<uint32_t, std::string>& stubs) {
    FunctionTableEmitter emitter(*this);
    return emitter.emit(functions, stubs);
}

const Symbol* CodeGenerator::findSymbolByAddress(uint32_t address) const {
    auto it = m_symbols.find(address);
    if (it != m_symbols.end()) {
        return &it->second;
    }

    return nullptr;
}
} // namespace ps2recomp
