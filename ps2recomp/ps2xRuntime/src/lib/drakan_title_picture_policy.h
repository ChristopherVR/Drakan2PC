// drakan_title_picture_policy.h: which picture the Original "Choose your game" list shows behind The Ancients' Gates.
//
// The title menu of the disc game draws the texture pack's wide title set in a wide window
// (<texture_dir>/wide/screen8_<a>x<b>.png, docs/reference/widescreen.md "Wide menu backdrops"): a sharp picture that
// holds the 4:3 title art in its centre. The game list used to show previews/background-d2.png instead, the 1280 x 720
// cover, which is the disc's 768 x 432 art scaled up and looks soft on a large screen. The list now shows the same
// wide title picture as the title menu and keeps the cover as the fallback (no texture pack, or a file that does not
// decode). Pure rules, tested in ps2xRuntime/tests/title_picture_policy_tests.cpp.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace drakan_title_picture {

// The wide set of the title menu (GUI screen 8).
inline constexpr char kTitleSet[] = "screen8";

// The aspect a file name of the wide set declares: "<set>_<a>x<b>.png" gives a / b, anything else 0.
inline float wideAspectOf(const std::string& file, const std::string& set = kTitleSet) {
    const std::string prefix = set + "_";
    if (file.compare(0, prefix.size(), prefix) != 0)
        return 0;
    unsigned a = 0, b = 0;
    int used = 0;
    if (std::sscanf(file.c_str() + prefix.size(), "%ux%u.png%n", &a, &b, &used) < 2 || !a || !b ||
        prefix.size() + static_cast<size_t>(used) != file.size())
        return 0;
    return static_cast<float>(a) / static_cast<float>(b);
}

// The image for a view of aspect `target`: the narrowest one at least as wide as the view, else the widest; -1 when
// there is none. The renderer's rule for the title (gsr hud::pickWideAspect), so the list and the title agree.
inline int pickWide(const std::vector<float>& aspects, float target) {
    int best = -1;
    for (size_t i = 0; i < aspects.size(); ++i) {
        if (!(aspects[i] > 0))
            continue;
        const bool covers = aspects[i] >= target * 0.995f;
        const bool bestCovers = best >= 0 && aspects[static_cast<size_t>(best)] >= target * 0.995f;
        if (best < 0 || (covers && !bestCovers) ||
            (covers == bestCovers && (covers ? aspects[i] < aspects[static_cast<size_t>(best)]
                                             : aspects[i] > aspects[static_cast<size_t>(best)])))
            best = static_cast<int>(i);
    }
    return best;
}

// How far a picture file has got on its way to the screen.
enum class Load {
    Missing, // no such file
    Pending, // being decoded
    Ready,   // decoded and uploaded
    Failed,  // the file did not decode
};

// Which picture the list draws behind the disc game.
enum class Source {
    None,     // neither exists: the plain black the list always had
    TitleArt, // the wide title picture
    Cover,    // previews/background-<id>.png
};

// The title picture whenever it exists and decodes. While it is being decoded it is still the choice (the list is
// black for those few frames, as for every other picture of the screen) so the cover, which frames the art differently,
// never shows for a moment first. A title picture that does not decode leaves the cover for good.
inline Source choose(Load titleArt, Load cover) {
    if (titleArt == Load::Ready || titleArt == Load::Pending)
        return Source::TitleArt;
    return cover == Load::Ready || cover == Load::Pending ? Source::Cover : Source::None;
}

} // namespace drakan_title_picture
