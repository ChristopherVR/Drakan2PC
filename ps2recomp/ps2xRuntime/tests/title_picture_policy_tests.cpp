// drakan_title_picture_policy.h: the Original "Choose your game" list shows the texture pack's wide title picture
// behind The Ancients' Gates (the picture the title menu shows) and falls back to the cover.
#include "../src/lib/drakan_title_picture_policy.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}
} // namespace

int main() {
    using namespace drakan_title_picture;
    // File names of the wide title set; the other sets, other files and malformed names are not it.
    assert(near(wideAspectOf("screen8_16x9.png"), 16.0f / 9));
    assert(near(wideAspectOf("screen8_7x3.png"), 7.0f / 3));
    assert(near(wideAspectOf("screen8_32x9.png"), 32.0f / 9));
    assert(wideAspectOf("default_16x9.png") == 0);
    assert(near(wideAspectOf("default_16x9.png", "default"), 16.0f / 9));
    assert(wideAspectOf("screen80_16x9.png") == 0);
    assert(wideAspectOf("screen8_16x9.png.bak") == 0);
    assert(wideAspectOf("screen8_16x9.jpg") == 0);
    assert(wideAspectOf("screen8_0x9.png") == 0);
    assert(wideAspectOf("screen8_16x0.png") == 0);
    assert(wideAspectOf("screen8_.png") == 0);
    assert(wideAspectOf("screen8") == 0);
    assert(wideAspectOf("") == 0);

    // The narrowest image that covers the view; a 4:3 window takes the 16:9 one (its centre is the 4:3 art), a view
    // wider than every image takes the widest. The renderer's rule for the title menu.
    const std::vector<float> set{32.0f / 9, 16.0f / 9, 7.0f / 3};
    assert(pickWide(set, 16.0f / 9) == 1);
    assert(pickWide(set, 3840.0f / 2160) == 1);
    assert(pickWide(set, 4.0f / 3) == 1);
    assert(pickWide(set, 16.0f / 10) == 1);
    assert(pickWide(set, 21.0f / 9) == 2);
    assert(pickWide(set, 3440.0f / 1440) == 0); // 2.389 is wider than 7:3
    assert(pickWide(set, 32.0f / 9) == 0);
    assert(pickWide(set, 5.0f) == 0);
    assert(pickWide({16.0f / 9}, 21.0f / 9) == 0);
    assert(pickWide({}, 16.0f / 9) == -1);
    assert(pickWide({0.0f, -1.0f}, 16.0f / 9) == -1);
    assert(pickWide({0.0f, 16.0f / 9}, 16.0f / 9) == 1);

    // The title picture when it exists; the cover when it is missing or does not decode; black only with neither.
    assert(choose(Load::Ready, Load::Ready) == Source::TitleArt);
    assert(choose(Load::Ready, Load::Missing) == Source::TitleArt);
    assert(choose(Load::Pending, Load::Ready) == Source::TitleArt); // never the cover for a moment first
    assert(choose(Load::Pending, Load::Pending) == Source::TitleArt);
    assert(choose(Load::Missing, Load::Ready) == Source::Cover);
    assert(choose(Load::Missing, Load::Pending) == Source::Cover);
    assert(choose(Load::Failed, Load::Ready) == Source::Cover);
    assert(choose(Load::Failed, Load::Pending) == Source::Cover);
    assert(choose(Load::Missing, Load::Missing) == Source::None);
    assert(choose(Load::Failed, Load::Failed) == Source::None);
    assert(choose(Load::Missing, Load::Failed) == Source::None);
    std::puts("title_picture_policy: ok");
    return 0;
}
