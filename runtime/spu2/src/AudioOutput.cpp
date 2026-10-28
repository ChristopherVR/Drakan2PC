#include "spu2/AudioOutput.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace spu2 {

std::string AudioOutput::deviceName() const {
    while (nameLock_.test_and_set(std::memory_order_acquire)) {
    }
    std::string n = name_;
    nameLock_.clear(std::memory_order_release);
    return n;
}

void AudioOutput::setOutputDevice(const std::string& idOrName) {
    {
        std::lock_guard<std::mutex> g(devM_);
        device_ = idOrName.empty() ? "default" : idOrName;
    }
    ++devGen_;
}

std::string AudioOutput::outputDevice() const {
    std::lock_guard<std::mutex> g(devM_);
    return device_;
}
std::string AudioOutput::currentDeviceId() const {
    std::lock_guard<std::mutex> g(devM_);
    return curId_;
}

void AudioOutput::renderBlock(float* stereo, size_t frames) {
    bool hold = paused_.load();
    if (const std::atomic<uint64_t>* hb = hb_.load()) {
        const long long nowMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
        const uint64_t t = hb->load(std::memory_order_relaxed);
        if (t != hbLast_) {
            hbLast_ = t;
            hbSinceMs_ = nowMs;
            hbSeen_ = true;
        }
        const bool stale = hbSeen_ && nowMs - hbSinceMs_ > (long long)hbStaleMs_;
        hbStalled_.store(stale);
        hold = hold || stale;
    }
    if (hold && pauseRamp_.primed && pauseRamp_.cur == 0.0f) {
        std::fill(stereo, stereo + frames * 2, 0.0f); // frozen: no SPU2 rendering, nothing advances
        dumpWrite(stereo, frames);
        return;
    }
    spu_.renderFloat(stereo, frames);
    framesRendered_.fetch_add(frames, std::memory_order_relaxed);
    pauseRamp_.process(stereo, frames, hold ? 0.0f : 1.0f, 240.0f);
    ramp_.process(stereo, frames, muted_.load() ? 0.0f : gain_.load());
    dumpWrite(stereo, frames);
}

void AudioOutput::takeLevels(double& rms, double& peak) {
    std::lock_guard<std::mutex> g(lvM_);
    rms = nSamples_ ? std::sqrt(sumSq_ / (double)nSamples_) : 0.0;
    peak = peak_;
    sumSq_ = 0;
    peak_ = 0;
    nSamples_ = 0;
}

void AudioOutput::dumpWrite(const float* st, size_t frames) {
    // levels (always) + WAV dump (optional)
    double sq = 0, pk = 0;
    for (size_t i = 0; i < frames * 2; ++i) {
        const double v = st[i];
        sq += v * v;
        pk = std::max(pk, std::fabs(v));
    }
    {
        std::lock_guard<std::mutex> g(lvM_);
        sumSq_ += sq;
        nSamples_ += frames * 2;
        peak_ = std::max(peak_, pk);
    }
    FILE* f = (FILE*)dumpFile_;
    if (!f)
        return;
    std::vector<int16_t> pcm(frames * 2);
    for (size_t i = 0; i < frames * 2; ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, st[i]));
        pcm[i] = (int16_t)std::lrintf(v * 32767.0f);
    }
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    dumpBytes_ += pcm.size() * 2;
    // keep the header valid at all times: the process usually leaves through _Exit without calling stop()
    static unsigned long long lastHdr = 0;
    if (dumpBytes_ - lastHdr >= 48000ull * 4) {
        lastHdr = dumpBytes_;
        dumpFinish();
    }
}

void AudioOutput::dumpFinish() {
    FILE* f = (FILE*)dumpFile_;
    if (!f)
        return;
    const uint32_t data = (uint32_t)dumpBytes_, riff = 36 + data, fmtLen = 16, rate = 48000, byteRate = 48000 * 4;
    const uint16_t tag = 1, ch = 2, align = 4, bits = 16;
    const long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtLen, 4, 1, f);
    std::fwrite(&tag, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    std::fseek(f, pos, SEEK_SET);
    std::fflush(f);
}

bool AudioOutput::start(std::string* err) {
    if (th_.joinable())
        return running_;
    stop_ = false;
    ready_ = false;
    running_ = false;
    err_.clear();
    ramp_ = GainRamp();
    if (!opt_.dumpPath.empty()) {
        FILE* f = std::fopen(opt_.dumpPath.c_str(), "wb");
        if (f) {
            dumpFile_ = f;
            dumpBytes_ = 0;
            std::vector<uint8_t> hdr(44, 0);
            std::fwrite(hdr.data(), 1, 44, f);
            dumpFinish();
        } else
            std::fprintf(stderr, "[audio] cannot open dump file '%s'\n", opt_.dumpPath.c_str());
    }
    th_ = std::thread([this] { threadMain(); });
    for (int i = 0; i < 500 && !ready_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!running_ && opt_.useDevice) {
        if (err)
            *err = err_.empty() ? "audio start timed out" : err_;
        return false;
    }
    return true;
}

void AudioOutput::stop() {
    stop_ = true;
    if (th_.joinable())
        th_.join();
    running_ = false;
    if (dumpFile_) {
        dumpFinish();
        std::fclose((FILE*)dumpFile_);
        dumpFile_ = nullptr;
    }
}

} // namespace spu2
