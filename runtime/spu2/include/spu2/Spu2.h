// Standalone SPU2 emulation + libsd API. Implements hle::ISpu2 (drakan_hle) so it can be handed to
// hle::RsrcIop as the sound sink, and exposes the libsd functions needed by sdrdrv (see Sdr.h).
//
// Mixer/ADSR/volume-slide/reverb algorithms are ports of PCSX2's SPU2 (GPL-3.0-or-later, PCSX2 Dev Team);
// libsd register semantics follow ps2sdk's freesd (AFL-2.0). No libsd data is built in: the reverb presets come
// from the player's own LIBSD.IRX (findLibsdEffectPresets).
//
// Threading: every public method may be called from any thread. Setters are queued (cheap, never wait for
// the mixer) and applied in order at the next 64-frame boundary of render(); getters / RAM access first
// apply everything queued so they observe the effect of earlier setters.
#pragma once
#include "hle/Interfaces.h"
#include "spu2/Libsd.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace spu2 {

using hle::s16;
using hle::s32;
using hle::u16;
using hle::u32;
using hle::u64;
using hle::u8;

constexpr u32 kRamBytes = 0x200000;      // 2 MiB
constexpr u32 kRamWords = kRamBytes / 2; // addresses inside the mixer are 16-bit word indices (20 bits)
constexpr u32 kVoices = 24;
constexpr u32 kSampleRate = 48000;

// Interrupt-style notifications, drained by the host with popEvents().
struct Event {
    enum Kind : u32 { TransDone = 1, SpuIrq = 2 };
    Kind kind;
    u32 arg; // TransDone: core (0/1)  (DMA channel == core);  SpuIrq: bit mask of cores (1<<core) as libsd passes it
};

// ---- register-level state -----------------------------------------------------------------------
struct VolSlide { // voice / master volume with sweep (PCSX2 V_VolumeSlide)
    u16 reg = 0;  // raw register value
    u32 counter = 0;
    s32 value = 0; // current volume, 16-bit signed range
    void set(u16 v);
    void update();
};

struct Adsr {
    enum Phase { Stopped = 0, Attack, Decay, Sustain, Release };
    u16 reg1 = 0, reg2 = 0;
    int phase = Stopped;
    u32 counter = 0;
    s32 value = 0; // 0..0x7fff
    struct Cached {
        bool decr, exp;
        int shift, step;
        s32 target;
    } cached[5] = {};
    void updateCache();
    bool calculate(); // false => envelope finished, stop the voice
    void attack() {
        phase = Attack;
        counter = 0;
        value = 0;
    }
    void release() {
        if (phase != Stopped) {
            phase = Release;
            counter = 0;
        }
    }
};

struct Voice {
    VolSlide volL, volR;
    u16 pitch = 0;
    Adsr adsr;
    u32 startA = 0x2800, loopStartA = 0x2800, nextA = 0; // word addresses
    bool loopMode = false;                               // LSAX written since key-on
    u8 loopFlags = 0;
    bool noise = false, modulated = false;
    s32 sp = 0; // 12-bit fractional pitch counter
    s32 prev1 = 0, prev2 = 0;
    s16 blk[28] = {};
    bool blkValid = false;
    s16 fifo[32] = {};
    u32 decW = 0, decR = 0;
    s32 outX = 0;
    void stop() {
        adsr.value = 0;
        adsr.phase = Adsr::Stopped;
    }
};

struct Reverb { // register values as stored by libsd's effect presets (word offsets relative to ESA)
    s16 IN_COEF_L = 0, IN_COEF_R = 0;
    u32 APF1_SIZE = 0, APF2_SIZE = 0;
    s16 APF1_VOL = 0, APF2_VOL = 0;
    u32 SAME_L_SRC = 0, SAME_R_SRC = 0, DIFF_L_SRC = 0, DIFF_R_SRC = 0;
    u32 SAME_L_DST = 0, SAME_R_DST = 0, DIFF_L_DST = 0, DIFF_R_DST = 0;
    s16 IIR_VOL = 0, WALL_VOL = 0;
    u32 COMB1_L_SRC = 0, COMB1_R_SRC = 0, COMB2_L_SRC = 0, COMB2_R_SRC = 0;
    u32 COMB3_L_SRC = 0, COMB3_R_SRC = 0, COMB4_L_SRC = 0, COMB4_R_SRC = 0;
    s16 COMB1_VOL = 0, COMB2_VOL = 0, COMB3_VOL = 0, COMB4_VOL = 0;
    u32 APF1_L_DST = 0, APF1_R_DST = 0, APF2_L_DST = 0, APF2_R_DST = 0;
};

