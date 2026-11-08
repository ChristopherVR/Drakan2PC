// Drakan port: render mode of the pre-rendered Order of the Flame opening (drakan_intro_movie.h).
//
// DRAKAN_INTRO_RENDER=<folder> turns the game into an offline renderer for "OotF 00 - Intro":
//   * the engine time step is fixed at 1/60 s per tick (renderTickSeconds), whatever the wall clock says, so the scenes
//     run frame by frame at one exact speed; with vsync on (the tool sets DRAKAN_VSYNC=1) each tick belongs to one
//     displayed frame;
//   * from the first frame with the world on screen, every completed frame is read back (Renderer::captureDisplay, the
//     game image at the internal scale, no window furniture) and piped as raw RGBA to an ffmpeg process that writes
//     <folder>/video.mkv;
//   * the audio output position (48 kHz frames rendered, the media clock of DRAKAN_AUDIO_DUMP's wav) of every frame
//   goes
//     to <folder>/frames.csv, so the tool can cut the wav to the video and report any drift;
//   * when the next level starts loading the pipe is closed, <folder>/render.json is written and the game quits.
// Env: DRAKAN_INTRO_RENDER_FFMPEG (ffmpeg executable, default "ffmpeg"), DRAKAN_INTRO_RENDER_ARGS (encoder arguments
// placed before the output file, default x264 crf 14), DRAKAN_INTRO_RENDER_FPS (default 60).
#include "drakan_intro_movie.h"

#include "drakan_lifecycle.h"
#include "drakan_pause.h"
#include "gsr/gsr.h"
#include "ps2_runtime.h"
#include "spu2/AudioOutput.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace drakan_intro_movie {
namespace {

constexpr const char* kIntroLevel = "OotF 00 - Intro";

struct Render {
    std::string dir;
    std::string ffmpeg = "ffmpeg";
    std::string args = "-c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p";
    int fps = 60;
    std::atomic<bool> loaded{false}; // the Intro level finished loading
    std::atomic<bool> done{false};
    std::mutex mu;
    bool recording = false;
    std::FILE* pipe = nullptr;
    std::FILE* csv = nullptr;
    uint32_t width = 0, height = 0;
    uint64_t frames = 0, audioFirst = 0, audioLast = 0;
    std::chrono::steady_clock::time_point wallFirst{}, wallLast{};
    PS2Runtime* runtime = nullptr;
};

Render& rs() {
    static Render r;
    return r;
}

const char* envDir() {
    static const char* d = std::getenv("DRAKAN_INTRO_RENDER");
    return d && *d ? d : nullptr;
}

std::atomic<spu2::AudioOutput*>& audio() {
    static std::atomic<spu2::AudioOutput*> a{nullptr};
    return a;
}

#if defined(_WIN32)
#define DRK_POPEN _popen
#define DRK_PCLOSE _pclose
#else
#define DRK_POPEN popen
#define DRK_PCLOSE pclose
#endif

bool openPipe(Render& r, uint32_t w, uint32_t h) {
    std::string cmd = "\"" + r.ffmpeg + "\" -y -hide_banner -loglevel error -f rawvideo -pix_fmt rgba -s " +
                      std::to_string(w) + "x" + std::to_string(h) + " -framerate " + std::to_string(r.fps) +
                      " -i - -an " + r.args + " \"" + r.dir + "/video.mkv\"";
#if defined(_WIN32)
    cmd = "\"" + cmd + "\""; // cmd.exe /c strips one pair of quotes
#endif
    r.pipe = DRK_POPEN(cmd.c_str(), "wb");
    if (!r.pipe) {
        std::fprintf(stderr, "[intro-render] cannot start ffmpeg: %s\n", cmd.c_str());
        return false;
    }
    r.csv = std::fopen((r.dir + "/frames.csv").c_str(), "wb");
    std::fprintf(stderr, "[intro-render] recording %ux%u @ %d fps: %s\n", w, h, r.fps, cmd.c_str());
    return true;
}

void finish(Render& r, const char* why) {
    std::lock_guard<std::mutex> lk(r.mu);
    if (!r.recording)
        return;
    r.recording = false;
    if (audio().load())
        audio().load()->setManualRender(false);
    if (r.pipe) {
        std::fflush(r.pipe);
        DRK_PCLOSE(r.pipe);
        r.pipe = nullptr;
    }
    if (r.csv) {
        std::fclose(r.csv);
        r.csv = nullptr;
    }
    const double wall = std::chrono::duration<double>(r.wallLast - r.wallFirst).count();
    if (std::FILE* f = std::fopen((r.dir + "/render.json").c_str(), "wb")) {
        std::fprintf(f,
                     "{\"frames\": %llu, \"width\": %u, \"height\": %u, \"fps\": %d, \"audioFirst\": %llu, "
                     "\"audioLast\": %llu, \"wallSeconds\": %.3f}\n",
                     static_cast<unsigned long long>(r.frames), r.width, r.height, r.fps,
                     static_cast<unsigned long long>(r.audioFirst), static_cast<unsigned long long>(r.audioLast), wall);
        std::fclose(f);
    }
    std::fprintf(stderr, "[intro-render] %s: %llu frames, audio %.3f s, wall %.3f s\n", why,
                 static_cast<unsigned long long>(r.frames), static_cast<double>(r.audioLast - r.audioFirst) / 48000.0,
                 wall);
    r.done.store(true);
}

} // namespace

