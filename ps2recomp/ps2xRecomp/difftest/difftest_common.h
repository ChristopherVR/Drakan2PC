// Shared by the generated difftest_cases_N.cpp files and difftest_run.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"

struct DtCase {
    const char* name;
    const char* shape;
    int nwords;
    uint32_t words[8];
    void (*fn)(uint8_t* __restrict, R5900Context* __restrict, PS2Runtime*);
};
extern const DtCase g_dtCases[];
extern const size_t g_dtCaseCount;
