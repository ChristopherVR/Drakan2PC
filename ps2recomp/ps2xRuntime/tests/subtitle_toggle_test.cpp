// Exercise the real subtitle visibility API with a deterministic media clock.
#include "../src/lib/drakan_subtitles.cpp"
#include "../src/lib/drakan_clip_subtitles_policy.h"
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
    {
        // Voice clips of a mod pack: timed pages from the clip's descriptor text (drakan_clip_subtitles).
        using namespace drakan_clip_subtitles::detail;
        const auto cues = clipCues("[[0.4]]First page[[3.1]] second caf\xe9 [[6]]", 9.0f);
        if (cues.size() != 2 || cues[0].startMs != 400 || cues[0].endMs != 3100 || cues[0].text != "First page" ||
            cues[1].startMs != 3100 || cues[1].endMs != 6000 || cues[1].text != "second caf\xc3\xa9")
            return 20;
        // no end mark: the clip length ends the last page; text without a leading mark is the disc's own, left alone
        const auto open = clipCues("[[1]]Only page", 4.5f);
        if (open.size() != 1 || open[0].startMs != 1000 || open[0].endMs != 4500)
            return 21;
        if (!clipCues("Plain disc text [[1]] with brackets", 4.0f).empty() || !clipCues("", 4.0f).empty())
            return 22;
        const auto odd = clipCues("[[0]]Keep [[this]] text[[2]][[2.5]][[4]]", 0.0f);
        if (odd.size() != 1 || odd[0].text != "Keep [[this]] text" || odd[0].endMs != 2000)
            return 23;
        StartedHandles handles;
        if (handles.seen(0x80000007u))
            return 24;
        handles.add(0x80000007u);
        if (!handles.seen(0x80000007u) || handles.seen(0x80000008u))
            return 25;
        for (uint32_t i = 0; i < 64; ++i)
            handles.add(0x80001000u + i);
        if (handles.seen(0x80000007u) || !handles.seen(0x8000103fu))
            return 26;
        // a clip replaces what the band shows and runs on the same media clock
        clock = 0;
        drakan_subtitles::setEnabled(true);
        drakan_subtitles::g_track.start({{0, 60000, "narration"}});
        drakan_subtitles::startClip(cues);
        clock = 1000;
        if (drakan_subtitles::g_track.current(&alpha) != "First page")
            return 27;
        clock = 4000;
        if (drakan_subtitles::g_track.current(&alpha) != "second caf\xc3\xa9")
            return 28;
        clock = 6500;
        if (!drakan_subtitles::g_track.current(&alpha).empty())
            return 29;
        drakan_subtitles::startClip({}); // nothing to show: the running track is kept
        drakan_subtitles::stop();
        std::puts("PASS pack voice clips: timed pages parsed, played and each play started once");
    }
    std::puts("PASS narration visibility: hidden timeline preserved, current cue restored, expired/stopped tracks stay "
              "stopped");
}
