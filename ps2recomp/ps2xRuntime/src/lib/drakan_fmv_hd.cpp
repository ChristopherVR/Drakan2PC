// Drakan port: HD movie pack. See drakan_fmv_hd.h and docs/reference/fmv.md section 9.
#include "drakan_fmv_hd.h"

#include "drakan_config.h"
#include "gsr/gsr.h"
#include "runtime/ps2_ipu.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if PS2X_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace drakan_fmv_hd {
namespace {

struct MoviePack {
    std::string name, video;
    uint32_t width = 0, height = 0, frames = 0;
};

struct FrameRef {
    uint32_t movie, frame;
};

std::vector<MoviePack> g_movies;
std::unordered_map<uint64_t, std::vector<FrameRef>> g_frames; // compressed frame key -> occurrences
std::atomic<gsr::Renderer*> g_gs{nullptr};
uint32_t g_lastMovie = ~0u, g_lastFrame = 0; // EE thread only

// Key of the compressed bytes IDEC consumed: CRC-32 (zlib polynomial) and length, as fmv_hd_pack.py frame_key().
uint64_t frameKey(const uint8_t* p, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return (uint64_t)(crc ^ 0xFFFFFFFFu) << 32 | (uint32_t)n;
}

bool loadPacks() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(drakan_cfg::exeDir() + "movies");
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".txt")
            continue;
        fs::path video = entry.path();
        video.replace_extension(".mkv");
        if (!fs::is_regular_file(video, ec))
            continue;
        std::ifstream in(entry.path());
        std::string tag;
        MoviePack m;
        uint32_t hdW = 0, hdH = 0;
        if (!(in >> tag >> m.width >> m.height >> m.frames >> hdW >> hdH) || tag != "movie" || !m.width || !m.height ||
            m.width % 16 || m.height % 16) {
            std::fprintf(stderr, "[fmv-hd] %s: bad index header, skipped\n", entry.path().filename().string().c_str());
            continue;
        }
        m.name = entry.path().stem().string();
        m.video = video.string();
        const uint32_t index = (uint32_t)g_movies.size();
        std::string hex;
        uint32_t n = 0;
        while (n < m.frames && in >> hex)
            g_frames[std::stoull(hex, nullptr, 16)].push_back({index, n++});
        m.frames = n;
        std::fprintf(stderr, "[fmv-hd] %s: %u frames %ux%u -> %ux%u\n", m.name.c_str(), n, m.width, m.height, hdW, hdH);
        g_movies.push_back(std::move(m));
    }
    return !g_movies.empty();
}

#if PS2X_HAS_FFMPEG
// Decodes the HD videos on its own thread. The window starts at the frame the GS asked for last (it shows each frame a
// little after the IPU decoded it, once per frame buffer) and reaches a few frames past the newest frame the IPU
// decoded, which runs ahead in bursts (the game's 6 slot ring).
class Decoder {
public:
    static constexpr uint32_t kAhead = 3, kMaxWindow = 16;

    ~Decoder() { stop(); }

    void request(uint32_t movie, uint32_t frame) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!thread_.joinable())
            thread_ = std::thread([this] { run(); });
        if (movie != wantMovie_ || frame + kMaxWindow < ipuFrame_) { // another movie, or this one restarted
            summary();
            lastServed_ = ~0u; // the final frame is re-stamped after the end: counted once
            gsFrame_ = frame;
        }
        wantMovie_ = movie;
        ipuFrame_ = frame;
        cv_.notify_all();
    }

    std::shared_ptr<const gsr::MovieImage> get(uint32_t movie, uint32_t frame) {
        std::unique_lock<std::mutex> lk(mu_);
        if (movie != wantMovie_)
            ipuFrame_ = frame;
        wantMovie_ = movie;
        gsFrame_ = frame;
        cv_.notify_all();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
        for (;;) {
            if (cacheMovie_ == movie) {
                auto it = cache_.find(frame);
                if (it != cache_.end()) {
                    if (it->second && frame != lastServed_) {
                        ++served_;
                        lastServed_ = frame;
                    }
                    if (frame + 1 >= g_movies[movie].frames)
                        summary();
                    return it->second;
                }
            }
            if (failed_ == movie || cv_.wait_until(lk, deadline) == std::cv_status::timeout)
                break;
        }
        ++misses_;
        if ((misses_ & (misses_ - 1)) == 0) // 1, 2, 4, 8, ...
            std::fprintf(stderr, "[fmv-hd] %s frame %u not decoded in time (%u misses; IPU at %u, cached %u..%u)\n",
                         movie < g_movies.size() ? g_movies[movie].name.c_str() : "?", frame, misses_, ipuFrame_,
                         cache_.empty() ? 0 : cache_.begin()->first, cache_.empty() ? 0 : cache_.rbegin()->first);
        return nullptr;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            summary();
            quit_ = true;
            cv_.notify_all();
        }
        if (thread_.joinable())
            thread_.join();
        close();
    }

