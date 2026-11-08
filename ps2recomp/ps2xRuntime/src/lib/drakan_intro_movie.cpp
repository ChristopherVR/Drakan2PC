// Drakan port: pre-rendered Order of the Flame opening. Playback side; the render side is drakan_intro_render.cpp.
// See drakan_intro_movie.h.
#include "drakan_intro_movie.h"

#include "drakan_config.h"
#include "drakan_levelchange.h"
#include "drakan_lifecycle.h"
#include "spu2/AudioOutput.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#if PS2X_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace drakan_intro_movie {
namespace {

constexpr const char* kIntroLevel = "OotF 00 - Intro";
constexpr const char* kMovieFile = "movies/ootf_intro.mkv";
constexpr double kEndFade = 0.4;                // seconds: the picture fades to black on a skip / at the end
constexpr double kSkipDebounce = 1.0;           // seconds after the start before a button press counts as a skip
constexpr double kCoverTimeout = 25.0;          // seconds the black cover waits for the live level to end after a skip
constexpr uint64_t kRingMax = 48000ull * 2 * 6; // 6 s of decoded stereo audio

enum Phase : int { Idle = 0, Playing = 1, Ending = 2 };

using SteadyClock = std::chrono::steady_clock;

struct Picture {
    double pts = 0.0;
    std::shared_ptr<Frame> frame;
};

struct State {
    std::atomic<int> phase{Idle};
    std::atomic<bool> skipRequested{false};
    std::atomic<bool> eof{false};  // the decoder reached the end of the file
    std::atomic<bool> quit{false}; // the decoder must stop
    std::atomic<bool> audioLive{false};
    std::atomic<uint64_t> played{0}; // 48 kHz frames handed to the audio output since the sound began
    std::atomic<bool> ready{false};  // first picture decoded, the clock runs
    std::atomic<bool> padWasDown{false};
    std::atomic<bool> exitFired{false}; // the Intro's exit was requested
    double exitAt = 0.0;
    std::atomic<bool> levelChanging{false}; // the live level ended and the next one is loading
    SteadyClock::time_point started{}, ending{};
    std::mutex mu; // pictures, ring
    std::deque<Picture> pictures;
    std::shared_ptr<const Frame> shown;
    double shownPts = -1.0;
    std::deque<float> ring; // interleaved stereo, 48 kHz
    std::condition_variable room;
    std::thread decoder;
    uint64_t serial = 0;
    SteadyClock::time_point clockStart{};
    bool movieHadAudio = false;
    std::atomic<double> period{1.0 / 60.0}; // seconds per picture
    SteadyClock::time_point lastPresent{};
    uint64_t lastSerial = 0, calls = 0, repeats = 0, skipped = 0, late = 0; // present thread: playback smoothness
    double presentEma = 0.0; // smoothed seconds between presents (present thread only)
};

State& st() {
    static State s;
    return s;
}

std::atomic<spu2::AudioOutput*> g_audio{nullptr};
std::atomic<bool> g_attached{false};

double sinceSeconds(SteadyClock::time_point t) {
    return std::chrono::duration<double>(SteadyClock::now() - t).count();
}

std::string moviePath() {
    return drakan_cfg::exeDir() + kMovieFile;
}

bool movieEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_OOTF_INTRO_MOVIE");
        return !(e && e[0] == '0');
    }();
    return on;
}

