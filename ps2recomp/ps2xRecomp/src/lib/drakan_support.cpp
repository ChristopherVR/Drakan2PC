// Drakan port additions to PS2Recompiler:
//  - loadFunctionsCsv(): authoritative function boundaries (Ghidra export) instead of heuristic discovery
//  - buildJumpTargets(): real jump-table recovery for non-$ra JR (ELF-verified, unioned with Ghidra's case targets)
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace ps2recomp {
// start_addr_hex,end_addr_hex,size,name,num_callers,is_thunk   (end exclusive)
bool PS2Recompiler::loadFunctionsCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        m_reporter.error("functions-csv", "Could not open " + path);
        return false;
    }
    const auto sections = m_elfParser->getSections();
    std::string line;
    std::getline(file, line); // header
    std::vector<Function> list;
    size_t skipped = 0;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        std::stringstream ss(line);
        std::string a, b, c, name;
        if (!std::getline(ss, a, ',') || !std::getline(ss, b, ',') || !std::getline(ss, c, ',') ||
            !std::getline(ss, name, ','))
            continue;
        Function f{};
        f.start = static_cast<uint32_t>(std::stoul(a, nullptr, 16));
        f.end = static_cast<uint32_t>(std::stoul(b, nullptr, 16));
        f.name = name;
        if (f.end <= f.start || !m_elfParser->isValidAddress(f.start)) {
            ++skipped;
            continue;
        }
        bool inCode = false;
        for (const auto& sec : sections) {
            if (sec.isCode && f.start >= sec.address && f.start < sec.address + sec.size) {
                inCode = true;
                if (f.end > sec.address + sec.size)
                    f.end = sec.address + sec.size;
                break;
            }
        }
        if (!inCode) {
            ++skipped;
            continue;
        }
        list.push_back(std::move(f));
    }
    std::sort(list.begin(), list.end(), [](const Function& x, const Function& y) { return x.start < y.start; });
    // Ghidra's function ranges only cover the entry chunk of non-contiguous functions; the other chunks
    // appear as gaps. Absorb each gap into the preceding function so every branch target has code.
    size_t absorbed = 0;
    for (size_t i = 0; i + 1 < list.size(); ++i) {
        if (list[i + 1].start > list[i].end) {
            absorbed += list[i + 1].start - list[i].end;
            list[i].end = list[i + 1].start;
        }
    }
    m_functions = std::move(list);
    std::ostringstream msg;
    msg << "functions_csv: " << m_functions.size() << " functions loaded (" << skipped << " skipped), absorbed "
        << absorbed << " bytes of inter-function gaps";
    m_reporter.info("functions-csv", msg.str());
    return !m_functions.empty();
}

