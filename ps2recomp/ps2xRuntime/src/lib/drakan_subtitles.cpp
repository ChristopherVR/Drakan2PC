// Drakan: narration subtitles - see drakan_subtitles.h and docs/reference/subtitles.md.
#include "drakan_subtitles.h"
#include "drakan_config.h"

#include <hle/ResourceOverride.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include <settings/Overlay.h>
#include <settings/Subtitles.h>
#include <spu2/AudioOutput.h>
#endif

namespace drakan_subtitles {
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
namespace {
constexpr uint32_t kGameSubtitleOption = 0x4fb740u; // guest word: Options > Audio > "Show Subtitles" (non zero = on)
constexpr uint16_t kTypeSoundData = 0x0304u;
constexpr uint16_t kNarrationSub = 14u;
constexpr uint16_t kIntroId = 99u, kOutroId = 100u;

dsettings::SubTrack g_track;
spu2::AudioOutput* g_audio = nullptr;
uint8_t* g_rdram = nullptr;
std::string g_language = "english";
std::atomic<bool> g_enabled{true};
std::atomic<float> g_size{0.0f};
// A resource can be requested again for streaming while the movie is still
// active. Only a real new voice key-on may restart an already started track.
std::atomic<uint16_t> g_startedInMovie{0};
double g_offsetMs = 0.0;

// Keep touching narration cues legible across a phrase change. SubTrack's
// default fades still apply at the beginning/end of speech and at real gaps.
std::mutex g_presentationMutex;
std::vector<int> g_touchingBoundaries;

// A narration request that has been seen but whose voice has not been key-on'd yet.
struct Pending {
    std::mutex m;
    int state =
        0; // 0 none, 1 waiting for the first upload (the stream's buffer), 2 waiting for the key-on of that buffer
    uint32_t addr = 0;
    double requestMs = 0.0;
    std::string name;
    std::vector<dsettings::SubCue> cues;
} g_pending;
std::atomic<bool> g_waiting{false};
constexpr double kKeyOnTimeoutMs = 4000.0;

double audioMs() {
    return g_audio ? static_cast<double>(g_audio->framesRendered()) * 1000.0 / 48000.0 : 0.0;
}

// Starts the track with cue time 0 at audio time `zeroMs` (the track's clock is read now, so the offset back-dates it).
void startTrackAt(std::vector<dsettings::SubCue>&& cues, const std::string& name, double zeroMs, const char* how) {
    std::lock_guard<std::mutex> presentation(g_presentationMutex);
    g_touchingBoundaries.clear();
    for (size_t i = 1; i < cues.size(); ++i)
        if (cues[i - 1].endMs == cues[i].startMs)
            g_touchingBoundaries.push_back(cues[i].startMs);
    g_track.start(std::move(cues), (zeroMs - audioMs()) + g_offsetMs);
    g_startedInMovie.store(name == "intro" ? kIntroId : kOutroId);
    std::fprintf(stderr, "[subtitles] %s narration: cue time 0 = audio time %.3f s (%s), offset %+.0f ms\n",
                 name.c_str(), zeroMs / 1000.0, how, g_offsetMs);
}

// Request seen but no key-on after kKeyOnTimeoutMs: fall back to the request time (called from the render thread).
void checkTimeout() {
    if (!g_waiting.load())
        return;
    std::lock_guard<std::mutex> l(g_pending.m);
    if (g_pending.state != 0 && audioMs() - g_pending.requestMs > kKeyOnTimeoutMs) {
        const uint16_t requestedId = g_pending.name == "intro" ? kIntroId : kOutroId;
        if (g_startedInMovie.load() == requestedId)
            std::fprintf(stderr, "[subtitles] %s repeat resource request ignored: no new voice key-on\n",
                         g_pending.name.c_str());
        else
            startTrackAt(std::move(g_pending.cues), g_pending.name, g_pending.requestMs + 100.0,
                         "no key-on seen, request time + 100 ms");
        g_pending.cues.clear();
        g_pending.state = 0;
        g_waiting = false;
    }
}

bool endsWithI(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::toupper(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::toupper(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}

bool gameOptionOn() {
    if (!g_rdram)
        return true;
    uint32_t v = 1;
    std::memcpy(&v, g_rdram + kGameSubtitleOption, 4);
    return v != 0;
}
} // namespace

void init(spu2::AudioOutput* audio, uint8_t* rdram, const std::string& language) {
    g_audio = audio;
    g_rdram = rdram;
    g_language = language.empty() ? std::string("english") : language;
    for (char& c : g_language)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    g_enabled = drakan_cfg::getBool("subtitles", "narration", "DRAKAN_SUBTITLES", true);
    const std::string sz = drakan_cfg::getString("subtitles", "size", nullptr, "auto");
    g_size = (sz.empty() || sz == "auto") ? 0.0f : static_cast<float>(std::atof(sz.c_str()));
    g_offsetMs = std::getenv("DRAKAN_SUB_OFFSET_MS") ? std::atof(std::getenv("DRAKAN_SUB_OFFSET_MS"))
                                                     : 0.0; // cue time 0 = key-on of the stream voice
    if (audio)
        g_track.setClock([audio]() { return static_cast<double>(audio->framesRendered()) * 1000.0 / 48000.0; });
}

void setEnabled(bool on) {
    // Visibility is independent of the media timeline. Keep following the
    // soundtrack while hidden so re-enabling resumes at the current cue.
    g_enabled = on;
}

void setSize(float size) {
    g_size = size;
}

void onResourceRequest(const hle::ResourceId& id, uint32_t offset) {
    if (offset != 0 || id.type != kTypeSoundData || id.sub != kNarrationSub || (id.id != kIntroId && id.id != kOutroId))
        return;
    if (!endsWithI(id.file, "LEVEL0RO.SDB"))
        return;
    const char* name = id.id == kIntroId ? "intro" : "outro";
    const std::string path = drakan_cfg::exeDir() + "subtitles\\" + name + "_" + g_language + ".srt";
    std::vector<dsettings::SubCue> cues = dsettings::loadSrtFile(path);
    if (cues.empty()) {
        std::fprintf(stderr, "[subtitles] %s narration requested but '%s' was not found / empty: no subtitles\n", name,
                     path.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> l(g_pending.m);
        g_pending.cues = std::move(cues);
        g_pending.name = name;
        g_pending.requestMs = audioMs();
        g_pending.addr = 0;
        g_pending.state = 1;
        g_waiting = true;
    }
    std::fprintf(stderr, "[subtitles] %s narration requested at audio time %.3f s (%s), waiting for the voice key-on\n",
                 name, audioMs() / 1000.0, path.c_str());
}

bool wantsSpuEvents() {
    return g_waiting.load();
}

void onSpuUpload(uint32_t spuAddr, uint32_t bytes) {
    if (!g_waiting.load() || bytes < 65536u)
        return;
    std::lock_guard<std::mutex> l(g_pending.m);
    if (g_pending.state == 1) {
        g_pending.addr = spuAddr;
        g_pending.state = 2;
    }
}

void onVoiceKeyOn(uint32_t spuAddr, uint64_t frames) {
    if (!g_waiting.load())
        return;
    std::lock_guard<std::mutex> l(g_pending.m);
    if (g_pending.state == 2 && spuAddr == g_pending.addr) {
        startTrackAt(std::move(g_pending.cues), g_pending.name, static_cast<double>(frames) * 1000.0 / 48000.0,
                     "voice key-on");
        g_pending.state = 0;
        g_waiting = false;
    }
}

void stop() {
    g_track.stop();
    std::lock_guard<std::mutex> l(g_pending.m);
    g_pending.state = 0;
    g_waiting = false;
    g_startedInMovie.store(0);
}

void installHudLayer(dsettings::SettingsOverlay& overlay) {
    dsettings::SettingsOverlay::HudLayer layer;
    layer.active = []() {
        checkTimeout();
        return g_enabled.load() && g_track.running();
    };
    layer.draw = [](float w, float h, float) {
        if (!gameOptionOn())
            return;
        float alpha = 0.0f;
        double trackMs = 0.0;
        std::string text;
        {
            std::lock_guard<std::mutex> presentation(g_presentationMutex);
            text = g_track.current(&alpha, &trackMs);
            if (!text.empty())
                for (const int boundary : g_touchingBoundaries)
                    if (trackMs >= boundary - 150.0 && trackMs <= boundary + 150.0) {
                        alpha = 1.0f;
                        break;
                    }
        }
        if (const char* level = std::getenv("DRAKAN_SUB_LOG"); level && level[0] == '2')
            std::fprintf(stderr, "[subframe] %.1f %.1f %.3f %.12s @%lld\n", audioMs(), trackMs,
                         text.empty() ? 0.0f : alpha, text.c_str(),
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count()));
        if (std::getenv("DRAKAN_SUB_LOG")) // diagnostics: when does each cue appear (audio clock), for comparison with
                                           // an audio dump
        {
            static std::string lastText;
            if (text != lastText) {
                std::fprintf(stderr, "[subtitles] cue %s at audio time %.3f s (track time %.3f s): %.50s\n",
                             text.empty() ? "off" : "on ", audioMs() / 1000.0, trackMs / 1000.0, text.c_str());
                lastText = text;
            }
        }
        if (!text.empty())
            dsettings::SettingsOverlay::drawSubtitleBand(text, alpha, w, h, g_size.load());
    };
    overlay.setHudLayer(std::move(layer));
}
#else
void init(spu2::AudioOutput*, uint8_t*, const std::string&) {}
void setEnabled(bool) {}
void setSize(float) {}
void onResourceRequest(const hle::ResourceId&, uint32_t) {}
bool wantsSpuEvents() {
    return false;
}
void onSpuUpload(uint32_t, uint32_t) {}
void onVoiceKeyOn(uint32_t, uint64_t) {}
void stop() {}
void installHudLayer(dsettings::SettingsOverlay&) {}
#endif
} // namespace drakan_subtitles
