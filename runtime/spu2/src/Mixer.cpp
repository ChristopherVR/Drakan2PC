// SPU2 sample generation: voices (ADPCM, gaussian interpolation, ADSR, noise, FM), core mixing, core0->core1
// routing, reverb, AutoDMA input. Structure and arithmetic follow PCSX2's SPU2 Mixer.cpp / Reverb.cpp /
// ReverbResample.cpp (GPL-3.0-or-later, PCSX2 Dev Team); gaussian table reconstructed from GPL-3.0+ PCSX2
// interpolate_table.h.
#include "spu2/Adpcm.h"
#include "spu2/Spu2.h"
#include <algorithm>
#include <cstring>

namespace spu2 {

#include "GaussTable.inc"

static constexpr u8 FLAG_LOOP_END = 1, FLAG_LOOP = 2, FLAG_LOOP_START = 4;

static inline s32 clampMix(s32 v) {
    return std::clamp<s32>(v, -0x8000, 0x7fff);
}
static inline s32 applyVol(s32 data, s32 vol) {
    return (vol * data) >> 15;
}

// interpolation coefficients per phase (oldest sample first), built once
struct InterpTable {
    s16 c[256][4];
    InterpTable() {
        for (int p = 0; p < 256; ++p) {
            c[p][0] = kGauss[0x0FF - p];
            c[p][1] = kGauss[0x1FF - p];
            c[p][2] = kGauss[0x100 + p];
            c[p][3] = kGauss[p];
        }
    }
};
static const InterpTable& interp() {
    static const InterpTable t;
    return t;
}

// ------------------------------------------------------------------------------------------------
//  IRQ helpers
// ------------------------------------------------------------------------------------------------
void Spu2::raiseIrq(int core) {
    // libsd's SPU2 interrupt handler clears the core's IRQ-enable bit and then calls the user handler
    cores_[core].attr &= ~SD_ENABLE_IRQ;
    cores_[core].irqEnable = false;
    if (spu2IntrEnabled_)
        pushEvent(Event::SpuIrq, 1u << core);
}

void Spu2::checkIrq(u32 wordAddr) {
    // both cores signal when an address is read, regardless of which core reads it
    for (int i = 0; i < 2; ++i)
        if (cores_[i].irqEnable && cores_[i].irqA == wordAddr)
            raiseIrq(i);
}

void Spu2::writeFast(u32 wordAddr, s32 v) {
    checkIrq(wordAddr);
    ram_[wordAddr & 0xFFFFF] = (u16)(s16)v;
}

// ------------------------------------------------------------------------------------------------
//  voice decoding
// ------------------------------------------------------------------------------------------------
void Spu2::incrementNextA(Voice& vc) {
    checkIrq(vc.nextA);
    vc.nextA = (vc.nextA + 1) & 0xFFFFF;
}

void Spu2::updateBlockHeader(Voice& vc) {
    for (int i = 0; i < 2; ++i)
        if (cores_[i].irqEnable && cores_[i].irqA == (vc.nextA & 0xFFFF8))
            raiseIrq(i);
    vc.loopFlags = (u8)(ram_[vc.nextA & 0xFFFF8] >> 8);
    if ((vc.loopFlags & FLAG_LOOP_START) && !vc.loopMode)
        vc.loopStartA = vc.nextA & 0xFFFF8;
}

void Spu2::getNextData(Voice& vc) {
    if (!vc.blkValid) {
        u8 block[16];
        const u32 a = vc.nextA & 0xFFFF8;
        memcpy(block, &ram_[a], 16); // block never straddles the end of RAM (8-word aligned)
        adpcmDecodeBlock(block, vc.prev1, vc.prev2, vc.blk);
        vc.blkValid = true;
    }
    const int sampleIdx = ((int)(vc.nextA % 8) - 1) * 4;
    for (int i = 0; i < 4; ++i)
        vc.fifo[(vc.decW + i) % 32] = vc.blk[sampleIdx + i];
}

void Spu2::decodeSamples(int c, int v) {
    Core& co = cores_[c];
    Voice& vc = co.voices[v];
    updateBlockHeader(vc);
    // when a voice starts at pitch 0 NAX quickly advances to SSA+5, i.e. the decode buffer holds ~12 samples
    if ((int)(vc.decW - vc.decR) > 12)
        return;
    if (vc.adsr.phase > Adsr::Stopped)
        getNextData(vc);
    vc.decW += 4;
    incrementNextA(vc);
    if ((vc.nextA & 7) == 0) {
        if (vc.loopFlags & FLAG_LOOP_END) {
            co.endx |= (1u << v);
            vc.nextA = vc.loopStartA;
            if (!(vc.loopFlags & FLAG_LOOP))
                vc.stop();
        }
        incrementNextA(vc);
        vc.blkValid = false;
    }
}

s32 Spu2::voiceValue(const Voice& vc) const {
    const int phase = (vc.sp & 0x0ff0) >> 4;
    const s16* k = interp().c[phase];
    s32 out = 0;
    for (int i = 0; i < 4; ++i)
        out += (k[i] * vc.fifo[(vc.decR + i) % 32]) >> 15;
    return out;
}

// Dr. Hell's noise generator as implemented in PCSX2/pcsxr
void Spu2::updateNoise(Core& co) {
    static const u8 noise_add[64] = {1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1,
                                     1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,
                                     1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1};
    static const u16 noise_freq_add[5] = {0, 84, 140, 180, 210};
    u32 level = 0x8000 >> (co.noiseClk >> 2);
    level <<= 16;
    co.noiseCnt += 0x10000;
    co.noiseCnt += noise_freq_add[co.noiseClk & 3];
    if ((co.noiseCnt & 0xffff) >= noise_freq_add[4]) {
        co.noiseCnt += 0x10000;
        co.noiseCnt -= noise_freq_add[co.noiseClk & 3];
    }
    if (co.noiseCnt >= level) {
        while (co.noiseCnt >= level)
            co.noiseCnt -= level;
        co.noiseOut = (co.noiseOut << 1) | noise_add[(co.noiseOut >> 10) & 63];
    }
}

void Spu2::startVoices(int c, u32 mask) {
    Core& co = cores_[c];
    co.endx &= ~mask;
    for (u32 v = 0; v < kVoices; ++v) {
        if (!((mask >> v) & 1))
            continue;
        Voice& vc = co.voices[v];
        if (vc.startA & 7)
            vc.startA = (vc.startA + 0xFFFF8) + 0x8; // misaligned: round up (PCSX2)
        vc.adsr.attack();
        vc.loopMode = false;
        vc.sp = 0;
        vc.loopFlags = 0;
        vc.nextA = vc.startA | 1;
        vc.prev1 = vc.prev2 = 0;
        vc.blkValid = false;
        vc.decR = vc.decW = 0;
    }
}

Spu2::Stereo Spu2::mixVoice(int c, int v) {
    Core& co = cores_[c];
    Voice& vc = co.voices[v];
    vc.volL.update();
    vc.volR.update();
    decodeSamples(c, v);

    Stereo out;
    s32 value = 0;
    if (vc.adsr.phase > Adsr::Stopped) {
        value = vc.noise ? (s32)(s16)co.noiseOut : voiceValue(vc);
        if (!vc.adsr.calculate())
            vc.stop();
        value = applyVol(value, vc.adsr.value);
        vc.outX = value;
        out.l = applyVol(value, vc.volL.value);
        out.r = applyVol(value, vc.volR.value);
    }
    // the SPU2 keeps processing stopped voices (IRQs, ENDX), so always advance pitch/position
    s32 pitch;
    if (!vc.modulated || v == 0)
        pitch = vc.pitch;
    else
        pitch = std::clamp<s32>((vc.pitch * (32768 + co.voices[v - 1].outX)) >> 15, 0, 0x3fff);
    pitch = std::min(pitch, 0x3FFF);
    vc.sp += pitch;
    vc.decR += (u32)(vc.sp >> 12);
    vc.sp &= 0xfff;

    // write-back of raw voice 1 / 3 output (games sometimes read it back)
    if (v == 1)
        writeFast((c == 0 ? 0x400 : 0xc00) + outPos_, value);
    else if (v == 3)
        writeFast((c == 0 ? 0x600 : 0xe00) + outPos_, value);
    return out;
}

// ------------------------------------------------------------------------------------------------
//  AutoDMA input (BlockTrans)
// ------------------------------------------------------------------------------------------------
void Spu2::admaRefill(int c, u32 spos) {
    Core& co = cores_[c];
    AdmaState& a = co.adma;
    if (!a.active)
        return;
    if (a.pos + 0x400 > a.size) {
        // stream exhausted: zero the half, finish
        memset(&ram_[0x2000 + (c << 10) + spos], 0, 0x200);
        memset(&ram_[0x2200 + (c << 10) + spos], 0, 0x200);
        if (!a.ended) {
            a.ended = true;
            a.active = false;
            if (co.transIntrEnabled)
                pushEvent(Event::TransDone, (u32)c);
        }
        return;
    }
    memcpy(&ram_[0x2000 + (c << 10) + spos], a.src + a.pos, 0x200);         // 0x100 L samples
    memcpy(&ram_[0x2200 + (c << 10) + spos], a.src + a.pos + 0x200, 0x200); // 0x100 R samples
    const u32 before = a.pos;
    a.pos += 0x400;
    if (a.half && (before / a.half) != (a.pos / a.half) && a.pos < a.size) {
        a.curHalf ^= 1;
        if (co.transIntrEnabled)
            pushEvent(Event::TransDone, (u32)c); // half consumed: host refills it
    } else if (a.pos >= a.size && a.loop) {
        a.pos = 0;
        a.curHalf = 0;
        if (co.transIntrEnabled)
            pushEvent(Event::TransDone, (u32)c);
    }
}

Spu2::Stereo Spu2::readInput(int c) {
    Core& co = cores_[c];
    for (int i = 0; i < 2; ++i)
        if (cores_[i].irqEnable && (0x2000u + (c << 10) + outPos_) == (cores_[i].irqA & 0xfffffdffu))
            raiseIrq(i);
    Stereo r;
    r.l = (s16)ram_[0x2000 + (c << 10) + outPos_];
    r.r = (s16)ram_[0x2200 + (c << 10) + outPos_];
    if (co.adma.active) {
        if (outPos_ == 0x100)
            admaRefill(c, 0);
        else if (outPos_ == 0) {
            if (!co.adma.primed) {
                co.adma.primed = true;
                admaRefill(c, 0);
                admaRefill(c, 0x100);
            } // prime at ring start
            else
                admaRefill(c, 0x100);
        }
    }
    return r;
}

// ------------------------------------------------------------------------------------------------
//  reverb (PCSX2 Reverb.cpp + ReverbResample.cpp reference path)
// ------------------------------------------------------------------------------------------------
static const s16 kDownCoef[39] = {-1,   0,    2,   0,     -10,  0,     35,    0,     -103, 0,     266, 0,    -616,
                                  0,    1332, 0,   -2960, 0,    10246, 16384, 10246, 0,    -2960, 0,   1332, 0,
                                  -616, 0,    266, 0,     -103, 0,     35,    0,     -10,  0,     2,   0,    -1};

Spu2::Stereo Spu2::doReverb(int c, Stereo in) {
    Core& co = cores_[c];
    const Reverb& rv = co.revb;
    const u32 startA = co.esa & 0x3fffff;
    const u32 endA = (co.eea & 0x3fffff);
    if (co.esa >= co.eea)
        return {};
    in.l = clampMix(in.l);
    in.r = clampMix(in.r);
    co.revDown[0][co.revPos] = in.l;
    co.revDown[1][co.revPos] = in.r;
    co.revDown[0][co.revPos | 64] = in.l;
    co.revDown[1][co.revPos | 64] = in.r;

    const bool R = cycles_ & 1;
    auto idx = [&](u32 offset) -> u32 {
        const u32 end = endA | 0xffff;
        u32 x = ((cycles_ >> 1) + offset) % ((end - startA) + 1);
        x += startA;
        return x & 0xfffff;
    };
    const u32 same_src = idx(R ? rv.SAME_R_SRC : rv.SAME_L_SRC);
    const u32 same_dst = idx(R ? rv.SAME_R_DST : rv.SAME_L_DST);
    const u32 same_prv = idx(R ? rv.SAME_R_DST - 1 : rv.SAME_L_DST - 1);
    const u32 diff_src = idx(R ? rv.DIFF_L_SRC : rv.DIFF_R_SRC);
    const u32 diff_dst = idx(R ? rv.DIFF_R_DST : rv.DIFF_L_DST);
    const u32 diff_prv = idx(R ? rv.DIFF_R_DST - 1 : rv.DIFF_L_DST - 1);
    const u32 comb1_src = idx(R ? rv.COMB1_R_SRC : rv.COMB1_L_SRC);
    const u32 comb2_src = idx(R ? rv.COMB2_R_SRC : rv.COMB2_L_SRC);
    const u32 comb3_src = idx(R ? rv.COMB3_R_SRC : rv.COMB3_L_SRC);
    const u32 comb4_src = idx(R ? rv.COMB4_R_SRC : rv.COMB4_L_SRC);
    const u32 apf1_src = idx(R ? (rv.APF1_R_DST - rv.APF1_SIZE) : (rv.APF1_L_DST - rv.APF1_SIZE));
    const u32 apf1_dst = idx(R ? rv.APF1_R_DST : rv.APF1_L_DST);
    const u32 apf2_src = idx(R ? (rv.APF2_R_DST - rv.APF2_SIZE) : (rv.APF2_L_DST - rv.APF2_SIZE));
    const u32 apf2_dst = idx(R ? rv.APF2_R_DST : rv.APF2_L_DST);

    auto M = [&](u32 a) -> s32 { return (s16)ram_[a]; };
#define MUL(x, y) (((x) * (y)) >> 15)
    // 39-tap downsample (48k -> 24k)
    s32 ds = 0;
    {
        const int base = (int)((co.revPos - 39u) & 63);
        for (int i = 0; i < 39; ++i)
            ds += co.revDown[R][base + i] * kDownCoef[i];
        ds = clampMix(ds >> 15);
    }
    s32 inp = MUL((s32)(R ? rv.IN_COEF_R : rv.IN_COEF_L), ds);
    s32 same = MUL((s32)rv.IIR_VOL, inp + MUL((s32)rv.WALL_VOL, M(same_src)) - M(same_prv)) + M(same_prv);
    s32 diff = MUL((s32)rv.IIR_VOL, inp + MUL((s32)rv.WALL_VOL, M(diff_src)) - M(diff_prv)) + M(diff_prv);
    s32 out = MUL((s32)rv.COMB1_VOL, M(comb1_src)) + MUL((s32)rv.COMB2_VOL, M(comb2_src)) +
              MUL((s32)rv.COMB3_VOL, M(comb3_src)) + MUL((s32)rv.COMB4_VOL, M(comb4_src));
    s32 apf1 = out - MUL((s32)rv.APF1_VOL, M(apf1_src));
    out = M(apf1_src) + MUL((s32)rv.APF1_VOL, apf1);
    s32 apf2 = out - MUL((s32)rv.APF2_VOL, M(apf2_src));
    out = M(apf2_src) + MUL((s32)rv.APF2_VOL, apf2);
#undef MUL
    if (co.fxEnable) { // effects always run but only write back while enabled
        ram_[same_dst] = (u16)(s16)clampMix(same);
        ram_[diff_dst] = (u16)(s16)clampMix(diff);
        ram_[apf1_dst] = (u16)(s16)clampMix(apf1);
        ram_[apf2_dst] = (u16)(s16)clampMix(apf2);
    }
    out = clampMix(out);
    co.revUp[R][co.revPos] = out;
    co.revUp[!R][co.revPos] = 0;
    co.revUp[R][co.revPos | 64] = out;
    co.revUp[!R][co.revPos | 64] = 0;
    co.revPos = (co.revPos + 1) & 63;

    // 39-tap upsample (24k -> 48k), coefficients doubled
    Stereo o;
    {
        const int base = (int)((co.revPos - 39u) & 63);
        s32 l = 0, r = 0;
        for (int i = 0; i < 39; ++i) {
            const s32 k = std::clamp<s32>(kDownCoef[i] * 2, INT16_MIN, INT16_MAX);
            l += co.revUp[0][base + i] * k;
            r += co.revUp[1][base + i] * k;
        }
        o.l = clampMix(l >> 15);
        o.r = clampMix(r >> 15);
    }
    if (!co.fxEnable)
        return {};
    return o;
}

// ------------------------------------------------------------------------------------------------
//  core mix
// ------------------------------------------------------------------------------------------------
Spu2::Stereo Spu2::mixCore(int c, const Stereo& vdry, const Stereo& vwet, const Stereo& input, const Stereo& ext) {
    Core& co = cores_[c];
    co.masterL.update();
    co.masterR.update();
    updateNoise(co);

    const Stereo dry{clampMix(vdry.l), clampMix(vdry.r)}, wet{clampMix(vwet.l), clampMix(vwet.r)};
    const u32 b = c == 0 ? 0x1000 : 0x1800;
    writeFast(b + outPos_, dry.l);
    writeFast(b + 0x200 + outPos_, dry.r);
    writeFast(b + 0x400 + outPos_, wet.l);
    writeFast(b + 0x600 + outPos_, wet.r);

    const u16 m = co.mmix;
    auto g = [&](u16 bit, s32 x) { return (m & bit) ? x : 0; };
    Stereo TD;
    TD.l = g(0x080, input.l) + g(0x800, dry.l) + g(0x008, ext.l);
    TD.r = g(0x040, input.r) + g(0x400, dry.r) + g(0x004, ext.r);
    Stereo TW;
    TW.l = g(0x020, input.l) + g(0x200, wet.l) + g(0x002, ext.l);
    TW.r = g(0x010, input.r) + g(0x100, wet.r) + g(0x001, ext.r);

    Stereo RV = doReverb(c, TW);
    Stereo o;
    o.l = TD.l + applyVol(RV.l, co.fxVolL);
    o.r = TD.r + applyVol(RV.r, co.fxVolR);
    return o;
}

void Spu2::mixSample(s32& outL, s32& outR) {
    for (int c = 0; c < 2; ++c) {
        Core& co = cores_[c];
        if (co.keyOff) {
            for (u32 v = 0; v < kVoices; ++v)
                if ((co.keyOff >> v) & 1)
                    co.voices[v].adsr.release();
            co.keyOff = 0;
        }
        if (co.keyOn) {
            startVoices(c, co.keyOn);
            co.keyOn = 0;
        }
    }
    cycles_++;

    Stereo in0 = readInput(0), in1 = readInput(1);
    in0.l = applyVol(in0.l, cores_[0].inpVolL);
    in0.r = applyVol(in0.r, cores_[0].inpVolR);
    in1.l = applyVol(in1.l, cores_[1].inpVolL);
    in1.r = applyVol(in1.r, cores_[1].inpVolR);

    Stereo vdry[2], vwet[2];
    for (int c = 0; c < 2; ++c) {
        Core& co = cores_[c];
        for (u32 v = 0; v < kVoices; ++v) {
            const Stereo s = mixVoice(c, v);
            if ((co.vmix[0] >> v) & 1)
                vdry[c].l += s.l;
            if ((co.vmix[2] >> v) & 1)
                vdry[c].r += s.r;
            if ((co.vmix[1] >> v) & 1)
                vwet[c].l += s.l;
            if ((co.vmix[3] >> v) & 1)
                vwet[c].r += s.r;
        }
    }

    Stereo ext = mixCore(0, vdry[0], vwet[0], in0, Stereo{});
    ext.l = clampMix(ext.l);
    ext.r = clampMix(ext.r);
    if (honorMute_ && (cores_[0].attr & SD_MUTE))
        ext = Stereo{};
    else {
        ext.l = applyVol(ext.l, cores_[0].masterL.value);
        ext.r = applyVol(ext.r, cores_[0].masterR.value);
    }
    writeFast(0x800 + outPos_, ext.l);
    writeFast(0xA00 + outPos_, ext.r);

    ext.l = applyVol(ext.l, cores_[1].extVolL);
    ext.r = applyVol(ext.r, cores_[1].extVolR);
    Stereo out = mixCore(1, vdry[1], vwet[1], in1, ext);
    out.l = clampMix(out.l);
    out.r = clampMix(out.r);
    if (honorMute_ && (cores_[1].attr & SD_MUTE))
        out = Stereo{};
    else {
        out.l = applyVol(out.l, cores_[1].masterL.value);
        out.r = applyVol(out.r, cores_[1].masterR.value);
    }
    outL = out.l;
    outR = out.r;

    outPos_ = (outPos_ + 1) & 0x1FF;
}

// ------------------------------------------------------------------------------------------------
//  public render
// ------------------------------------------------------------------------------------------------
void Spu2::renderLocked(s16* out, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        s32 l, r;
        mixSample(l, r);
        if (gain_ != 1.0f) {
            l = (s32)(l * gain_);
            r = (s32)(r * gain_);
        }
        out[i * 2] = (s16)clampMix(l);
        out[i * 2 + 1] = (s16)clampMix(r);
    }
    frames_ += frames;
}

