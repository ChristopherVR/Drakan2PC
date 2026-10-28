#include "ps2_host_pad.h"
#include "host_mouse_capture.h"
#include <cassert>
#include <thread>

int main() {
    HostMouseCapture capture;
    assert(!capture.update(true, false, false, false, false, false).capture);
    assert(capture.update(true, true, false, false, false, false).capture);
    assert(!capture.update(true, true, true, true, false, false).capture);
    assert(!capture.update(true, true, false, false, false, false).capture);
    auto recaptured = capture.update(true, true, false, false, true, true);
    assert(recaptured.capture && recaptured.suppressLeft);
    assert(capture.update(true, true, true, false, false, true).suppressLeft);
    assert(capture.update(true, true, true, false, false, false).suppressLeft);
    assert(!capture.update(true, true, true, false, false, false).suppressLeft);
    assert(!capture.update(true, false, true, false, false, false).capture);
    assert(capture.update(true, true, false, false, false, false).capture);
    assert(!capture.update(false, true, true, false, false, false).capture);
    assert(!capture.update(true, true, false, false, false, false).capture);
    assert(capture.update(true, true, false, false, true, false).capture);

    HostPadState pad;
    auto neutral = pad.consume();
    assert(neutral[2] == 0xff && neutral[3] == 0xff && neutral[6] == 128);
    auto down = neutral;
    down[3] &= ~0x40; // Cross
    // A complete tap between guest reads still appears for one read.
    pad.publish(down);
    pad.publish(neutral);
    assert((pad.consume()[3] & 0x40) == 0);
    assert(pad.consume()[3] == 0xff);
    // A second port / multitap probe cannot steal player one's input.
    pad.publish(down, 4, -2, true);
    pad.publish(neutral, 0, 0, true);
    assert(pad.consume(0, 1) == neutral);
    assert(pad.consume(0, 0, 1) == neutral);
    auto firstPort = pad.consume();
    assert((firstPort[3] & 0x40) == 0 && firstPort[4] == 148 && firstPort[5] == 138);
    // Held input stays down across reads; releasing does not synthesize a tap.
    pad.publish(down);
    assert((pad.consume()[3] & 0x40) == 0);
    assert((pad.consume()[3] & 0x40) == 0);
    pad.publish(neutral);
    assert(pad.consume()[3] == 0xff);
    // Changing GUI screens releases held controls until the physical release.
    pad.publish(down);
    assert((pad.consume()[3] & 0x40) == 0);
    assert((pad.consume(0x1234)[3] & 0x40) != 0);
    pad.publish(down);
    assert((pad.consume(0x1234)[3] & 0x40) != 0);
    pad.publish(down); // another host sample, followed by release before a guest read
    pad.publish(neutral);
    assert((pad.consume(0x1234)[3] & 0x40) != 0);
    pad.publish(down);
    assert((pad.consume(0x1234)[3] & 0x40) == 0);
    pad.publish(neutral);
    pad.consume(0);
    // Window frames may outnumber guest reads: preserve relative mouse motion.
    pad.publish(neutral, 3, -2, true);
    pad.publish(neutral, 2, -1, true);
    auto mouse = pad.consume();
    assert(mouse[4] == 153 && mouse[5] == 143);
    mouse = pad.consume();
    assert(mouse[4] == 128 && mouse[5] == 128);
    pad.publish(neutral, 10, 10, true);
    pad.publish(neutral); // releasing capture discards stale movement
    mouse = pad.consume();
    assert(mouse[4] == 128 && mouse[5] == 128);
    // Padman can poll many times between engine input frames. Every read in
    // that frame must see the edge / mouse; a neutral poll cannot erase it.
    HostPadState framed;
    framed.publish(down, 20, 0, true);
    framed.publish(neutral, 0, 0, true);
    framed.latchFrame(0);
    assert(framed.mouseLookActive());
    for (int i = 0; i < 20; ++i) {
        const auto sample = framed.readFrame(0, 0, 0);
        assert((sample[3] & 0x40) == 0 && sample[4] == 228);
        assert(framed.readFrame(0, 1, 0) == neutral);
        framed.publish(neutral, 0, 0, true);
    }
    framed.latchFrame(0);
    assert(framed.readFrame(0, 0, 0) == neutral);
    auto physicalStick = neutral;
    physicalStick[4] = 200;
    framed.publish(physicalStick, 0, 0, true);
    framed.latchFrame(0);
    assert(!framed.mouseLookActive());
    // Direct camera motion must not also deflect the emulated right stick.
    HostPadState direct;
    direct.publish(neutral, 80, -45, true);
    direct.latchFrame(0, true);
    assert(direct.readFrame(0, 0, 0)[4] == 128 && direct.readFrame(0, 0, 0)[5] == 128);
    auto displacement = direct.mouseFrame();
    assert(displacement.active && displacement.x == 80 && displacement.y == -45);
    direct.publish(neutral, 0, 0, true);
    direct.latchFrame(0, true);
    displacement = direct.mouseFrame();
    assert(displacement.active && displacement.x == 0 && displacement.y == 0);
    direct.publish(physicalStick, 80, 20, true);
    direct.latchFrame(0, true);
    assert(!direct.mouseFrame().active && !direct.mouseFrame().ownsView && direct.readFrame(0, 0, 0)[4] == 200);
    // Flight retains native stick steering, normalized to elapsed sample time.
    HostPadState flight;
    flight.publish(neutral, 10, 0, true);
    flight.latchFrame(0, false, 1.0f / 30.0f);
    const auto normalRate = flight.readFrame(0, 0, 0)[4];
    flight.publish(neutral, 20, 0, true);
    flight.latchFrame(0, false, 1.0f / 15.0f);
    assert(flight.readFrame(0, 0, 0)[4] == normalRate);
    // An idle keyboard/mouse camera retains its view without requiring motion
    // or pointer lock; a controller/focus change hands ownership back safely.
    HostPadState idle;
    idle.publish(neutral);
    idle.latchFrame(0, true);
    assert(idle.mouseFrame().active && idle.mouseFrame().x == 0 && idle.mouseFrame().y == 0);
    idle.publish(neutral, 0, 0, false, false, true);
    idle.latchFrame(0, true);
    assert(!idle.mouseFrame().active);
    idle.publish(neutral, 0, 0, false, true, false);
    idle.latchFrame(0, true);
    assert(idle.mouseFrame().active);
    idle.publish(down, 12, 3, true, true, false, false);
    idle.latchFrame(0, true);
    assert(!idle.mouseFrame().active && idle.mouseFrame().ownsView && idle.readFrame(0, 0, 0) == neutral);
    idle.publish(neutral, 0, 0, false, false, true, false);
    idle.latchFrame(0, true);
    assert(idle.mouseFrame().ownsView); // Another app's controller activity cannot take the idle view.
    idle.publish(neutral, 0, 0, false, false, true, true);
    idle.latchFrame(0, true);
    assert(!idle.mouseFrame().ownsView); // A focused controller retains its native camera ownership.
    std::thread producer([&] {
        for (unsigned i = 0; i < 10000; ++i) {
            auto sample = neutral;
            sample[4] = sample[5] = static_cast<uint8_t>(i);
            pad.publish(sample);
        }
    });
    for (unsigned i = 0; i < 10000; ++i) {
        const auto sample = pad.consume();
        assert(sample[4] == sample[5]); // no torn samples
    }
    producer.join();
    // The pad and mouse ownership must come from the same publication, even
    // when the window thread publishes between engine input frames.
    HostPadState coherent;
    std::thread updater([&] {
        for (unsigned i = 0; i < 20000; ++i)
            coherent.publish(i % 2 ? neutral : physicalStick, 0, 0, true);
    });
    for (unsigned i = 0; i < 20000; ++i) {
        coherent.latchFrame(0, true);
        const auto sample = coherent.readFrame(0, 0, 0);
        const auto motion = coherent.mouseFrame();
        assert(sample[4] == 128 || (!motion.active && !motion.ownsView));
    }
    updater.join();
}
