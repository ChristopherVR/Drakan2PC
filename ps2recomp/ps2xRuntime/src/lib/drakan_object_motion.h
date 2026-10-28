#pragma once
#include "runtime/object_motion_tracker.h"
#include <atomic>
class PS2Runtime;
namespace drakan_object_motion {
// Sink runs on VIF executor (worker OR inline), ordered with the GIF it emits.
// Forward by value to the GS queue; never access the D3D device here.
using Sink = Cursor::Sink;
bool install(PS2Runtime&, Sink, void*);
void setEnabled(bool);
// Called while copying the DMAC chain at CHCR write, not at delayed dispatch.
Tracker::Chain beginChain(bool vif1 = true);
class VifJob {
    bool close = true;

public:
    explicit VifJob(const Job*);
    VifJob(const Job*, bool begin, bool end);
    ~VifJob();
};
void command(uint32_t byteOffset);
bool boundary(uint32_t byteOffset);
bool tracking();
void clipping(bool possible);
// Apply an immutable worker event on the ordered collector.
void replayCaptured(const Scope&);
Cursor captureCursor();
void restoreCursor(const Cursor&);
} // namespace drakan_object_motion