#if PS2X_HAS_FFMPEG
// Decoder thread: video to RGBA pictures with a presentation time, audio to 48 kHz stereo float in the ring.
void decodeMain(std::string path) {
    State& s = st();
    AVFormatContext* fmt = nullptr;
    AVCodecContext* vctx = nullptr;
    AVCodecContext* actx = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    int vs = -1, as = -1;
    auto fail = [&](const char* why) {
        std::fprintf(stderr, "[intro-movie] %s (%s): the live level plays instead\n", why, path.c_str());
        s.eof.store(true);
        s.quit.store(true); // the cover is dropped by tick()
    };
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
        fail("cannot open");
        goto done;
    }
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && vs < 0)
            vs = static_cast<int>(i);
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && as < 0)
            as = static_cast<int>(i);
    }
    if (vs < 0) {
        fail("no video stream");
        goto done;
    }
    {
        auto open = [&](int index, AVCodecContext*& ctx) {
            const AVCodecParameters* par = fmt->streams[index]->codecpar;
            const AVCodec* dec = avcodec_find_decoder(par->codec_id);
            if (!dec)
                return false;
            ctx = avcodec_alloc_context3(dec);
            if (!ctx || avcodec_parameters_to_context(ctx, par) < 0)
                return false;
            ctx->thread_count = 4;
            return avcodec_open2(ctx, dec, nullptr) >= 0;
        };
        if (!open(vs, vctx)) {
            fail("no decoder for the video");
            goto done;
        }
        if (as >= 0 && !open(as, actx)) {
            std::fprintf(stderr, "[intro-movie] no decoder for the sound track, playing silent\n");
            as = -1;
        }
        if (as >= 0) {
            AVChannelLayout out{};
            av_channel_layout_default(&out, 2);
            if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, 48000, &actx->ch_layout, actx->sample_fmt,
                                    actx->sample_rate, 0, nullptr) < 0 ||
                swr_init(swr) < 0) {
                std::fprintf(stderr, "[intro-movie] cannot resample the sound track, playing silent\n");
                swr_free(&swr);
                as = -1;
            }
        }
        s.movieHadAudio = as >= 0;
        {
            const AVRational fr = fmt->streams[vs]->avg_frame_rate;
            if (fr.num > 0 && fr.den > 0)
                s.period.store(static_cast<double>(fr.den) / fr.num);
        }
        const double vbase = fmt->streams[vs]->start_time == AV_NOPTS_VALUE
                                 ? 0.0
                                 : fmt->streams[vs]->start_time * av_q2d(fmt->streams[vs]->time_base);
        std::fprintf(stderr, "[intro-movie] %s: %dx%d, %s sound\n", path.c_str(), vctx->width, vctx->height,
                     as >= 0 ? "with" : "without");
        auto pushPicture = [&](AVFrame* f) {
            auto pic = std::make_shared<Frame>();
            pic->width = static_cast<uint32_t>(f->width);
            pic->height = static_cast<uint32_t>(f->height);
            pic->rgba.resize(static_cast<size_t>(pic->width) * pic->height * 4);
            sws = sws_getCachedContext(sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), f->width,
                                       f->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            uint8_t* dst[4] = {pic->rgba.data(), nullptr, nullptr, nullptr};
            int stride[4] = {static_cast<int>(pic->width) * 4, 0, 0, 0};
            if (!sws || sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride) <= 0)
                return;
            const int64_t t = f->best_effort_timestamp;
            const double pts = t == AV_NOPTS_VALUE ? 0.0 : t * av_q2d(fmt->streams[vs]->time_base) - vbase;
            std::unique_lock<std::mutex> lk(s.mu);
            // Keep at most ~12 pictures ahead of the clock.
            s.room.wait(lk, [&] { return s.quit.load() || s.pictures.size() < 12; });
            if (s.quit.load())
                return;
            pic->serial = ++s.serial;
            s.pictures.push_back({pts, std::move(pic)});
            if (!s.ready.load() && s.pictures.size() >= 3) {
                s.clockStart = SteadyClock::now();
                s.ready.store(true);
            }
        };
        auto pushAudio = [&](AVFrame* f) {
            const int cap = swr_get_out_samples(swr, f->nb_samples);
            if (cap <= 0)
                return;
            std::vector<float> buf(static_cast<size_t>(cap) * 2);
            uint8_t* o[1] = {reinterpret_cast<uint8_t*>(buf.data())};
            const int n = swr_convert(swr, o, cap, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
            if (n <= 0)
                return;
            std::unique_lock<std::mutex> lk(s.mu);
            s.room.wait(lk, [&] { return s.quit.load() || s.ring.size() < kRingMax; });
            if (s.quit.load())
                return;
            s.ring.insert(s.ring.end(), buf.begin(), buf.begin() + static_cast<size_t>(n) * 2);
        };
        auto drain = [&](AVCodecContext* ctx, bool video) {
            for (;;) {
                const int r = avcodec_receive_frame(ctx, frame);
                if (r < 0)
                    return;
                if (video)
                    pushPicture(frame);
                else
                    pushAudio(frame);
                av_frame_unref(frame);
                if (s.quit.load())
                    return;
            }
        };
        while (!s.quit.load() && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == vs) {
                avcodec_send_packet(vctx, pkt);
                drain(vctx, true);
            } else if (pkt->stream_index == as && actx) {
                avcodec_send_packet(actx, pkt);
                drain(actx, false);
            }
            av_packet_unref(pkt);
        }
        if (!s.quit.load()) {
            avcodec_send_packet(vctx, nullptr);
            drain(vctx, true);
            if (actx) {
                avcodec_send_packet(actx, nullptr);
                drain(actx, false);
            }
            if (!s.ready.load()) { // a movie shorter than three pictures still plays
                s.clockStart = SteadyClock::now();
                s.ready.store(true);
            }
            s.eof.store(true);
        }
    }
