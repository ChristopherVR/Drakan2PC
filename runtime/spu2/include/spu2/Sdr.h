// sdrdrv (SID 0x80000701) command dispatcher on top of spu2::Spu2. Mirrors ps2sdk iop/sound/sdrdrv sdd_com.c
// (argument layout: word0 = pointer to itself, word1.. = arguments, little endian u32 slots).
#pragma once
#include "spu2/Spu2.h"
#include <functional>
#include <vector>

namespace spu2 {

// Message for the EE callback server (SID 0x80000704): SdrEECBData {mode, voice_bit, status, opt, pad[12]}.
struct SdrCallbackMsg {
    u32 mode;     // SDR_CB_DMA0INT / DMA1INT / IRQINT bit flags (stock sdrdrv)
    u32 code;     // numeric code used by the game's EE dispatcher: 0xb DMA0, 0xc DMA1, 0xd SPU2 IRQ
    u32 voiceBit; // IRQ: core bit mask (1<<core)
};

class SdrServer {
public:
    // resolve(iopAddr, n) must return a host pointer to n bytes that the IOP address `iopAddr` denotes (the buffers
    // sceSdRemote DMA'd to the IOP, or - in a native port - the EE buffers). May be empty if only commands without
    // IOP pointers are used.
    using Resolver = std::function<void*(u32 iopAddr, u32 n)>;
    SdrServer(Spu2& spu, Resolver resolve) : spu_(spu), resolve_(std::move(resolve)) {}

    // Handles one RPC. `data` = RPC send buffer (size bytes). Writes the reply (always at least 4 bytes: s32 result;
    // for 0x8140 GetEffectAttr the 20-byte attr struct; for ProcBatch2/Ex2 result followed by rets) into reply.
    // Returns the number of reply bytes written.
    int call(u32 fno, const void* data, u32 size, void* reply, u32 replyCap);

    // Converts queued Spu2 events (per registered handlers) into EE callback messages. The host posts each as an RPC
    // to SID 0x80000704 (struct of 64 bytes, see SdrCallbackMsg).
    void pollCallbacks(std::vector<SdrCallbackMsg>& out);

    bool callbackThreadStarted() const { return cbStarted_; }

private:
    Spu2& spu_;
    Resolver resolve_;
    bool cbStarted_ = false;
    SdEffectAttr lastAttr_{};
};

} // namespace spu2
