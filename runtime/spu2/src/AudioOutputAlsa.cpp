// ALSA owns only the output device; mixing, pause, gain and WAV dumping are shared
// with WASAPI in AudioOutput.cpp. No Linux types escape this implementation.
#include "spu2/AudioOutput.h"

#include <alsa/asoundlib.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>

namespace spu2 {
std::vector<OutputDevice> AudioOutput::enumerateDevices() {
    // Do not advertise a default hardware device on a machine with no audio.
    // ALSA hints also expose configured PipeWire/PulseAudio PCMs and the null PCM.
    std::vector<OutputDevice> devices;
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0 || !hints)
        return devices;
    for (void** hint = hints; *hint; ++hint) {
        std::unique_ptr<char, decltype(&std::free)> name(snd_device_name_get_hint(*hint, "NAME"), std::free);
        std::unique_ptr<char, decltype(&std::free)> description(snd_device_name_get_hint(*hint, "DESC"), std::free);
        std::unique_ptr<char, decltype(&std::free)> io(snd_device_name_get_hint(*hint, "IOID"), std::free);
        if (!name || (io && std::string_view(io.get()) == "Input"))
            continue;
        // Hints can describe unconfigured plugins (upmix, resamplers, JACK),
        // rather than playable endpoints. Only offer PCMs this backend can open.
        snd_pcm_t* handle = nullptr;
        if (snd_pcm_open(&handle, name.get(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
            continue;
        std::unique_ptr<snd_pcm_t, decltype(&snd_pcm_close)> probe(handle, snd_pcm_close);
        if (snd_pcm_set_params(probe.get(), SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
                               40000) < 0)
            continue;
        std::string label = description ? description.get() : name.get();
        for (char& ch : label)
            if (ch == '\n')
                ch = ' ';
        devices.push_back({name.get(), std::move(label), std::string_view(name.get()) == "default"});
    }
    snd_device_name_free_hint(hints);
    std::stable_partition(devices.begin(), devices.end(), [](const auto& device) { return device.isDefault; });
    return devices;
}

void AudioOutput::threadMain() {
    using Clock = std::chrono::steady_clock;
    std::array<float, 480 * 2> samples{};
    auto retry = Clock::now();
    auto next = Clock::now();
    std::string lastError;
    unsigned generation = devGen_.load();
    std::unique_ptr<snd_pcm_t, decltype(&snd_pcm_close)> device(nullptr, snd_pcm_close);

    while (!stop_.load()) {
        if (generation != devGen_.load()) {
            generation = devGen_.load();
            device.reset();
            running_ = false;
            retry = Clock::now();
        }
        if (!device && opt_.useDevice && Clock::now() >= retry) {
            std::string want = outputDevice();
            snd_pcm_t* handle = nullptr;
            // Nonblocking writes keep stop()/device changes bounded even when an
            // audio server stops consuming the stream.
            int result = snd_pcm_open(&handle, want.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
            if (result < 0 && want == "default" && std::getenv("PULSE_SERVER")) {
                // WSLg exposes PulseAudio without an ALSA default hardware card.
                if (handle)
                    snd_pcm_close(handle);
                handle = nullptr;
                want = "pulse";
                result = snd_pcm_open(&handle, want.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
            }
            device.reset(handle);
            if (result >= 0)
                result = snd_pcm_set_params(device.get(), SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                                            48000, 1, 40000);
            if (result < 0) {
                device.reset();
                const std::string message = snd_strerror(result);
                // start() reads err_ after the release/acquire ready_ handoff.
                if (!ready_.load())
                    err_ = message;
                if (message != lastError)
                    std::fprintf(stderr, "[audio] ALSA: %s (rendering silently; retrying)\n", message.c_str());
                lastError = message;
                retry = Clock::now() + std::chrono::seconds(1);
            } else {
                rate_ = 48000;
                {
                    std::lock_guard<std::mutex> lock(devM_);
                    curId_ = want;
                }
                while (nameLock_.test_and_set(std::memory_order_acquire)) {
                }
                name_ = "ALSA: " + want;
                nameLock_.clear(std::memory_order_release);
                ++reopens_;
                running_ = true;
                lastError.clear();
            }
        }
        ready_ = true;
        renderBlock(samples.data(), 480);
        size_t offset = 0;
        const auto deadline = Clock::now() + std::chrono::milliseconds(50);
        while (device && offset < 480 && !stop_.load() && generation == devGen_.load()) {
            const auto written = snd_pcm_writei(device.get(), samples.data() + offset * 2, 480 - offset);
            if (written > 0) {
                offset += static_cast<size_t>(written);
            } else if (written == -EAGAIN || written == 0) {
                if (Clock::now() >= deadline) {
                    device.reset();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (written == -EPIPE && Clock::now() < deadline && snd_pcm_prepare(device.get()) >= 0) {
                // Recover underrun. Suspend/disconnect instead closes the device:
                // snd_pcm_recover may wait indefinitely for a suspended server.
                continue;
            } else {
                device.reset();
                break;
            }
        }
        if (!device) {
            running_ = false;
            if (retry < Clock::now())
                retry = Clock::now() + std::chrono::seconds(1);
        }
        // Always advance the mixer, including without hardware. The guest polls
        // voice positions. Bound catch-up after suspension instead of bursting.
        next += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next);
        if (Clock::now() > next + std::chrono::milliseconds(100))
            next = Clock::now();
    }
    running_ = false;
}
} // namespace spu2
