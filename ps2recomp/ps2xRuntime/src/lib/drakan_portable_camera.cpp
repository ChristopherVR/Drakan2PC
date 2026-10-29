#include "drakan_portable_camera.h"
#include "drakan_config.h"
#include "drakan_controlled_actor.h"
#include "drakan_quick_inventory.h"
#include "drakan_settings_host.h"
#include "input/DrakanInput.h"
#include "input/QuickSlots.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <mutex>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;
void drakanInstallPortableMenuHooks(PS2Runtime&);
void drakanTickPortableMenuHooks(uint8_t*, R5900Context*, PS2Runtime*);
void drakanPortableMenuBoundary(uint8_t*, R5900Context*, PS2Runtime*);

namespace {
drakan_input::InputModule g_camera;
drakan_input::InputModule& g_controls = drakan_input::module();
drakan_input::InputAccumulator g_accumulator;
drakan_input::InputAccumulator g_uiAccumulator;
std::mutex g_inputMutex;
drakan_input::FrameInput g_hostInput;
std::atomic<bool> g_controlsReady{false};
std::atomic<unsigned> g_postsPending{0};
drakan_input::QuickInventoryBridge g_quickInventory;
drakan_input::QuickSlotDriver g_quickSlots;
drakan_input::QuickSlotDriver::Overlay g_quickOverlay{};
uint16_t g_quickButtons = 0;
PS2Runtime::RecompiledFunction g_pad = nullptr, g_look = nullptr, g_follow = nullptr;
std::array<std::atomic<float>, 8> g_cameraValues{};
std::array<std::atomic<float>, 10> g_actorValues{};
std::atomic<uint32_t> g_cameraSamples{0}, g_cameraRevision{0}, g_scanoutMode{0};

class CameraGuest final : public drakan_input::IGuest {
    uint8_t* memory_;
    R5900Context* context_;
    PS2Runtime* runtime_;
    static constexpr uint32_t kMask = 0x1ffffffu;

public:
    CameraGuest(uint8_t* memory, R5900Context* context, PS2Runtime* runtime)
        : memory_(memory), context_(context), runtime_(runtime) {}
    uint8_t read8(uint32_t address) override { return memory_[address & kMask]; }
    uint32_t read32(uint32_t address) override {
        uint32_t value;
        std::memcpy(&value, memory_ + (address & kMask), 4);
        return value;
    }
    void write8(uint32_t address, uint8_t value) override { memory_[address & kMask] = value; }
    void write32(uint32_t address, uint32_t value) override { std::memcpy(memory_ + (address & kMask), &value, 4); }
    uint32_t call(const drakan_input::GuestCall& call) override {
        if (g_quickInventory.intercept(
                *this, call, !g_controls.menuActive() && g_controls.frame().focused && !g_controls.suspended(),
                [](const char* text) { std::fputs(text, stderr); }))
            return 1;
        if (!call.addr || !runtime_->hasFunction(call.addr))
            return 0;
        auto temporary = std::make_unique<R5900Context>(*context_);
        for (int i = 0; i < call.ni; ++i)
            SET_GPR_U32(temporary.get(), 4 + i, call.i[i]);
        for (int i = 0; i < call.nf; ++i)
            temporary->f[12 + i] = call.f[i];
        SET_GPR_U32(temporary.get(), 31, 0);
        temporary->pc = call.addr;
        temporary->in_delay_slot = false;
        struct NoPreempt {
            NoPreempt() { ps2xNoPreemptEnter(); }
            ~NoPreempt() { ps2xNoPreemptLeave(); }
        } scope;
        for (unsigned guard = 0; guard < 64 && temporary->pc; ++guard) {
            auto function = runtime_->lookupFunction(temporary->pc);
            if (!function)
                return 0;
            function(memory_, temporary.get(), runtime_);
        }
        return temporary->pc ? 0 : getRegU32(temporary.get(), 2);
    }
    void post(const drakan_input::GuestCall& call) override {
        if (!call.addr || !runtime_->hasFunction(call.addr))
            return;
        ++g_postsPending;
        runtime_->eeScheduler().postGuestCall(
            call.addr, call.i[0], call.i[1], call.i[2], call.i[3], [](uint32_t) { --g_postsPending; }, 6);
    }
    bool postBusy() const override { return g_postsPending.load() != 0; }
};

void snapshot(CameraGuest& guest, uint32_t camera, uint64_t scanoutMode) {
    if (!drakan_controlled_actor::ram(camera, 0x224))
        return;
    const uint32_t owner = guest.read32(g_camera.config().addrCtxPtr);
    if (!drakan_controlled_actor::ram(owner, 0x4b8))
        return;
    const auto objectAtHandle = [&](uint32_t handle) -> uint32_t {
        if (!drakan_controlled_actor::ram(handle, 0x100))
            return 0;
        const uint32_t inner = guest.read32(handle + 0xfc);
        return drakan_controlled_actor::ram(inner, 0x14) ? guest.read32(inner + 0x10) : 0;
    };
    if (objectAtHandle(guest.read32(owner + 0x4b4)) != camera)
        return; // Do not mix another camera's follow tick into this diagnostic.
    g_cameraRevision.fetch_add(1, std::memory_order_acq_rel);
    constexpr uint32_t offsets[] = {0x84, 0x90, 0x9c, 0xa0, 0xa4};
    for (unsigned i = 0; i < std::size(offsets); ++i)
        g_cameraValues[i].store(guest.readF(camera + offsets[i]), std::memory_order_relaxed);
    const uint32_t object = guest.read32(camera + 0x148);
    if (drakan_controlled_actor::ram(object, 0x20))
        for (unsigned i = 0; i < 3; ++i)
            g_cameraValues[5 + i].store(guest.readF(object + 0x14 + 4 * i), std::memory_order_relaxed);
    const uint32_t candidate = objectAtHandle(guest.read32(owner + 0x4b0));
    const uint32_t player = drakan_controlled_actor::ram(candidate, 0x524) ? candidate : 0;
    const uint32_t pose = player ? guest.read32(player + 0x80) : 0;
    if (drakan_controlled_actor::ram(pose, 0x30)) {
        for (unsigned i = 0; i < 3; ++i)
            g_actorValues[i].store(guest.readF(pose + 0x14 + 4 * i), std::memory_order_relaxed);
        g_actorValues[3].store(std::atan2(guest.readF(pose + 0x28), guest.readF(pose + 0x20)),
                               std::memory_order_relaxed);
    }
    if (player) {
        g_actorValues[4].store(guest.readF(player + 0x520), std::memory_order_relaxed);
        g_actorValues[7].store(float(guest.read32(player + 0x94)), std::memory_order_relaxed);
    }
    g_actorValues[5].store(float(guest.read32(camera + 0x48)), std::memory_order_relaxed);
    g_actorValues[6].store(guest.readF(camera + 0x210), std::memory_order_relaxed);
    const auto mouse = ps2HostMouseFrame();
    g_actorValues[8].store(mouse.x, std::memory_order_relaxed);
    g_actorValues[9].store(mouse.y, std::memory_order_relaxed);
    g_scanoutMode.store(uint32_t(scanoutMode), std::memory_order_relaxed);
    g_cameraSamples.fetch_add(1, std::memory_order_relaxed);
    g_cameraRevision.fetch_add(1, std::memory_order_release);
}

void pad(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    drakanPortableMenuBoundary(memory, context, runtime);
    ps2BeginHostPadFrame(memory, g_camera.config().lookMode == drakan_input::LookMode::Direct,
                         g_camera.config().captureMouse);
    CameraGuest guest(memory, context, runtime);
    const auto actor = drakan_controlled_actor::read(guest);
    const auto mouse = ps2HostMouseFrame();
    const uint32_t screen = guest.read32(0x4fb60cu);
    const bool menu = drakan_controlled_actor::ram(screen, 0x1c);
    const bool scripted = guest.read32(0x4fbafcu) != 0;
    // Physical input is already cleared by the host on blur. Keep ownership
    // of the idle direct view until a controller takes over; otherwise native
    // stick centering would recenter the camera merely because focus was lost.
    g_camera.setSuspended(!mouse.ownsView);
    g_camera.setActiveScreen(menu ? int(guest.read32(screen + 0x18)) : -1);
    g_camera.setMenuActive(menu || scripted);
    drakan_input::FrameInput frame;
    frame.captured = mouse.ownsView && !menu && !scripted;
    frame.dx = int(std::lround(mouse.x));
    frame.dy = int(std::lround(mouse.y));
    frame.frameSeconds = mouse.seconds;
    g_camera.beginFrame(frame);
    g_camera.beforePadMgrUpdate(guest, actor.dragon);
    drakan_input::FrameInput controls;
    {
        std::lock_guard lock(g_inputMutex);
        static uint32_t previousScreen = UINT32_MAX;
        const uint32_t currentScreen = menu ? screen : (scripted ? UINT32_MAX - 1 : 0);
        if (previousScreen != currentScreen) {
            g_accumulator.releaseAll();
            previousScreen = currentScreen;
        }
        controls = g_hostInput;
        g_accumulator.consume(controls);
    }
    controls.captured = mouse.active && !menu && !scripted;
    controls.dx = int(std::lround(mouse.x));
    controls.dy = int(std::lround(mouse.y));
    controls.frameSeconds = mouse.seconds;
    g_controls.setMenuActive(menu || scripted);
    g_controls.setMenuContext(menu);
    g_controls.setActiveScreen(menu ? int(guest.read32(screen + 0x18)) : -1);
    g_controls.setExternalCapture(controls.captured);
    const float imageHeight = std::min(float(controls.clientH), float(controls.clientW) * 0.75f);
    const float imageWidth = imageHeight * (4.f / 3.f);
    g_controls.setMenuView({(controls.clientW - imageWidth) * 0.5f, (controls.clientH - imageHeight) * 0.5f, imageWidth,
                            imageHeight, 0.5f, 1.0f, true});
    g_controls.beginExternalFrame(controls);
    drakanTickPortableMenuHooks(memory, context, runtime);
    g_controls.beforePadMgrUpdate(guest, actor.dragon);
    g_controls.updateBowAim(guest);
    int quickSlot = 0;
    for (const auto& binding : g_controls.config().game)
        if (binding.act == drakan_input::Binding::Quick &&
            ((binding.src == drakan_input::Binding::Key && controls.keyPressed[size_t(binding.code) & 255]) ||
             (binding.src == drakan_input::Binding::Mouse && (controls.mousePressed & binding.code))))
            quickSlot = int(binding.value);
    const bool gameplay = !menu && !scripted && controls.focused && !actor.dragon;
    g_quickSlots.tick(guest, quickSlot, gameplay, g_controls.config().addrPadMgr, nullptr);
    g_quickOverlay = g_quickSlots.overlay();
    g_quickButtons = g_quickInventory.buttons(guest, gameplay);
    g_pad(memory, context, runtime);
    g_controls.afterPadMgrUpdate(guest);
}

void look(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    CameraGuest guest(memory, context, runtime);
    const uint32_t player = getRegU32(context, 4), limits = getRegU32(context, 5);
    const bool dragon = getRegU32(context, 6) != 0;
    const uint32_t owner = guest.read32(g_camera.config().addrCtxPtr);
    const uint32_t handle = owner ? guest.read32(owner + 0x4b4) : 0;
    const uint32_t inner = handle ? guest.read32(handle + 0xfc) : 0;
    const uint32_t camera = inner ? guest.read32(inner + 0x10) : 0;
    if (!dragon && g_camera.freeCombatAllowed(guest, player, camera))
        guest.writeF(camera + 0x90, 0); // Match the Windows direct-follow camera hook.
    g_camera.beforeCameraUpdateLook(guest, dragon, player);
    g_look(memory, context, runtime);
    g_camera.afterCameraUpdateLook(guest, player, limits, dragon);
}

void follow(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    CameraGuest guest(memory, context, runtime);
    const uint32_t camera = getRegU32(context, 4);
    g_camera.beforeCameraFollow(guest, camera);
    g_follow(memory, context, runtime);
    static const bool metrics = [] {
        const char* value = std::getenv("DRAKAN_FRAME_METRICS");
        return value && std::strcmp(value, "1") == 0;
    }();
    if (metrics)
        snapshot(guest, camera, runtime->memory().gs().smode2);
}
} // namespace

