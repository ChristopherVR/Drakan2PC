#pragma once
// Ahead-of-time specialised VU1 microcode (see ps2_vu1_aot_slice.inc).
//
// The game's VU1 microcode is a fixed image (analysis/vu/vu1_micromem.bin, 1523 instruction pairs). Every pair of that
// image has its own function that runs the interpreter's pair step (Vu1Step::step) with the decoded pair as a
// compile-time constant, so the compiler removes the decoding, the dispatch and the unused paths. VU1Interpreter::run()
// uses the table while the microcode memory still equals the compiled image and falls back to the generic loop
// otherwise (or for pcs outside the image). DRAKAN_VU_AOT=0 disables it.
#include "../ps2_vu1_step.h"

namespace Vu1Aot {
using Fn = bool (*)(VU1Interpreter&, Vu1Step::Ctx&);

extern const uint32_t kPairs;
extern Fn g_table[];
// Dispatch by pair index: the block function at the start of a block, the pair function everywhere else (g_table with
// DRAKAN_VU_BLOCKS=0).
extern Fn g_dispatch[];
// Ordinary block dispatch before native loop/packet replacement. Private
// batch executors use this table for dependency observation.
extern Fn g_blockDispatch[];

bool enabled();
bool blocksEnabled();
extern const bool g_blockStats; // DRAKAN_VU_STATS=1: optional block counters / fallback analysis
bool recoveryEnabled();         // DRAKAN_VU_RECOVERY=0: original block dispatch, for A/B tests
// True when `code` (VU1 micro memory) starts with the compiled image. Also registers the compiled pairs on first use.
bool imageMatches(const uint8_t* code, uint32_t size);
} // namespace Vu1Aot
