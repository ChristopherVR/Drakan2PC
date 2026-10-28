// Narration subtitles (std only): SRT parser + a track that is driven by a media clock. The game itself has no text for
// the intro / outro narration (docs/reference/subtitles.md); the runtime starts a track when the narration soundtrack
// is requested and the overlay draws the current cue (SettingsOverlay::setHudLayer).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dsettings {

struct SubCue {
    int startMs = 0, endMs = 0;
    std::string text; // UTF-8, lines separated by '\n'
};

inline int parseSrtTime(const std::string& s) { // 00:01:02,345 (also '.')
    int h = 0, m = 0, sec = 0, ms = 0;
    std::sscanf(s.c_str(), "%d:%d:%d%*[,.]%d", &h, &m, &sec, &ms);
    return ((h * 60 + m) * 60 + sec) * 1000 + ms;
}

inline std::vector<SubCue> parseSrt(const std::string& text) {
    std::vector<SubCue> out;
    std::istringstream f(text);
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        first = false;
        const size_t arrow = line.find("-->");
        if (arrow == std::string::npos)
            continue;
        SubCue c;
        c.startMs = parseSrtTime(line.substr(0, arrow));
        c.endMs = parseSrtTime(line.substr(arrow + 3));
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            if (!c.text.empty())
                c.text += '\n';
            c.text += line;
        }
        if (c.endMs > c.startMs && !c.text.empty())
            out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const SubCue& a, const SubCue& b) { return a.startMs < b.startMs; });
    return out;
}

inline std::vector<SubCue> loadSrtFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return parseSrt(ss.str());
}

// A running subtitle timeline. `clock` returns the media time in milliseconds (monotonic, frozen while the game's audio
// is frozen); the track keeps the clock value at start() as its zero. Thread safe (started from the resource worker,
// read by the render thread).
class SubTrack {
public:
    using Clock = std::function<double()>;
    void setClock(Clock c) {
        std::lock_guard<std::mutex> l(m_);
        clock_ = std::move(c);
    }
    // `offsetMs` shifts the cues (positive = the narration starts that much AFTER the trigger).
    void start(std::vector<SubCue> cues, double offsetMs = 0.0) {
        std::lock_guard<std::mutex> l(m_);
        cues_ = std::move(cues);
        t0_ = clock_ ? clock_() : 0.0;
        offset_ = offsetMs;
        running_ = !cues_.empty();
    }
    void stop() {
        std::lock_guard<std::mutex> l(m_);
        running_ = false;
    }
    bool running() const {
        std::lock_guard<std::mutex> l(m_);
        return running_;
    }
    // Current cue text ("" = none) and alpha 0..1 (150 ms fades). Stops itself after the last cue.
    std::string current(float* alpha, double* tMs = nullptr) {
        std::lock_guard<std::mutex> l(m_);
        if (!running_ || !clock_)
            return {};
        const double now = clock_() - t0_ - offset_;
        if (tMs)
            *tMs = now;
        if (!cues_.empty() && now > cues_.back().endMs + 1500.0) {
            running_ = false;
            return {};
        }
        for (const SubCue& c : cues_)
            if (now >= c.startMs && now < c.endMs) {
                const double a = std::min({1.0, (now - c.startMs) / 150.0, (c.endMs - now) / 150.0});
                *alpha = (float)std::max(0.0, a);
                return c.text;
            }
        return {};
    }

private:
    mutable std::mutex m_;
    Clock clock_;
    std::vector<SubCue> cues_;
    double t0_ = 0.0, offset_ = 0.0;
    bool running_ = false;
};

} // namespace dsettings
