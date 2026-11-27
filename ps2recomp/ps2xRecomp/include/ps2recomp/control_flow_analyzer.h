#ifndef PS2RECOMP_CONTROL_FLOW_ANALYZER_H
#define PS2RECOMP_CONTROL_FLOW_ANALYZER_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2recomp {
struct Function;
class FunctionIndex;
struct Instruction;
struct Section;
class RecompilerReporter;

struct ControlFlowAnalysisResult {
    std::unordered_set<uint32_t> entryPoints;
    std::unordered_set<uint32_t> externalEntryPoints;
    std::unordered_set<uint32_t> resumeEntryPoints;
    std::unordered_set<uint32_t> indirectFallbackEntryPoints;
    std::unordered_map<uint32_t, std::vector<uint32_t>> jumpTableTargets;
    // Non-$ra JR sites neither jumptables.csv nor the table detector resolved (only without indirect_fallback).
    std::vector<uint32_t> unresolvedIndirectJumps;
};

class ControlFlowAnalyzer {
public:
    ControlFlowAnalyzer(const std::vector<Section>& sections,
                        const std::unordered_map<uint32_t, std::vector<uint32_t>>& configuredJumpTableTargetsByAddress,
                        RecompilerReporter* reporter,
                        const std::unordered_map<uint32_t, std::vector<uint32_t>>* jumpTargetsByJr = nullptr,
                        bool indirectFallback = false);

    // owners (FunctionIndex::Scope::Owners) resolves branch targets outside the function to external entry points;
    // without it they are not reported.
    ControlFlowAnalysisResult analyze(const Function& function, const std::vector<Instruction>& instructions,
                                      const FunctionIndex* owners = nullptr) const;

private:
    const std::vector<Section>& m_sections;
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& m_configJumpTableTargetsByAddress;
    RecompilerReporter* m_reporter = nullptr;
    const std::unordered_map<uint32_t, std::vector<uint32_t>>* m_jumpTargetsByJr = nullptr;
    bool m_indirectFallback = false;
};
} // namespace ps2recomp

#endif // PS2RECOMP_CONTROL_FLOW_ANALYZER_H
