#pragma once

// Window-thread capture policy. Guest menu/gameplay state is published separately.
class HostMouseCapture {
    bool released_ = false;
    bool suppressLeft_ = false;

public:
    struct Result {
        bool capture;
        bool suppressLeft;
    };
    Result update(bool focused, bool gameplay, bool captured, bool toggle, bool leftPressed, bool leftDown) {
        if (!focused && captured)
            released_ = true;
        if (focused && toggle)
            released_ = !released_;
        if (focused && gameplay && !captured && leftPressed) {
            released_ = false;
            suppressLeft_ = true;
        }
        const bool suppress = suppressLeft_;
        if (!leftDown)
            suppressLeft_ = false;
        return {focused && gameplay && !released_, suppress};
    }
};
