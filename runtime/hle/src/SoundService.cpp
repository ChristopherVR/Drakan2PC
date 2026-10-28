#include "hle/SoundService.h"
#include <cstring>
#include <vector>

namespace hle {

static u32 get32(const void* p) {
    u32 v;
    memcpy(&v, p, 4);
    return v;
}

int SoundService::handle(u32 fno, const void* send, u32 sendSize, void* reply, u32 replyCap) {
    auto rep = [&](s32 v) -> int {
        if (replyCap < 4)
            return 0;
        memcpy(reply, &v, 4);
        return 4;
    };
    const u8* in = (const u8*)send;
    switch (fno) {
        case 0: { // sdBatch records -> sceSdProcBatch semantics
            u32 n = sendSize / sizeof(SdBatch);
            for (u32 i = 0; i < n; ++i) {
                SdBatch b;
                memcpy(&b, in + i * sizeof b, sizeof b);
                switch (b.func) {
                    case SD_BATCH_SETPARAM:
                        spu_.setParam(b.entry, (u16)b.value);
                        break;
                    case SD_BATCH_SETSWITCH:
                        spu_.setSwitch(b.entry, b.value);
                        break;
                    case SD_BATCH_SETADDR:
                        spu_.setAddr(b.entry, b.value);
                        break;
                    case SD_BATCH_SETCOREATTR:
                        spu_.setCoreAttr(b.entry, (u16)b.value);
                        break;
                    // Read-back commands: results go to a 'rets' array the RSRCIOP caller does not supply (assumed), so
                    // discarded.
                    case SD_BATCH_GETPARAM:
                        (void)spu_.getParam(b.entry);
                        break;
                    case SD_BATCH_GETSWITCH:
                        (void)spu_.getSwitch(b.entry);
                        break;
                    case SD_BATCH_GETADDR:
                        (void)spu_.getAddr(b.entry);
                        break;
                    case SD_BATCH_GETCOREATTR:
                        (void)spu_.getCoreAttr(b.entry);
                        break;
                    // TODO: WRITEIOP/WRITEEE/EERETURN are IOP-memory/EE-return tricks; never expected from the game's
                    // ring.
                    default:
                        logf("sound: unhandled sdBatch func %u entry %04x value %08x", b.func, b.entry, b.value);
                        break;
                }
            }
            return rep(0);
        }
        case 1: { // {spuAddr, eeAddr, size}: SPU2 RAM -> EE memory (read-back)  [direction inferred, see doc 3.3]
            if (sendSize < 12)
                return -1;
            u32 spu = get32(in), ee = get32(in + 4), size = get32(in + 8);
            std::vector<u8> t(size);
            spu_.ramRead(spu, t.data(), size);
            if (!mem_.write(ee, t.data(), size)) {
                logf("sound fno1: bad EE addr %08x", ee);
                return rep(-1);
            }
            return rep(0);
        }
        case 2: { // {srcSpu, dstSpu, size}: SPU2 RAM -> SPU2 RAM copy
            if (sendSize < 12)
                return -1;
            u32 src = get32(in), dst = get32(in + 4), size = get32(in + 8);
            std::vector<u8> t(size);
            spu_.ramRead(src, t.data(), size);
            spu_.ramWrite(dst, t.data(), size);
            return rep(0);
        }
        default:
            logf("sound: unknown fno %u", fno);
            return rep(-1);
    }
}

} // namespace hle