void Spu2::render(s16* out, size_t frames) {
    while (frames) {
        const size_t n = std::min<size_t>(frames, 64);
        {
            std::lock_guard<std::mutex> g(m_);
            drainLocked();
            renderLocked(out, n);
        }
        { // output level meter (diagnostics, see takePeak())
            int pk = peak_.load(std::memory_order_relaxed);
            for (size_t i = 0; i < n * 2; ++i) {
                const int a = out[i] < 0 ? -(int)out[i] : (int)out[i];
                if (a > pk)
                    pk = a;
            }
            peak_.store(pk, std::memory_order_relaxed);
        }
        out += n * 2;
        frames -= n;
    }
}

void Spu2::renderFloat(float* out, size_t frames) {
    s16 tmp[64 * 2];
    while (frames) {
        const size_t n = std::min<size_t>(frames, 64);
        render(tmp, n);
        for (size_t i = 0; i < n * 2; ++i)
            out[i] = tmp[i] * (1.0f / 32768.0f);
        out += n * 2;
        frames -= n;
    }
}

void Spu2::peekVoice(int core, int voice, u32& nextWordAddr, s32& envx, int& phase) {
    std::lock_guard<std::mutex> g(m_);
    drainLocked();
    const Voice& v = cores_[core & 1].voices[voice % kVoices];
    nextWordAddr = v.nextA;
    envx = v.adsr.value;
    phase = v.adsr.phase;
}

} // namespace spu2