struct AdmaState { // AutoDMA (BlockTrans) streaming input
    bool active = false, loop = false;
    const u8* src = nullptr;
    u32 iopTag = 0;         // value reported back by BlockTransStatus (caller's address of src)
    u32 size = 0, half = 0; // total / per-half bytes (loop mode halves = size/2)
    u32 pos = 0;            // next source byte to consume
    u32 curHalf = 0;        // 0/1, which half the stream is in
    bool ended = false, primed = false;
};

struct Core {
    std::array<Voice, kVoices> voices;
    u32 pmon = 0, non = 0, vmix[4] = {0xffffff, 0xffffff, 0xffffff, 0xffffff}; // VMIXL, VMIXEL, VMIXR, VMIXER
    u32 keyOn = 0, keyOff = 0, endx = 0xffffff;
    u16 mmix = 0xFF0, attr = 0;
    bool fxEnable = false, irqEnable = false;
    u32 irqA = 0, tsa = 0, esa = 0, eea = 0xFFFFF; // words
    VolSlide masterL, masterR;
    s32 fxVolL = 0, fxVolR = 0, extVolL = 0, extVolR = 0, inpVolL = 0, inpVolR = 0;
    u32 noiseClk = 0, noiseCnt = 0, noiseOut = 0;
    Reverb revb;
    s32 revDown[2][128] = {}, revUp[2][128] = {};
    u32 revPos = 0;
    AdmaState adma;
    bool transIntrEnabled = false;
};

// One libsd reverb preset per SD_EFFECT_MODE_* (OFF..PIPE): the work-area size and the 32 reverb register values
// in SPU2 register order.
struct EffectPreset {
    u32 workAreaBlocks = 0; // work-area size in 16-byte blocks
    std::array<u16, 32> regs{};
};
using EffectPresets = std::array<EffectPreset, 10>;

// Locates libsd's reverb preset table inside a LIBSD.IRX image read from the player's disc. Returns false when the
// image does not contain the table this runtime was verified against (Drakan PAL, SCES-500.06).
bool findLibsdEffectPresets(const u8* irx, size_t size, EffectPresets& out);

class Spu2 final : public hle::ISpu2 {
public:
    Spu2();
    ~Spu2() override;
    Spu2(const Spu2&) = delete;
    Spu2& operator=(const Spu2&) = delete;

    // ---- hle::ISpu2 (byte addresses for RAM) ----
    void ramWrite(u32 spuAddr, const void* src, u32 n) override;
    void ramRead(u32 spuAddr, void* dst, u32 n) override;
    void setParam(u16 entry, u16 value) override;
    u16 getParam(u16 entry) override;
    void setSwitch(u16 entry, u32 value) override;
    u32 getSwitch(u16 entry) override;
    void setAddr(u16 entry, u32 value) override;
    u32 getAddr(u16 entry) override;
    void setCoreAttr(u16 entry, u16 value) override;
    u16 getCoreAttr(u16 entry) override;

    // ---- libsd ----
    int sdInit(int flag); // sceSdInit (flag&1: keep volumes/EEA)
    int procBatch(const SdBatch* batch, u32* rets, u32 num);
    int procBatchEx(const SdBatch* batch, u32* rets, u32 num, u32 voiceMask);
    // iopPtr: host pointer to the IOP-side buffer (the caller maps IOP addresses). Transfers complete
    // synchronously; a TransDone event is queued if setTransIntrHandler(core,true) was called.
    int voiceTrans(s16 chan, u16 mode, void* iopPtr, u32 spuAddr, u32 size);
    int blockTrans(s16 chan, u16 mode, void* iopPtr, u32 size, u32 iopTag = 0, void* startPtr = nullptr);
    u32 voiceTransStatus(s16 chan, s16 flag);
    u32 blockTransStatus(s16 chan, s16 flag);
    int stopTrans(int channel);                         // SD_TRANS_STOP / sceSdStopTrans
    bool setTransIntrHandler(int channel, bool enable); // returns previous state
    bool setSpu2IntrHandler(bool enable);               // returns previous state
    static u16 note2Pitch(u16 centerNote, u16 centerFine, u16 note, s16 fine);
    static u16 pitch2Note(u16 centerNote, u16 centerFine, u16 pitch);
    // Without presets (setEffectPresets) every mode other than OFF is accepted but leaves the reverb silent.
    void setEffectPresets(const EffectPresets& presets);
    bool hasEffectPresets();
    int setEffectAttr(int core, const SdEffectAttr& attr);
    void getEffectAttr(int core, SdEffectAttr& attr);
    int clearEffectWorkArea(int core, int channel, int effectMode);

