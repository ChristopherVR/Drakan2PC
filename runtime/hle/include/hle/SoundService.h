// SID 0x1203 "Sound Command"
#pragma once
#include "Interfaces.h"

namespace hle {

struct SdBatch {
    u16 func;
    u16 entry;
    u32 value;
}; // 8 bytes, libsd sdBatch
static_assert(sizeof(SdBatch) == 8, "sdBatch size");
enum : u16 {
    SD_BATCH_SETPARAM = 1,
    SD_BATCH_SETSWITCH = 2,
    SD_BATCH_SETADDR = 3,
    SD_BATCH_SETCOREATTR = 4,
    SD_BATCH_WRITEIOP = 5,
    SD_BATCH_WRITEEE = 6,
    SD_BATCH_EERETURN = 7,
    SD_BATCH_GETPARAM = 0x11,
    SD_BATCH_GETSWITCH = 0x12,
    SD_BATCH_GETADDR = 0x13,
    SD_BATCH_GETCOREATTR = 0x14,
};

class SoundService {
public:
    SoundService(ISpu2& spu, IEeMemory& mem) : spu_(spu), mem_(mem) {}
    // Replies an s32 status (4 bytes) for fno 0/1/2; <0 on malformed call.
    int handle(u32 fno, const void* send, u32 sendSize, void* reply, u32 replyCap);

private:
    ISpu2& spu_;
    IEeMemory& mem_;
};

} // namespace hle
