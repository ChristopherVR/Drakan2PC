// Exercise the real subtitle visibility API with a deterministic media clock.
#include "../src/lib/drakan_subtitles.cpp"
namespace drakan_cfg {
std::string testExeDir;
const std::string& exeDir() {
    return testExeDir;
}
bool getBool(const char*, const char*, const char*, bool value) {
    return value;
}
std::string getString(const char*, const char*, const char*, const char* value) {
    return value;
}
} // namespace drakan_cfg
int main(int argc, char** argv) {
    double clock = 0;
    drakan_subtitles::g_track.setClock([&] { return clock; });
    drakan_subtitles::g_track.start({{0, 1000, "first"}, {1000, 3000, "second"}});
    clock = 500;
    float alpha = 0;
    if (drakan_subtitles::g_track.current(&alpha) != "first")
        return 1;
    drakan_subtitles::setEnabled(false);
    if (!drakan_subtitles::g_track.running() || drakan_subtitles::g_enabled.load())
        return 2;
    clock = 2000;
    drakan_subtitles::setEnabled(true);
    if (!drakan_subtitles::g_enabled.load() || drakan_subtitles::g_track.current(&alpha) != "second")
        return 3;
    drakan_subtitles::setEnabled(false);
    clock = 6000;
    drakan_subtitles::setEnabled(true);
    if (!drakan_subtitles::g_track.current(&alpha).empty())
        return 4;
    drakan_subtitles::stop();
    drakan_subtitles::setEnabled(true);
    if (drakan_subtitles::g_track.running())
        return 5;
    if (argc > 1) {
        drakan_cfg::testExeDir = argv[1];
        clock = 0;
        drakan_subtitles::setEnabled(false);
        hle::ResourceId id;
        id.file = "LEVEL0RO.SDB";
        id.type = 0x304;
        id.id = 99;
        id.sub = 14;
        drakan_subtitles::onResourceRequest(id, 0);
        if (!drakan_subtitles::wantsSpuEvents())
            return 6;
        drakan_subtitles::onSpuUpload(4096, 65536);
        drakan_subtitles::onVoiceKeyOn(4096, 0);
        if (!drakan_subtitles::g_track.running() || drakan_subtitles::g_enabled.load())
            return 7;
        clock = 7000;
        drakan_subtitles::setEnabled(true);
        if (drakan_subtitles::g_track.current(&alpha).empty())
            return 8;
        drakan_subtitles::stop();
        std::puts("PASS hidden-at-start soundtrack request/upload/key-on synchronization");
    }
    std::puts("PASS narration visibility: hidden timeline preserved, current cue restored, expired/stopped tracks stay "
              "stopped");
}
