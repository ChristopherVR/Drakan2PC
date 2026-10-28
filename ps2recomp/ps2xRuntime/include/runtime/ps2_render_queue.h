#pragma once
#include <functional>
#include <vector>
#include <cstdint>

class PS2Memory;
namespace vu1async {
// PATH3 packets and renderer markers share the VIF1 FIFO. The EE owns
// register/DMA bookkeeping; the worker owns VIF1 state and GIF arbitration.
// sync()/shutdown() include all three kinds of work. Setting
// DRAKAN_RENDER_QUEUE=0 restores the synchronous PATH3/marker barriers.
bool renderQueueEnabled();
void submitPath3(PS2Memory* memory, std::vector<uint8_t>&& packet);
// Captures must own their data and keep renderer resources alive until
// execution. Tasks must not wait for the EE or for this same queue.
// Renderer tasks may not inspect or change VU/VIF state: geometry can be
// computed ahead while these immutable renderer records stay in FIFO order.
void submitRenderTask(std::function<void()> task);
} // namespace vu1async
