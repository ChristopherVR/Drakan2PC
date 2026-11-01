// The results screen's map vote: card rectangles, click and Q / E handling, and the cards in drawHud. Headless, std
// only.
#include <cstdio>
#include <cstdlib>

#include "gameui/Multiplayer.h"

using namespace gameui;
using namespace gameui::mp;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

int main() {
    const Viewport vp{0, 0, 1920, 1080};
    const ScreenConfig cfg;
    const std::vector<Rect> cards = mapVoteRects(3, vp, cfg);
    CHECK(cards.size() == 3);
    CHECK(cards[0].right() <= cards[1].x && cards[1].right() <= cards[2].x);
    CHECK(mapVoteRects(5, vp, cfg).size() == 3); // at most three cards

    UiInput click;
    click.pointerValid = true;
    click.leftClick = true;
    click.pointerX = cards[2].cx();
    click.pointerY = cards[2].cy();
    CHECK(mapVoteFrame(3, -1, click, vp, cfg) == 2);
    click.pointerX = 5;
    click.pointerY = 5;
    CHECK(mapVoteFrame(3, -1, click, vp, cfg) == -1);

    UiInput next, prev;
    next.keys.push_back(UiKey::PageNext);
    prev.keys.push_back(UiKey::PagePrev);
    CHECK(mapVoteFrame(3, -1, next, vp, cfg) == 0 && mapVoteFrame(3, 2, next, vp, cfg) == 0);
    CHECK(mapVoteFrame(3, 0, prev, vp, cfg) == 2 && mapVoteFrame(3, -1, prev, vp, cfg) == 2);
    CHECK(mapVoteFrame(0, -1, next, vp, cfg) == -1);

    // drawHud with a vote: the candidates' pictures are asked for as LevelPreview images.
    HudState hs;
    hs.results = true;
    hs.mode = GameMode::Deathmatch;
    hs.roster.push_back({});
    hs.vote = {{"Alpha", 7, 2}, {"Beta", 0, 0}, {"Gamma", 9, 1}};
    hs.voteMine = 0;
    const DrawList d = drawHud(hs, vp, cfg);
    int pictures = 0;
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview)
            ++pictures;
    CHECK(pictures == 2); // Beta has none: the drawn sketch stands in
    std::puts("gameui_mapvote_test: ok");
    return 0;
}
