#include "drakan_movie_policy.h"
#include "drakan_input_flow.h"
#include "drakan_pause_policy.h"

#include "input/DrakanInput.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

// Exercise actual input code using each profile, including a deliberately relocated object layout.
class CameraGuest final : public drakan_input::IGuest {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x2000000);
    drakan_input::CameraProfile profile;
    float turnAngle = 0;
    unsigned turns = 0;
    std::vector<drakan_input::GuestCall> commands;
    explicit CameraGuest(drakan_input::CameraProfile p) : profile(p) {}
    uint8_t read8(uint32_t a) override {
        assert(a < ram.size());
        return ram[a];
    }
    uint32_t read32(uint32_t a) override {
        assert(a <= ram.size() - 4);
        uint32_t value;
        std::memcpy(&value, ram.data() + a, 4);
        return value;
    }
    void write8(uint32_t a, uint8_t v) override {
        assert(a < ram.size());
        ram[a] = v;
    }
    void write32(uint32_t a, uint32_t v) override {
        assert(a <= ram.size() - 4);
        std::memcpy(ram.data() + a, &v, 4);
    }
    uint32_t call(const drakan_input::GuestCall& c) override {
        if (c.addr == profile.rynnPitch || c.addr == profile.dragonPitch)
            return 0;
        if (c.addr == profile.timerMs)
            return 123;
        if (c.addr == profile.invokeCallback) {
            commands.push_back(c);
            return 0;
        }
        assert(c.addr == profile.turn);
        assert(c.nf == 1 && c.ni == 2 && c.i[1] == profile.turnUp);
        turnAngle = c.f[0];
        ++turns;
        return 0;
    }
};

void testCamera(drakan_input::CameraProfile profile) {
    using namespace drakan_input;
    CameraGuest g(profile);
    const auto& l = profile.layout;
    constexpr uint32_t ctx = 0x1000000, h = 0x1002000, inner = 0x1004000, cam = 0x1006000, player = 0x1008000,
                       vt = 0x100a000, transform = 0x100c000;
    g.write32(profile.context, ctx);
    g.write32(ctx + l.contextCamera, h);
    g.write32(h + l.handleInner, inner);
    g.write32(inner + l.innerData, cam);
    g.write32(player + l.playerTransform, transform);
    g.write32(player + l.playerVtable, vt);
    g.write32(vt + l.vtablePitch, profile.rynnPitch);
    g.write32(cam + l.cameraMode, 6);
    g.write32(profile.stickMode, 1);
    Config c = Config::defaults();
    c.freeCombat = false;
    c.checkTurnGate = false;
    c.sensX = c.sensY = .002f;
    InputModule m;
    m.setCameraProfile(profile);
    m.initHeadless(c);
    // A saved PAL advanced section must never overwrite the selected disc's ABI.
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    assert(m.config().addrCtxPtr == profile.context && m.config().addrTurnFn == profile.turn);
    assert(m.config().addrStickModeFlag == profile.stickMode);
    if (!profile.advancedInput)
        assert(!m.config().bowAimModern);
    assert(m.config().strafeKeys == profile.movementInput);
    FrameInput f;
    f.focused = f.captured = true;
    f.dx = 10;
    f.dy = -18;
    m.beginFrame(f);
    g.writeF(cam + l.cameraPitchRate, .5f);
    m.beforeCameraUpdateLook(g, false, player);
    assert(g.read32(profile.stickMode) == 0 && g.readF(cam + l.cameraPitchRate) == 0);
    m.afterCameraUpdateLook(g, player, 0, false);
    assert(g.read32(profile.stickMode) == 1);
    assert(std::fabs(g.readF(cam + l.cameraPitch) - .02f) < .00001f);
    assert(g.turns == 1 && std::fabs(g.turnAngle + .02f) < .00001f);
    // Mounted control comes from this disc's resource class before pad synthesis,
    // even when the native camera-look callback is skipped during a transition.
    constexpr uint32_t resources = 0x1010000, table = 0x1012000;
    g.write32(ctx + l.contextActor, h);
    g.write32(ctx + l.contextResources, resources);
    g.write32(resources + l.resourceCount, 0);
    g.write32(resources + l.resourceTable, table);
    g.write32(table + l.resourceClass, l.dragonClass);
    assert(m.controlledDragon(g));
    g.write32(table + l.resourceClass, 0);
    assert(!m.controlledDragon(g));
    g.write32(resources + l.resourceTable, 0x1fffffff);
    assert(!m.controlledDragon(g));
    // No second consumption and no displacement from an overlay-owned sample.
    m.afterCameraUpdateLook(g, player, 0, false);
    assert(g.turns == 1);
    m.setSuspended(true);
    m.beginFrame(f);
    m.beforeCameraUpdateLook(g, false, player);
    m.afterCameraUpdateLook(g, player, 0, false);
    assert(g.turns == 1 && g.read32(profile.stickMode) == 1);
    m.setSuspended(false);
    f.gamepad.connected = true;
    f.gamepad.rx = .5f;
    m.beginFrame(f);
    m.beforeCameraUpdateLook(g, false, player);
    m.afterCameraUpdateLook(g, player, 0, false);
    assert(g.turns == 1 && g.read32(profile.stickMode) == 1);
}