done:
    sws_freeContext(sws);
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&vctx);
    avcodec_free_context(&actx);
    avformat_close_input(&fmt);
}
#endif

// Audio output callback (render thread): adds the movie's sound and advances the media clock.
void mixAudio(float* stereo, size_t frames) {
    State& s = st();
    if (!s.ready.load())
        return;
    std::unique_lock<std::mutex> lk(s.mu);
    const size_t want = frames * 2;
    const size_t have = std::min(want, s.ring.size());
    for (size_t i = 0; i < have; ++i)
        stereo[i] += s.ring[i];
    s.ring.erase(s.ring.begin(), s.ring.begin() + static_cast<std::ptrdiff_t>(have));
    lk.unlock();
    s.room.notify_all();
    s.played.fetch_add(frames, std::memory_order_relaxed);
}

double clockSeconds() {
    State& s = st();
    if (s.audioLive.load())
        return static_cast<double>(s.played.load(std::memory_order_relaxed)) / 48000.0;
    return s.ready.load() ? sinceSeconds(s.clockStart) : 0.0;
}

void stopMovie(const char* why) {
    State& s = st();
    if (s.phase.load() == Idle && !s.decoder.joinable())
        return;
    std::fprintf(stderr,
                 "[intro-movie] cover removed (%s) after %.1f s; %llu presents (%llu later than 25 ms) showed %llu "
                 "repeated and %llu skipped pictures\n",
                 why, sinceSeconds(s.started), static_cast<unsigned long long>(s.calls),
                 static_cast<unsigned long long>(s.late), static_cast<unsigned long long>(s.repeats),
                 static_cast<unsigned long long>(s.skipped));
    if (auto* a = g_audio.load()) {
        a->setExternalSource({}, false);
        a->setGameSilenced(false);
    }
    s.quit.store(true);
    s.room.notify_all();
    if (s.decoder.joinable())
        s.decoder.join();
    s.audioLive.store(false);
    s.phase.store(Idle);
}

void startMovie() {
    State& s = st();
    if (s.phase.load() != Idle)
        stopMovie("restart");
    if (s.decoder.joinable()) { // a decoder that gave up on its own
        s.quit.store(true);
        s.room.notify_all();
        s.decoder.join();
    }
    s.quit.store(false);
    s.eof.store(false);
    s.ready.store(false);
    s.skipRequested.store(false);
    s.levelChanging.store(false);
    s.exitFired.store(false);
    s.lastSerial = s.calls = s.repeats = s.skipped = s.late = 0;
    s.lastPresent = {};
    s.presentEma = 0.0;
    s.padWasDown.store(true); // the press that started New Game is not a skip
    s.played.store(0);
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.pictures.clear();
        s.ring.clear();
        s.shown.reset();
        s.shownPts = -1.0;
    }
    s.started = SteadyClock::now();
    s.phase.store(Playing);
    auto* audio = g_audio.load();
    // The live level's sound is silenced from the first frame; the movie's own sound joins once its first pictures
    // are decoded (mixAudio does nothing before that, and the clock starts then).
    if (audio) {
        audio->setExternalSource(&mixAudio, true);
        s.audioLive.store(true);
    }
#if PS2X_HAS_FFMPEG
    s.decoder = std::thread(decodeMain, moviePath());
#endif
}

