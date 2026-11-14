#pragma once

// Function variants: the recompiler emits a second copy of a guest function with a named set of instruction patches
// (`[[variants]]` in configs/drakan.toml) and registers it here at static initialisation (register_functions.cpp).
// The originals stay in the dense function table; a runtime module may swap a variant in at start-up, so a patched
// guest behaviour is a setting instead of a rebuild.

#include "ps2_runtime.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct Ps2FunctionVariant {
    uint32_t start = 0; // guest function [start, end)
    uint32_t end = 0;
    const char* name = nullptr; // variant name from the config
    PS2Runtime::RecompiledFunction fn = nullptr;
};

// Called by the generated code only.
void ps2RegisterFunctionVariant(uint32_t start, uint32_t end, const char* name, PS2Runtime::RecompiledFunction fn);

// Every registered function of the variant `name`, sorted by start address.
std::vector<Ps2FunctionVariant> ps2FunctionVariants(const char* name);

// Points every dense-table slot in [start, end) that holds `original` (the entry and the resume labels of the guest
// function) at the variant. Returns the number of slots changed; 0 when the entry slot does not hold `original`
// (another module replaced it first), in which case nothing is changed.
size_t ps2InstallFunctionVariant(PS2Runtime& rt, const Ps2FunctionVariant& variant,
                                 PS2Runtime::RecompiledFunction original);