void testMovement(drakan_input::CameraProfile profile) {
    using namespace drakan_input;
    CameraGuest g(profile);
    InputModule input;
    input.setCameraProfile(profile);
    auto cfg = Config::defaults();
    cfg.toggleRun = false;
    input.initHeadless(cfg);
    input.reloadConfig(cfg);
    FrameInput frame;
    frame.focused = frame.captured = true;
    frame.keyDown.set(0x2a); // Left Shift
    g.write32(profile.stickMap, 0);
    g.writeF(profile.stickMap + 8, .5f);
    input.beginFrame(frame);
    input.beforePadMgrUpdate(g, false);
    assert(g.read32(profile.stickMap) == 3);
    input.afterMovementInput(g);
    assert(g.commands.size() == 1);
    const auto& press = g.commands.back();
    assert(press.addr == profile.invokeCallback && press.ni == 5);
    assert(press.i[0] == profile.padManager && press.i[1] == 0x4010 && press.i[3] == 1 && press.i[4] == 123);
    input.afterMovementInput(g);
    assert(g.commands.size() == 1);
    // Overlay ownership releases walking once; fresh held input can resume it.
    input.setSuspended(true);
    input.afterMovementInput(g);
    assert(g.commands.size() == 2 && g.commands.back().i[3] == 0);
    input.setSuspended(false);
    input.beginFrame(frame);
    input.afterMovementInput(g);
    assert(g.commands.size() == 3 && g.commands.back().i[3] == 1);
    // A physical left stick keeps its native gait and axis mapping.
    frame.gamepad.connected = true;
    frame.gamepad.lx = .5f;
    input.beginFrame(frame);
    input.beforePadMgrUpdate(g, false);
    input.afterMovementInput(g);
    assert(g.commands.size() == 4 && g.commands.back().i[3] == 0);
    assert(g.read32(profile.stickMap) == 0 && g.readF(profile.stickMap + 8) == .5f);
    frame.gamepad = {};
    input.beginFrame(frame);
    input.beforePadMgrUpdate(g, false);
    input.afterMovementInput(g);
    assert(g.commands.size() == 5 && g.commands.back().i[3] == 1);
    // Flight restores the disc's native axis map and scale and releases walk.
    input.beforePadMgrUpdate(g, true);
    input.afterMovementInput(g);
    assert(g.commands.size() == 6 && g.commands.back().i[3] == 0);
    assert(g.read32(profile.stickMap) == 0 && g.readF(profile.stickMap + 8) == .5f);
    profile.movementInput = false;
    input.setCameraProfile(profile);
    input.reloadConfig(cfg);
    input.beginFrame(frame);
    input.beforePadMgrUpdate(g, false);
    input.afterMovementInput(g);
    assert(!input.config().strafeKeys && g.commands.size() == 6);
}

