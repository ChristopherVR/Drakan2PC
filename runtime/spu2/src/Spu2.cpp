// SPU2 register model + libsd layer.
// ADSR / volume slide: port of PCSX2 SPU2 ADSR.cpp (GPL-3.0-or-later, PCSX2 Dev Team).
// libsd-level behaviour (init sequence, register encodings, effect attributes) follows ps2sdk's freesd
// (iop/sound/libsd/src, AFL-2.0). Note2Pitch/Pitch2Note are an independent rewrite; the reverb presets themselves are
// read from the player's own LIBSD.IRX at runtime (LibsdPresets.cpp).
#include "spu2/Spu2.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace spu2 {

// ------------------------------------------------------------------------------------------------
//  Volume slide / ADSR
// ------------------------------------------------------------------------------------------------
void VolSlide::set(u16 v) {
    reg = v;
    if (!(v & 0x8000))
        value = (s16)(u16)(v << 1); // direct mode: 15-bit signed, shifted into 16 bit
}

void VolSlide::update() {
    if (!(reg & 0x8000))
        return; // sweep disabled
    const int Step = reg & 3, Shift = (reg >> 2) & 0x1f;
    const bool Phase = (reg >> 12) & 1, Decr = (reg >> 13) & 1, Exp = (reg >> 14) & 1;
    s32 step_size = 7 - Step;
    if (Decr)
        step_size = ~step_size;
    u32 counter_inc = 0x8000 >> std::max(0, Shift - 11);
    s32 level_inc = step_size << std::max(0, 11 - Shift);
    if (Exp) {
        if (!Decr && value > 0x6000)
            counter_inc >>= 2;
        if (Decr)
            level_inc = (s16)((level_inc * value) >> 15);
    }
    if (Step != 3 && Shift != 0x1f)
        counter_inc = std::max<u32>(1, counter_inc);
    counter += counter_inc;
    if (!(Exp && Decr))
        level_inc = Phase ? -level_inc : level_inc;
    if (counter >= 0x8000) {
        counter = 0;
        if (!Decr) {
            value = std::clamp<s32>(value + level_inc, INT16_MIN, INT16_MAX);
        } else {
            s32 low = Phase ? INT16_MIN : 0, high = Phase ? 0 : INT16_MAX;
            if (Exp) {
                low = 0;
                high = INT16_MAX;
            }
            value = std::clamp<s32>(value + level_inc, low, high);
        }
    }
}

void Adsr::updateCache() {
    const u32 sustainLevel = reg1 & 0xf, decayShift = (reg1 >> 4) & 0xf, attackStep = (reg1 >> 8) & 3,
              attackShift = (reg1 >> 10) & 0x1f, attackMode = (reg1 >> 15) & 1;
    const u32 releaseShift = reg2 & 0x1f, releaseMode = (reg2 >> 5) & 1, sustainStep = (reg2 >> 6) & 3,
              sustainShift = (reg2 >> 8) & 0x1f, sustainDir = (reg2 >> 14) & 1, sustainMode = (reg2 >> 15) & 1;
    cached[Attack] = {false, attackMode != 0, (int)attackShift, (int)(7 - attackStep), 0x7fff};
    cached[Decay] = {true, true, (int)decayShift, -8, (s32)((sustainLevel + 1) << 11)};
    int sstep = (int)(7 - sustainStep);
    if (sustainDir)
        sstep = ~sstep;
    cached[Sustain] = {sustainDir != 0, sustainMode != 0, (int)sustainShift, sstep, 0};
    cached[Release] = {true, releaseMode != 0, (int)releaseShift, -8, 0};
}

bool Adsr::calculate() {
    const Cached& p = cached[phase];
    u32 counter_inc = 0x8000 >> std::max(0, p.shift - 11);
    s32 level_inc = p.step << std::max(0, 11 - p.shift);
    if (p.exp) {
        if (!p.decr && value > 0x6000)
            counter_inc >>= 2;
        if (p.decr)
            level_inc = (s16)((level_inc * value) >> 15);
    }
    counter_inc = std::max<u32>(1, counter_inc);
    counter += counter_inc;
    if (counter >= 0x8000) {
        counter = 0;
        value = std::clamp<s32>(value + level_inc, 0, INT16_MAX);
    }
    if (phase == Sustain)
        return value != 0;
    if ((!p.decr && value >= p.target) || (p.decr && value <= p.target))
        phase++;
    if (phase > Release)
        return false;
    return true;
}

// ------------------------------------------------------------------------------------------------
//  construction / queue
// ------------------------------------------------------------------------------------------------
Spu2::Spu2() : ram_(kRamWords, 0) {
    for (int c = 0; c < 2; ++c) {
        Core& co = cores_[c];
        co.mmix = c ? 0xFFC : 0xFF0;
        co.esa = c ? 0xFFFF8 : 0xEFFF8;
        co.eea = c ? 0xFFFFF : 0xEFFFF;
        co.extVolL = co.extVolR = c ? 0x7fff : 0;
        co.noiseOut = 0;
    }
    effectAddr_ = {0, 0};
}
Spu2::~Spu2() = default;