void drakanInstallPortableCamera(PS2Runtime& runtime) {
    g_pad = runtime.lookupFunction(0x1e3970u);
    g_look = runtime.lookupFunction(0x44e4a8u);
    g_follow = runtime.lookupFunction(0x41f848u);
    if (!g_pad || !g_look || !g_follow)
        return;
    const char* configured = std::getenv("DRAKAN_INPUT_INI");
    const auto path = configured ? std::string(configured) : drakan_cfg::exeDir() + "drakan_input.ini";
    std::string warnings;
    auto config = drakan_input::Config::loadOrCreate(path, &warnings);
    g_controls.initHeadless(config);
    g_controlsReady.store(true, std::memory_order_release);
    // Keep direct-view ownership across focus loss independently of physical
    // controls. Only the controls module changes the guest's movement mapping.
    config.game.clear();
    config.menu.clear();
    config.strafeKeys = false;
    g_camera.initHeadless(config);
    drakanInstallPortableMenuHooks(runtime);
    runtime.replaceFunction(0x1e3970u, &pad);
    runtime.replaceFunction(0x44e4a8u, &look);
    runtime.replaceFunction(0x41f848u, &follow);
    std::fprintf(stderr, "[input] portable direct mouse camera active\n");
}

void drakanPortablePublishInput(const drakan_input::FrameInput& frame) {
    std::lock_guard lock(g_inputMutex);
    for (unsigned key = 0; key < 256; ++key) {
        g_accumulator.onKey(int(key), frame.keyDown[key]);
        g_uiAccumulator.onKey(int(key), frame.keyDown[key]);
    }
    for (unsigned button = 0; button < 5; ++button) {
        g_accumulator.onMouseButton(uint8_t(1u << button), (frame.mouseDown & (1u << button)) != 0);
        g_uiAccumulator.onMouseButton(uint8_t(1u << button), (frame.mouseDown & (1u << button)) != 0);
    }
    if (frame.wheel) {
        g_accumulator.onWheel(frame.wheel);
        g_uiAccumulator.onWheel(frame.wheel);
    }
    if (!frame.focused && g_hostInput.focused) {
        g_accumulator.releaseAll();
        g_uiAccumulator.releaseAll();
    }
    g_hostInput = frame;
}