int main() {
    auto cameraProfile = drakan_region::inputCameraProfile();
    testMovement(cameraProfile);
    testCamera(cameraProfile);
    cameraProfile.layout.contextCamera += 0x40;
    cameraProfile.layout.cameraPitch += 0x400;
    cameraProfile.layout.playerVtable += 0x400;
    cameraProfile.layout.vtablePitch += 0x40;
    testCamera(cameraProfile);
    assert(!drakan_input_flow::autostartReady(0, 80));
    assert(!drakan_input_flow::autostartReady(100, 79));
    assert(drakan_input_flow::autostartReady(100, 80));
    assert(drakan_input_flow::autostartReady(0, 240));
    using namespace drakan_region;
    assert(supports(Feature::MovieGuard));
    assert(supports(Feature::MoviePacing));
    assert(supports(Feature::StereoSync));
    if (std::strcmp(build().id, "pal") == 0) {
        assert(build().features == (0x7ffffu & ~featureBit(Feature::BasicInput)));
        assert(guestAddress(0x1767a0u) == 0x1767a0u);
        assert(guestAddress(0x123456u) == 0x123456u);
        assert(supports(Feature::AdvancedInput));
        assert(!supports(Feature::BasicInput));
        assert(drakan_movie::framePeriod(build().movie).count() == 40000000);
        assert(build().movie.screenCount == 33);
        assert(guestAddress(0x4db4f0u) + build().audio.managerStereo == 0x4db930u);
        assert(guestAddress(0x4fb6d0u) + build().audio.optionStereo == 0x4fb744u);
    } else {
        assert(std::strcmp(build().id, "usa") == 0);
        assert(build().features == (featureBit(Feature::BasicInput) | featureBit(Feature::MoviePacing) |
                                    featureBit(Feature::MovieGuard) | featureBit(Feature::StereoSync) |
                                    featureBit(Feature::InputFlow) | featureBit(Feature::OptionsSettings) |
                                    featureBit(Feature::DirectMouseLook) | featureBit(Feature::MovementInput)));
        assert(std::strcmp(build().bootElf, "SCUS_971.28") == 0);
        assert(std::strcmp(build().saveId, "BASCUS-97128") == 0);
        assert(guestAddress(0x1767a0u) == 0x176740u);
        assert(guestAddress(0x440610u) == 0x43ff38u);
        assert(guestAddress(0x4fb600u) == 0x4fc480u);
        assert(guestAddress(0x123456u) == 0u);
        assert(guestAddress(0u) == 0u);
        assert(!supports(Feature::AdvancedInput));
        assert(supports(Feature::BasicInput));
        assert(drakan_movie::framePeriod(build().movie).count() == 33366666);
        assert(build().movie.screenCount == 32);
        assert(guestAddress(0x4db4f0u) + build().audio.managerStereo == 0x4dc7a8u);
        assert(guestAddress(0x4fb6d0u) + build().audio.optionStereo == 0x4fc5c4u);
    }
    for (const auto& pair : kAddressPairs)
        assert(guestAddress(pair.pal) == pair.region);

    std::vector<uint8_t> memory(4096);
    constexpr uint32_t manager = 128, listener = 2048;
    const auto& layout = build().movie;
    const size_t end = manager + layout.screenEntries + layout.screenCount * 4;
    // A recycled pointer outside this release's actual table must not count as alive.
    std::memcpy(memory.data() + end, &listener, 4);
    assert(!drakan_movie::liveListener(memory, manager, listener, layout));
    std::memcpy(memory.data() + end - 4, &listener, 4);
    assert(drakan_movie::liveListener(memory, manager, listener, layout));
    assert(!drakan_movie::liveListener(memory, manager, 0, layout));
    assert(!drakan_movie::liveListener(memory, 0, listener, layout));
    assert(!drakan_movie::liveListener(memory, 0xfffffff0u, listener, layout));
    assert(!drakan_movie::liveListener(std::span(memory).first(end - 1), manager, listener, layout));
    // Read the same synthetic objects through each disc's addresses and layout.
    std::vector<uint8_t> ram(0x2000000);
    auto put = [&](uint32_t address, uint32_t value) { std::memcpy(ram.data() + address, &value, 4); };
    const auto& flow = build().inputFlow;
    constexpr uint32_t context = 0x1000000, handle = 0x1001000, inner = 0x1002000, camera = 0x1003000;
    drakan_input_flow::Reader reader(ram);
    assert(reader.screen() == -1 && !reader.gameplay());
    put(guestAddress(0x5565d4), context);
    put(context + flow.contextCamera, handle);
    put(handle + flow.handleInner, inner);
    put(inner + flow.innerData, camera);
    put(camera + flow.cameraMode, 1);
    // Gameplay is independent of native option values (including AutoMountDragon).
    assert(reader.gameplay());
    put(camera + flow.cameraMode, 6);
    assert(reader.gameplay());
    put(guestAddress(0x4fbafc), 1);
    assert(!reader.gameplay() && reader.scripted());
    put(guestAddress(0x4fbafc), 0);
    put(guestAddress(0x4fb60c), 0x1004000);
    put(0x1004000 + flow.screenId, flow.pauseScreen);
    assert(reader.screen() == static_cast<int>(flow.pauseScreen) && !reader.gameplay());
    put(guestAddress(0x4fb60c), 0x1ffffff);
    assert(reader.screen() == -1);
    put(inner + flow.innerData, 0xfffffff0);
    assert(!reader.gameplay());
    assert(!drakan_input_flow::inMask(-1, flow.menuScreens));
    assert(!drakan_input_flow::inMask(32, flow.menuScreens));
    assert(drakan_input_flow::inMask(flow.titleScreen, flow.menuScreens));

    // F1 requests exactly one native pause, waits for it, and resumes only our own pause.
    for (bool resume : {false, true}) {
        drakan_pause::InputPolicy policy;
        auto a = policy.tick(true, -1, true, false, false, resume, flow.pauseScreen, 0);
        assert(a.tapStart && !a.open);
        a = policy.tick(true, -1, true, false, false, resume, flow.pauseScreen, 20);
        assert(!a.tapStart && !a.open);
        a = policy.tick(false, flow.pauseScreen, false, false, false, resume, flow.pauseScreen, 100);
        assert(a.open && !a.tapStart);
        policy.tick(false, flow.pauseScreen, false, false, true, resume, flow.pauseScreen, 200);
        a = policy.tick(false, flow.pauseScreen, false, false, false, resume, flow.pauseScreen, 300);
        assert(a.tapStart == resume && !a.open);
        a = policy.tick(false, flow.pauseScreen, false, false, false, resume, flow.pauseScreen, 400);
        assert(!a.tapStart);
    }
    drakan_pause::InputPolicy menu;
    auto a = menu.tick(true, flow.pauseScreen, false, false, false, true, flow.pauseScreen, 0);
    assert(a.open && !a.tapStart);
    menu.tick(false, flow.pauseScreen, false, false, true, true, flow.pauseScreen, 100);
    assert(!menu.tick(false, flow.pauseScreen, false, false, false, true, flow.pauseScreen, 200).tapStart);
    drakan_pause::InputPolicy timeout;
    timeout.tick(true, -1, true, false, false, false, flow.pauseScreen, 0);
    assert(timeout.tick(false, -1, true, false, false, false, flow.pauseScreen, 3001).open);
    drakan_pause::InputPolicy cutscene;
    cutscene.tick(true, -1, true, false, false, false, flow.pauseScreen, 0);
    assert(!cutscene.tick(false, -1, true, true, false, false, flow.pauseScreen, 100).open);
    assert(!cutscene.tick(false, flow.pauseScreen, false, false, false, false, flow.pauseScreen, 200).open);
    return 0;
}
