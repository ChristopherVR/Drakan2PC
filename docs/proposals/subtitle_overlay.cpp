// Proposed: PC subtitle overlay for the narration (no text on the disc) - see docs/reference/subtitles.md section 6.
// Standalone core (SRT parser + timeline); the ImGui draw function is meant to be called from the gsr overlay callback
// (runtime/settings uses the same callback: gsr::Renderer::setOverlayCallback). Test: cl /std:c++20 /DSUBTITLE_TEST subtitle_overlay.cpp
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace dsubs {
struct Cue { int startMs, endMs; std::string text; };

static int parseTime(const std::string& s) { // 00:01:02,345
    int h = 0, m = 0, sec = 0, ms = 0;
    std::sscanf(s.c_str(), "%d:%d:%d,%d", &h, &m, &sec, &ms);
    return ((h * 60 + m) * 60 + sec) * 1000 + ms;
}
inline std::vector<Cue> loadSrt(const std::string& path) {
    std::ifstream f(path);
    std::vector<Cue> out; std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t arrow = line.find(" --> ");
        if (arrow == std::string::npos) continue;
        Cue c{parseTime(line.substr(0, arrow)), parseTime(line.substr(arrow + 5)), {}};
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            if (!c.text.empty()) c.text += '\n';
            c.text += line;
        }
        out.push_back(std::move(c));
    }
    return out;
}

class Track {
public:
    // trigger: first resource request of the soundtrack (sdb 0304/99/14 or /100/14) or the movie start
    void start(std::vector<Cue> cues) { std::lock_guard<std::mutex> l(m_); cues_ = std::move(cues); t0_ = Clock::now(); pausedMs_ = 0; running_ = true; }
    void stop() { std::lock_guard<std::mutex> l(m_); running_ = false; }
    void setPaused(bool p) { std::lock_guard<std::mutex> l(m_); if (p && !paused_) pauseAt_ = Clock::now(); if (!p && paused_) pausedMs_ += ms(Clock::now() - pauseAt_); paused_ = p; }
    // text to show now ("" = nothing); alpha 0..1 with 150 ms fades
    std::string current(float* alpha) {
        std::lock_guard<std::mutex> l(m_);
        if (!running_) return {};
        const int now = (int)(ms((paused_ ? pauseAt_ : Clock::now()) - t0_) - pausedMs_);
        for (const Cue& c : cues_)
            if (now >= c.startMs && now < c.endMs) {
                *alpha = std::min({1.0f, (now - c.startMs) / 150.0f, (c.endMs - now) / 150.0f});
                return c.text;
            }
        return {};
    }
private:
    using Clock = std::chrono::steady_clock;
    static long long ms(Clock::duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); }
    std::mutex m_; std::vector<Cue> cues_; Clock::time_point t0_{}, pauseAt_{}; long long pausedMs_ = 0; bool running_ = false, paused_ = false;
};

#ifdef IMGUI_VERSION
// call between ImGui::NewFrame() and Render() of the overlay's own context; outW/outH = output size in pixels
inline void draw(Track& t, float outW, float outH, float uiScale) {
    float a = 0; const std::string s = t.current(&a);
    if (s.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float fs = ImGui::GetFontSize() * 1.3f * uiScale, maxW = outW * 0.7f;
    const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, maxW, maxW, s.c_str());
    const ImVec2 p0((outW - sz.x) * 0.5f, outH * 0.90f - sz.y), pad(14 * uiScale, 8 * uiScale);
    dl->AddRectFilled(ImVec2(p0.x - pad.x, p0.y - pad.y), ImVec2(p0.x + sz.x + pad.x, p0.y + sz.y + pad.y), IM_COL32(0, 0, 0, (int)(160 * a)), 6 * uiScale);
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx || dy)
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + dx, p0.y + dy), IM_COL32(0, 0, 0, (int)(255 * a)), s.c_str(), nullptr, maxW);
    dl->AddText(ImGui::GetFont(), fs, p0, IM_COL32(255, 255, 255, (int)(255 * a)), s.c_str(), nullptr, maxW);
}
#endif
} // namespace dsubs

#ifdef SUBTITLE_TEST
int main(int argc, char** argv) {
    auto cues = dsubs::loadSrt(argc > 1 ? argv[1] : "subtitles/intro_en.srt");
    std::printf("%zu cues, first: %d-%d %s\n", cues.size(), cues.empty() ? 0 : cues[0].startMs, cues.empty() ? 0 : cues[0].endMs, cues.empty() ? "" : cues[0].text.c_str());
}
#endif