void PS2Recompiler::buildJumpTargets() {
    std::unordered_map<uint32_t, std::vector<uint32_t>> csvTargets;
    if (!m_config.jumpTablesCsvPath.empty()) {
        std::ifstream file(m_config.jumpTablesCsvPath);
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string jr, tbl, n, targets;
            if (!std::getline(ss, jr, ',') || !std::getline(ss, tbl, ',') || !std::getline(ss, n, ',') ||
                !std::getline(ss, targets, ','))
                continue;
            std::vector<uint32_t> v;
            std::stringstream ts(targets);
            std::string t;
            while (std::getline(ts, t, ';'))
                if (!t.empty())
                    v.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16)));
            csvTargets[static_cast<uint32_t>(std::stoul(jr, nullptr, 16))] = std::move(v);
        }
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> result;
    size_t fromPattern = 0, fromCsvOnly = 0, none = 0, total = 0;
    for (const auto& function : m_functions) {
        auto it = m_decodedFunctions.find(function.start);
        if (it == m_decodedFunctions.end())
            continue;
        const auto& ins = it->second;
        std::unordered_set<uint32_t> addrs;
        for (const auto& i : ins)
            addrs.insert(i.address);

        // destination GPR of an instruction (-1 if it writes none that we model)
        auto dest = [](const Instruction& i) -> int {
            switch (i.opcode) {
                case OPCODE_SPECIAL:
                    switch (i.function) {
                        case SPECIAL_ADDU:
                        case SPECIAL_ADD:
                        case SPECIAL_SUBU:
                        case SPECIAL_SUB:
                        case SPECIAL_AND:
                        case SPECIAL_OR:
                        case SPECIAL_XOR:
                        case SPECIAL_NOR:
                        case SPECIAL_SLT:
                        case SPECIAL_SLTU:
                        case SPECIAL_SLL:
                        case SPECIAL_SRL:
                        case SPECIAL_SRA:
                        case SPECIAL_SLLV:
                        case SPECIAL_SRLV:
                        case SPECIAL_SRAV:
                        case SPECIAL_DADDU:
                        case SPECIAL_DADD:
                        case SPECIAL_MFHI:
                        case SPECIAL_MFLO:
                        case SPECIAL_JALR:
                        case SPECIAL_DSLL:
                        case SPECIAL_DSRL:
                        case SPECIAL_DSRA:
                        case SPECIAL_DSLL32:
                        case SPECIAL_DSRL32:
                        case SPECIAL_DSRA32:
                            return static_cast<int>(i.rd);
                        default:
                            return -1;
                    }
                case OPCODE_ADDI:
                case OPCODE_ADDIU:
                case OPCODE_SLTI:
                case OPCODE_SLTIU:
                case OPCODE_ANDI:
                case OPCODE_ORI:
                case OPCODE_XORI:
                case OPCODE_LUI:
                case OPCODE_LB:
                case OPCODE_LH:
                case OPCODE_LW:
                case OPCODE_LBU:
                case OPCODE_LHU:
                case OPCODE_LWU:
                case OPCODE_LD:
                case OPCODE_DADDIU:
                case OPCODE_LWL:
                case OPCODE_LWR:
                case OPCODE_LQ:
                    return static_cast<int>(i.rt);
                default:
                    return -1;
            }
        };

        // constant value of `reg` just before instruction index `idx` (lui/addiu/ori chains)
        std::function<bool(int, int, uint32_t&, int)> constOf = [&](int reg, int idx, uint32_t& val,
                                                                    int depth) -> bool {
            if (reg == 0) {
                val = 0;
                return true;
            }
            if (depth > 4)
                return false;
            for (int k = idx - 1; k >= 0 && k >= idx - 40; --k) {
                if (dest(ins[k]) != reg)
                    continue;
                const Instruction& w = ins[k];
                if (w.opcode == OPCODE_LUI) {
                    val = w.immediate << 16;
                    return true;
                }
                if (w.opcode == OPCODE_ADDIU || w.opcode == OPCODE_ADDI) {
                    uint32_t base;
                    if (!constOf(static_cast<int>(w.rs), k, base, depth + 1))
                        return false;
                    val = base + w.simmediate;
                    return true;
                }
                if (w.opcode == OPCODE_ORI) {
                    uint32_t base;
                    if (!constOf(static_cast<int>(w.rs), k, base, depth + 1))
                        return false;
                    val = base | w.immediate;
                    return true;
                }
                return false;
            }
            return false;
        };

        for (size_t j = 0; j < ins.size(); ++j) {
            const Instruction& jr = ins[j];
            if (!(jr.opcode == OPCODE_SPECIAL && jr.function == SPECIAL_JR && jr.rs != 31))
                continue;
            ++total;
            std::set<uint32_t> targets;
            bool patternHit = false;

            // lw rs, off(rb) feeding the jr
            int lw = -1;
            for (int k = static_cast<int>(j) - 1; k >= 0 && k >= static_cast<int>(j) - 24; --k) {
                if (dest(ins[k]) == static_cast<int>(jr.rs)) {
                    if (ins[k].opcode == OPCODE_LW || ins[k].opcode == OPCODE_LWU)
                        lw = k;
                    break;
                }
            }
            if (lw >= 0) {
                const int32_t off = static_cast<int32_t>(ins[lw].simmediate);
                const int rb = static_cast<int>(ins[lw].rs);
                uint32_t tableAddr = 0;
                bool have = false;
                int au = -1;
                for (int k = lw - 1; k >= 0 && k >= lw - 16; --k) {
                    if (dest(ins[k]) == rb) {
                        au = k;
                        break;
                    }
                }
                uint32_t c = 0;
                if (au >= 0 && ins[au].opcode == OPCODE_SPECIAL &&
                    (ins[au].function == SPECIAL_ADDU || ins[au].function == SPECIAL_ADD)) {
                    if (ins[au].rs != 0 && constOf(static_cast<int>(ins[au].rs), au, c, 0)) {
                        tableAddr = c + off;
                        have = true;
                    } else if (ins[au].rt != 0 && constOf(static_cast<int>(ins[au].rt), au, c, 0)) {
                        tableAddr = c + off;
                        have = true;
                    }
                } else if (au >= 0 && constOf(rb, lw, c, 0)) {
                    tableAddr = c + off;
                    have = true;
                }
                if (have && m_elfParser->isValidAddress(tableAddr) && (tableAddr & 3u) == 0) {
                    for (uint32_t n = 0; n < 1024; ++n) {
                        const uint32_t a = tableAddr + n * 4;
                        if (!m_elfParser->isValidAddress(a))
                            break;
                        const uint32_t w = m_elfParser->readWord(a);
                        if ((w & 3u) == 0 && w >= function.start && w < function.end && addrs.contains(w))
                            targets.insert(w);
                        else
                            break;
                    }
                    patternHit = !targets.empty();
                }
            }
            bool csvUsed = false;
            auto csvIt = csvTargets.find(jr.address);
            if (csvIt != csvTargets.end()) {
                for (uint32_t t : csvIt->second) {
                    if ((t & 3u) == 0 && t >= function.start && t < function.end && addrs.contains(t)) {
                        targets.insert(t);
                        csvUsed = true;
                    }
                }
            }
            if (targets.empty()) {
                ++none;
                continue;
            }
            if (patternHit)
                ++fromPattern;
            else if (csvUsed)
                ++fromCsvOnly;
            result[jr.address] = std::vector<uint32_t>(targets.begin(), targets.end());
        }
    }
    std::ostringstream msg;
    msg << "jump tables: " << total << " non-ra JR; " << fromPattern << " ELF-verified, " << fromCsvOnly
        << " csv-only, " << none << " unresolved (tail-call dispatch)";
    m_reporter.info("jump-tables", msg.str());
    m_codeGenerator->setJumpTargetsByJr(std::move(result));
}

