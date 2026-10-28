#pragma once
// Functional execution of the game's fixed VU1 microcode (aot/ps2_vu1_func.cpp).
//
// The exact engine (Vu1Step, aot/ blocks) models every cycle: pending register writes, the MAC/status/CLIP FIFO, the
// FDIV/EFU queues, store retirement and PATH1 progress. Only a few of those effects are observable by this program:
// Q and P values (no interlock), MAC and CLIP flag reads (4-cycle visibility), the old VI value seen by a branch right
// after a write, and the packets XGKICK sends. The functional engine executes the same pairs with the same arithmetic
// (Vu1Step::calculateFmac etc.) but replaces the timing model by one cycle per pair plus the FDIV/XGKICK stalls, which
// reproduces every timing-observable read of the image (analysis/scripts/vu_timing_reads.py). Status flags are not
// maintained (the image never reads them). DRAKAN_VU_FUNC=0 selects the exact engine.
#include "ps2_vu1_step.h"

namespace Vu1Func {
enum class Result { Finished, Stopped, Fallback };
bool enabled();
// Runs from v.m_state.pc while it is a block of the compiled image. Finished: E bit reached; Stopped: cycle budget
// exhausted; Fallback: reached code the engine does not cover (state is committed, the exact loop continues at pc).
Result run(VU1Interpreter& v, Vu1Step::Ctx& c);
// Entry check: the pc starts a block and no exact-engine pipeline is pending.
bool canEnter(const VU1Interpreter& v);
} // namespace Vu1Func
