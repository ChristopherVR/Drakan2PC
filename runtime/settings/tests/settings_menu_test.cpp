// CPU-only menu apply tests: in-memory files and a fake host, no window, GPU, or ImGui backend.
#include "settings/Menu.h"
#include <cstdio>

using namespace dsettings;
static int checks = 0, failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(c)) {                                                                                                    \
            ++failures;                                                                                                \
            std::printf("FAIL %d: %s\n", __LINE__, #c);                                                                \
        }                                                                                                              \
    } while (0)

struct Host : ISettingsHost {
    int videoApplies = 0, restarts = 0;
    VideoSettings lastVideo, prevVideo;
    HostCaps capabilities() const override {
        HostCaps c;
        c.restartKeys.clear(); // Startup-only world rate must require restart even with a custom host list.
        return c;
    }
    ApplyResult applyVideo(const VideoSettings& n, const VideoSettings& p) override {
        ++videoApplies;
        lastVideo = n;
        prevVideo = p;
        return {};
    }
    ApplyResult applyAudio(const AudioSettings&, const AudioSettings&) override { return {}; }
    void reloadBindings(const std::string&) override {}
    void requestRestart(const std::vector<std::string>&) override { ++restarts; }
};

static void testApply(int initialRate, int nextRate, bool mixed) {
    auto rateText = [](int rate) { return rate == 0 ? std::string("uncapped") : std::to_string(rate); };
    std::string video = "[video]\nworld_rate = " + rateText(initialRate) + "\nframe_limit = 120\nvsync = 1\n";
    std::string input = "[general]\nlook_mode = direct\n";
    int writes = 0;
    FileIO io;
    io.read = [&](const std::string& path, std::string& out) {
        out = path == "drakan.ini" ? video : input;
        return true;
    };
    io.write = [&](const std::string& path, const std::string& text) {
        ++writes;
        CHECK(path == "drakan.ini");
        (path == "drakan.ini" ? video : input) = text;
        return true;
    };
    SettingsStore store({}, io);
    Host host;
    SettingsMenu menu(host, store);
    menu.open();
    CHECK(menu.pending().video.worldRate == initialRate);
    menu.pendingMut().video.worldRate = nextRate;
    if (mixed) {
        menu.pendingMut().video.frameLimit = 144;
        menu.pendingMut().video.vsync = false;
    }
    menu.applyPending(true);
    CHECK(menu.isOpen());      // OK waits for the restart prompt instead of closing immediately.
    CHECK(host.restarts == 0); // Restart stays a user choice.
    CHECK(store.loaded().video.worldRate == nextRate);
    CHECK(video.find("world_rate = " + rateText(nextRate) + "\n") != std::string::npos);
    CHECK(input == "[general]\nlook_mode = direct\n" && writes == 1);
    CHECK(host.videoApplies == (mixed ? 1 : 0));
    if (mixed) {
        CHECK(host.lastVideo.frameLimit == 144 && !host.lastVideo.vsync);
        CHECK(host.lastVideo.worldRate == initialRate && host.prevVideo.worldRate == initialRate);
    }
    menu.close();
    menu.open();
    CHECK(menu.pending().video.worldRate == nextRate);
    menu.close();
}

static void testPresentationApply() {
    FileIO io;
    io.read = [](const std::string&, std::string& out) {
        out.clear();
        return true;
    };
    io.write = [](const std::string&, const std::string&) { return true; };
    SettingsStore store({}, io);
    Host host;
    SettingsMenu menu(host, store);
    menu.open();
    menu.pendingMut().video.frameLimit = 144;
    menu.pendingMut().video.fieldOfView = 100;
    menu.applyPending(true);
    CHECK(!menu.isOpen()); // Presentation-only edits keep the live apply path.
    CHECK(host.videoApplies == 1 && host.lastVideo.frameLimit == 144 && host.lastVideo.worldRate == 0);
    CHECK(host.restarts == 0);
    CHECK(host.lastVideo.fieldOfView == 100); // live apply, no restart or display-revert prompt
}

int main() {
    for (int initial : {30, 60, 0}) {
        for (int next : {30, 60, 0}) {
            if (next == initial)
                continue;
            testApply(initial, next, false);
            testApply(initial, next, true);
        }
    }
    testPresentationApply();
    {
        FileIO io;
        io.read = [](const std::string&, std::string& out) {
            out.clear();
            return true;
        };
        io.write = [](const std::string&, const std::string&) { return true; };
        SettingsStore store({}, io);
        Host host;
        SettingsMenu menu(host, store);
        menu.open();
        menu.pendingMut().video.modernWater = true;
        menu.pendingMut().video.rayReflections = false;
        menu.applyPending(true);
        CHECK(host.videoApplies == 1 && host.lastVideo.modernWater && !host.lastVideo.rayReflections &&
              host.restarts == 0);
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