bool renderModeActive() {
    return envDir() != nullptr;
}

double renderTickSeconds() {
    if (!envDir())
        return 0.0;
    return 1.0 / static_cast<double>(rs().fps > 0 ? rs().fps : 60);
}

void renderModeAttach() {
    if (!envDir())
        return;
    Render& r = rs();
    r.dir = envDir();
    if (const char* e = std::getenv("DRAKAN_INTRO_RENDER_FFMPEG"))
        if (*e)
            r.ffmpeg = e;
    if (const char* e = std::getenv("DRAKAN_INTRO_RENDER_ARGS"))
        if (*e)
            r.args = e;
    if (const char* e = std::getenv("DRAKAN_INTRO_RENDER_FPS"))
        if (std::atoi(e) > 0)
            r.fps = std::atoi(e);
    static const auto after =
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
            if (!e.fromSave && e.level == kIntroLevel && e.ok) {
                rs().runtime = e.runtime;
                rs().loaded.store(true);
                std::fprintf(stderr,
                             "[intro-render] the Intro level is loaded; recording starts with its first frame\n");
            }
        });
    static const auto before =
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event& e) {
            Render& r = rs();
            if (e.level == kIntroLevel || !r.loaded.load())
                return;
            finish(r, "the next level is loading");
            if (e.runtime)
                e.runtime->requestStop();
        });
    (void)after;
    (void)before;
    std::fprintf(stderr, "[intro-render] render mode: fixed 1/%d s time step, output %s\n", r.fps, r.dir.c_str());
}

void onCompletedFrame(gsr::Renderer& renderer, uint64_t) {
    if (!envDir())
        return;
    Render& r = rs();
    if (!r.loaded.load() || r.done.load())
        return;
    if (drakan_pause::guiScreen() >= 0 && !r.recording)
        return; // still loading screens around the level
    gsr::Image img;
    if (!renderer.captureDisplay(img))
        return;
    std::lock_guard<std::mutex> lk(r.mu);
    if (r.done.load())
        return;
    if (!r.recording) {
        if (r.frames != 0)
            return;
        r.width = img.width & ~1u; // yuv420p needs even sizes
        r.height = img.height & ~1u;
        if (!openPipe(r, r.width, r.height)) {
            r.done.store(true);
            return;
        }
        r.recording = true;
    }
    // Sound in lockstep: from the first frame the SPU2 is stepped here, one video frame of audio per frame (the
    // wall-clock thread stops), so the sound is as deterministic as the engine time.
    spu2::AudioOutput* out = audio().load();
    if (out) {
        if (r.frames == 0)
            out->setManualRender(true);
        out->renderManual(static_cast<size_t>(48000 / (r.fps > 0 ? r.fps : 60)));
    }
    const uint64_t played = out ? out->framesRendered() : 0;
    const auto now = std::chrono::steady_clock::now();
    if (r.frames == 0) {
        r.audioFirst = played >= 800 ? played - 48000 / (r.fps > 0 ? r.fps : 60) : 0; // the start of this frame's block
        r.wallFirst = now;
    }
    r.audioLast = played;
    r.wallLast = now;
    if (img.width == r.width) {
        std::fwrite(img.rgba.data(), 4, static_cast<size_t>(r.width) * r.height, r.pipe);
    } else {
        for (uint32_t y = 0; y < r.height; ++y)
            std::fwrite(img.rgba.data() + static_cast<size_t>(y) * img.width * 4, 4, r.width, r.pipe);
    }
    if (r.csv)
        std::fprintf(r.csv, "%llu,%llu\n", static_cast<unsigned long long>(r.frames),
                     static_cast<unsigned long long>(played));
    ++r.frames;
}

void renderModeSetAudio(spu2::AudioOutput* a) {
    audio().store(a);
}

} // namespace drakan_intro_movie
