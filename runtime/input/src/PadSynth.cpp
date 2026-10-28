#include "input/PadSynth.h"
#include <algorithm>
#include <cmath>

namespace drakan_input {

float gameStickResponse(uint8_t byte) {
    float f = std::clamp((static_cast<int>(byte) - 128) / 128.0f, -1.0f, 1.0f);
    return (f < 0 ? -1.0f : 1.0f) * f * f;
}

uint8_t encodeStickLinear(float u) {
    float a = std::min(1.0f, std::fabs(u));
    int v = static_cast<int>(std::lround(128.0f * std::sqrt(a)));
    if (u < 0)
        v = -v;
    return static_cast<uint8_t>(std::clamp(128 + v, 0, 255));
}

uint8_t encodeStickRaw(float f) {
    return static_cast<uint8_t>(
        std::clamp(128 + static_cast<int>(std::lround(std::clamp(f, -1.0f, 1.0f) * 127.0f)), 0, 255));
}

bool sourceLevel(const Binding& b, const FrameInput& in, bool tapSafe) {
    switch (b.src) {
        case Binding::Key:
            return in.keyDown[static_cast<size_t>(b.code) & 255] ||
                   (tapSafe && in.keyPressed[static_cast<size_t>(b.code) & 255]);
        case Binding::Mouse:
            return (in.mouseDown & b.code) || (tapSafe && (in.mousePressed & b.code));
        case Binding::WheelUp:
            return in.wheel > 0;
        case Binding::WheelDown:
            return in.wheel < 0;
    }
    return false;
}

namespace {
// Jitter-filter dither: see header.
uint8_t dither(PadSynthState& st, int idx, uint8_t want) {
    uint8_t last = st.last[idx];
    int v = static_cast<int>(want) - 128;
    if (v != 0 && std::abs(v) <= 47 && std::abs(static_cast<int>(want) - static_cast<int>(last)) < 4) {
        st.flip[idx] = !st.flip[idx];
        int sign = v < 0 ? -1 : 1;
        // alternate between want and want+-4 (away from centre keeps the sign)
        int nb = st.flip[idx] ? static_cast<int>(want) + sign * 4 : static_cast<int>(want);
        if (std::abs(nb - static_cast<int>(last)) < 4)
            nb = static_cast<int>(last) + sign * 4;
        want = static_cast<uint8_t>(std::clamp(nb, 0, 255));
    }
    st.last[idx] = want;
    return want;
}
} // namespace

PadSample synthesizePad(const Config& cfg, const FrameInput& in, const std::vector<Binding>& layer, bool menuActive,
                        bool directLook, bool walk, PadSynthState& st) {
    PadSample s;
    bool ls[4] = {}, rs[4] = {};
    for (const Binding& b : layer) {
        bool on = sourceLevel(b, in, true);
        if (!on)
            continue;
        switch (b.act) {
            case Binding::PadBtn:
                s.buttons |= static_cast<uint16_t>(b.value);
                break;
            case Binding::LStick:
                ls[b.value & 3] = true;
                break;
            case Binding::RStick:
                rs[b.value & 3] = true;
                break;
            default:
                break;
        }
    }

    // --- left stick from keys: 8-way normalised linear vector (x right+, y down+)
    float lx = static_cast<float>(ls[3]) - static_cast<float>(ls[2]);
    float ly = static_cast<float>(ls[1]) - static_cast<float>(ls[0]);
    if (lx != 0 && ly != 0) {
        lx *= 0.70710678f;
        ly *= 0.70710678f;
    }
    if (walk) {
        lx *= cfg.walkScale;
        ly *= cfg.walkScale;
    }
    if (menuActive)
        lx = ly = 0;
    auto enc = [&](float v) { return cfg.curveCompensation ? encodeStickLinear(v) : encodeStickRaw(v); };
    uint8_t blx = enc(lx), bly = enc(ly);

    // --- right stick: digital keys + (stick mode) mouse
    float rx = static_cast<float>(rs[3]) - static_cast<float>(rs[2]);
    float ry = static_cast<float>(rs[1]) - static_cast<float>(rs[0]);
    if (!menuActive && !directLook && in.captured && cfg.stickFullCounts > 0) {
        // stick_full_counts is calibrated per 30 Hz tick. The game integrates the deflection
        // over its tick time, so scale to the counts' real duration to keep turning rate-independent.
        const float full =
            cfg.stickFullCounts * std::clamp(in.frameSeconds * 30.0f, 1.0f / 33.0f, 7.5f); // 1000 Hz .. 4 Hz
        rx += cfg.stickSignX * (cfg.invertX ? -1.0f : 1.0f) * static_cast<float>(in.dx) / full;
        ry += cfg.stickSignY * (cfg.invertY ? -1.0f : 1.0f) * static_cast<float>(in.dy) / full;
    }
    rx = std::clamp(rx, -1.0f, 1.0f);
    ry = std::clamp(ry, -1.0f, 1.0f);
    uint8_t brx = encodeStickLinear(rx), bry = encodeStickLinear(ry);

    // --- XInput pad merges in: buttons OR, a deflected stick overrides the synthesized one (real-pad encoding)
    if (cfg.gamepadEnabled && in.gamepad.connected) {
        s.buttons |= in.gamepad.buttons;
        if (in.gamepad.lx != 0 || in.gamepad.ly != 0) {
            blx = encodeStickRaw(in.gamepad.lx);
            bly = encodeStickRaw(-in.gamepad.ly);
        }
        if (in.gamepad.rx != 0 || in.gamepad.ry != 0) {
            brx = encodeStickRaw(in.gamepad.rx);
            bry = encodeStickRaw(-in.gamepad.ry);
        }
    }

    s.rx = dither(st, 0, brx);
    s.ry = dither(st, 1, bry);
    s.lx = dither(st, 2, blx);
    s.ly = dither(st, 3, bly);
    return s;
}

void packPadSample(const PadSample& s, uint8_t out[32]) {
    for (int i = 0; i < 32; ++i)
        out[i] = 0;
    uint16_t inv = static_cast<uint16_t>(~s.buttons);
    out[0] = 0x00; // status
    out[1] = 0x73; // DualShock analog mode id
    out[2] = static_cast<uint8_t>(inv >> 8);
    out[3] = static_cast<uint8_t>(inv & 0xff);
    out[4] = s.rx;
    out[5] = s.ry;
    out[6] = s.lx;
    out[7] = s.ly;
}

PadSample unpackPadSample(const uint8_t in[32]) {
    PadSample s;
    s.buttons = static_cast<uint16_t>(~((in[2] << 8) | in[3]));
    s.rx = in[4];
    s.ry = in[5];
    s.lx = in[6];
    s.ly = in[7];
    return s;
}

void CommandTracker::update(const std::vector<Binding>& layer, const FrameInput& in, std::vector<CommandEvent>& out) {
    prev_.resize(layer.size(), false);
    held_.clear();
    for (size_t i = 0; i < layer.size(); ++i) {
        const Binding& b = layer[i];
        if (b.act != Binding::Action && b.act != Binding::Command)
            continue;
        bool isAct = b.act == Binding::Action;
        if (b.src == Binding::WheelUp || b.src == Binding::WheelDown) {
            const int64_t count =
                b.src == Binding::WheelUp ? static_cast<int64_t>(in.wheel) : -static_cast<int64_t>(in.wheel);
            for (int64_t notch = 0; notch < count; ++notch) {
                out.push_back({isAct, b.value, true});
                out.push_back({isAct, b.value, false});
            }
            prev_[i] = false;
            continue;
        }
        bool down, pe, re;
        if (b.src == Binding::Key) {
            size_t k = static_cast<size_t>(b.code) & 255;
            down = in.keyDown[k];
            pe = in.keyPressed[k];
            re = in.keyReleased[k];
        } else if (b.src == Binding::Mouse) {
            down = (in.mouseDown & b.code) != 0;
            pe = (in.mousePressed & b.code) != 0;
            re = (in.mouseReleased & b.code) != 0;
        } else {
            bool w = (b.src == Binding::WheelUp) ? in.wheel > 0 : in.wheel < 0; // instantaneous tap
            down = false;
            pe = w;
            re = w;
        }
        bool was = prev_[i];
        if (pe || (down && !was))
            out.push_back({isAct, b.value, true});
        if (re || (!down && was))
            out.push_back({isAct, b.value, false});
        prev_[i] = down;
        if (down)
            held_.push_back({isAct, b.value, true});
    }
}

void CommandTracker::reset(std::vector<CommandEvent>* releases) {
    if (releases)
        for (CommandEvent e : held_) {
            e.pressed = false;
            releases->push_back(e);
        }
    held_.clear();
    prev_.clear();
}

} // namespace drakan_input
