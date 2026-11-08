// The title picture (title_backdrop.h): which wide set the renderers look up, and how a picture of any aspect fills
// the wide output.
#include "../src/title_backdrop.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_fail;
    }
}
bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}
} // namespace

int main() {
    namespace tb = gsr::title_backdrop;
    // No picture: every name is its own set. A picture replaces only the title set, under its own key.
    check(tb::setName("screen8", "") == "screen8", "no picture keeps screen8");
    check(tb::setName("default", "p/background-ootf.png") == "default", "default stays the disc's margins");
    check(tb::setName("screen4", "p/background-ootf.png") == "screen4", "other screens keep their art");
    const std::string key = tb::setName("screen8", "p/background-ootf.png");
    check(tb::isPicture(key) && !tb::isPicture("screen8"), "picture key is recognised");
    check(tb::picturePath(key) == "p/background-ootf.png", "picture path round trip");
    tb::set("x.png");
    check(tb::get() == "x.png", "set / get");
    tb::set("");
    check(tb::get().empty(), "cleared");

    // 16:9 output: aspectScale = (4/3) / (16/9) = 0.75.
    const float s169 = (4.0f / 3) / (16.0f / 9);
    const tb::Cover exact = tb::cover(16.0f / 9, s169); // the same aspect: the whole canvas
    check(near(exact.left, 0) && near(exact.width, 1) && near(exact.top, 0) && near(exact.height, 1), "16:9 on 16:9");
    const tb::Cover tall = tb::cover(1.6f, s169); // 16:10 art: full width, top and bottom cropped
    check(near(tall.left, 0) && near(tall.width, 1) && tall.height > 1 && near(tall.top, 0.5f - 0.5f * tall.height),
          "16:10 on 16:9 crops vertically");
    check(near(tall.height, (16.0f / 9) / 1.6f), "16:10 on 16:9 height");
    const float s219 = (4.0f / 3) / (21.0f / 9);
    const tb::Cover wide = tb::cover(16.0f / 9, s219); // 16:9 art on 21:9: still full width
    check(near(wide.width, 1) && wide.height > 1, "16:9 on 21:9 crops vertically");
    const tb::Cover pan = tb::cover(32.0f / 9, s169); // 32:9 art on 16:9: sides cropped
    check(near(pan.width, 2) && near(pan.left, -0.5f) && near(pan.height, 1), "32:9 on 16:9 crops the sides");

    // Shrinking: a 4 x 2 picture at most 2 wide becomes 2 x 1, each pixel the rounded mean of its 2 x 2 box.
    std::vector<uint32_t> px = {0x00000000u, 0x04040404u, 0xff0000ffu, 0xff0000ffu,
                                0x02020202u, 0x02020202u, 0xff0000ffu, 0xff0000ffu};
    uint32_t w = 4, h = 2;
    tb::shrink(px, w, h, 2);
    check(w == 2 && h == 1 && px.size() == 2, "shrink size");
    check(px[0] == 0x02020202u && px[1] == 0xff0000ffu, "shrink box mean");
    std::vector<uint32_t> small(6, 7u);
    uint32_t sw = 3, sh = 2;
    tb::shrink(small, sw, sh, 3840);
    check(sw == 3 && sh == 2 && small.size() == 6, "small pictures are kept");

    if (g_fail == 0)
        std::printf("title_backdrop: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
