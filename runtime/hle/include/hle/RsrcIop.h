// Facade: one object servicing all three RSRCIOP RPC servers.
#pragma once
#include "IsoFs.h"
#include "FileService.h"
#include "ResourceService.h"
#include "SoundService.h"

namespace hle {

class RsrcIop {
public:
    RsrcIop(IsoFs& fs, IEeMemory& mem, IEeRpcPoster& ee, ISpu2& spu)
        : file(fs, mem), res(file, mem, ee, spu), snd(spu, mem) {}

    static bool isRsrcSid(u32 sid) { return sid == SID_FILE || sid == SID_RESOURCE || sid == SID_SOUND; }

    // Entry point for the host's sceSifCallRpc: send/recv are host copies of the guest buffers.
    // Returns reply bytes written to `recv`, or <0 if sid is not an RSRCIOP server / call malformed.
    // SID_RESOURCE (async on the real IOP, no reply) returns 0 after completions were posted:
    // call it from a worker thread, not from the EE thread that issued the RPC.
    int call(u32 sid, u32 fno, const void* send, u32 sendSize, void* recv, u32 recvCap) {
        switch (sid) {
            case SID_FILE:
                return file.handle(fno, send, sendSize, recv, recvCap);
            case SID_RESOURCE:
                res.handle(fno, send, sendSize);
                return 0;
            case SID_SOUND:
                return snd.handle(fno, send, sendSize, recv, recvCap);
            default:
                return -1;
        }
    }

    FileService file;
    ResourceService res;
    SoundService snd;
};

} // namespace hle
