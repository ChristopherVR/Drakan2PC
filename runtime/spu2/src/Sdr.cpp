#include "spu2/Sdr.h"
#include <algorithm>
#include <cstring>

namespace spu2 {

int SdrServer::call(u32 fno, const void* data, u32 size, void* reply, u32 replyCap) {
    const u8* d = (const u8*)data;
    auto w = [&](int i) -> u32 {
        u32 v = 0;
        if ((u32)(i * 4 + 4) <= size)
            memcpy(&v, d + i * 4, 4);
        return v;
    };
    auto reply32 = [&](s32 v) -> int {
        if (replyCap >= 4) {
            memcpy(reply, &v, 4);
            return 4;
        }
        return 0;
    };
    auto ptr = [&](u32 addr, u32 n) -> void* { return (addr && resolve_) ? resolve_(addr, n) : nullptr; };
    s32 ret = 0;
    switch (fno & 0xFFF0) {
        case rSdInit:
            ret = spu_.sdInit((int)w(1));
            break;
        case rSdSetParam:
            spu_.setParam((u16)w(1), (u16)w(2));
            break;
        case rSdGetParam:
            ret = spu_.getParam((u16)w(1));
            break;
        case rSdSetSwitch:
            spu_.setSwitch((u16)w(1), w(2));
            break;
        case rSdGetSwitch:
            ret = (s32)spu_.getSwitch((u16)w(1));
            break;
        case rSdSetAddr:
            spu_.setAddr((u16)w(1), w(2));
            break;
        case rSdGetAddr:
            ret = (s32)spu_.getAddr((u16)w(1));
            break;
        case rSdSetCoreAttr:
            spu_.setCoreAttr((u16)w(1), (u16)w(2));
            break;
        case rSdGetCoreAttr:
            ret = spu_.getCoreAttr((u16)w(1));
            break;
        case rSdNote2Pitch:
            ret = Spu2::note2Pitch((u16)w(1), (u16)w(2), (u16)w(3), (s16)w(4));
            break;
        case rSdPitch2Note:
            ret = Spu2::pitch2Note((u16)w(1), (u16)w(2), (u16)w(3));
            break;
        case rSdProcBatch: {
            const u32 num = w(3);
            const SdBatch* b = (const SdBatch*)ptr(w(1), num * 8);
            u32* rets = (u32*)ptr(w(2), num * 4);
            ret = b ? spu_.procBatch(b, rets, num) : -1;
        } break;
        case rSdProcBatchEx: {
            const u32 num = w(3);
            const SdBatch* b = (const SdBatch*)ptr(w(1), num * 8);
            u32* rets = (u32*)ptr(w(2), num * 4);
            ret = b ? spu_.procBatchEx(b, rets, num, w(4)) : -1;
        } break;
        case rSdProcBatch2:
        case rSdProcBatchEx2: {
            // batch records are carried inline: header record at +0 (entry = count, value = voice mask), records at +8
            u16 cnt;
            memcpy(&cnt, d + 2, 2);
            u32 voice = w(1);
            if (size < 8 + (u32)cnt * 8u) {
                ret = -1;
                break;
            }
            std::vector<u32> rets(cnt + 1u, 0);
            ret = (fno & 0xFFF0) == rSdProcBatch2 ? spu_.procBatch((const SdBatch*)(d + 8), rets.data(), cnt)
                                                  : spu_.procBatchEx((const SdBatch*)(d + 8), rets.data(), cnt, voice);
            int n = reply32(ret);
            const u32 room = replyCap > 4 ? (replyCap - 4) / 4 : 0;
            const u32 cp = std::min<u32>(room, cnt);
            if (cp) {
                memcpy((u8*)reply + 4, rets.data(), cp * 4);
                n += (int)(cp * 4);
            }
            return n;
        }
        case rSdVoiceTrans: {
            const u32 sz = w(5);
            void* p = ptr(w(3), sz);
            ret = spu_.voiceTrans((s16)w(1), (u16)w(2), p, w(4), sz);
        } break;
        case rSdBlockTrans: {
            const u16 mode = (u16)w(2);
            const u32 sz = w(4);
            void* p = ptr(w(3), sz);
            void* start = (mode & 3) == SD_TRANS_WRITE_FROM ? ptr(w(5), 1) : nullptr;
            ret = spu_.blockTrans((s16)w(1), mode, p, sz, w(3), start);
        } break;
        case rSdVoiceTransStatus:
            ret = (s32)spu_.voiceTransStatus((s16)w(1), (s16)w(2));
            break;
        case rSdBlockTransStatus:
            ret = (s32)spu_.blockTransStatus((s16)w(1), (s16)w(2));
            break;
        case rSdSetEffectAttr:
        case rSdSetEffectMode:
        case rSdSetEffectModeParams: {
            SdEffectAttr a{};
            memcpy(&a, d, std::min<u32>(size, sizeof a));
            ret = spu_.setEffectAttr((int)(fno & 0xF), a);
        } break;
        case rSdGetEffectAttr: {
            spu_.getEffectAttr((int)(fno & 0xF), lastAttr_);
            const u32 n = std::min<u32>(replyCap, sizeof lastAttr_);
            memcpy(reply, &lastAttr_, n);
            return (int)n;
        }
        case rSdClearEffectWorkArea:
        case rSdCleanEffectWorkArea:
            ret = spu_.clearEffectWorkArea((int)w(1), (int)w(2), (int)w(3));
            break;
        case rSdSetTransIntrHandler:
            ret = spu_.setTransIntrHandler(w(1) ? 1 : 0, w(2) != 0);
            break;
        case rSdSetSpu2IntrHandler:
            ret = spu_.setSpu2IntrHandler(w(1) != 0);
            break;
        case rSdSetTransCallback:
        case rSdSetIRQCallback:
            break; // obsolete in sdrdrv 4.x
        case rSdStopTrans:
            ret = spu_.stopTrans((int)w(1));
            break;
        case rSdChangeThreadPriority:
            break;
        case SDR_CMD_CB_INIT:
            cbStarted_ = true;
            break;
        case SDR_CMD_CB_DEINIT:
            cbStarted_ = false;
            break;
        default:
            if ((fno & 0xFFF0) >= 0x9000 && (fno & 0xFFF0) <= 0x90F0)
                break; // user commands: none registered
            hle::logf("sdrdrv: unknown command %x", fno & 0xFFF0);
            break;
    }
    return reply32(ret);
}

void SdrServer::pollCallbacks(std::vector<SdrCallbackMsg>& out) {
    std::vector<Event> ev;
    if (!spu_.popEvents(ev))
        return;
    for (const Event& e : ev) {
        if (e.kind == Event::TransDone) {
            const u32 core = e.arg & 1;
            out.push_back({core ? SDR_CB_DMA1INT : SDR_CB_DMA0INT, SDR_CODE_TRANS0 + core, 0});
        } else {
            out.push_back({SDR_CB_IRQINT, SDR_CODE_SPU2IRQ, e.arg});
        }
    }
}

} // namespace spu2
