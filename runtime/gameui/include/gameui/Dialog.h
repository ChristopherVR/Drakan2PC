// Read-only presentation of native message boxes. Native widgets retain input and activation.
#pragma once
#include "gameui/GuestMemory.h"
#include "gameui/Layout.h"

namespace gameui {

struct DialogButton {
    std::string text;
    Rect rect;
    uint32_t widget = 0;
    bool focused = false, enabled = false;
};

struct DialogContent {
    std::string text;
    std::vector<DialogButton> buttons;
    bool valid = false;
};

DialogContent readDialog(const IGuestMemory& mem, uint32_t screen);
// `panel` and button rectangles are already in output pixels. The layout keeps native hit regions intact.
DrawList drawDialog(const DialogContent& content, const Rect& panel, const Viewport& view, const TextMeasure& measure);

} // namespace gameui
