#include "settings/Overlay.h"
#include "settings/Subtitles.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    std::vector<std::string> texts = {
        "A complete sentence with several words.\nA second explicit line follows.",
        "A deliberately long subtitle sentence that must wrap consistently at large sizes and narrow output widths "
        "without hiding any letters beyond the screen edge.",
        "AFirstLongWordWithoutSpacesThatMustAlsoWrapAcrossSeveralLines", "First line\n\nThird line"};
    for (int i = 1; i < argc; ++i)
        for (const auto& cue : dsettings::loadSrtFile(argv[i]))
            texts.push_back(cue.text);
    unsigned checks = 0;
    for (float aspect : {4.f / 3, 16.f / 9, 21.f / 9, 32.f / 9})
        for (float size : {.5f, 1.f, 1.5f, 2.f, 3.f})
            for (float height : {240.f, 720.f, 2160.f})
                for (const auto& text : texts) {
                    const float width = height * aspect;
                    io.DisplaySize = {width, height};
                    io.DeltaTime = 1.f / 60;
                    ImGui::NewFrame();
                    dsettings::SettingsOverlay::drawSubtitleBand(text, 1, width, height, size);
                    auto* draw = ImGui::GetForegroundDrawList();
                    if (draw->VtxBuffer.Size < 4)
                        return 1;
                    const float top = draw->VtxBuffer[0].pos.y, bottom = draw->VtxBuffer[2].pos.y;
                    if (top < 0 || bottom > height)
                        return 2;
                    for (int v = 4; v < draw->VtxBuffer.Size; ++v) {
                        auto p = draw->VtxBuffer[v].pos;
                        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < width * .1f - 2 ||
                            p.x > width * .9f + 2 || p.y < top || p.y > bottom) {
                            std::printf("FAIL aspect=%g size=%g h=%g vertex=%g,%g band=%g,%g\n", aspect, size, height,
                                        p.x, p.y, top, bottom);
                            return 3;
                        }
                        ++checks;
                    }
                    unsigned ink = 0;
                    for (unsigned char c : text)
                        if (c > 32 && (c & 0xc0) != 0x80)
                            ++ink;
                    if (draw->VtxBuffer.Size != 4 + ink * 8) {
                        std::puts("FAIL missing glyphs");
                        return 4;
                    }
                    ImGui::Render();
                }
    double clock = 0;
    dsettings::SubTrack track;
    track.setClock([&] { return clock; });
    track.start({{100, 1000, "one"}, {1000, 2000, "two"}});
    float alpha = 0;
    clock = 500;
    if (track.current(&alpha) != "one")
        return 5;
    if (track.current(&alpha) != "one")
        return 6; // frozen media clock
    clock = 1500;
    if (track.current(&alpha) != "two")
        return 7;
    clock = 4000;
    if (!track.current(&alpha).empty() || track.running())
        return 8;
    ImGui::DestroyContext();
    std::printf("PASS subtitle rendering: %u glyph bounds, all glyphs retained, four aspects, five sizes, three output "
                "heights; media timeline\n",
                checks);
}