void beginEnding(const char* why) {
    State& s = st();
    int expect = Playing;
    if (s.phase.compare_exchange_strong(expect, Ending)) {
        s.ending = SteadyClock::now();
        std::fprintf(stderr, "[intro-movie] %s at %.1f s; the Intro level's hand-over is taken now\n", why,
                     sinceSeconds(s.started));
    }
}

} // namespace

void setAudio(spu2::AudioOutput* audio) {
    g_audio.store(audio);
    renderModeSetAudio(audio);
}

void attach(gsr::Renderer* renderer) {
    if (!renderer) {
        stopMovie("renderer detached");
        g_attached.store(false);
        return;
    }
    if (g_attached.exchange(true))
        return;
    static const auto after =
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
            if (e.level != kIntroLevel) {
                stopMovie("next level loaded"); // a fallback: normally the cover goes with the end of the movie
                return;
            }
            if (e.fromSave || !e.ok || !movieEnabled())
                return;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(moviePath(), ec) || std::getenv("DRAKAN_INTRO_RENDER"))
                return;
#if PS2X_HAS_FFMPEG
            startMovie();
#endif
        });
    static const auto before =
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event& e) {
            State& s = st();
            if (e.level == kIntroLevel || s.phase.load() == Idle)
                return;
            // The live level has ended. The movie, which runs a little longer than the level (the engine catches up
            // after each scene's loading stall, the render does not), goes on playing over the next level's loading
            // and the cover goes when it is over; after a skip or its end the cover goes now.
            s.levelChanging.store(true);
            if (s.phase.load() == Ending)
                stopMovie("next level");
        });
    (void)after;
    (void)before;
    renderModeAttach();
}

bool coverActive() {
    State& s = st();
    const int phase = s.phase.load();
    if (phase == Idle)
        return false;
    if (phase == Ending && s.levelChanging.load() && sinceSeconds(s.ending) > kEndFade) {
        if (auto* a = g_audio.load()) {
            a->setExternalSource({}, false);
            a->setGameSilenced(false);
        }
        std::fprintf(stderr,
                     "[intro-movie] cover removed (movie over, next level loading) after %.1f s; %llu presents (%llu "
                     "later than 25 ms) showed %llu repeated and %llu skipped pictures\n",
                     sinceSeconds(s.started), static_cast<unsigned long long>(s.calls),
                     static_cast<unsigned long long>(s.late), static_cast<unsigned long long>(s.repeats),
                     static_cast<unsigned long long>(s.skipped));
        s.phase.store(Idle);
        return false;
    }
    if (phase == Ending && sinceSeconds(s.ending) > kCoverTimeout) {
        // The live level did not end: show it rather than a black screen forever.
        if (auto* a = g_audio.load()) {
            a->setExternalSource({}, false);
            a->setGameSilenced(false);
        }
        s.phase.store(Idle);
        return false;
    }
    if (s.quit.load() && s.eof.load() && !s.ready.load()) { // the file could not be opened
        if (auto* a = g_audio.load()) {
            a->setExternalSource({}, false);
            a->setGameSilenced(false);
        }
        s.phase.store(Idle);
        return false;
    }
    return true;
}

