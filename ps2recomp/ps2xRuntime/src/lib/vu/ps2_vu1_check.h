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
// While a recording is on, the VIF interpreter reports every lane an UNPACK writes into VU1 memory (bit 0..3 = x y
// z w of quadword `qw`); the writes between two runs go to <file>.writes, one list per run. A replay needs them to
// tell a rewritten lane from an untouched one when the value is the same.
void noteVifWrite(uint32_t qw, uint32_t lanes);
bool benchRequested();
void bench(PS2Memory& memory, GS& gs);

// DRAKAN_VERTEX_SHADOW=1 (diagnosis, vu/ps2_vu1_gpu_live.h): the game's VU1 executes every run; beside it the batch
// tracker decides on a memory of its own, and its records and the runs it lets through are compared with what the
// microprogram sent, like the replay's batch proof but on live play. shadowWrite: a lane the VIF or the EE wrote.
// One `[vertex-shadow]` line every 250,000 runs and from shadowFinish().
bool shadowRequested();
void shadowWrite(uint32_t qw, uint32_t lanes);
void shadowBefore(bool resume, uint32_t startPc, VU1Interpreter& vu, uint8_t* code, uint8_t* data, GS& gs,
                  PS2Memory* memory, uint32_t top, uint32_t itop, uint32_t families);
void shadowAfter(VU1Interpreter& vu, const uint8_t* data);
void shadowFinish();
} // namespace vu1check