private:
    void summary() { // mu_ held
        if (wantMovie_ < g_movies.size() && (served_ || misses_))
            std::fprintf(stderr, "[fmv-hd] %s: %u HD frames shown, %u frames fell back to the original\n",
                         g_movies[wantMovie_].name.c_str(), served_, misses_);
        served_ = misses_ = 0;
    }

    void run() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!quit_) {
            const uint32_t movie = wantMovie_, want = gsFrame_;
            if (movie == ~0u || movie == failed_) {
                cv_.wait(lk);
                continue;
            }
            if (movie != cacheMovie_) {
                cache_.clear();
                cacheMovie_ = movie;
            }
            // Keep two frames behind the GS (the second frame buffer gets the same frame), drop anything far ahead.
            for (auto it = cache_.begin(); it != cache_.end();)
                it = it->first + 2 < want || it->first > want + kMaxWindow ? cache_.erase(it) : std::next(it);
            const uint32_t last =
                std::min({std::max(want, ipuFrame_) + kAhead, want + kMaxWindow - 1, g_movies[movie].frames - 1});
            uint32_t next = want;
            while (next <= last && cache_.count(next))
                ++next;
            if (next > last) {
                cv_.wait(lk);
                continue;
            }
            lk.unlock();
            std::shared_ptr<gsr::MovieImage> image;
            uint32_t got = 0;
            const bool ok = decodeFrame(movie, next, image, got);
            lk.lock();
            if (!ok) {
                failed_ = movie;
                cv_.notify_all();
                continue;
            }
            if (movie == cacheMovie_) {
                if (!image || got > next)
                    cache_[next] = nullptr; // past the end of the stream or missing: keep the native frame
                if (image)
                    cache_[got] = std::move(image);
            }
            cv_.notify_all();
        }
    }

    // Decodes the next frame of the stream, seeking first when `frame` is not just ahead of the stream position.
    bool decodeFrame(uint32_t movie, uint32_t frame, std::shared_ptr<gsr::MovieImage>& out, uint32_t& index) {
        if (movie != openMovie_ && !open(movie))
            return false;
        if (frame < pos_ || frame > pos_ + kMaxWindow) { // decoding forward beats a seek to the keyframe before
            const int64_t ts = av_rescale_q((int64_t)frame, av_inv_q(rate_), stream_->time_base);
            if (av_seek_frame(fmt_, stream_->index, ts, AVSEEK_FLAG_BACKWARD) < 0)
                return false;
            avcodec_flush_buffers(codec_);
            pos_ = 0; // unknown until the first decoded frame
            skipTo_ = frame;
        }
        for (;;) {
            const int r = avcodec_receive_frame(codec_, frame_);
            if (r == 0) {
                const int64_t pts = frame_->best_effort_timestamp;
                index = pts == AV_NOPTS_VALUE
                            ? pos_
                            : (uint32_t)av_rescale_q(pts - start_, stream_->time_base, av_inv_q(rate_));
                pos_ = index + 1;
                if (index < skipTo_)
                    continue;
                skipTo_ = 0;
                out = std::make_shared<gsr::MovieImage>();
                out->width = (uint32_t)frame_->width;
                out->height = (uint32_t)frame_->height;
                out->rgba.resize((size_t)out->width * out->height);
                // Plain bilinear chroma: ~2 ms per 2048x2048 frame; SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT costs ~36 ms
                // on this (single) thread and held the decoder below the movie's frame rate.
                sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height, (AVPixelFormat)frame_->format,
                                            frame_->width, frame_->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                            nullptr, nullptr);
                uint8_t* dst[4] = {reinterpret_cast<uint8_t*>(out->rgba.data()), nullptr, nullptr, nullptr};
                int stride[4] = {(int)out->width * 4, 0, 0, 0};
                if (!sws_ || sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, stride) <= 0)
                    return false;
                return true;
            }
            if (r == AVERROR_EOF) {
                out.reset();
                index = pos_;
                pos_ = ~0u >> 1; // past the end: the next request seeks
                return true;
            }
            if (r != AVERROR(EAGAIN))
                return false;
            const int rd = av_read_frame(fmt_, packet_);
            if (rd < 0) {
                avcodec_send_packet(codec_, nullptr); // drain
                continue;
            }
            if (packet_->stream_index == stream_->index)
                avcodec_send_packet(codec_, packet_);
            av_packet_unref(packet_);
        }
    }

    bool open(uint32_t movie) {
        close();
        const MoviePack& m = g_movies[movie];
        const AVCodec* dec = nullptr;
        if (avformat_open_input(&fmt_, m.video.c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(fmt_, nullptr) < 0) {
            std::fprintf(stderr, "[fmv-hd] %s: cannot open %s\n", m.name.c_str(), m.video.c_str());
            return false;
        }
        const int si = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
        if (si < 0 || !dec) {
            std::fprintf(stderr, "[fmv-hd] %s: no decodable video stream\n", m.name.c_str());
            return false;
        }
        stream_ = fmt_->streams[si];
        codec_ = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(codec_, stream_->codecpar);
        codec_->thread_count = 0; // automatic: frame + slice threads
        if (avcodec_open2(codec_, dec, nullptr) < 0) {
            std::fprintf(stderr, "[fmv-hd] %s: decoder %s failed to open\n", m.name.c_str(), dec->name);
            return false;
        }
        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        rate_ = stream_->avg_frame_rate.num ? stream_->avg_frame_rate : AVRational{25, 1};
        start_ = stream_->start_time == AV_NOPTS_VALUE ? 0 : stream_->start_time;
        pos_ = 0;
        skipTo_ = 0;
        openMovie_ = movie;
        std::fprintf(stderr, "[fmv-hd] %s: %s %dx%d @ %d/%d fps\n", m.name.c_str(), dec->name, codec_->width,
                     codec_->height, rate_.num, rate_.den);
        return true;
    }

    void close() {
        sws_freeContext(sws_);
        sws_ = nullptr;
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_);
        avformat_close_input(&fmt_);
        stream_ = nullptr;
        openMovie_ = ~0u;
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    bool quit_ = false;
    uint32_t wantMovie_ = ~0u, gsFrame_ = 0, ipuFrame_ = 0, failed_ = ~0u, misses_ = 0;
    uint32_t served_ = 0, lastServed_ = ~0u;
    uint32_t cacheMovie_ = ~0u;
    std::map<uint32_t, std::shared_ptr<const gsr::MovieImage>> cache_; // nullptr: frame not in the stream
    // decoder thread only
    AVFormatContext* fmt_ = nullptr;
    AVStream* stream_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVRational rate_{25, 1};
    int64_t start_ = 0;
    uint32_t openMovie_ = ~0u, pos_ = 0, skipTo_ = 0;
};

