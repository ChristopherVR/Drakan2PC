// Native audio output (Windows WASAPI / Linux ALSA): a dedicated thread pulls
// 48 kHz stereo float from Spu2::renderFloat. Mixing, gain, pause and dumping are shared.
//
// The thread ALWAYS drives the SPU2: when no device can be opened (no audio hardware, device unplugged, driver reset,
// audio disabled) it keeps rendering on a wall clock so that voices still advance - the game polls voice positions to
// detect the end of a sound and would stall otherwise - and it retries the device once a second. The "default" device
// follows the Windows default render endpoint (re-opened when it changes).
#pragma once
#include "spu2/Spu2.h"
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spu2 {

// Click-free gain: moves the applied gain towards `target` by at most 1/rampFrames per sample (default 480 = 10 ms for
// a full 0..1 swing), so volume changes, mute and unmute never produce a step. Not thread safe (owned by the render
// thread / caller).
struct GainRamp {
    float cur = 1.0f;
    bool primed = false;
    // Applies the gain in place to interleaved stereo. The first call jumps to `target` (no fade-in at start-up).
    void process(float* stereo, size_t frames, float target, float rampFrames = 480.0f) {
        if (!primed) {
            cur = target;
            primed = true;
        }
        const float step = 1.0f / (rampFrames > 1.0f ? rampFrames : 1.0f);
        if (cur == target) {
            if (target != 1.0f)
                for (size_t i = 0; i < frames * 2; ++i)
                    stereo[i] *= target;
            return;
        }
        for (size_t i = 0; i < frames; ++i) {
            if (cur < target) {
                cur += step;
                if (cur > target)
                    cur = target;
            } else if (cur > target) {
                cur -= step;
                if (cur < target)
                    cur = target;
            }
            stereo[2 * i] *= cur;
            stereo[2 * i + 1] *= cur;
        }
    }
};

struct OutputDevice {
    std::string id;   // WASAPI endpoint id (stable; pass to setOutputDevice)
    std::string name; // friendly name, e.g. "Speakers (Realtek Audio)"
    bool isDefault = false;
};

class AudioOutput {
public:
    struct Options {
        std::string device =
            "default"; // "default", an endpoint id (OutputDevice::id) or a case-insensitive part of the friendly name
        float volume =
            1.0f; // initial linear master gain applied to the float output (and to the dump); see setMasterGain
        bool useDevice = true; // false: wall-clock silent rendering only (DRAKAN_AUDIO=0)
        std::string dumpPath;  // if set, the mixed output (after gain) is written as a 16-bit stereo 48 kHz WAV
    };
    explicit AudioOutput(Spu2& spu) : spu_(spu) {}
    ~AudioOutput() { stop(); }
    void setOptions(const Options& o) {
        opt_ = o;
        gain_.store(o.volume);
        device_ = o.device;
    }
    // Starts the render thread. Returns true when a device was opened (or the device is not wanted); false if it could
    // not be opened (rendering continues on the wall clock and the device is retried).
    bool start(std::string* err = nullptr);
    void stop();
    bool running() const { return running_; }     // a WASAPI stream is currently playing
    unsigned deviceRate() const { return rate_; } // engine mix rate (WASAPI converts our 48 kHz if different)
    std::string deviceName() const;
    unsigned reopenCount() const { return reopens_; }

    // ---- live controls: thread safe, callable at any time (also before start())
    // ---------------------------------------- Master gain (linear, 0..4; 1 = unity) and mute. Applied to everything
    // that leaves the output (device AND the WAV dump AND the levels) with a ~10 ms ramp, so there is no click. Mute
    // keeps the SPU2 running (the game polls voice positions).
    void setMasterGain(float g) { gain_.store(g < 0.0f ? 0.0f : (g > 4.0f ? 4.0f : g)); }
    float masterGain() const { return gain_.load(); }
    void setMuted(bool m) { muted_.store(m); }
    bool muted() const { return muted_.load(); }
    // Pause: the SPU2 is NOT rendered (voices, ADMA rings and envelopes freeze exactly where they are) and silence is
    // output after a 5 ms fade; resume continues seamlessly. Use it whenever the guest cannot service the SPU2 (window
    // unfocused with pause_when_unfocused, settings overlay open, ...), otherwise a streamed voice keeps replaying its
    // last buffered chunk.
    void setPaused(bool p) { paused_.store(p); }
    bool paused() const { return paused_.load(); }
    // Guest liveness watchdog: `tick` must advance while the emulated EE runs (the runtime's vblank counter). When it
    // has not changed for `staleMs` the guest is stalled for some other reason (blocked host call, debugger, console
    // selection, hung guest thread...) and the output is paused exactly like setPaused(true) until it moves again.
    // Never pauses before the first change was seen.
    void setHeartbeat(const std::atomic<uint64_t>* tick, unsigned staleMs = 250) {
        hbStaleMs_ = staleMs;
        hb_.store(tick);
    }
    bool stalled() const { return hbStalled_.load(); }
    // 48 kHz frames the SPU2 has actually been rendered for (does not advance while paused / stalled): the
    // sample-accurate media clock of the game's audio.
    uint64_t framesRendered() const {
        return framesRendered_.load(std::memory_order_relaxed);
    } // true while the watchdog is holding the output
    // Switches the output device while running (the stream is closed and re-opened on the render thread; the SPU2 keeps
    // rendering meanwhile, so there is at most a short gap). `idOrName`: "default" (follow the Windows default), an id
    // from enumerateDevices(), or a case-insensitive part of a friendly name. If it cannot be found the default device
    // is used.
    void setOutputDevice(const std::string& idOrName);
    std::string outputDevice() const;    // what was requested
    std::string currentDeviceId() const; // endpoint id currently playing ("" if none)
    // Active render endpoints (id + friendly name), default first. Safe to call from any thread, with or without
    // start().
    static std::vector<OutputDevice> enumerateDevices();
    // The single render path used by the device thread, the wall-clock thread and offline callers: renders `frames`
    // stereo frames from the SPU2 into `stereo` (48 kHz float), applies gain / mute, updates the levels and appends to
    // the dump. Call it yourself (without start()) for deterministic offline rendering; do not mix with a running
    // thread.
    void renderBlock(float* stereo, size_t frames);
    // Statistics for diagnostics: RMS / peak (0..1) of the samples rendered since the last call.
    void takeLevels(double& rms, double& peak);

private:
    void threadMain();
    void dumpWrite(const float* stereo, size_t frames);
    void dumpFinish();
    Spu2& spu_;
    Options opt_;
    std::atomic<float> gain_{1.0f};
    std::atomic<bool> muted_{false};
    std::atomic<bool> paused_{false};
    std::atomic<const std::atomic<uint64_t>*> hb_{nullptr};
    unsigned hbStaleMs_ = 250;
    std::atomic<bool> hbStalled_{false};
    std::atomic<uint64_t> framesRendered_{0};
    uint64_t hbLast_ = 0;
    long long hbSinceMs_ = 0;
    bool hbSeen_ = false;
    GainRamp ramp_, pauseRamp_;
    mutable std::mutex devM_;
    std::string device_ = "default";
    std::string curId_;
    std::atomic<unsigned> devGen_{0};
    std::thread th_;
    std::atomic<bool> stop_{false}, running_{false}, ready_{false};
    std::string err_;
    std::atomic<unsigned> rate_{0};
    std::atomic<unsigned> reopens_{0};
    mutable std::atomic_flag nameLock_ = ATOMIC_FLAG_INIT;
    std::string name_;
    std::mutex lvM_;
    double sumSq_ = 0, peak_ = 0;
    unsigned long long nSamples_ = 0;
    void* dumpFile_ = nullptr;
    unsigned long long dumpBytes_ = 0;
};

} // namespace spu2
