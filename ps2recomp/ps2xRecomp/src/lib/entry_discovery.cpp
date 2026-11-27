#include "ps2recomp/entry_discovery.h"

#include "ps2recomp/code_generator.h"
#include "ps2recomp/function_index.h"
#include "ps2recomp/instructions.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>

namespace ps2recomp {
namespace {
// `jr $ra` at the target: the entry is a return thunk, two instructions with the delay slot.
bool isReturnThunkStart(const Instruction& inst) {
    return inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_JR && inst.rs == 31;
}

std::string entryFunctionName(uint32_t start) {
    std::stringstream name;
    name << "entry_" << std::hex << start;
    return name.str();
}

// First start in the sorted `starts` after `address`.
std::optional<uint32_t> nextStartAfter(const std::vector<uint32_t>& starts, uint32_t address) {
    auto it = std::upper_bound(starts.begin(), starts.end(), address);
    if (it == starts.end()) {
        return std::nullopt;
    }
    return *it;
}

// Highest-starting function of `index` that contains `address` as a decoded instruction (strictly after its start when
// `strictlyInside`).
const Function* findDecodedOwner(const FunctionIndex& index, const DecodedFunctions& decoded, uint32_t address,
                                 bool strictlyInside = false) {
    return index.find(address, [&](const Function& function) {
        if (strictlyInside && address <= function.start) {
            return false;
        }
        const auto decodedIt = decoded.find(function.start);
        if (decodedIt == decoded.end()) {
            return false;
        }
        return std::any_of(decodedIt->second.begin(), decodedIt->second.end(),
                           [address](const Instruction& instruction) { return instruction.address == address; });
    });
}
} // namespace

EntryDiscovery::EntryDiscovery(std::vector<Function>& functions, DecodedFunctions& decoded,
                               const std::vector<Section>& sections)
    : m_functions(functions), m_decoded(decoded), m_sections(sections) {}

bool EntryDiscovery::isExecutableAddress(uint32_t address) const {
    return std::any_of(m_sections.begin(), m_sections.end(), [address](const Section& section) {
        return section.isCode && address >= section.address && address < section.address + section.size;
    });
}

size_t EntryDiscovery::synthesizeEntryFunctions(CodeGenerator* analyzer,
                                                const std::function<bool(Function&)>& decodeStandalone,
                                                const std::unordered_set<uint32_t>& seeds) {
    std::unordered_set<uint32_t> existingStarts;
    for (const auto& function : m_functions) {
        existingStarts.insert(function.start);
    }

    auto executableSectionEnd = [&](uint32_t address) -> std::optional<uint32_t> {
        for (const auto& section : m_sections) {
            if (section.isCode && address >= section.address && address < section.address + section.size) {
                return section.address + section.size;
            }
        }
        return std::nullopt;
    };

    size_t discoveredCount = 0;
    for (size_t pass = 1;; ++pass) {
        // Targets found this pass, with the decoded function that holds each (if any) to slice it from.
        struct PendingEntry {
            uint32_t target = 0;
            std::optional<uint32_t> containingStart;
            uint32_t containingEnd = 0;
        };
        std::vector<PendingEntry> pendingEntries;
        std::unordered_set<uint32_t> pendingStarts;

        // m_functions does not change until the end of the pass.
        const FunctionIndex allRecompiled(m_functions, FunctionIndex::Scope::AllRecompiled);
        const FunctionIndex owners(m_functions, FunctionIndex::Scope::Owners);

        auto queuePendingEntry = [&](uint32_t target) {
            if (!isExecutableAddress(target) || existingStarts.contains(target) || pendingStarts.contains(target)) {
                return;
            }

            PendingEntry pending{};
            pending.target = target;
            if (const Function* containing = findDecodedOwner(allRecompiled, m_decoded, target)) {
                pending.containingStart = containing->start;
                pending.containingEnd = containing->end;
            }
            pendingEntries.push_back(pending);
            pendingStarts.insert(target);
        };

        if (pass == 1u) {
            for (uint32_t target : seeds) {
                queuePendingEntry(target);
            }
        }

        for (const auto& function : m_functions) {
            if (!FunctionIndex::isRecompiled(function) || FunctionIndex::isEntrySlice(function) || !analyzer) {
                continue;
            }
            const auto decodedIt = m_decoded.find(function.start);
            if (decodedIt == m_decoded.end()) {
                continue;
            }

            const CodeGenerator::AnalysisResult analysis =
                analyzer->collectInternalBranchTargets(function, decodedIt->second, owners);
            for (uint32_t target : analysis.externalEntryPoints) {
                queuePendingEntry(target);
            }
            for (uint32_t target : analysis.resumeEntryPoints) {
                queuePendingEntry(target);
            }
        }

        if (pendingEntries.empty()) {
            break;
        }

        std::sort(pendingEntries.begin(), pendingEntries.end(),
                  [](const PendingEntry& a, const PendingEntry& b) { return a.target < b.target; });

        // An entry function ends at the next known or pending start.
        std::vector<uint32_t> boundaryStarts;
        boundaryStarts.reserve(existingStarts.size() + pendingStarts.size());
        boundaryStarts.insert(boundaryStarts.end(), existingStarts.begin(), existingStarts.end());
        boundaryStarts.insert(boundaryStarts.end(), pendingStarts.begin(), pendingStarts.end());
        std::sort(boundaryStarts.begin(), boundaryStarts.end());
        boundaryStarts.erase(std::unique(boundaryStarts.begin(), boundaryStarts.end()), boundaryStarts.end());

        std::vector<Function> newEntries;
        std::unordered_set<uint32_t> successfulStarts;
        for (const auto& pending : pendingEntries) {
            const uint32_t target = pending.target;

            Function entryFunction;
            entryFunction.name = entryFunctionName(target);
            entryFunction.start = target;
            entryFunction.isStub = false;
            entryFunction.isSkipped = false;
            entryFunction.isRecompiled = true;

            uint32_t entryEnd = 0;
            if (pending.containingStart.has_value()) {
                const auto containingDecodedIt = m_decoded.find(*pending.containingStart);
                if (containingDecodedIt == m_decoded.end()) {
                    continue;
                }

                const auto& containingInstructions = containingDecodedIt->second;
                auto sliceIt = std::find_if(containingInstructions.begin(), containingInstructions.end(),
                                            [&](const Instruction& candidate) { return candidate.address == target; });
                if (sliceIt == containingInstructions.end()) {
                    continue;
                }

                entryEnd = pending.containingEnd;
                const auto nextStart = nextStartAfter(boundaryStarts, target);
                if (nextStart.has_value() && *nextStart < entryEnd) {
                    entryEnd = *nextStart;
                }
                if (isReturnThunkStart(*sliceIt) && target <= (std::numeric_limits<uint32_t>::max() - 8u) &&
                    target + 8u < entryEnd) {
                    entryEnd = target + 8u;
                }
                if (entryEnd <= target) {
                    continue;
                }

                auto sliceEndIt =
                    std::find_if(sliceIt, containingInstructions.end(),
                                 [&](const Instruction& candidate) { return candidate.address >= entryEnd; });
                if (sliceEndIt == sliceIt) {
                    continue;
                }

                m_decoded[target] = std::vector<Instruction>(sliceIt, sliceEndIt);
                entryFunction.end = entryEnd;
            } else {
                const auto sectionEnd = executableSectionEnd(target);
                if (!sectionEnd.has_value()) {
                    continue;
                }

                entryEnd = *sectionEnd;
                const auto nextStart = nextStartAfter(boundaryStarts, target);
                if (nextStart.has_value() && *nextStart < entryEnd) {
                    entryEnd = *nextStart;
                }
                if (entryEnd <= target) {
                    continue;
                }

                entryFunction.end = entryEnd;
                if (!decodeStandalone(entryFunction)) {
                    continue;
                }
            }

            newEntries.push_back(entryFunction);
            successfulStarts.insert(target);
        }

        if (newEntries.empty()) {
            break;
        }

        discoveredCount += newEntries.size();
        m_functions.insert(m_functions.end(), newEntries.begin(), newEntries.end());
        existingStarts.insert(successfulStarts.begin(), successfulStarts.end());
        std::sort(m_functions.begin(), m_functions.end(),
                  [](const Function& a, const Function& b) { return a.start < b.start; });
    }

    return discoveredCount;
}

size_t EntryDiscovery::resliceEntryFunctions() {
    std::vector<uint32_t> boundaryStarts;
    boundaryStarts.reserve(m_functions.size());
    for (const auto& function : m_functions) {
        if (FunctionIndex::isRecompiled(function)) {
            boundaryStarts.push_back(function.start);
        }
    }
    std::sort(boundaryStarts.begin(), boundaryStarts.end());
    boundaryStarts.erase(std::unique(boundaryStarts.begin(), boundaryStarts.end()), boundaryStarts.end());

    // Only entry functions change below, and the owner index leaves them out.
    const FunctionIndex owners(m_functions, FunctionIndex::Scope::Owners);

    size_t reslicedCount = 0;
    for (auto& function : m_functions) {
        if (!FunctionIndex::isRecompiled(function) || !FunctionIndex::isEntrySlice(function)) {
            continue;
        }

        const Function* owner = findDecodedOwner(owners, m_decoded, function.start);
        uint32_t sliceEndAddress = owner ? owner->end : function.end;
        const auto nextStart = nextStartAfter(boundaryStarts, function.start);
        if (nextStart.has_value() && *nextStart < sliceEndAddress) {
            sliceEndAddress = *nextStart;
        }
        if (sliceEndAddress <= function.start) {
            continue;
        }

        const auto sourceIt = m_decoded.find(owner ? owner->start : function.start);
        if (sourceIt == m_decoded.end()) {
            continue;
        }
        const std::vector<Instruction>& source = sourceIt->second;

        auto sliceBeginIt = std::find_if(source.begin(), source.end(), [&](const Instruction& candidate) {
            return candidate.address == function.start;
        });
        if (sliceBeginIt == source.end()) {
            continue;
        }
        auto sliceEndIt = std::find_if(sliceBeginIt, source.end(), [&](const Instruction& candidate) {
            return candidate.address >= sliceEndAddress;
        });
        if (sliceEndIt == sliceBeginIt) {
            continue;
        }

        std::vector<Instruction> sliced(sliceBeginIt, sliceEndIt);
        bool changed = (function.end != sliceEndAddress);
        const auto existingIt = m_decoded.find(function.start);
        if (existingIt == m_decoded.end()) {
            changed = true;
        } else if (!changed) {
            const auto& existing = existingIt->second;
            if (existing.size() != sliced.size()) {
                changed = true;
            } else if (!existing.empty() && (existing.front().address != sliced.front().address ||
                                             existing.back().address != sliced.back().address)) {
                changed = true;
            }
        }

        function.end = sliceEndAddress;
        m_decoded[function.start] = std::move(sliced);
        if (changed) {
            ++reslicedCount;
        }
    }

    return reslicedCount;
}

size_t EntryDiscovery::collectInternalEntryTargets(const std::vector<Function>& functions,
                                                   const DecodedFunctions& decoded,
                                                   const std::unordered_set<uint32_t>& entryAddresses,
                                                   TargetsByOwner& targetsByOwner) {
    std::unordered_set<uint32_t> functionStarts;
    functionStarts.reserve(functions.size());
    for (const auto& function : functions) {
        functionStarts.insert(function.start);
    }

    const FunctionIndex allRecompiled(functions, FunctionIndex::Scope::AllRecompiled);
    size_t addedCount = 0u;
    for (uint32_t entryAddress : entryAddresses) {
        if (functionStarts.contains(entryAddress)) {
            continue;
        }
        const Function* owner = findDecodedOwner(allRecompiled, decoded, entryAddress, true);
        if (!owner) {
            continue;
        }

        auto& targets = targetsByOwner[owner->start];
        if (std::find(targets.begin(), targets.end(), entryAddress) == targets.end()) {
            targets.push_back(entryAddress);
            ++addedCount;
        }
    }

    return addedCount;
}

void EntryDiscovery::collectResumeTargets(CodeGenerator& analyzer, TargetsByOwner& targetsByOwner,
                                          std::vector<std::pair<uint32_t, std::string>>& unresolvedJumps) const {
    const FunctionIndex owners(m_functions, FunctionIndex::Scope::Owners);
    for (const auto& function : m_functions) {
        if (!FunctionIndex::isRecompiled(function)) {
            continue;
        }

        // Ghidra exports standalone code blocks (static initializers, thunks, ...) as entry_* labels. Only entry_*
        // blocks nested inside a real function are synthetic wrappers; standalone ones need their own resume
        // points, otherwise a thread switched out inside a call from them cannot be resumed.
        if (FunctionIndex::isEntrySlice(function) && owners.find(function.start) != nullptr) {
            continue;
        }

        const auto decodedIt = m_decoded.find(function.start);
        if (decodedIt == m_decoded.end()) {
            continue;
        }

        const CodeGenerator::AnalysisResult analysis =
            analyzer.collectInternalBranchTargets(function, decodedIt->second, owners);

        auto& ownTargets = targetsByOwner[function.start];
        ownTargets.insert(ownTargets.end(), analysis.resumeEntryPoints.begin(), analysis.resumeEntryPoints.end());
        ownTargets.insert(ownTargets.end(), analysis.indirectFallbackEntryPoints.begin(),
                          analysis.indirectFallbackEntryPoints.end());

        for (uint32_t jumpAddress : analysis.unresolvedIndirectJumps) {
            unresolvedJumps.emplace_back(jumpAddress, function.name);
        }

        for (uint32_t target : analysis.externalEntryPoints) {
            const Function* owner = findDecodedOwner(owners, m_decoded, target);
            if (owner && owner->start != target) {
                targetsByOwner[owner->start].push_back(target);
            }
        }
    }
}
} // namespace ps2recomp