void Spu2::enqueue(Cmd::T t, u16 entry, u32 value) {
    std::lock_guard<std::mutex> g(qm_);
    pending_.push_back({t, entry, value});
}

void Spu2::drainLocked() {
    std::vector<Cmd> tmp;
    {
        std::lock_guard<std::mutex> g(qm_);
        tmp.swap(pending_);
    }
    for (const Cmd& c : tmp)
        applyLocked(c);
}

void Spu2::applyLocked(const Cmd& c) {
    switch (c.t) {
        case Cmd::Param:
            doSetParam(c.entry, (u16)c.value);
            break;
        case Cmd::Switch:
            doSetSwitch(c.entry, c.value);
            break;
        case Cmd::Addr:
            doSetAddr(c.entry, c.value);
            break;
        case Cmd::Attr:
            doSetCoreAttr(c.entry, (u16)c.value);
            break;
    }
}

void Spu2::pushEvent(Event::Kind k, u32 arg) {
    {
        std::lock_guard<std::mutex> g(em_);
        if (events_.size() > 8192)
            events_.pop_front();
        events_.push_back({k, arg});
    }
    if (notifyFn_)
        notifyFn_(notifyUser_);
}

bool Spu2::popEvents(std::vector<Event>& out) {
    std::lock_guard<std::mutex> g(em_);
    if (events_.empty())
        return false;
    while (!events_.empty()) {
        out.push_back(events_.front());
        events_.pop_front();
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
//  ISpu2 entry points
// ------------------------------------------------------------------------------------------------
void Spu2::setParam(u16 e, u16 v) {
    enqueue(Cmd::Param, e, v);
}
void Spu2::setSwitch(u16 e, u32 v) {
    enqueue(Cmd::Switch, e, v);
}
void Spu2::setAddr(u16 e, u32 v) {
    enqueue(Cmd::Addr, e, v);
}
void Spu2::setCoreAttr(u16 e, u16 v) {
    enqueue(Cmd::Attr, e, v);
}

u16 Spu2::getParam(u16 e) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    return doGetParam(e);
}
u32 Spu2::getSwitch(u16 e) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    return doGetSwitch(e);
}
u32 Spu2::getAddr(u16 e) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    return doGetAddr(e);
}
u16 Spu2::getCoreAttr(u16 e) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    return doGetCoreAttr(e);
}

void Spu2::ramWriteLocked(u32 a, const void* src, u32 n) {
    a &= kRamBytes - 1;
    const u8* s = (const u8*)src;
    u8* base = (u8*)ram_.data();
    u32 first = std::min(n, kRamBytes - a);
    memcpy(base + a, s, first);
    if (first < n)
        memcpy(base, s + first, std::min(n - first, kRamBytes));
    // address-match IRQ for DMA/CPU writes into [a, a+n)
    for (int c = 0; c < 2; ++c) {
        if (!cores_[c].irqEnable)
            continue;
        const u32 w = cores_[c].irqA, wa = a >> 1, wn = (n + 1) >> 1;
        if (w >= wa && w < wa + wn)
            raiseIrq(c);
    }
}

void Spu2::ramWrite(u32 a, const void* src, u32 n) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    ramWriteLocked(a, src, n);
}
void Spu2::ramRead(u32 a, void* dst, u32 n) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    a &= kRamBytes - 1;
    u8* d = (u8*)dst;
    const u8* base = (const u8*)ram_.data();
    u32 first = std::min(n, kRamBytes - a);
    memcpy(d, base + a, first);
    if (first < n)
        memcpy(d + first, base, std::min(n - first, kRamBytes));
}

// ------------------------------------------------------------------------------------------------
//  libsd register encodings (state lock held)
// ------------------------------------------------------------------------------------------------
void Spu2::doSetParam(u16 entry, u16 value) {
    const u32 idx = entry >> 8;
    const int c = entry & 1;
    Core& co = cores_[c];
    if (idx <= 7) {
        const u32 v = (entry & 0x3E) >> 1;
        if (v >= kVoices)
            return;
        Voice& vc = co.voices[v];
        switch (idx) {
            case 0:
                vc.volL.set(value);
                break;
            case 1:
                vc.volR.set(value);
                break;
            case 2:
                vc.pitch = value;
                break;
            case 3:
                vc.adsr.reg1 = value;
                vc.adsr.updateCache();
                break;
            case 4:
                vc.adsr.reg2 = value;
                vc.adsr.updateCache();
                break;
            case 5:
                vc.adsr.value = value & 0x7fff;
                break;
            default:
                break; // VOLXL/VOLXR: read only
        }
        return;
    }
    switch (idx) {
        case 0x08:
            co.mmix = value & (c == 0 ? 0xFF0 : 0xFFF);
            break;
        case 0x09:
            co.masterL.set(value);
            break;
        case 0x0A:
            co.masterR.set(value);
            break;
        case 0x0B:
            co.fxVolL = (s16)value;
            break;
        case 0x0C:
            co.fxVolR = (s16)value;
            break;
        case 0x0D:
            co.extVolL = (s16)value;
            break;
        case 0x0E:
            co.extVolR = (s16)value;
            break;
        case 0x0F:
            co.inpVolL = (s16)value;
            break;
        case 0x10:
            co.inpVolR = (s16)value;
            break;
        case 0x11:
        case 0x12:
            break; // MVOLX: read only
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
            // SetParam on a switch register writes the low 16 voices (HI halfword in libsd naming)
            doSetSwitch(entry, (doGetSwitch(entry) & 0xFF0000) | value);
            break;
        default:
            hle::logf("spu2: SetParam unsupported entry %04x value %04x", entry, value);
    }
}