drakan_input::FrameInput drakanPortableTakeUiInput() {
    std::lock_guard lock(g_inputMutex);
    auto result = g_hostInput;
    g_uiAccumulator.consume(result);
    return result;
}

void drakanPortableReloadControls(const std::string& path) {
    auto config = drakan_input::Config::loadOrCreate(path, nullptr);
    g_controls.reloadConfig(config);
    config.game.clear();
    config.menu.clear();
    config.strafeKeys = false;
    g_camera.reloadConfig(config);
}

bool drakanPortablePadSample(int port, int slot, uint8_t out[32]) {
    if (!g_controlsReady.load(std::memory_order_acquire) || !g_controls.fillPadSample(port, slot, out))
        return false;
    out[2] |= uint8_t(g_quickOverlay.up >> 8);
    out[3] |= uint8_t(g_quickOverlay.up);
    const uint16_t down = uint16_t(g_quickButtons | g_quickOverlay.down);
    out[2] &= uint8_t(~(down >> 8));
    out[3] &= uint8_t(~down);
    return true;
}

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE double drakan_web_camera_metric(unsigned index) {
    if (index == 8)
        return g_cameraSamples.load(std::memory_order_relaxed);
    if (index == 19)
        return g_cameraRevision.load(std::memory_order_acquire);
    if (index == 20)
        return g_scanoutMode.load(std::memory_order_relaxed);
    if (index >= 9 && index - 9 < g_actorValues.size())
        return g_actorValues[index - 9].load(std::memory_order_relaxed);
    return index < g_cameraValues.size() ? g_cameraValues[index].load(std::memory_order_relaxed) : 0;
}
#endif
