#pragma once

#include "ps2recomp/types.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ps2recomp {
// Address -> containing function lookup over the recompiled functions of one function list. Functions are kept sorted
// by start together with a running maximum of their ends, so a lookup only walks back over functions that can still
// contain the address (ranges overlap: entry_* slices lie inside their owner). The index holds pointers into the list:
// rebuild it after the list changes.
class FunctionIndex {
public:
    enum class Scope {
        AllRecompiled, // every recompiled function, entry_* slices included
        Owners,        // recompiled functions that are not entry_* slices
    };

    FunctionIndex(const std::vector<Function>& functions, Scope scope);

    // The function in [start, end) of `address` with the highest start that `accept` takes; on equal starts the one
    // first in the original list. nullptr when there is none.
    template <typename Accept>
    const Function* find(uint32_t address, Accept&& accept) const {
        auto it = std::upper_bound(m_byStart.begin(), m_byStart.end(), address,
                                   [](uint32_t value, const Function* function) { return value < function->start; });
        for (size_t i = static_cast<size_t>(it - m_byStart.begin()); i-- > 0;) {
            if (m_maxEndUpTo[i] <= address) {
                return nullptr; // no function at or before i reaches the address
            }
            const Function* candidate = m_byStart[i];
            if (address >= candidate->end || !accept(*candidate)) {
                continue;
            }
            // Equal starts: the earliest in list order wins (the stable sort keeps it at the lowest index).
            while (i > 0 && m_byStart[i - 1]->start == candidate->start) {
                --i;
                if (address < m_byStart[i]->end && accept(*m_byStart[i])) {
                    candidate = m_byStart[i];
                }
            }
            return candidate;
        }
        return nullptr;
    }

    const Function* find(uint32_t address) const {
        return find(address, [](const Function&) { return true; });
    }

    static bool isEntrySlice(const Function& function) { return function.name.rfind("entry_", 0) == 0; }
    static bool isRecompiled(const Function& function) {
        return function.isRecompiled && !function.isStub && !function.isSkipped;
    }

private:
    std::vector<const Function*> m_byStart;
    std::vector<uint32_t> m_maxEndUpTo; // max end over m_byStart[0..i]
};
} // namespace ps2recomp