u16 Spu2::doGetParam(u16 entry) {
    const u32 idx = entry >> 8;
    const int c = entry & 1;
    Core& co = cores_[c];
    if (idx <= 7) {
        const u32 v = (entry & 0x3E) >> 1;
        if (v >= kVoices)
            return 0;
        Voice& vc = co.voices[v];
        switch (idx) {
            case 0:
                return vc.volL.reg;
            case 1:
                return vc.volR.reg;
            case 2:
                return vc.pitch;
            case 3:
                return vc.adsr.reg1;
            case 4:
                return vc.adsr.reg2;
            case 5:
                return (u16)vc.adsr.value;
            case 6:
                return (u16)vc.volL.value;
            case 7:
                return (u16)vc.volR.value;
        }
    }
    switch (idx) {
        case 0x08:
            return co.mmix;
        case 0x09:
            return co.masterL.reg;
        case 0x0A:
            return co.masterR.reg;
        case 0x0B:
            return (u16)co.fxVolL;
        case 0x0C:
            return (u16)co.fxVolR;
        case 0x0D:
            return (u16)co.extVolL;
        case 0x0E:
            return (u16)co.extVolR;
        case 0x0F:
            return (u16)co.inpVolL;
        case 0x10:
            return (u16)co.inpVolR;
        case 0x11:
            return (u16)co.masterL.value;
        case 0x12:
            return (u16)co.masterR.value;
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
            return (u16)doGetSwitch(entry);
        default:
            return 0;
    }
}

void Spu2::doSetSwitch(u16 entry, u32 value) {
    const u32 idx = entry >> 8;
    Core& co = cores_[entry & 1];
    value &= 0xFFFFFF;
    switch (idx) {
        case 0x13:
            co.pmon = value & ~1u;
            for (u32 v = 0; v < kVoices; ++v)
                co.voices[v].modulated = (co.pmon >> v) & 1;
            break;
        case 0x14:
            co.non = value;
            for (u32 v = 0; v < kVoices; ++v)
                co.voices[v].noise = (value >> v) & 1;
            break;
        case 0x15:
            co.keyOn |= value;
            break;
        case 0x16:
            co.keyOff |= value;
            break;
        case 0x17:
            co.endx = 0;
            break; // any write clears ENDX
        case 0x18:
            co.vmix[0] = value;
            break; // VMIXL  (dry L)
        case 0x19:
            co.vmix[1] = value;
            break; // VMIXEL (wet L)
        case 0x1A:
            co.vmix[2] = value;
            break; // VMIXR  (dry R)
        case 0x1B:
            co.vmix[3] = value;
            break; // VMIXER (wet R)
        default:
            hle::logf("spu2: SetSwitch unsupported entry %04x value %08x", entry, value);
    }
}

u32 Spu2::doGetSwitch(u16 entry) {
    const u32 idx = entry >> 8;
    Core& co = cores_[entry & 1];
    switch (idx) {
        case 0x13:
            return co.pmon;
        case 0x14:
            return co.non;
        case 0x15:
            return co.keyOn;
        case 0x16:
            return co.keyOff;
        case 0x17:
            return co.endx & 0xFFFFFF;
        case 0x18:
            return co.vmix[0];
        case 0x19:
            return co.vmix[1];
        case 0x1A:
            return co.vmix[2];
        case 0x1B:
            return co.vmix[3];
        default:
            return 0;
    }
}

// libsd stores byte address >>1 (16-bit word index); lo word masked to 8-word (16 byte) alignment,
// hi nibble = bits 16..19.
static inline u32 wordFromByteAddr(u32 v) {
    return (((v >> 17) & 0xF) << 16) | ((v >> 1) & 0xFFF8);
}

void Spu2::doSetAddr(u16 entry, u32 value) {
    const u32 idx = entry >> 8;
    Core& co = cores_[entry & 1];
    const u32 v = (entry & 0x3E) >> 1;
    switch (idx) {
        case 0x1C:
            co.esa = wordFromByteAddr(value);
            break;
        case 0x1D:
            co.eea = (((value >> 17) & 0xF) << 16) | 0xFFFF;
            break; // only the upper nibble is writable
        case 0x1E:
            co.tsa = wordFromByteAddr(value);
            break;
        case 0x1F:
            co.irqA = wordFromByteAddr(value);
            break;
        case 0x20:
            if (v < kVoices)
                co.voices[v].startA = wordFromByteAddr(value);
            break;
        case 0x21:
            if (v < kVoices) {
                co.voices[v].loopStartA = wordFromByteAddr(value);
                co.voices[v].loopMode = true;
            }
            break;
        case 0x22:
            if (v < kVoices)
                co.voices[v].nextA = wordFromByteAddr(value) | 1;
            break;
        default:
            hle::logf("spu2: SetAddr unsupported entry %04x value %08x", entry, value);
    }
}

