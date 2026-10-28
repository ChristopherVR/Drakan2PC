// Focused diagnostic for click delivery and ownership release. No game assets.
#include "input/DrakanInput.h"
#include <cstdio>
#include <map>
using namespace drakan_input;
struct Guest : IGuest {
    std::map<uint32_t, uint8_t> memory;
    std::vector<GuestCall> calls;
    uint8_t read8(uint32_t a) override { return memory[a]; }
    uint32_t read32(uint32_t a) override {
        return read8(a) | (read8(a + 1) << 8) | (read8(a + 2) << 16) | (uint32_t(read8(a + 3)) << 24);
    }
    void write8(uint32_t a, uint8_t v) override { memory[a] = v; }
    void write32(uint32_t a, uint32_t v) override {
        for (int i = 0; i < 4; ++i)
            write8(a + i, uint8_t(v >> (i * 8)));
    }
    uint32_t call(const GuestCall& c) override {
        calls.push_back(c);
        return 100;
    }
};
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}
int main() {
    Config cfg = Config::defaults();
    InputAccumulator acc;
    FrameInput frame;
    PadSynthState state;
    // Three complete clicks received before the next game tick must produce
    // three distinct Square down/up pairs, not one tap or a permanently held bit.
    for (int i = 0; i < 3; ++i) {
        acc.onMouseButton(MBTN_LEFT, true);
        acc.onMouseButton(MBTN_LEFT, false);
    }
    unsigned downs = 0, ups = 0;
    bool previous = false;
    for (int i = 0; i < 8; ++i) {
        acc.consume(frame);
        frame.captured = true;
        const bool down = (synthesizePad(cfg, frame, cfg.game, false, true, state).buttons & Pad::SQUARE) != 0;
        if (down && !previous)
            ++downs;
        if (!down && previous)
            ++ups;
        previous = down;
    }
    std::printf("burst downs=%u ups=%u final=%d\n", downs, ups, previous);
    check(downs == 3 && ups == 3 && !previous, "three subframe clicks survive as three pad transitions");
    acc.onMouseButton(MBTN_RIGHT, true);
    acc.consume(frame);
    acc.onMouseButton(MBTN_RIGHT, false);
    acc.onMouseButton(MBTN_RIGHT, true);
    acc.consume(frame);
    const bool released = !(frame.mouseDown & MBTN_RIGHT);
    acc.consume(frame);
    check(released && (frame.mouseDown & MBTN_RIGHT), "release/repress gets an intervening neutral sample");
    acc.onMouseButton(MBTN_LEFT, true);
    acc.onMouseButton(MBTN_LEFT, false);
    acc.releaseAll();
    acc.consume(frame);
    check(!frame.mouseDown && !frame.mousePressed, "focus loss cancels queued attacks");
    acc.onMouseButton(MBTN_LEFT, true);
    acc.consume(frame);
    acc.releaseAll();
    acc.onMouseButton(MBTN_LEFT, true);
    acc.consume(frame);
    const bool neutralAfterReset = !(frame.mouseDown & MBTN_LEFT) && (frame.mouseReleased & MBTN_LEFT);
    acc.consume(frame);
    check(neutralAfterReset && (frame.mouseDown & MBTN_LEFT), "fresh press after focus reset follows a release sample");
    acc.releaseAll();
    acc.consume(frame);
    acc.onKey(0x11, true);
    acc.onKey(0x11, false);
    acc.onKey(0x11, true);
    acc.onKey(0x11, false);
    unsigned presses = 0, releases = 0;
    for (int i = 0; i < 5; ++i) {
        acc.consume(frame);
        presses += frame.keyPressed[0x11];
        releases += frame.keyReleased[0x11];
    }
    check(presses == 2 && releases == 2 && !frame.keyDown.any(), "keyboard taps preserve both transitions");
    CommandTracker tracker;
    std::vector<CommandEvent> events;
    frame = FrameInput{};
    frame.wheel = -3;
    tracker.update(cfg.game, frame, events);
    check(events.size() == 6, "three wheel notches produce three weapon command pairs");
    cfg.game = {{Binding::Key, 0x22, Binding::Command, 0x4007}};
    cfg.menu.clear();
    InputModule module;
    module.initHeadless(cfg);
    Guest guest;
    frame = FrameInput{};
    frame.keyDown[0x22] = true;
    module.beginFrame(frame);
    module.afterPadMgrUpdate(guest);
    guest.calls.clear();
    module.setSuspended(true);
    module.beginFrame(frame);
    module.afterPadMgrUpdate(guest);
    unsigned releaseCalls = 0, pressCalls = 0;
    for (const auto& c : guest.calls)
        if (c.addr == cfg.addrInvokeCallback) {
            if (c.i[3])
                ++pressCalls;
            else
                ++releaseCalls;
        }
    check(releaseCalls == 1 && pressCalls == 0, "F1 suspension delivers held command release to guest");
    module.setSuspended(false);
    frame = FrameInput{};
    frame.keyDown[0x22] = true;
    module.beginFrame(frame);
    module.afterPadMgrUpdate(guest);
    guest.calls.clear();
    frame.focused = false;
    module.beginFrame(frame);
    module.afterPadMgrUpdate(guest);
    releaseCalls = pressCalls = 0;
    for (const auto& c : guest.calls)
        if (c.addr == cfg.addrInvokeCallback) {
            if (c.i[3])
                ++pressCalls;
            else
                ++releaseCalls;
        }
    check(releaseCalls == 1 && pressCalls == 0, "focus loss releases ACT/CMD even when snapshot still held");
    frame.keyDown[0x11] = true;
    frame.gamepad.connected = true;
    frame.gamepad.buttons = Pad::SQUARE;
    cfg.game.push_back({Binding::Key, 0x11, Binding::PadBtn, Pad::SQUARE});
    module.reloadConfig(cfg);
    module.beginFrame(frame);
    uint8_t pad[32];
    module.fillPadSample(0, 0, pad);
    check(unpackPadSample(pad).buttons == 0, "unfocused pad sample is neutral");
    return failures ? 1 : 0;
}
