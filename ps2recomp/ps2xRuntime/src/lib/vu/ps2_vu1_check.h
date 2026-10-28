#pragma once
#include <cstdint>

class GS;
class PS2Memory;
class VU1Interpreter;

// DRAKAN_VUCHECK=1: differential test of the VU1 interpreter against the reference copy in vu/ref (see
// ps2_vu1_check.cpp).
namespace vu1check {
bool enabled();
void run(bool resume, VU1Interpreter& vu, uint8_t* code, uint32_t codeSize, uint8_t* data, uint32_t dataSize, GS& gs,
         PS2Memory* memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t maxCycles);
void report();

// Offline benchmark / regression harness. DRAKAN_VU_RECORD=<file>[,skip[,count]] records VU1 runs of a live game
// session; DRAKAN_VU_BENCH=<file>[,reps] replays them on the optimised interpreter, checks every run against the
// reference interpreter, prints the speed and exits the process (see docs/reference/perf.md).
bool recording();
void record(bool resume, uint32_t startPC, uint32_t top, uint32_t itop, bool dEn, bool tEn, const VU1Interpreter& vu,
            const uint8_t* code, uint32_t codeSize, const uint8_t* data, uint32_t dataSize);
bool benchRequested();
void bench(PS2Memory& memory, GS& gs);
} // namespace vu1check