u32 Spu2::doGetAddr(u16 entry) {
    const u32 idx = entry >> 8;
    Core& co = cores_[entry & 1];
    const u32 v = (entry & 0x3E) >> 1;
    switch (idx) {
        case 0x1C:
            return co.esa << 1;
        case 0x1D:
            return ((co.eea >> 16) << 17) | 0x1FFFF;
        case 0x1E:
            return co.tsa << 1;
        case 0x1F:
            return co.irqA << 1;
        case 0x20:
            return v < kVoices ? co.voices[v].startA << 1 : 0;
        case 0x21:
            return v < kVoices ? co.voices[v].loopStartA << 1 : 0;
        case 0x22:
            return v < kVoices ? co.voices[v].nextA << 1 : 0;
        default:
            return 0;
    }
}

void Spu2::writeCoreAttr(int core, u16 v) {
    Core& co = cores_[core];
    co.attr = v;
    co.fxEnable = (v & SD_ENABLE_EFFECTS) != 0;
    co.irqEnable = (v & SD_ENABLE_IRQ) != 0;
    co.noiseClk = (v >> 8) & 0x3f;
}

static const u8 kCoreAttrShifts[4] = {7, 6, 14, 8};

void Spu2::doSetCoreAttr(u16 entry, u16 value) {
    const int c = entry & 1;
    Core& co = cores_[c];
    switch (entry & ~1) {
        case SD_CORE_NOISE_CLK:
            writeCoreAttr(c, (u16)((co.attr & ~0x3F00) | ((value & 0x3F) << 8)));
            break;
        case SD_CORE_SPDIF_MODE:
            spdif_ = value;
            break;
        case SD_CORE_EFFECT_ENABLE:
        case SD_CORE_IRQ_ENABLE:
        case SD_CORE_MUTE_ENABLE: {
            const u8 sh = kCoreAttrShifts[(entry >> 1) - 1];
            u16 a = co.attr;
            a &= ~(1 << sh);
            a |= (value & 1) << sh;
            writeCoreAttr(c, a);
        } break;
        default:
            hle::logf("spu2: SetCoreAttr unsupported entry %04x", entry);
    }
}

u16 Spu2::doGetCoreAttr(u16 entry) {
    const int c = entry & 1;
    Core& co = cores_[c];
    switch (entry & ~1) {
        case SD_CORE_NOISE_CLK:
            return (co.attr >> 8) & 0x3F;
        case SD_CORE_SPDIF_MODE:
            return spdif_;
        case SD_CORE_EFFECT_ENABLE:
        case SD_CORE_IRQ_ENABLE:
        case SD_CORE_MUTE_ENABLE:
            return (co.attr >> kCoreAttrShifts[(entry >> 1) - 1]) & 1;
        default:
            return 0;
    }
}