// Code addresses that are not function starts but are taken as pointers (callbacks, vtable slots, end functions of
// SIF RPC calls, ...). Ghidra's function list misses those that sit inside an absorbed gap; they must be callable
// through the function table, so register them as extra entry points of their owner function.
void PS2Recompiler::collectCodePointerEntries() {
    std::unordered_set<uint32_t> starts;
    for (const auto& f : m_functions)
        starts.insert(f.start);
    std::unordered_set<uint32_t> instrAddrs;
    for (const auto& [start, ins] : m_decodedFunctions)
        for (const auto& i : ins)
            instrAddrs.insert(i.address);

    size_t fromData = 0, fromCode = 0;
    auto consider = [&](uint32_t value, size_t& counter) {
        if ((value & 3u) == 0 && !starts.contains(value) && instrAddrs.contains(value) &&
            m_entryPointHintStarts.insert(value).second)
            ++counter;
    };

    for (const auto& sec : m_sections) {
        if (sec.isCode || sec.isBSS || sec.data == nullptr || sec.size < 4 || sec.address == 0)
            continue;
        for (uint32_t off = 0; off + 4 <= sec.size; off += 4) {
            uint32_t v;
            std::memcpy(&v, sec.data + off, 4);
            consider(v, fromData);
        }
    }

    // lui rX,hi ; (addiu|ori) rX,rX,lo  (also with a few unrelated instructions in between)
    for (const auto& [start, ins] : m_decodedFunctions) {
        for (size_t j = 0; j < ins.size(); ++j) {
            const Instruction& lo = ins[j];
            if (!((lo.opcode == OPCODE_ADDIU || lo.opcode == OPCODE_ORI) && lo.rs != 0))
                continue;
            for (int k = static_cast<int>(j) - 1; k >= 0 && k >= static_cast<int>(j) - 32; --k) {
                const Instruction& hi = ins[k];
                if (hi.opcode == OPCODE_LUI && hi.rt == lo.rs) {
                    const uint32_t value =
                        (hi.immediate << 16) + (lo.opcode == OPCODE_ADDIU ? lo.simmediate : lo.immediate);
                    if (value >= 0x100000u && value < 0x4d7400u)
                        consider(value, fromCode);
                    break;
                }
            }
        }
    }
    std::ostringstream msg;
    msg << "code pointers: " << fromData << " from data sections, " << fromCode
        << " from lui/addiu pairs (non-function-start code addresses)";
    m_reporter.info("code-pointers", msg.str());
}
} // namespace ps2recomp
