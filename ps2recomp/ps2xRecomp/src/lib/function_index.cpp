#include "ps2recomp/function_index.h"

#include <algorithm>

namespace ps2recomp {
FunctionIndex::FunctionIndex(const std::vector<Function>& functions, Scope scope) {
    m_byStart.reserve(functions.size());
    for (const Function& function : functions) {
        if (!isRecompiled(function) || (scope == Scope::Owners && isEntrySlice(function))) {
            continue;
        }
        m_byStart.push_back(&function);
    }
    std::stable_sort(m_byStart.begin(), m_byStart.end(),
                     [](const Function* a, const Function* b) { return a->start < b->start; });

    m_maxEndUpTo.reserve(m_byStart.size());
    uint32_t maxEnd = 0;
    for (const Function* function : m_byStart) {
        maxEnd = std::max(maxEnd, function->end);
        m_maxEndUpTo.push_back(maxEnd);
    }
}
} // namespace ps2recomp