std::shared_ptr<const Frame> currentFrame(float& alpha) {
    State& s = st();
    alpha = 0.0f;
    const int phase = s.phase.load();
    if (phase == Idle || !s.ready.load())
        return nullptr;
    const double now = clockSeconds();
    const double period = s.period.load();
    // Present cadence: when the display refreshes at about the movie's rate (60 Hz and 60 fps) every present shows the
    // next picture, a steady one-to-one beat. Choosing the picture from the audio clock instead lands on a picture
    // boundary at some phase and then alternates repeats and skips with the clock's few ms of jitter. The audio clock
    // only corrects a drift of more than a picture or two (a late decoder, a 59.997 Hz display over minutes).
    const auto tp = SteadyClock::now();
    double gapMs = 0.0;
    if (s.lastPresent != SteadyClock::time_point{}) {
        const double dt = std::chrono::duration<double>(tp - s.lastPresent).count();
        gapMs = dt * 1000.0;
        if (s.lastSerial && gapMs > 25.0)
            ++s.late;
        // A missed refresh (a gap over 25 ms) is not the display's rate: it would pull the average out of the band
        // and drop the steady one-picture-per-present beat for the next second.
        if (dt < 0.025)
            s.presentEma = s.presentEma > 0.0 ? s.presentEma * 0.9 + dt * 0.1 : dt;
    }
    s.lastPresent = tp;
    const bool locked = s.presentEma > period * 0.85 && s.presentEma < period * 1.15;
    std::shared_ptr<const Frame> out;
    bool finished = false;
    size_t queued = 0;
    double ahead = 0.0;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto take = [&] {
            s.shown = s.pictures.front().frame;
            s.shownPts = s.pictures.front().pts;
            s.pictures.pop_front();
            s.room.notify_all();
        };
        if (locked && s.shown) {
            if (!s.pictures.empty() && s.shownPts - now < period * 1.5)
                take(); // one picture per present
            while (s.pictures.size() > 1 && now - s.pictures.front().pts > period * 2.5)
                take(); // far behind the sound: catch up
        } else {
            while (!s.pictures.empty() && s.pictures.front().pts <= now)
                take();
        }
        out = s.shown;
        queued = s.pictures.size();
        ahead = s.shownPts - now;
        finished = s.eof.load() && s.pictures.empty() && s.ring.empty();
    }
    if (out) {
        ++s.calls;
        if (out->serial == s.lastSerial) {
            ++s.repeats;
            static const bool log = std::getenv("DRAKAN_INTRO_MOVIE_LOG") != nullptr;
            if (log)
                std::fprintf(stderr,
                             "[intro-movie] t=%.3f repeat of %llu: %zu queued, picture %.1f ms ahead of the sound, "
                             "present gap %.1f ms\n",
                             sinceSeconds(s.started), static_cast<unsigned long long>(out->serial), queued,
                             ahead * 1000.0, gapMs);
        } else if (s.lastSerial && out->serial > s.lastSerial + 1) {
            s.skipped += out->serial - s.lastSerial - 1;
            static const bool log = std::getenv("DRAKAN_INTRO_MOVIE_LOG") != nullptr;
            if (log)
                std::fprintf(stderr,
                             "[intro-movie] t=%.3f picture %llu after %llu (present gap %.1f ms, ema %.1f ms, %s)\n",
                             sinceSeconds(s.started), static_cast<unsigned long long>(out->serial),
                             static_cast<unsigned long long>(s.lastSerial), gapMs, s.presentEma * 1000.0,
                             locked ? "locked" : "free");
        }
        s.lastSerial = out->serial;
    }
    if (phase == Playing && s.skipRequested.exchange(false))
        beginEnding("skipped");
    else if (phase == Playing && finished)
        beginEnding("end of the movie");
    alpha = 1.0f;
    if (s.phase.load() == Ending)
        alpha = static_cast<float>(std::max(0.0, 1.0 - sinceSeconds(s.ending) / kEndFade));
    return out;
}

void padTick(uint8_t* pad, bool escDown) {
    State& s = st();
    const int phase = s.phase.load();
    if (phase == Idle)
        return;
    const bool down = escDown || !(pad[2] & 0x08) || !(pad[3] & 0x40) || !(pad[3] & 0x20) || !(pad[3] & 0x10);
    // START, CROSS, CIRCLE, TRIANGLE (active low) or Esc, edge triggered after the debounce
    if (phase == Playing) {
        if (down && !s.padWasDown.load() && sinceSeconds(s.started) > kSkipDebounce)
            s.skipRequested.store(true);
        s.padWasDown.store(down);
        return;
    }
    // Ending: the movie is over or skipped, so the live Intro's hand-over to Ruined Village is taken now instead of by
    // its own 44 s timer (scene skips do not end that level): fire the level's exit the way the game's own trigger
    // does.
    if (s.levelChanging.load())
        return;
    const double since = sinceSeconds(s.ending);
    if (since < 0.15)
        return;
    drakan_levelchange::requestScan();
    if (!s.exitFired.load() || since - s.exitAt > 3.0) {
        if (!drakan_levelchange::exits().empty() && drakan_levelchange::requestExitIndex(0)) {
            s.exitFired.store(true);
            s.exitAt = since;
            std::fprintf(stderr, "[intro-movie] firing the Intro's exit to the next level\n");
        }
    }
}

} // namespace drakan_intro_movie
