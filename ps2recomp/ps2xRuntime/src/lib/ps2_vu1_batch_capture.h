#pragma once
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/object_motion_scope.h"
#include "drakan_vu1_hook.h"
class PS2Memory;
namespace vu1batch {
extern thread_local bool captureActive;
bool captureGif(PS2Memory*, GifPathId, const uint8_t*, uint32_t, bool drain, bool directHl);
bool captureUpload(PS2Memory*, drakan_vu1::UploadHook, uint32_t, uint32_t, const uint8_t*);
bool captureMotion(const gsr::ObjectMotionScope&);
void command(PS2Memory*, uint32_t, uint32_t offset);
bool capturePairs(uint64_t);
bool captureMask(PS2Memory*, bool masked);
bool captureDrain(PS2Memory*);
bool trackVif(PS2Memory*);
void memoryRead(PS2Memory*, uint32_t byteAddress, uint32_t bytes);
void packetRead(PS2Memory*, uint32_t byteAddress, uint32_t bytes, uint32_t packetOffset, bool tag);
void memoryWrite(PS2Memory*, uint32_t qwordByteAddress, uint8_t laneBits);
void unsupportedTrace();
void scalarCommitted(bool q);
void runFinished(bool complete, bool idle);
} // namespace vu1batch
