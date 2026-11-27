#pragma once

#include "ps2recomp/types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ps2recomp {
class CodeGenerator;

using DecodedFunctions = std::unordered_map<uint32_t, std::vector<Instruction>>;
using TargetsByOwner = std::unordered_map<uint32_t, std::vector<uint32_t>>;

// Finds the guest addresses that must be enterable from outside their function and decides how each is reached:
//  - a standalone entry function ("entry_<addr>") for a target no known function covers, or one that starts in the
//    middle of a function (sliced from the owner's decoded instructions, up to the next known start);
//  - a resume target of its owner function, registered in the function table under the owner's name, for addresses
//    the scheduler or another function re-enters inside an owner (after a call, a syscall, a loop back-edge, ...).
// Works on the recompiler's function list and decoded instructions in place.
class EntryDiscovery {
public:
    EntryDiscovery(std::vector<Function>& functions, DecodedFunctions& decoded, const std::vector<Section>& sections);

    // Repeats until nothing new is found: queues `seeds` (first pass only) and, with an `analyzer`, every external
    // branch target and resume point of the analysed functions that is not a function start yet, then adds an entry
    // function for each. A target inside a decoded function is sliced from it; any other target is decoded on its own
    // with `decodeStandalone`. Returns the number of entry functions added.
    size_t synthesizeEntryFunctions(CodeGenerator* analyzer, const std::function<bool(Function&)>& decodeStandalone,
                                    const std::unordered_set<uint32_t>& seeds = {});

    // Re-cuts every entry function from its owner up to the next recompiled function start (after new starts appeared).
    // Returns how many changed.
    size_t resliceEntryFunctions();

    // Adds each address of `entryAddresses` that is no function start but lies inside a decoded recompiled function to
    // that function's resume targets. Returns how many were added.
    static size_t collectInternalEntryTargets(const std::vector<Function>& functions, const DecodedFunctions& decoded,
                                              const std::unordered_set<uint32_t>& entryAddresses,
                                              TargetsByOwner& targetsByOwner);

    // Resume targets of every recompiled function (its own resume points and indirect-fallback labels, plus the
    // external branch targets other functions jump to inside it) appended to `targetsByOwner`, and the non-$ra JR sites
    // no jump table resolved, with the name of the function containing each.
    void collectResumeTargets(CodeGenerator& analyzer, TargetsByOwner& targetsByOwner,
                              std::vector<std::pair<uint32_t, std::string>>& unresolvedJumps) const;

private:
    bool isExecutableAddress(uint32_t address) const;

    std::vector<Function>& m_functions;
    DecodedFunctions& m_decoded;
    const std::vector<Section>& m_sections;
};
} // namespace ps2recomp