    // ---- audio generation ----
    // Renders `frames` stereo frames (interleaved L,R int16) at 48 kHz. Safe to call from the audio thread.
    void render(s16* out, size_t frames);
    void renderFloat(float* out, size_t frames);
    // Pops pending interrupt events. Returns false if there were none. `notify` (optional) is invoked (from the
    // thread that produced the event, with internal locks held: must not call back into Spu2) whenever an event is
    // queued.
    bool popEvents(std::vector<Event>& out);
    void setEventNotify(void (*fn)(void*), void* user) {
        notifyFn_ = fn;
        notifyUser_ = user;
    }
    // Host hook for SD_BATCH_WRITEEE / SD_BATCH_EERETURN (write bytes into EE memory). Optional.
    void setEeWriter(std::function<void(u32 eeAddr, const void* data, u32 n)> fn) { eeWriter_ = std::move(fn); }

    // ---- options / diagnostics ----
    void setHonorMute(bool b) { honorMute_ = b; } // CORE_ATTR bit 14; PCSX2 ignores it (default off)
    void setGlobalGain(float g) { gain_ = g; }
    void peekVoice(int core, int voice, u32& nextWordAddr, s32& envx, int& phase); // test helper
    u64 framesRendered() const { return frames_; }
    int takePeak() { return peak_.exchange(0); } // max |sample| (0..32768) rendered since the last call

private:
    struct Cmd {
        enum T : u8 { Param, Switch, Addr, Attr } t;
        u16 entry;
        u32 value;
    };
    std::mutex qm_; // protects pending_
    std::vector<Cmd> pending_;
    std::mutex m_;         // protects all emulator state below
    std::vector<u16> ram_; // kRamWords
    Core cores_[2];
    u32 cycles_ = 0, outPos_ = 0;
    u64 frames_ = 0;
    std::atomic<int> peak_{0};
    bool honorMute_ = false;
    float gain_ = 1.0f;
    bool spu2IntrEnabled_ = false;
    std::deque<Event> events_;
    std::mutex em_;
    void (*notifyFn_)(void*) = nullptr;
    void* notifyUser_ = nullptr;
    std::function<void(u32, const void*, u32)> eeWriter_;
    u16 spdif_ = 0;
    std::array<SdEffectAttr, 2> effectAttr_ = {};
    std::array<u32, 2> effectAddr_ = {};
    EffectPresets presets_ = {};
    bool havePresets_ = false;
    bool warnedNoPresets_ = false;

    void enqueue(Cmd::T t, u16 entry, u32 value);
    void drainLocked();
    void applyLocked(const Cmd& c);
    // register-level (state lock held)
    void doSetParam(u16 entry, u16 value);
    u16 doGetParam(u16 entry);
    void doSetSwitch(u16 entry, u32 value);
    u32 doGetSwitch(u16 entry);
    void doSetAddr(u16 entry, u32 value);
    u32 doGetAddr(u16 entry);
    void doSetCoreAttr(u16 entry, u16 value);
    u16 doGetCoreAttr(u16 entry);
    void writeCoreAttr(int core, u16 v);
    void ramWriteLocked(u32 byteAddr, const void* src, u32 n);
    void pushEvent(Event::Kind k, u32 arg);
    // mixer
    struct Stereo {
        s32 l = 0, r = 0;
    };
    void mixSample(s32& outL, s32& outR);
    Stereo mixVoice(int c, int v);
    void decodeSamples(int c, int v);
    void getNextData(Voice& vc);
    void incrementNextA(Voice& vc);
    void updateBlockHeader(Voice& vc);
    s32 voiceValue(const Voice& vc) const;
    void updateNoise(Core& co);
    Stereo mixCore(int c, const Stereo& vdry, const Stereo& vwet, const Stereo& input, const Stereo& ext);
    Stereo doReverb(int c, Stereo in);
    Stereo readInput(int c);
    void admaRefill(int c, u32 spos);
    void startVoices(int c, u32 mask);
    void checkIrq(u32 wordAddr);
    void raiseIrq(int core);
    void writeFast(u32 wordAddr, s32 v);
    void setEffectData(int core, const u16* d);
    void clearEffectLocked(int core, int type);
    void renderLocked(s16* out, size_t frames);
};

} // namespace spu2
