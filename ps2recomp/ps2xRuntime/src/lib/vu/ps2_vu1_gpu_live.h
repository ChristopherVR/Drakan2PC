#pragma once
// The vertex path at run time: what happens before a VU1 run starts, and what the VIF tells the batch tracker.
//
// Under the player's `cpu` (vpath::Eval::Microprogram) nothing here acts: the callers test one flag per run
// (gsr::vpath::vuAttention) and one per UNPACK (following()). Under `records` or `gpu`:
//   * the batch tracker (ps2_vu1_gpu_batch.h) decides for every MSCAL and MSCNT whether the microprogram runs; a run
//     it takes is not executed, and an index-list run goes to the renderer as one record in the PATH1 stream;
//   * the strip stage is handed over as before (ps2_vu1_gpu_program.h), unless it would read VU memory whose content
//     an earlier run that was not executed would have set;
//   * the VIF reports every lane an UNPACK writes into VU memory, and the EE's direct accesses are reported too.
// Which stage families may leave the microprogram is read from the control word for every run.
//
// Switching back to the microprogram at any run boundary: what was deferred is written to VU memory before the next
// run executes, and the tracker keeps following the runs and the writes (it takes nothing) for as long as a lane of
// VU memory is not what the microprogram would have left, so that a later switch to `records` starts from marks that
// are true. When every lane is the microprogram's again it stops, and the flag is cleared.
//
// DRAKAN_VERTEX_SHADOW=1 (diagnosis): the microprogram runs everything and draws; a second tracker on a memory of its
// own decides as it would under `records`, and every record it builds is evaluated on the CPU and compared with the
// packets the microprogram sent for that run (vu1check::shadow*, ps2_vu1_check.cpp).
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

class GS;
class PS2Memory;
class VU1Interpreter;

namespace vu1gpu::live {

extern std::atomic<bool> g_following;
// True while the VIF has to report its writes into VU1 data memory.
inline bool following() {
    return g_following.load(std::memory_order_relaxed);
}

struct Run {
    bool resume;        // MSCNT
    uint32_t startPc;   // MSCAL: the entry address
    uint32_t top, itop; // as the run will see them
    VU1Interpreter& vu; // the engine of `memory`
    uint8_t* code;      // VU1 micro memory
    uint8_t* data;      // VU1 data memory
    uint32_t dataSize;
    GS& gs;
    PS2Memory& memory;
};

enum class Before : uint32_t {
    Execute, // run the microprogram
    Skipped, // the run was taken: do not execute it (the pc is where it would have stopped)
    Shadow,  // run the microprogram, then call afterRun()
};
// Called before an MSCAL or MSCNT executes, when gsr::vpath::vuAttention() is set.
Before beforeRun(const Run& run);
void afterRun(const Run& run);

// An UNPACK the fast path of the VIF wrote (ps2_vif_unpack.h, same arguments): `memory` is the VU data memory it
// wrote into, vn the format's component count minus one.
void noteUnpack(const uint8_t* memory, uint32_t address, uint32_t count, uint32_t cl, uint32_t wl, uint32_t vn,
                bool masked, uint32_t mask, uint32_t mode);
// One quadword the slow path wrote: `lanes` took the EE's data (or ROW / COL), `unknown` a value made from what the
// lane held before.
void noteWrite(const uint8_t* memory, uint32_t qw, uint32_t lanes, uint32_t unknown = 0);
// A cheap clock for the cost of the reports (processor ticks where there are any, else nanoseconds), and the times
// of one followed UNPACK: the unpack itself and the report of its lanes (slow: the scalar path, one time for both).
inline uint64_t ticks() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}
void unpackTimes(uint64_t unpackTicks, uint64_t reportTicks, bool slow);
// The EE reads or writes VU1 data memory through its address space (the VU1 worker is idle: PS2Memory waits for it).
void directAccess(uint8_t* memory);
void directWrite(const uint8_t* memory, uint32_t offset, uint32_t bytes);

// Stage bits of the control word (gsr/vertex_path.h) to the tracker's family bits, and back.
uint32_t familiesOfStages(uint32_t stages);
uint32_t stagesOfFamilies(uint32_t families);

// One line with the tracker's own counters (empty while it never followed anything).
void countersText(char* text, size_t size);

} // namespace vu1gpu::live