Decoder& decoder() {
    static Decoder d;
    return d;
}

// PS2Ipu frame observer (EE thread): identify the frame, register its signature, start decoding the HD frame.
void onIpuFrame(const uint8_t* bits, size_t bytes, const uint8_t* macroblocks, uint32_t count) {
    gsr::Renderer* gs = g_gs.load(std::memory_order_acquire);
    if (!gs)
        return;
    const auto it = g_frames.find(frameKey(bits, bytes));
    if (it == g_frames.end())
        return;
    // Identical compressed frames (black lead-in) occur more than once: prefer the next one of the current movie.
    const FrameRef* ref = &it->second.front();
    for (const FrameRef& r : it->second)
        if (r.movie == g_lastMovie && r.frame > g_lastFrame) {
            ref = &r;
            break;
        }
    const MoviePack& m = g_movies[ref->movie];
    if ((uint64_t)count * 256u != (uint64_t)m.width * m.height)
        return;
    if (ref->movie != g_lastMovie)
        std::fprintf(stderr, "[fmv-hd] %s started (frame %u)\n", m.name.c_str(), ref->frame);
    g_lastMovie = ref->movie;
    g_lastFrame = ref->frame;
    uint64_t signature = 0;
    for (uint32_t i = 0; i < count; ++i)
        signature += gsr::movie::blockTerm(gsr::movie::blockHash(macroblocks + (size_t)i * gsr::movie::kBlockBytes));
    gs->noteMovieFrame(signature, m.width, m.height, ref->movie, ref->frame);
    decoder().request(ref->movie, ref->frame);
}
#endif

} // namespace

void attach(gsr::Renderer* renderer) {
#if PS2X_HAS_FFMPEG
    static const bool loaded = drakan_cfg::getBool("video", "hd_movies", "DRAKAN_HD_MOVIES", true) && loadPacks();
    if (!loaded)
        return;
    if (!renderer) {
        PS2Ipu::setFrameObserver(nullptr);
        g_gs.store(nullptr, std::memory_order_release);
        decoder().stop();
        return;
    }
    renderer->setMovieFrameSource([](uint32_t movie, uint32_t frame) { return decoder().get(movie, frame); });
    g_gs.store(renderer, std::memory_order_release);
    PS2Ipu::setFrameObserver(&onIpuFrame);
#else
    (void)renderer;
#endif
}

} // namespace drakan_fmv_hd
