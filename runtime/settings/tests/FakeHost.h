// A fake ISettingsHost for the demo / tests: records every call, pretends to be a 2560x1440 144 Hz monitor.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "settings/Host.h"
#include "settings/TxdImage.h"

struct FakeHost : dsettings::ISettingsHost {
    std::vector<std::string> log;
    int opened = 0, closed = 0, reloads = 0, regens = 0, restarts = 0, videoApplies = 0, audioApplies = 0,
        generalApplies = 0;
    std::vector<std::string> restartReasons;
    dsettings::VideoSettings lastVideo;
    dsettings::AudioSettings lastAudio;

    void note(const std::string& s) {
        log.push_back(s);
        std::printf("[host] %s\n", s.c_str());
    }

    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps c;
        c.exclusiveFullscreen = true;
        c.textureUpscale = true;
        c.anisotropic = true;
        c.mipBias = true;
        c.hdTextures = true;
        c.textureDump = false; // shown greyed out
        c.languageSelect = true;
        c.textureUpscaleFilters = {"default", "bicubic", "lanczos", "xbrz"};
        return c;
    }
    dsettings::MonitorInfo monitor() const override {
        dsettings::MonitorInfo m;
        m.name = "Fake 27\" monitor";
        m.nativeWidth = 2560;
        m.nativeHeight = 1440;
        m.refreshHz = 144;
        m.modes = {{2560, 1440, 144}, {2560, 1440, 60}, {1920, 1080, 144},
                   {1920, 1080, 60},  {1600, 900, 60},  {1280, 720, 60}};
        return m;
    }
    std::vector<std::string> audioDevices() const override {
        return {"Speakers (Realtek Audio)", "Headphones (USB Audio)", "HDMI Output (NVIDIA)"};
    }
    void windowSize(int& w, int& h) const override {
        w = 1600;
        h = 900;
    }

    dsettings::ApplyResult applyVideo(const dsettings::VideoSettings& n, const dsettings::VideoSettings& p) override {
        ++videoApplies;
        lastVideo = n;
        note(std::string("applyVideo vsync=") + (n.vsync ? "1" : "0") + " mode=" + dsettings::toIni(n.windowMode) +
             " " + std::to_string(n.windowWidth) + "x" + std::to_string(n.windowHeight) +
             " scale=" + (n.scaleAuto ? "auto" : std::to_string(n.scale)) + " aspect=" + dsettings::toIni(n.aspect));
        dsettings::ApplyResult r;
        r.needsRestart = (n.scaleAuto != p.scaleAuto || n.scale != p.scale);
        return r;
    }
    dsettings::ApplyResult applyAudio(const dsettings::AudioSettings& n, const dsettings::AudioSettings&) override {
        ++audioApplies;
        lastAudio = n;
        note("applyAudio volume=" + std::to_string(n.volume) + " device=" + n.device);
        return {};
    }
    dsettings::ApplyResult applyGeneral(const dsettings::GeneralSettings& n,
                                        const dsettings::GeneralSettings&) override {
        ++generalApplies;
        note("applyGeneral language=" + n.language);
        return {};
    }
    void reloadBindings(const std::string& path) override {
        ++reloads;
        note("reloadBindings " + path);
    }
    void regeneratePrompts() override {
        ++regens;
        note("regeneratePrompts");
    }
    void requestRestart(const std::vector<std::string>& r) override {
        ++restarts;
        restartReasons = r;
        note("requestRestart (" + std::to_string(r.size()) + " reasons)");
    }
    // The game's art: INTERFCE.TXD from env DRAKAN_DEMO_TXD (a copy extracted from the disc); without it the menu keeps
    // the plain style.
    int sounds[5] = {0, 0, 0, 0, 0};
    mutable std::vector<unsigned char> txd_;
    mutable bool txdTried_ = false;
    bool gameTexture(int id, dsettings::GameImage& out) const override {
        if (!txdTried_) {
            txdTried_ = true;
            const char* e = std::getenv("DRAKAN_DEMO_TXD");
            if (e) {
                std::ifstream f(e, std::ios::binary);
                txd_.assign(std::istreambuf_iterator<char>(f), {});
            }
        }
        if (txd_.size() < 16 || std::memcmp(txd_.data(), "SRSC", 4) != 0)
            return false;
        uint32_t idx = 0, cnt = 0;
        std::memcpy(&idx, &txd_[6], 4);
        std::memcpy(&cnt, &txd_[10], 4);
        for (uint32_t i = 0; i < cnt; ++i) {
            const size_t o = idx + static_cast<size_t>(i) * 14;
            if (o + 14 > txd_.size())
                break;
            uint16_t t, tid, sub;
            uint32_t off, sz;
            std::memcpy(&t, &txd_[o], 2);
            std::memcpy(&tid, &txd_[o + 2], 2);
            std::memcpy(&sub, &txd_[o + 4], 2);
            std::memcpy(&off, &txd_[o + 6], 4);
            std::memcpy(&sz, &txd_[o + 10], 4);
            if (t != 0x40 || tid != id || off + sz > txd_.size())
                continue;
            return dsettings::decodeTxdBlob(txd_.data() + off, sz, out);
        }
        return false;
    }
    void playUiSound(dsettings::UiSound s) override { ++sounds[static_cast<int>(s)]; }
    void onOverlayOpened() override {
        ++opened;
        note("overlay opened -> pause game, release mouse");
    }
    void onOverlayClosed() override {
        ++closed;
        note("overlay closed -> resume game, recapture mouse");
    }
};