// ------------------------------------------------------------------------------------------------
//  libsd API
// ------------------------------------------------------------------------------------------------
int Spu2::sdInit(int flag) {
    std::lock_guard<std::mutex> g(m_);
    {
        std::lock_guard<std::mutex> q(qm_);
        pending_.clear();
    }
    flag &= 1;
    // Reset(): effect attrs, intr handler state, default effect area
    for (int c = 0; c < 2; ++c) {
        effectAttr_[c] = SdEffectAttr{c, 0, 0, 0, 0, 0};
        cores_[c].transIntrEnabled = false;
        cores_[c].adma = AdmaState{};
    }
    spu2IntrEnabled_ = false;
    if (flag == 0) {
        for (int c = 0; c < 2; ++c) {
            writeCoreAttr(c, SD_SPU2_ON);
            cores_[c].masterL.set(0);
            cores_[c].masterR.set(0);
            cores_[c].keyOff = 0xFFFFFF;
            cores_[c].eea = (c ? 0xFu : 0xEu) << 16 | 0xFFFF; // InitCoreVolume value, applied early for GetEEA
        }
        cores_[1].pmon = 0;
        cores_[1].non = 0;
        for (int c = 0; c < 2; ++c) {
            for (auto& v : cores_[c].voices) {
                v.modulated = v.noise = false;
            }
        }
    }
    // InitVoices(): silent looping ADPCM block at 0x5000
    static const u16 initData[16] = {0x707, 0x707, 0x707, 0x707, 0x707, 0x707, 0x707, 0x707, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(&ram_[0x5000 >> 1], initData, sizeof initData);
    for (int c = 0; c < 2; ++c) {
        Core& co = cores_[c];
        for (u32 v = 0; v < kVoices; ++v) {
            Voice& vc = co.voices[v];
            vc.volL.set(0);
            vc.volR.set(0);
            vc.pitch = 0x3FFF;
            vc.adsr.reg1 = vc.adsr.reg2 = 0;
            vc.adsr.updateCache();
            vc.startA = 0x5000 >> 1;
            vc.loopStartA = vc.startA;
            vc.stop();
            vc.nextA = vc.startA | 1;
            vc.sp = 0;
            vc.decR = vc.decW = 0;
            vc.blkValid = false;
        }
        co.keyOn = co.keyOff = 0;
        co.endx = 0;
    }
    // InitCoreVolume()
    for (int c = 0; c < 2; ++c) {
        Core& co = cores_[c];
        writeCoreAttr(c, (u16)(SD_SPU2_ON | (flag ? SD_ENABLE_EFFECTS : 0) | SD_MUTE | (c ? SD_ENABLE_EX_INPUT : 0)));
        for (int k = 0; k < 4; ++k)
            co.vmix[k] = 0xFFFFFF;
        co.mmix = c ? 0xFFC : 0xFF0;
        if (flag == 0) {
            co.masterL.set(0);
            co.masterR.set(0);
            co.fxVolL = co.fxVolR = 0;
            co.eea = (c ? 0xFu : 0xEu) << 16 | 0xFFFF;
        }
        co.extVolL = co.extVolR = c ? 0x7FFF : 0;
        co.inpVolL = co.inpVolR = 0;
    }
    if (flag == 0) {
        for (int c = 0; c < 2; ++c) {
            const u32 eeaBytes = ((cores_[c].eea >> 16) << 17) | 0x1FFFF;
            effectAddr_[c] = eeaBytes - ((1u << 4) - 2);
            cores_[c].esa = (((effectAddr_[c] >> 17) & 0xF) << 16) | ((effectAddr_[c] >> 1) & 0xFFFF);
        }
    }
    return 0;
}

int Spu2::procBatch(const SdBatch* b, u32* rets, u32 num) {
    u32 loop;
    for (loop = 0; loop < num; ++loop) {
        u32 ret = 0;
        switch (b[loop].func) {
            case SD_BATCH_SETPARAM:
                setParam(b[loop].entry, (u16)b[loop].value);
                break;
            case SD_BATCH_GETPARAM:
                ret = getParam(b[loop].entry);
                break;
            case SD_BATCH_SETSWITCH:
                setSwitch(b[loop].entry, b[loop].value);
                break;
            case SD_BATCH_GETSWITCH:
                ret = getSwitch(b[loop].entry);
                break;
            case SD_BATCH_SETADDR:
                setAddr(b[loop].entry, b[loop].value);
                break;
            case SD_BATCH_GETADDR:
                ret = getAddr(b[loop].entry);
                break;
            case SD_BATCH_SETCORE:
                setCoreAttr(b[loop].entry, (u16)b[loop].value);
                break;
            case SD_BATCH_GETCORE:
                ret = getCoreAttr(b[loop].entry);
                break;
            case SD_BATCH_WRITEIOP:
                hle::logf("spu2: BATCH_WRITEIOP ignored (iop addr %08x)", b[loop].value);
                break;
            case SD_BATCH_WRITEEE: {
                u32 d = b[loop].entry;
                if (eeWriter_)
                    eeWriter_(b[loop].value, &d, 4);
            } break;
            case SD_BATCH_EERETURN:
                if (eeWriter_ && rets)
                    eeWriter_(b[loop].value, rets, b[loop].entry);
                break;
            default:
                return -1 - (int)loop;
        }
        if (rets)
            rets[loop] = ret;
    }
    return (int)loop;
}

int Spu2::procBatchEx(const SdBatch* b, u32* rets, u32 num, u32 voice) {
    int cmd_count = 0;
    for (u32 loop = 0; loop < num; ++loop) {
        u32 ret = 0;
        const bool multi = (b[loop].entry & 0x3E) == 0x3E; // voice field all-ones => apply to the voices in `voice`
        switch (b[loop].func) {
            case SD_BATCH_SETPARAM:
                if (!multi)
                    setParam(b[loop].entry, (u16)b[loop].value);
                else {
                    for (int v = 0; v < 24; ++v)
                        if (voice & (1u << v)) {
                            setParam((u16)((b[loop].entry & 0xFFC1) | (1 << (v + 1))), (u16)b[loop].value);
                            cmd_count++;
                        }
                    cmd_count--;
                }
                break;
            case SD_BATCH_GETPARAM:
                if (!multi)
                    ret = getParam(b[loop].entry);
                else {
                    for (int v = 0; v < 24; ++v) {
                        if (voice & (1u << v)) {
                            ret = getParam((u16)((b[loop].entry & 0xFFC1) | (1 << (v + 1))));
                            cmd_count++;
                        }
                        if (rets)
                            rets[cmd_count] = ret;
                    }
                    cmd_count--;
                }
                break;
            case SD_BATCH_SETSWITCH:
                setSwitch(b[loop].entry, b[loop].value);
                break;
            case SD_BATCH_GETSWITCH:
                ret = getSwitch(b[loop].entry);
                break;
            case SD_BATCH_SETADDR:
                if (!multi)
                    setAddr(b[loop].entry, b[loop].value);
                else
                    for (int v = 0; v < 24; ++v)
                        if (voice & (1u << v)) {
                            setAddr((u16)((b[loop].entry & 0xFFC1) | (1 << (v + 1))), b[loop].value);
                            cmd_count++;
                        }
                cmd_count--;
                break;
            case SD_BATCH_GETADDR:
                if (!multi)
                    ret = getAddr(b[loop].entry);
                else
                    for (int v = 0; v < 24; ++v) {
                        if (voice & (1u << v)) {
                            ret = getAddr((u16)((b[loop].entry & 0xFFC1) | (1 << (v + 1))));
                            cmd_count++;
                        }
                        if (rets)
                            rets[cmd_count] = ret;
                    }
                cmd_count--;
                break;
            case SD_BATCH_SETCORE:
                setCoreAttr(b[loop].entry, (u16)b[loop].value);
                break;
            case SD_BATCH_GETCORE:
                ret = getCoreAttr(b[loop].entry);
                break;
            case SD_BATCH_WRITEIOP:
                break;
            case SD_BATCH_WRITEEE: {
                u32 d = b[loop].entry;
                if (eeWriter_)
                    eeWriter_(b[loop].value, &d, 4);
            } break;
            case SD_BATCH_EERETURN:
                if (eeWriter_ && rets)
                    eeWriter_(b[loop].value, rets, b[loop].entry);
                break;
            default:
                return -1 - cmd_count;
        }
        if (rets)
            rets[cmd_count] = ret;
        cmd_count++;
    }
    return cmd_count;
}

int Spu2::voiceTrans(s16 chan, u16 mode, void* iopPtr, u32 spuAddr, u32 size) {
    const int c = chan & 1;
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    const bool io = (mode & SD_TRANS_MODE_IO) != 0;
    const u16 dir = mode & 3;
    if (dir == SD_TRANS_WRITE) {
        if (iopPtr)
            ramWriteLocked(spuAddr, iopPtr, size);
    } else if (dir == SD_TRANS_READ) {
        if (io)
            return 0; // libsd: IO-mode read is a no-op
        if (iopPtr) {
            u32 a = spuAddr & (kRamBytes - 1);
            u32 first = std::min(size, kRamBytes - a);
            memcpy(iopPtr, (u8*)ram_.data() + a, first);
            if (first < size)
                memcpy((u8*)iopPtr + first, ram_.data(), size - first);
        }
    } else {
        return -1;
    }
    cores_[c].tsa = ((spuAddr + size) >> 1) & 0xFFFFF;
    if (!io && cores_[c].transIntrEnabled)
        pushEvent(Event::TransDone, (u32)c);
    return io ? 0 : (int)size;
}

u32 Spu2::voiceTransStatus(s16, s16) {
    return 1;
} // transfers are synchronous

int Spu2::blockTrans(s16 chan, u16 mode, void* iopPtr, u32 size, u32 iopTag, void* startPtr) {
    const int c = chan & 1;
    const u16 dir = mode & 3;
    if (dir == SD_TRANS_STOP)
        return stopTrans(c);
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    Core& co = cores_[c];
    if (dir == SD_TRANS_READ) {
        hle::logf("spu2: BlockTrans READ (output capture) not implemented");
        return -1;
    }
    if (co.adma.active)
        return -1;
    AdmaState a;
    a.active = true;
    a.loop = (mode & SD_TRANS_LOOP) != 0;
    a.src = (const u8*)iopPtr;
    a.iopTag = iopTag;
    a.size = size;
    a.half = a.loop ? size / 2 : size;
    if (dir == SD_TRANS_WRITE_FROM && startPtr && iopPtr) {
        u32 off = (u32)((const u8*)startPtr - (const u8*)iopPtr);
        if (off & 1023)
            off = (off & ~1023u) + 1024;
        a.pos = off % std::max<u32>(a.size, 1);
    }
    co.adma = a;
    co.adma.ended = false;
    // MMIX input bits stay as configured by the game; ring is primed at the next ring wrap (outPos == 0)
    memset(&ram_[0x2000 + (c << 10)], 0, 0x400 * 2);
    return 0;
}

int Spu2::stopTrans(int channel) {
    std::lock_guard<std::mutex> g(m_);
    Core& co = cores_[channel & 1];
    u32 r = co.adma.active ? ((co.adma.curHalf << 24) | ((co.adma.iopTag + co.adma.pos) & 0xFFFFFF))
                           : (co.adma.curHalf << 24);
    co.adma.active = false;
    return (int)r;
}

u32 Spu2::blockTransStatus(s16 chan, s16) {
    std::lock_guard<std::mutex> g(m_);
    const AdmaState& a = cores_[chan & 1].adma;
    u32 addr = a.active ? (a.iopTag + a.pos) & 0xFFFFFF : 0;
    return (a.curHalf << 24) | addr;
}

bool Spu2::setTransIntrHandler(int ch, bool en) {
    std::lock_guard<std::mutex> g(m_);
    bool old = cores_[ch & 1].transIntrEnabled;
    cores_[ch & 1].transIntrEnabled = en;
    return old;
}
bool Spu2::setSpu2IntrHandler(bool en) {
    std::lock_guard<std::mutex> g(m_);
    bool old = spu2IntrEnabled_;
    spu2IntrEnabled_ = en;
    return old;
}

// ---- Note2Pitch / Pitch2Note ----
namespace {
// Q15 frequency ratios: kSemitone[k] = floor(2^15 * 2^(k/12)), kFineStep[j] = floor(2^15 * 2^(j/1536)), i.e. one entry
// per 1/128 of a semitone. libsd works from these truncated values, so rounding them would change its results.
struct Tables {
    u16 semitone[12];
    u16 fine[128];
    Tables() {
        for (int k = 0; k < 12; ++k)
            semitone[k] = (u16)std::floor(32768.0 * std::exp2(k / 12.0));
        for (int j = 0; j < 128; ++j)
            fine[j] = (u16)std::floor(32768.0 * std::exp2(j / 1536.0));
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}
s32 floorDiv(s32 a, s32 b) {
    s32 q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}
} // namespace

// Pitch register value (0x1000 = sample rate unchanged) for playing `note`+`fine` (fine in 1/128 semitones) of a
// sample recorded at `centerNote`. Results at or above two octaves over the centre stay at the two-octave scale,
// matching libsd.
u16 Spu2::note2Pitch(u16 centerNote, u16 centerFine, u16 note, s16 fine) {
    const Tables& t = tables();
    const s32 fineTotal = (s32)fine + (s32)centerFine;
    const s32 carry = floorDiv(fineTotal, 128);
    const s32 semis = (s32)note - (s32)centerNote + carry;
    const s32 fineStep = fineTotal - carry * 128;
    const s32 octave = floorDiv(semis, 12);
    const s32 step = semis - octave * 12;
    const u32 q14 = ((u32)t.semitone[step] * t.fine[fineStep]) >> 16; // 0x4000 = ratio 1.0
    const s32 shift = 2 - octave;
    if (shift <= 0)
        return (u16)q14;
    if (shift >= 16)
        return 0;
    return (u16)((q14 + (1u << (shift - 1))) >> shift);
}

// Inverse of note2Pitch: (note << 8) | (fine & 0x7E), relative to the given centre.
u16 Spu2::pitch2Note(u16 centerNote, u16 centerFine, u16 pitch) {
    const Tables& t = tables();
    pitch = std::min<u16>(pitch, 0x3FFF);
    int msb = 0;
    for (int b = 0; b < 14; ++b)
        if (pitch & (1u << b))
            msb = b;
    const u32 mantissa = ((u32)pitch << (15 - msb)) & 0xFFFF; // normalised to [0x8000, 0xFFFF]
    int step = 0;
    for (int k = 11; k >= 0; --k)
        if (mantissa >= t.semitone[k]) {
            step = k;
            break;
        }
    const u32 rest = (u32)((mantissa * 0x8000u) / t.semitone[step]) & 0xFFFF;
    int fineStep = 0;
    for (int j = 127; j >= 0; --j)
        if (rest >= t.fine[j]) {
            fineStep = j;
            break;
        }
    const u32 fineTotal = (u16)(centerFine + fineStep + 1);
    const s32 semis = (s32)centerNote + step + (msb - 12) * 12 + (s32)(fineTotal / 128);
    return (u16)((semis * 256 + (s32)(fineTotal & 0x7E)) & 0xFFFE);
}

// ---- effects ----
// The reverb presets are not built in: the host reads them from the disc's LIBSD.IRX (see LibsdPresets.cpp).
void Spu2::setEffectPresets(const EffectPresets& presets) {
    std::lock_guard<std::mutex> g(m_);
    presets_ = presets;
    havePresets_ = true;
}

bool Spu2::hasEffectPresets() {
    std::lock_guard<std::mutex> g(m_);
    return havePresets_;
}

void Spu2::setEffectData(int c, const u16* d) {
    Reverb& r = cores_[c].revb;
    r.APF1_SIZE = (u32)d[0] << 2;
    r.APF2_SIZE = (u32)d[1] << 2;
    r.IIR_VOL = (s16)d[2];
    r.COMB1_VOL = (s16)d[3];
    r.COMB2_VOL = (s16)d[4];
    r.COMB3_VOL = (s16)d[5];
    r.COMB4_VOL = (s16)d[6];
    r.WALL_VOL = (s16)d[7];
    r.APF1_VOL = (s16)d[8];
    r.APF2_VOL = (s16)d[9];
    auto A = [&](int i) { return (u32)d[i] << 2; };
    r.SAME_L_DST = A(10);
    r.SAME_R_DST = A(11);
    r.COMB1_L_SRC = A(12);
    r.COMB1_R_SRC = A(13);
    r.COMB2_L_SRC = A(14);
    r.COMB2_R_SRC = A(15);
    r.SAME_L_SRC = A(16);
    r.SAME_R_SRC = A(17);
    r.DIFF_L_DST = A(18);
    r.DIFF_R_DST = A(19);
    r.COMB3_L_SRC = A(20);
    r.COMB3_R_SRC = A(21);
    r.COMB4_L_SRC = A(22);
    r.COMB4_R_SRC = A(23);
    if (c == 0) {
        r.DIFF_L_SRC = A(24);
        r.DIFF_R_SRC = A(25);
    } else {
        r.DIFF_R_SRC = A(24);
        r.DIFF_L_SRC = A(25);
    } // PCSX2 swaps these two for core 1
    r.APF1_L_DST = A(26);
    r.APF1_R_DST = A(27);
    r.APF2_L_DST = A(28);
    r.APF2_R_DST = A(29);
    r.IN_COEF_L = (s16)d[30];
    r.IN_COEF_R = (s16)d[31];
}

namespace {
// Positions in EffectPreset::regs (the order of the SPU2 reverb registers).
enum ReverbReg : int {
    kRegApf1Size = 0,
    kRegApf2Size = 1,
    kRegWallVol = 7,
    kRegSameLDst = 10,
    kRegSameRDst = 11,
    kRegComb1LSrc = 12,
    kRegComb1RSrc = 13,
    kRegSameLSrc = 16,
    kRegSameRSrc = 17,
    kRegApf1LDst = 26,
    kRegApf1RDst = 27,
    kRegApf2LDst = 28,
    kRegApf2RDst = 29,
};

// First byte of a reverb work area of `bytes` bytes that ends at the core's EEA.
u32 workAreaStart(const Core& co, u32 bytes) {
    const u32 areaEnd = ((co.eea >> 16) << 17) | 0x1FFFF;
    return areaEnd - (bytes - 2);
}
} // namespace

int Spu2::setEffectAttr(int core, const SdEffectAttr& attr) {
    core &= 1;
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    const bool clearRam = (attr.mode & SD_EFFECT_MODE_CLEAR) != 0;
    const u32 mode = (u32)attr.mode & ~(u32)SD_EFFECT_MODE_CLEAR;
    if (mode > 9)
        return -1;
    const int previousMode = clearRam ? effectAttr_[core].mode : 0;
    if (!havePresets_ && mode != SD_EFFECT_MODE_OFF && !warnedNoPresets_) {
        warnedNoPresets_ = true;
        hle::logf("spu2: reverb mode %u requested but no LIBSD.IRX presets are loaded; reverb stays silent", mode);
    }
    EffectPreset preset = havePresets_ ? presets_[mode] : EffectPreset{};
    Core& co = cores_[core];

    // Echo and Delay are the two modes whose delay line (and, for Echo, feedback) follow the caller's attributes.
    const bool tunable = mode == SD_EFFECT_MODE_ECHO || mode == SD_EFFECT_MODE_DELAY;
    if (tunable) {
        auto& r = preset.regs;
        const u16 delayWords = (u16)((attr.delay + 1) * 32);
        r[kRegWallVol] = (u16)(attr.feedback * 258);
        r[kRegSameLDst] = (u16)(delayWords * 2 - r[kRegApf1Size]);
        r[kRegSameRDst] = (u16)(delayWords - r[kRegApf2Size]);
        r[kRegSameLSrc] = (u16)(r[kRegSameRSrc] + delayWords);
        r[kRegComb1LSrc] = (u16)(r[kRegComb1RSrc] + delayWords);
        r[kRegApf1RDst] = (u16)(r[kRegApf2RDst] + delayWords);
        r[kRegApf1LDst] = (u16)(r[kRegApf2LDst] + delayWords);
    }
    // What GetEffectAttr reports back: the caller's values, except delay/feedback which libsd pins per mode.
    SdEffectAttr stored = attr;
    stored.core = core;
    stored.delay = tunable ? 0x80 : 0;
    stored.feedback = mode == SD_EFFECT_MODE_ECHO ? 0x80 : 0;
    effectAttr_[core] = stored;

    const bool wasEnabled = (co.attr & SD_ENABLE_EFFECTS) != 0;
    if (wasEnabled) {
        writeCoreAttr(core, co.attr & ~SD_ENABLE_EFFECTS);
        if (clearRam)
            clearEffectLocked(core, previousMode);
    }
    co.fxVolL = attr.depth_L;
    co.fxVolR = attr.depth_R;
    setEffectData(core, preset.regs.data());
    if (preset.workAreaBlocks != 0) {
        effectAddr_[core] = workAreaStart(co, preset.workAreaBlocks * 16);
        co.esa = (((effectAddr_[core] >> 17) << 16) | ((effectAddr_[core] >> 1) & 0xFFFF)) & 0xFFFFF;
    }
    if (clearRam)
        clearEffectLocked(core, (int)mode);
    if (wasEnabled)
        writeCoreAttr(core, co.attr | SD_ENABLE_EFFECTS);
    return 0;
}

void Spu2::getEffectAttr(int core, SdEffectAttr& attr) {
    std::lock_guard<std::mutex> g(m_);
    attr = effectAttr_[core & 1];
}

void Spu2::clearEffectLocked(int core, int type) {
    if (type <= 0 || type > 9 || !havePresets_)
        return;
    const u32 bytes = presets_[type].workAreaBlocks * 16;
    const u32 start = workAreaStart(cores_[core & 1], bytes) & ~1u;
    if (bytes == 0 || start + bytes > kRamBytes)
        return;
    memset((u8*)ram_.data() + start, 0, bytes);
}

int Spu2::clearEffectWorkArea(int core, int /*channel*/, int effectMode) {
    if (effectMode > 9)
        return -1;
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    clearEffectLocked(core, effectMode);
    return 0;
}

} // namespace spu2
