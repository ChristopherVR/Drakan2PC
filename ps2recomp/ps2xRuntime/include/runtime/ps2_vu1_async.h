#ifndef PS2_VU1_ASYNC_H
#define PS2_VU1_ASYNC_H

// VU1 on its own host thread. On the real console VU1 (microprogram execution, XGKICK to the GIF) runs in parallel with
// the EE; here the VIF1 stream (UNPACK / MPG / MSCAL / MSCNT / DIRECT ...) of a DMA transfer is copied into a job at
// the CHCR write and executed in order by a worker thread (PS2Memory::runVif1Job). The EE thread only waits for it
// (sync()) when it is about to look at, or to change, something the worker owns: VU1 memory, the VIF1 / GIF registers,
// the GIF paths and the GS state.
//
// PS2X_VU1_THREAD=0 runs every job inline on the caller (the single-threaded behaviour, required where no extra thread
// may be started; see docs/reference/portability.md).
#include <atomic>
#include <cstdint>
#include <vector>
#include "runtime/object_motion_job_fwd.h"

class PS2Memory;

namespace vu1async {
struct Meta {
    void* ctx = nullptr; // R5900Context of the thread that started the transfer (VPU_STAT is reported there)
    bool dEn = false, tEn = false;
    drakan_object_motion::JobPtr objectMotion;
};

bool enabled();
extern std::atomic<uint32_t> g_pending; // jobs submitted and not finished
void syncSlow();
// Waits until every submitted job has finished (no-op on the worker itself).
inline void sync() {
    if (g_pending.load(std::memory_order_acquire) != 0)
        syncSlow();
}
void submit(PS2Memory* mem, std::vector<uint8_t>&& data, const Meta& meta);
const Meta* currentMeta(); // non-null while the worker runs a job
bool onWorker();
void shutdown();
void printStats();
} // namespace vu1async

#endif
