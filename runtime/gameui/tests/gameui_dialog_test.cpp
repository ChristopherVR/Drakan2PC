#include "FakeGame.h"
#include "gameui/Dialog.h"

#include <cstdio>
#include <cstdlib>

using namespace gameui;
static void check(bool value, const char* name) {
    if (!value) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}

int main() {
    test::FakeGame g;
    constexpr uint32_t s = 0x100000, label = 0x101000, button = 0x102000, text = 0x103000, caption = 0x104000;
    g.w32(s + 0x18, 1);
    g.w32(s + 0x7c, label);
    g.w32(s + 0x84, button);
    constexpr uint32_t emptyLabel = 0x105000;
    g.w32(label + 4, emptyLabel);
    g.w16(emptyLabel + 0xc, 1);
    g.w32(emptyLabel + 0x3c, 1);
    g.w32(emptyLabel + 4, button);
    g.w16(label + 0xc, 1);
    g.w32(label + 0x3c, 1);
    g.w32(label + 0x5c, text);
    g.wstr(text, "Equipment: Shows info about the items Rynn is carrying.");
    g.w16(button + 0xc, 3);
    g.w32(button + 0x3c, 1);
    g.w32(button + 0x40, 1);
    g.w32(button + 0x5c, caption);
    g.wstr(caption, "OK");
    g.w16(button + 0x1c, 200);
    g.w16(button + 0x1e, 300);
    g.w16(button + 0x20, 100);
    g.w16(button + 0x22, 32);
    auto content = readDialog(g.mem(), s);
    check(content.valid && content.buttons.size() == 1, "read help and button");
    check(content.buttons[0].focused && content.buttons[0].enabled, "native focus and enabled state");
    check(content.buttons[0].rect == Rect{200, 300, 100, 32}, "preserve native geometry");

    for (const auto& vp : {Viewport{0, 0, 1440, 1080}, Viewport{0, 0, 1920, 1080}, Viewport{0, 0, 3840, 2160}}) {
        const float k = vp.h / 1080;
        auto c = content;
        c.buttons[0].rect = {vp.w / 2 - 80 * k, 660 * k, 160 * k, 50 * k};
        const auto d = drawDialog(c, {vp.w / 2 - 500 * k, 360 * k, 1000 * k, 380 * k}, vp, {});
        bool title = false, body = false, buttonDrawn = false;
        for (const auto& cmd : d.commands()) {
            if (cmd.kind == DrawKind::Text) {
                title |= cmd.text == "Equipment";
                body |= cmd.text.find("Shows info") != std::string::npos;
                buttonDrawn |= cmd.text == "OK";
                check(cmd.rect.x >= 0 && cmd.rect.right() <= vp.w, "text stays inside viewport");
            }
        }
        check(title && body && buttonDrawn, "modern title, body and native caption");
    }
    constexpr uint32_t noButton = 0x106000, noCaption = 0x107000;
    g.w32(button + 4, noButton);
    g.w16(noButton + 0xc, 3);
    g.w32(noButton + 0x3c, 1);
    g.w32(noButton + 0x40, 1);
    g.w32(noButton + 0x5c, noCaption);
    g.wstr(caption, "Yes");
    g.wstr(noCaption, "No");
    g.wstr(text, "Apply the skill points?");
    g.w16(noButton + 0x1c, 320);
    g.w16(noButton + 0x1e, 300);
    g.w16(noButton + 0x20, 100);
    g.w16(noButton + 0x22, 32);
    g.w32(s + 0x84, noButton);
    content = readDialog(g.mem(), s);
    check(content.valid && content.buttons.size() == 2, "read confirmation choices");
    check(!content.buttons[0].focused && content.buttons[1].focused, "native default No stays selected");
    g.w32(button + 0x40, 0);
    check(!readDialog(g.mem(), s).buttons[0].enabled, "disabled choice stays disabled");
    g.w32(button + 4, label);
    check(!readDialog(g.mem(), s).valid, "cyclic widgets fall back");
    g.w32(button + 4, 0);
    g.w32(button + 0x5c, 0x1fffff0);
    check(!readDialog(g.mem(), s).valid, "invalid string span falls back");
    g.w32(button + 0x5c, caption);
    g.w16(button + 0xc, 9);
    check(!readDialog(g.mem(), s).valid, "unsupported controls fall back");
    g.w32(s + 0x18, 21);
    check(!readDialog(g.mem(), s).valid, "ordinary menus are not dialogs");
    std::puts("dialog reader and presentation passed");
}
