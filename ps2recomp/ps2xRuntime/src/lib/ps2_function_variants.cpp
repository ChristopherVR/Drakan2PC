#include "ps2_function_variants.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace {
struct Registry {
    std::mutex mutex;
    std::vector<Ps2FunctionVariant> entries;
};

Registry& registry() {
    static Registry r; // function-local: safe from the generated static initialisers
    return r;
}
} // namespace

void ps2RegisterFunctionVariant(uint32_t start, uint32_t end, const char* name, PS2Runtime::RecompiledFunction fn) {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.entries.push_back({start, end, name, fn});
}

std::vector<Ps2FunctionVariant> ps2FunctionVariants(const char* name) {
    Registry& r = registry();
    std::vector<Ps2FunctionVariant> out;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        for (const Ps2FunctionVariant& v : r.entries)
            if (v.name && name && std::strcmp(v.name, name) == 0)
                out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

size_t ps2InstallFunctionVariant(PS2Runtime& rt, const Ps2FunctionVariant& variant,
                                 PS2Runtime::RecompiledFunction original) {
    if (!variant.fn || !original || variant.end <= variant.start || !rt.hasFunction(variant.start) ||
        rt.lookupFunction(variant.start) != original)
        return 0;
    size_t changed = 0;
    for (uint32_t pc = variant.start; pc < variant.end; pc += 4u) {
        if (rt.hasFunction(pc) && rt.lookupFunction(pc) == original) {
            rt.replaceFunction(pc, variant.fn);
            ++changed;
        }
    }
    return changed;
}
