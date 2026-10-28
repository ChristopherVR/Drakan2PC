#include "input/DrakanInput.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace drakan_input {

InputModule::InputModule() = default;
InputModule::~InputModule() {
    shutdown();
}

bool InputModule::init(void* hwnd, const std::string& configPath) {
    std::string warn;
    cfg_ = Config::loadOrCreate(configPath, &warn);
    if (!warn.empty())
        std::fprintf(stderr, "[drakan_input] %s: %s", configPath.c_str(), warn.c_str());
    host_ = std::make_unique<HostInput>();
    if (!host_->init(hwnd, &acc_)) {
        std::fprintf(stderr, "[drakan_input] raw input could not be started\n");
        host_.reset();
        return false;
    }
    return true;
}

void InputModule::initHeadless(const Config& cfg) {
    cfg_ = cfg;
    host_.reset();
    scriptClientW_ = scriptClientH_ = 0;
}

void InputModule::shutdown() {
    if (host_) {
        host_->shutdown();
        host_.reset();
    }
}

bool InputModule::onWindowMessage(void* hwnd, unsigned msg, uintptr_t w, intptr_t l, intptr_t* r) {
    return host_ ? host_->handleMessage(hwnd, msg, w, l, r) : false;
}

bool InputModule::loadScript(const std::string& path, std::string* err) {
    if (!script_.load(path, err))
        return false;
    scripted_ = true;
    return true;
}

void InputModule::setCaptureWindow(void* hwnd) {
    if (host_)
        host_->setCaptureWindow(hwnd);
}

void InputModule::setSuspended(bool s) {
    std::lock_guard<std::mutex> lock(m_);
    const bool was = hostOwned();
    suspended_.store(s);
    if (!was && hostOwned())
        releaseOwnedInputLocked();
}

void InputModule::setHostScreen(bool on) {
    std::lock_guard<std::mutex> lock(m_);
    const bool was = hostOwned();
    hostScreen_.store(on);
    if (!was && hostOwned())
        releaseOwnedInputLocked();
}

void InputModule::queueHostScreenEvents(const FrameInput& f) {
    std::lock_guard<std::mutex> lk(hostEvM_);
    if (hostEv_.size() > 512)
        hostEv_.clear(); // nobody drains (the overlay is not drawing)
    if (f.cursorX != hostCursorX_ || f.cursorY != hostCursorY_) {
        hostCursorX_ = f.cursorX;
        hostCursorY_ = f.cursorY;
        HostScreenEvent e;
        e.kind = HostScreenEvent::Cursor;
        e.x = f.cursorX;
        e.y = f.cursorY;
        e.clientW = f.clientW;
        e.clientH = f.clientH;
        hostEv_.push_back(e);
    }
    for (int k = 0; k < 256; ++k) {
        if (f.keyPressed[size_t(k)])
            hostEv_.push_back({HostScreenEvent::Key, k, true});
        if (f.keyReleased[size_t(k)])
            hostEv_.push_back({HostScreenEvent::Key, k, false});
    }
    for (uint8_t b : {uint8_t(MBTN_LEFT), uint8_t(MBTN_RIGHT), uint8_t(MBTN_MIDDLE)}) {
        if (f.mousePressed & b)
            hostEv_.push_back({HostScreenEvent::MouseButton, b, true});
        if (f.mouseReleased & b)
            hostEv_.push_back({HostScreenEvent::MouseButton, b, false});
    }
    const uint16_t pad = f.gamepad.connected ? f.gamepad.buttons : 0;
    for (int bit = 0; bit < 16; ++bit) {
        const uint16_t m = uint16_t(1u << bit);
        if ((pad ^ hostPadPrev_) & m)
            hostEv_.push_back({HostScreenEvent::Pad, m, (pad & m) != 0});
    }
    hostPadPrev_ = pad;
}

std::vector<InputModule::HostScreenEvent> InputModule::drainHostScreenEvents() {
    std::lock_guard<std::mutex> lk(hostEvM_);
    std::vector<HostScreenEvent> out;
    out.swap(hostEv_);
    return out;
}

void InputModule::releaseOwnedInputLocked() { // m_ held
    walkHeld_ = walkLatched_ = walkActive_ = false;
    // Invalidate at the ownership transition itself: no guest hook is guaranteed
    // to run before the overlay closes again. Keep explicit host taps intact.
    frame_.keyDown.reset();
    frame_.keyPressed.reset();
    frame_.keyReleased.reset();
    frame_.mouseDown = frame_.mousePressed = frame_.mouseReleased = 0;
    frame_.dx = frame_.dy = frame_.wheel = 0;
    frame_.gamepad = GamepadState{};
    pendingDx_ = pendingDy_ = 0;
    aimPitchOwned_ = aimPitchSaved_ = false;
    padReady_ = false;
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const CommandEvent& e) { return e.pressed; }),
                   pending_.end());
    tracker_.reset(&pending_);
    acc_.releaseAll();
    inputWasSuspended_ = true;
}

void InputModule::setMenuView(const MenuView& v) {
    std::lock_guard<std::mutex> lk(viewM_);
    view_ = v;
}

void InputModule::injectPadTap(uint16_t bits, int frames) {
    if (!bits || frames <= 0)
        return;
    std::lock_guard<std::mutex> lk(tapM_);
    taps_.push_back({bits, frames});
}

bool InputModule::reloadConfig(const std::string& path, std::string* warnings) {
    { // do not create a file here: a missing file is an error for a live reload
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        std::fclose(f);
    }
    std::string warn;
    Config c = Config::loadOrCreate(path, &warn);
    if (!warn.empty())
        std::fprintf(stderr, "[drakan_input] %s: %s", path.c_str(), warn.c_str());
    if (warnings)
        *warnings = warn;
    reloadConfig(c);
    return true;
}

void InputModule::reloadConfig(const Config& cfg) {
    auto p = std::make_unique<Config>(cfg);
    std::lock_guard<std::mutex> lk(pendM_);
    pending_cfg_ = std::move(p);
    hasPending_.store(true);
}

void InputModule::applyPendingLocked() { // m_ held
    if (!hasPending_.load())
        return;
    std::unique_ptr<Config> p;
    {
        std::lock_guard<std::mutex> lk(pendM_);
        p = std::move(pending_cfg_);
        hasPending_.store(false);
    }
    if (!p)
        return;
    cfg_ = std::move(*p);
    walkHeld_ = walkLatched_ = walkActive_ = false;
    tracker_.reset(&pending_); // release commands held through the old bindings
    padReady_ = false;
    pendingDx_ = pendingDy_ = 0;
    aimPitchOwned_ = aimPitchSaved_ = false;
    ++cfgGen_;
}

void InputModule::applyPendingConfig() {
    if (!hasPending_.load())
        return;
    std::lock_guard<std::mutex> lock(m_);
    applyPendingLocked();
}

void InputModule::beginFrame() {
    applyPendingConfig();
    ++frameNo_;
    const auto frameAt = std::chrono::steady_clock::now();
    const float frameSeconds =
        haveFrameAt_ ? std::chrono::duration<float>(frameAt - lastFrameAt_).count() : 1.0f / 30.0f;
    lastFrameAt_ = frameAt;
    haveFrameAt_ = true;
    FrameInput f;
    if (scripted_) {
        uint16_t pad = 0;
        scriptEv_ = ScriptEvents();
        f = frame_; // previous snapshot is not needed (the script keeps its own edge state)
        f.keyPressed.reset();
        f.keyReleased.reset();
        const auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        script_.setScreen(activeScreen_);
        script_.advance(frameNo_, playRun_, f, pad, scriptEv_, static_cast<uint64_t>(nowMs));
        int clientW = scriptClientW_, clientH = scriptClientH_;
        if ((clientW <= 0 || clientH <= 0) && host_) {
            FrameInput real;
            host_->poll(real, false, cfg_.gamepadDeadzone);
            clientW = real.clientW;
            clientH = real.clientH;
        }
        if (clientW > 0 && clientH > 0 && f.clientW > 0 && f.clientH > 0) {
            f.cursorX = static_cast<int>(static_cast<int64_t>(f.cursorX) * clientW / f.clientW);
            f.cursorY = static_cast<int>(static_cast<int64_t>(f.cursorY) * clientH / f.clientH);
            f.clientW = clientW;
            f.clientH = clientH;
        }
        f.gamepad = GamepadState{};
        if (pad) {
            f.gamepad.connected = true;
            f.gamepad.buttons = pad;
        }
    } else {
        // Discard delayed edges before changing input ownership. A held combat
        // button must not become a menu confirm, or replay when F1 closes.
        const bool titleShortcut = activeScreen_ == 8 && hostScreen_.load() && !suspended_.load();
        if (!titleShortcut && (menuActive_.load() != layerIsMenu_ || hostOwned() || inputWasSuspended_))
            acc_.releaseAll();
        acc_.consume(f, mouseCutoffUs(InputAccumulator::nowUs()));
        if (host_)
            host_->poll(f, cfg_.gamepadEnabled, cfg_.gamepadDeadzone);
    }
    f.frameSeconds = frameSeconds;
    beginFrame(f);
}

int64_t InputModule::mouseCutoffUs(int64_t now) {
    const double refresh = sampleRefresh_.load();
    const int64_t prevFrame = lastFrameUs_;
    lastFrameUs_ = now;
    if (!(refresh > 0.002 && refresh < 0.1) || !prevFrame || !mouseCutoff_) {
        mouseCutoff_ = now;
        return now;
    }
    // The tick covered a whole number of refreshes: advance the window by exactly that much. Never read ahead of
    // now, and never fall more than 1.5 refreshes behind it (a stall, a refresh change), so the window settles on
    // the earliest phase beginFrame runs at and stays there.
    const int64_t period = static_cast<int64_t>(refresh * 1.0e6);
    const int64_t n = std::max<int64_t>(1, (now - prevFrame + period / 2) / period);
    mouseCutoff_ = std::clamp(mouseCutoff_ + n * period, now - period * 3 / 2, now);
    return mouseCutoff_;
}

void InputModule::beginExternalFrame(const FrameInput& injected) {
    ++frameNo_;
    beginFrame(injected);
}

void InputModule::beginFrame(const FrameInput& injected) {
    std::lock_guard<std::mutex> lock(m_);
    applyPendingLocked();
    const bool menu = menuActive_.load(); // one consistent value for the whole frame
    const bool menuCtx =
        menuCtx_.load(); // a game menu / inventory / map / the overlay is really up (the cursor is needed); cut scenes
                         // and loading are "menu layer" but keep the mouse captured
    aimPitchSaved_ = false; // a before/after pair must belong to one input snapshot
    frame_ = injected;
    titleQuickLoad_ = false;
    if (activeScreen_ == 8 && injected.focused && !suspended_.load()) {
        FrameInput edge = injected;
        edge.keyDown = edge.keyPressed;
        edge.mouseDown = edge.mousePressed;
        for (const auto& b : cfg_.game)
            if (b.act == Binding::Save && b.value == kQuickLoad && sourceLevel(b, edge, true))
                titleQuickLoad_ = true;
    }
    if (scripted_ && hostScreen_.load())
        queueHostScreenEvents(injected);
    const bool suspended = hostOwned();
    if (!frame_.focused || suspended) {
        frame_.keyDown.reset();
        frame_.keyPressed.reset();
        frame_.keyReleased.reset();
        frame_.mouseDown = frame_.mousePressed = frame_.mouseReleased = 0;
        frame_.dx = frame_.dy = frame_.wheel = 0;
        frame_.gamepad = GamepadState{};
    }
    // ---- capture state machine
    if (!frame_.focused) {
        focusLost_ = true;
        userReleased_ = true;
    } else {
        bool toggled = false;
        if (frame_.keyPressed[1] && cfg_.releaseOnEsc)
            toggled = true;
        if (cfg_.toggleKeyDik >= 0 && frame_.keyPressed[static_cast<size_t>(cfg_.toggleKeyDik) & 255])
            toggled = true;
        if (focusLost_ && (frame_.mousePressed & MBTN_LEFT)) {
            focusLost_ = false;
            userReleased_ = false;
            if (!menuCtx)
                recaptureButtons_ |= MBTN_LEFT;
        } else if (toggled) {
            focusLost_ = false;
            userReleased_ = !userReleased_;
        }
    }
    bool want = cfg_.captureMouse && frame_.focused && !menuCtx && !userReleased_ && !hostOwned();
    if (const int external = externalCapture_.load(); external >= 0)
        want = external != 0 && cfg_.captureMouse && frame_.focused && !menuCtx && !hostOwned();
    if (host_) {
        host_->setCapture(want);
        host_->setMenuCursorHidden(menuCtx && cfg_.menuMouse && cfg_.menuDrawCursor && overlayCursor_.load() &&
                                   !hostOwned());
    }
    frame_.captured = want;
    // Keep input ownership across a pad button's release. An idle connected
    // controller must not turn mouse combat back into controller targeting.
    const auto& gp = frame_.gamepad;
    if (cfg_.gamepadEnabled && gp.connected && (gp.buttons || gp.lx || gp.ly || gp.rx || gp.ry))
        mouseKeyboardOwner_ = false;
    if (frame_.dx || frame_.dy || frame_.wheel || frame_.mouseDown || frame_.mousePressed || frame_.keyDown.any() ||
        frame_.keyPressed.any())
        mouseKeyboardOwner_ = true;
    freeCombatFrame_.store(cfg_.freeCombat && cfg_.lookMode == LookMode::Direct && mouseKeyboardOwner_ && want &&
                           !menu && !dragon_ && !nativeLookControlActive());
    if (inputWasCaptured_ && !want)
        acc_.releaseAll();
    inputWasCaptured_ = want;
    { // injected taps (START for the pause menu, wheel -> D-pad, ...): this frame's share
        std::lock_guard<std::mutex> lk(tapM_);
        tapNow_ = 0;
        for (auto it = taps_.begin(); it != taps_.end();) {
            tapNow_ = static_cast<uint16_t>(tapNow_ | it->bits);
            if (--it->left <= 0)
                it = taps_.erase(it);
            else
                ++it;
        }
    }

    if (!frame_.captured) {
        frame_.dx = frame_.dy = 0;
        if (!menuCtx) { // free cursor over a non-menu screen: the click must not attack
            frame_.mouseDown = frame_.mousePressed = frame_.mouseReleased = 0;
            frame_.wheel = 0;
        }
    }
    recaptureButtons_ &= static_cast<uint8_t>(injected.mouseDown | injected.mousePressed);
    frame_.mouseDown &= static_cast<uint8_t>(~recaptureButtons_);
    frame_.mousePressed &= static_cast<uint8_t>(~recaptureButtons_);
    frame_.mouseReleased &= static_cast<uint8_t>(~recaptureButtons_);
    bowAimFrame();
    // Counts are displacement, not a per-frame stick rate. Keep samples when
    // the native camera skips a tick; discard them whenever ownership changes.
    const bool direct = cfg_.lookMode == LookMode::Direct && !(dragon_ && cfg_.dragonStick);
    if (!direct || menu || !frame_.captured || suspended || nativeLookControlActive())
        aimPitchOwned_ = false;
    if (!direct || menu || !frame_.captured || suspended || menu != layerIsMenu_) {
        pendingDx_ = pendingDy_ = 0;
    } else {
        auto add = [](int a, int b) {
            return static_cast<int>(std::clamp<long long>(
                static_cast<long long>(a) + b, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        };
        pendingDx_ = add(pendingDx_, frame_.dx);
        pendingDy_ = add(pendingDy_, frame_.dy);
    }

    const auto& layer = menu ? cfg_.menu : cfg_.game;
    bool walkHeld = false;
    if (!menu && !suspended && frame_.focused) {
        for (const Binding& b : layer)
            if (b.act == Binding::Walk && sourceLevel(b, frame_, true))
                walkHeld = true;
        if (cfg_.toggleRun && walkHeld && !walkHeld_)
            walkLatched_ = !walkLatched_;
        walkActive_ = cfg_.toggleRun ? walkLatched_ : walkHeld;
    } else {
        walkLatched_ = walkActive_ = false;
    }
    walkHeld_ = walkHeld;
    if (menu != layerIsMenu_) {
        tracker_.reset(&pending_);
        layerIsMenu_ = menu;
    }
    if (suspended || !frame_.focused)
        tracker_.reset(&pending_);
    else
        tracker_.update(layer, frame_, pending_);
    inputWasSuspended_ = suspended;
    padReady_ = false;
}

bool InputModule::fillPadSample(int port, int /*slot*/, uint8_t out[32]) {
    std::lock_guard<std::mutex> lock(m_);
    if (port != 0)
        return false;
    // Ownership can change between pad reads. Replace the cache too, so closing
    // the overlay cannot replay the pre-overlay attack in this same frame.
    if (hostOwned()) {
        PadSample neutral;
        neutral.buttons = tapNow_; // explicit host taps retain their documented suspension exemption
        packPadSample(neutral, padSample_);
        padReady_ = true;
    }
    if (!padReady_) {
        const auto& layer = layerIsMenu_ ? cfg_.menu : cfg_.game;
        PadSample s; // neutral while suspended
        const bool direct = cfg_.lookMode == LookMode::Direct && !(dragon_ && cfg_.dragonStick);
        FrameInput f = frame_;
        const bool inventory = layerIsMenu_ && mm_.activeScreenId() == kInventoryScreenId;
        if (inventory &&
            cfg_.inventoryMouse) { // right click is handled by MenuMouse there (SELECT closes the inventory)
            f.mouseDown &= static_cast<uint8_t>(~MBTN_RIGHT);
            f.mousePressed &= static_cast<uint8_t>(~MBTN_RIGHT);
            f.mouseReleased &= static_cast<uint8_t>(~MBTN_RIGHT);
        }
        if (!hostOwned())
            s = synthesizePad(cfg_, f, layer, layerIsMenu_, direct, walkActive_, ps_);
        // The inventory / map closes with SELECT; START opens the pause menu over it. The menu layer's "back" keys
        // (Esc, Backspace = START | CIRCLE) must only close it, so a bound START becomes SELECT there, and stays
        // SELECT until released: the screen is gone a frame before the layer switch drops the held key, and a START
        // edge in that gap would still open the pause menu. A real controller's START keeps its native meaning.
        const uint16_t padStart = (cfg_.gamepadEnabled && f.gamepad.connected) ? (f.gamepad.buttons & Pad::START) : 0;
        const bool boundStart = (s.buttons & Pad::START & ~padStart) != 0;
        inventoryBack_ = boundStart && (inventory || inventoryBack_);
        if (inventoryBack_)
            s.buttons = static_cast<uint16_t>((s.buttons & ~Pad::START) | Pad::SELECT | padStart);
        s.buttons = static_cast<uint16_t>(s.buttons | tapNow_);
        packPadSample(s, padSample_);
        padReady_ = true;
    }
    std::memcpy(out, padSample_, 32);
    return true;
}

bool InputModule::nativeLookControlActive() const {
    if (cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.rx != 0.0f || frame_.gamepad.ry != 0.0f))
        return true;
    for (const auto& b : cfg_.game)
        if (b.act == Binding::RStick && sourceLevel(b, frame_, true))
            return true;
    return false;
}

bool InputModule::freeCombatAllowed(IGuest& g, uint32_t player, uint32_t cam) {
    if (!freeCombatActive() || !player || !cam || !frame_.focused || !g.read32(0x4fb730) || g.read32(0x4fbafc) ||
        g.read32(cam + 0x14c) != 6 || (g.read32(cam + 0x48) & (0x800u | 0x1000u | 0x80000u)) ||
        (g.read32(player + 0x94) & 0xc003u) || (g.read32(player + 0x90) & 0x4000u))
        return false;
    const uint32_t vt = g.read32(player + 0x57c);
    return vt && g.read32(vt + 0x17c) == 0x32cda8u; // controlled Rynn, never a dragon/NPC override
}

bool InputModule::pitchAllowed(IGuest& g, uint32_t player, uint32_t cam) {
    // 44e4a8 skips pitch for camera bit12, suppresses axis1 for player94 bit2,
    // and routes alternate pitch to +94 when 423790 returns true.
    // 423790 is exactly: camera bit12 ? 0 : ((cameraFlags >> 11) & 1).
    if ((g.read32(cam + 0x48) & 0x1800u) || (g.read32(player + 0x94) & 4u))
        return false;
    const uint32_t vt = g.read32(player + 0x57c);
    const uint32_t fn = vt ? g.read32(vt + 0x17c) : 0;
    // This runs under m_. Only audited native leaf predicates may be called:
    // Rynn 32cda8 reads +94/+ab0/+af0 and returns; dragon 276e40 returns zero.
    // Neither has a call/jump to another function or can enter an input hook.
    // Unknown overrides fail closed; do not extend the old yaw-call pattern to
    // arbitrary vtable targets while adding this new per-camera call.
    if (fn != 0x32cda8u && fn != 0x276e40u)
        return false;
    GuestCall veto(fn);
    veto.arg(player + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x178))));
    return g.call(veto) == 0;
}

bool InputModule::readAimPitchState(IGuest& g, uint32_t player, AimPitchState& s) {
    if (!player)
        return false;
    s = AimPitchState{};
    s.player = player;
    s.ctx = g.read32(cfg_.addrCtxPtr);
    if (!s.ctx)
        return false;
    s.handle = g.read32(s.ctx + 0x4b4);
    if (!s.handle)
        return false;
    s.inner = g.read32(s.handle + 0xfc);
    if (!s.inner)
        return false;
    s.cam = g.read32(s.inner + 0x10);
    if (!s.cam)
        return false;
    s.heading = g.read32(player + 0x80);
    // 331d60's response update ORs motion bits6/7 while writing +f0;
    // 32cc00 clears that pair and +f0. Controller C uses bit6 for vertical
    // motion bookkeeping. Neither 44e4a8 nor 32cda8 uses this pair as a pitch
    // permission/ownership gate. Ignore them in this key only, never in RAM.
    s.flags90 = g.read32(player + 0x90) & ~0xc0u;
    // Controller C runs camera look before its bit8 state handling. 331ec0
    // clears bit8 together with movement/animation state; neither 44e4a8 nor
    // the native 32cda8 pitch veto treats it as a pitch ownership transition.
    // Keep every other flag in the key; do not generalize to unknown states.
    s.flags94 = g.read32(player + 0x94) & ~0x100u;
    s.cameraFlags = g.read32(s.cam + 0x48);
    s.mode = g.read32(s.cam + 0x14c);
    s.target = g.read16(player + 0x4da);
    // 460670 recomputes +114 from this target's relative position every tick,
    // optionally smoothing it and clamping via vslot9c. It is not an identity
    // or mode field. Preserve mouse pitch across finite anchor updates only
    // while a real, unchanged target remains eligible below.
    s.vtable = g.read32(player + 0x57c);
    if (!s.vtable)
        return false;
    s.pitchFn = g.read32(s.vtable + 0x17c);
    s.pitchThis = player + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(s.vtable + 0x178)));
    if (!s.heading || !s.target || !(s.flags90 & 0x4000u) || s.mode != 6 ||
        (s.cameraFlags & (0x1800u | 0x80000u | 0x20u)) || !g.read32(0x4fb730) || g.read32(0x4fbafc) ||
        !std::isfinite(g.readF(player + 0x114)) || !pitchAllowed(g, player, s.cam))
        return false;
    if (s.target) {
        // Same inclusive native reference bound as Controller C. Observe target
        // identity as well as slot, so slot reuse cannot restore an old pitch.
        const uint32_t base = g.read32(s.ctx + 0x7c), count = g.read32(s.ctx + 0x84);
        const uint32_t offset = (s.target - 1) * 8;
        if (count > 10000 || s.target > count + 1 || base < 0x100000u || base > 0x02000000u - 8 - offset)
            return false;
        s.targetHandle = g.read32(base + offset);
        if (!s.targetHandle)
            return false;
    }
    return true;
}

void InputModule::beforeCameraUpdateLook(IGuest& g, bool dragon, uint32_t player) {
    std::lock_guard<std::mutex> lock(m_);
    modeFlagSaved_ = false;
    aimPitchSaved_ = false;
    if (cfg_.lookMode != LookMode::Direct || layerIsMenu_ || menuActive_.load() || !frame_.captured || hostOwned() ||
        nativeLookControlActive()) {
        aimPitchOwned_ = false;
        return;
    }
    // A physical right stick (pad) keeps the game's own behaviour (position mode: the pitch follows the stick and
    // re-centres on release).
    if (dragon && cfg_.dragonStick)
        return; // flight: the game's own stick path steers
    // Direct mouse displacement must not inherit a controller's decaying rates.
    // Clear them BEFORE native integration, including idle mouse frames. A live
    // right stick/RSTICK binding returned above and retains its native momentum.
    const bool padSteering =
        cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f);
    if (player && !padSteering) {
        const uint32_t ctx = g.read32(cfg_.addrCtxPtr);
        const uint32_t handle = ctx ? g.read32(ctx + 0x4b4) : 0;
        const uint32_t inner = handle ? g.read32(handle + 0xfc) : 0;
        const uint32_t cam = inner ? g.read32(inner + 0x10) : 0;
        if (cam && !(g.read32(cam + 0x48) & 0x1000u)) {
            g.writeF(cam + 0x9c, 0);
            g.writeF(cam + 0xa0, 0);
            g.writeF(cam + 0xa4, 0);
        }
    }
    if (!cfg_.pitchPersist) {
        aimPitchOwned_ = false;
        return;
    }
    AimPitchState state;
    if (!dragon && readAimPitchState(g, player, state)) {
        if (!(aimPitchState_ == state))
            aimPitchOwned_ = false;
        // Do not freeze a newly acquired target's native pitch before the user
        // has actually moved the mouse vertically in that state.
        if (aimPitchOwned_ || pendingDy_ != 0) {
            savedAimPitch_ = g.readF(state.cam + 0x84);
            savedAimPitchState_ = state;
            aimPitchSaved_ = std::isfinite(savedAimPitch_);
        }
    } else
        aimPitchOwned_ = false;
    const uint32_t v = g.read32(cfg_.addrStickModeFlag);
    if (v == 0)
        return;
    modeFlagValue_ = v;
    modeFlagSaved_ = true;
    g.write32(cfg_.addrStickModeFlag, 0); // Camera_UpdateLook: rate mode, no Camera_SoftLimitAxis re-centring
}

float InputModule::pitchViewGain(IGuest& g, uint32_t player, uint32_t cam) const {
    // 41f848, plain on-foot branch: the view is rotated by -(0.9 / -0.5) * cam+0x84, while the heading (mouse x)
    // reaches the view 1:1. Other branches (player94 bit2, target swing 168 != 170, cam+0x1c4, camera bits 5 / 25,
    // the rider) use other gains: leave those uncompensated.
    const float k = cfg_.pitchViewGain;
    if (!(k >= 0.25f && k <= 4.0f) || !plainPitchBranch(g, player, cam))
        return 1.0f;
    return k;
}

bool InputModule::plainPitchBranch(IGuest& g, uint32_t player, uint32_t cam) const {
    return !dragon_ && !(g.read32(player + 0x94) & 4u) && !g.read32(cam + 0x1c4) &&
           !(g.read32(cam + 0x48) & 0x2000020u) && g.readF(cam + 0x168) == g.readF(cam + 0x170);
}

namespace {
// Rynn's render transform (ctx+0x4b0 handle): position (+0x14) and heading in the ground plane (row 0, +0x20/+0x28),
// the pivot and orientation the camera target is built from.
bool cameraPlayerPose(IGuest& g, uint32_t ctx, float pos[3], float& heading, uint32_t& player) {
    const uint32_t handle = g.read32(ctx + 0x4b0);
    const uint32_t inner = handle ? g.read32(handle + 0xfc) : 0;
    player = inner ? g.read32(inner + 0x10) : 0;
    const uint32_t xform = player ? g.read32(player + 0x80) : 0;
    if (!xform)
        return false;
    for (int i = 0; i < 3; ++i)
        pos[i] = g.readF(xform + 0x14 + 4 * static_cast<uint32_t>(i));
    const float rx = g.readF(xform + 0x20), rz = g.readF(xform + 0x28);
    heading = std::atan2(rz, rx);
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]) && std::isfinite(heading) &&
           rx * rx + rz * rz > 1.0e-6f;
}

// Camera ticks without mouse / keyboard ownership after which cam+0x210 goes back to the game.
constexpr int kFollowReleaseTicks = 10;

// 41f848 plain on-foot branch: view pitch = -(0.9 / -0.5) * cam+0x84.
constexpr float kPlainViewGain = 1.8f;

// v rotated by `angle` about the unit axis a (Rodrigues).
void rotateAbout(float v[3], const float a[3], float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    const float d = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
    const float x[3] = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};
    for (int i = 0; i < 3; ++i)
        v[i] = v[i] * c + x[i] * s + a[i] * d * (1.0f - c);
}
} // namespace

void InputModule::beforeCameraFollow(IGuest& g, uint32_t cam) {
    std::lock_guard<std::mutex> lock(m_);
    const float rate = cfg_.cameraFollow;
    const uint32_t ctx = g.read32(cfg_.addrCtxPtr);
    const uint32_t handle = ctx ? g.read32(ctx + 0x4b4) : 0;
    const uint32_t inner = handle ? g.read32(handle + 0xfc) : 0;
    const uint32_t obj = cam ? g.read32(cam + 0x148) : 0;
    float pos[3], heading = 0;
    uint32_t player = 0;
    // Only the player-controlled camera of the context, outside cut scenes / control locks. Flags 0x80 / 0x20000
    // already snap, 0x100 skips the orientation, bit5 is another view; a running 0x21c timer blends the orientation
    // with cam+0x210 too, so leave those alone.
    if (!(rate > 0.0f && rate < 1.0e6f) || !cam || !mouseKeyboardOwner_ || !frame_.captured || layerIsMenu_ ||
        menuActive_.load() || hostOwned() || dragon_ || nativeLookControlActive() || !inner ||
        g.read32(inner + 0x10) != cam || g.read32(cam + 0x14c) != 6 ||
        (g.read32(cam + 0x48) & (0x20u | 0x80u | 0x100u | 0x20000u)) || g.readF(cam + 0x21c) > 0.0f ||
        !g.read32(0x4fb730) || g.read32(0x4fbafc) || !obj || !cameraPlayerPose(g, ctx, pos, heading, player) ||
        !std::isfinite(g.readF(cam + 0x210))) {
        // Hold the follow rate and the orbit state through short gaps (single ticks fail some of these checks now
        // and then; each such tick ran at the native rate and the camera hitched). Hand cam+0x210 back to the
        // game, and forget the heading, only once ownership has been gone for a while.
        if (++followBailTicks_ >= kFollowReleaseTicks || !(rate > 0.0f)) {
            // Only while it is still the context's camera: a level change frees the old object.
            if (followOwned_ && inner && g.read32(inner + 0x10) == followCam_)
                g.writeF(followCam_ + 0x210, followNative_);
            followOwned_ = false;
            orbitValid_ = false;
        }
        return;
    }
    // Carry the camera's previous position round Rynn by the turn and the look up / down since the last tick, the
    // way the native target moves: the native easing then only covers her travel, so neither a turn nor a pitch
    // change swings the camera into place late. The pivot is her anchor point (player +0x51c.., ~240 units up,
    // measured: the camera's angle round it follows the view pitch exactly).
    const float pitch = g.readF(cam + 0x84);
    if (orbitValid_ && orbitCam_ == cam && std::isfinite(pitch)) {
        float anchorY = g.readF(player + 0x520);
        if (!(anchorY > -2000.0f && anchorY < 2000.0f))
            anchorY = 0;
        const float pivot[3] = {pos[0], pos[1] + anchorY, pos[2]};
        float off[3], right[3], back[3];
        for (int i = 0; i < 3; ++i) {
            off[i] = g.readF(obj + 0x14 + 4 * static_cast<uint32_t>(i)) - pivot[i];
            right[i] = g.readF(obj + 0x20 + 12 * static_cast<uint32_t>(i)); // camera axes: columns of +0x20
            back[i] = g.readF(obj + 0x28 + 12 * static_cast<uint32_t>(i));
        }
        // Heading row 0 turns the opposite way to bearings atan2(dz, dx) (measured: heading -68.8 degrees moves the
        // settled camera bearing by +68.8 degrees); a positive turn about +y lowers atan2(dz, dx), so the heading
        // change is the angle about +y.
        const float up[3] = {0, 1, 0};
        const float yaw = heading - orbitHeading_;
        rotateAbout(off, up, yaw);
        rotateAbout(right, up, yaw);
        rotateAbout(back, up, yaw);
        // Looking up (pitch +) tilts the camera's back axis down and lowers it behind her; the view turns by the
        // branch's gain (1.8 on foot) times the pitch change. Pick the rotation sense from the back axis.
        const float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        const float dp = pitch - orbitPitch_;
        if (dp != 0.0f && rl > 0.5f && rl < 2.0f) {
            for (float& r : right)
                r /= rl;
            float angle = (plainPitchBranch(g, player, cam) ? kPlainViewGain : 1.0f) * dp;
            float probe[3] = {back[0], back[1], back[2]};
            rotateAbout(probe, right, angle);
            if ((probe[1] - back[1]) * dp > 0.0f)
                angle = -angle;
            rotateAbout(off, right, angle);
        }
        bool finite = true;
        for (float v : off)
            finite = finite && std::isfinite(v);
        if (finite)
            for (int i = 0; i < 3; ++i)
                g.writeF(obj + 0x14 + 4 * static_cast<uint32_t>(i), pivot[i] + off[i]);
    }
    orbitValid_ = true;
    orbitCam_ = cam;
    orbitHeading_ = heading;
    orbitPitch_ = std::isfinite(pitch) ? pitch : 0.0f;
    followBailTicks_ = 0;
    if (!followOwned_ || followCam_ != cam) {
        followNative_ = g.readF(cam + 0x210);
        followCam_ = cam;
        followOwned_ = true;
    }
    // The tick eases cam+0x210 towards the strength (and resets it after a collision push); rewrite it every tick.
    g.writeF(cam + 0x210, rate);
}

bool InputModule::afterCameraUpdateLook(IGuest& g, uint32_t player, uint32_t limitsAddr, bool dragon) {
    std::lock_guard<std::mutex> lock(m_);
    const bool saved = aimPitchSaved_;
    aimPitchSaved_ = false; // consumed even on a rejected/unloaded callback
    if (modeFlagSaved_) {
        g.write32(cfg_.addrStickModeFlag, modeFlagValue_);
        modeFlagSaved_ = false;
    }
    if (cfg_.lookMode != LookMode::Direct || layerIsMenu_ || menuActive_.load() || !frame_.captured || hostOwned() ||
        (dragon && cfg_.dragonStick)) {
        pendingDx_ = pendingDy_ = 0;
        aimPitchOwned_ = false;
        return false;
    }
    int dx = pendingDx_, dy = pendingDy_;
    if (saved) {
        pendingDy_ = 0;         // unload/early-return also discards the old aim sample
        aimPitchOwned_ = false; // re-acquire only after the entire pair validates
    }
    if (!player)
        return false;
    if (dx == 0 && dy == 0 && !saved)
        return false;

    uint32_t ctx = g.read32(cfg_.addrCtxPtr);
    if (!ctx)
        return false;
    uint32_t camH = g.read32(ctx + 0x4b4);
    if (!camH)
        return false;
    uint32_t inner = g.read32(camH + 0xfc);
    if (!inner)
        return false;
    uint32_t cam = g.read32(inner + 0x10);
    if (!cam)
        return false;
    pendingDx_ = pendingDy_ = 0; // consumed: Camera_UpdateLook can run more than once per frame
    const bool freeCombat = !dragon && freeCombatAllowed(g, player, cam);

    // FUN_00423790 selects a separate camera aiming channel. Native look adds
    // pitch/yaw to +94/+98 there and deliberately does not rotate the player.
    // Previously pitchAllowed rejected this mode and the yaw vcall rejected
    // the player turn, so BOTH mouse axes stopped working on entering it.
    const uint32_t cameraFlags = g.read32(cam + 0x48);
    if (!freeCombat && !(cameraFlags & 0x1000u) && (cameraFlags & 0x800u)) {
        aimPitchOwned_ = false;
        if (saved)
            return false; // native changed camera channel during this callback pair
        bool applied = false;
        if (dy && !(g.read32(player + 0x94) & 4u)) {
            g.writeF(cam + 0x94, g.readF(cam + 0x94) + cfg_.pitchSign * static_cast<float>(dy) * cfg_.sensY *
                                                           (cfg_.invertY ? -1.0f : 1.0f));
            applied = true;
        }
        if (dx) {
            g.writeF(cam + 0x98, g.readF(cam + 0x98) + cfg_.yawSign * static_cast<float>(dx) * cfg_.sensX *
                                                           (cfg_.invertX ? -1.0f : 1.0f));
            applied = true;
        }
        return applied;
    }

    bool preserve = false;
    if (saved) {
        AimPitchState current;
        preserve = !dragon && cfg_.pitchPersist && !nativeLookControlActive() &&
                   readAimPitchState(g, player, current) && current == savedAimPitchState_;
        if (!preserve) {
            aimPitchOwned_ = false;
            dy = 0; // motion sampled in the old aim state must not apply to its replacement
        }
    }

    // ---- pitch: raw angle, no smoothing, no acceleration (the re-centring is off while the mouse looks, see
    // beforeCameraUpdateLook)
    float lim = limitsAddr ? g.readF(limitsAddr) : 0.0f;
    if (!(lim > 0.01f && lim < 3.0f))
        lim = dragon ? 1.0472f : 0.6283f;
    float hi = dragon ? lim * 0.55f : lim * 0.7f;
    float lo = dragon ? -lim * 0.45f : -lim;
    bool pitchApplied = false;
    if ((dy != 0 || preserve) && (freeCombat || preserve || pitchAllowed(g, player, cam))) {
        float dyEff =
            static_cast<float>(dy) * cfg_.sensY * (cfg_.invertY ? -1.0f : 1.0f) / pitchViewGain(g, player, cam);
        float p = std::clamp((preserve ? savedAimPitch_ : g.readF(cam + 0x84)) + cfg_.pitchSign * dyEff, lo, hi);
        g.writeF(cam + 0x84, p);
        pitchApplied = true;
        if (preserve) {
            aimPitchOwned_ = true;
            aimPitchState_ = savedAimPitchState_;
        }
    }
    if (dx == 0)
        return pitchApplied;

    // ---- yaw: rotate the player heading exactly like the stick path does
    if (dx != 0) {
        bool allowed = true;
        uint32_t heading = g.read32(player + 0x80);
        if (!heading)
            allowed = false;
        if (allowed && cfg_.checkTurnGate && !freeCombat) {
            uint32_t vt = g.read32(player + 0x57c);
            if (!vt) {
                allowed = false;
            } else {
                GuestCall c1(g.read32(vt + 0x174));
                c1.arg(player + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x170)))).arg(cam);
                if (g.call(c1) == 0)
                    allowed = false;
                if (allowed) {
                    GuestCall c2(g.read32(vt + 0x17c));
                    c2.arg(player + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x178))));
                    if (g.call(c2) != 0)
                        allowed = false;
                }
            }
        }
        if (allowed) {
            float angle = cfg_.yawSign * static_cast<float>(dx) * cfg_.sensX * (cfg_.invertX ? -1.0f : 1.0f);
            GuestCall t(cfg_.addrTurnFn);
            if (cfg_.turnCallAbi == CallAbi::FprFloat) {
                t.argf(angle);
            } else {
                uint32_t bits;
                std::memcpy(&bits, &angle, 4);
                t.arg(bits);
            }
            t.arg(heading + 0x14).arg(cfg_.addrTurnUpVec);
            g.call(t);
        }
    }
    return true;
}

void InputModule::beforePadMgrUpdate(IGuest& g, bool dragon) {
    std::lock_guard<std::mutex> lock(m_);
    if (dragon_ != dragon)
        pendingDx_ = pendingDy_ = 0;
    dragon_ = dragon;
    if (dragon)
        freeCombatFrame_.store(false);
    // record i (0 = lx) at addrStickMap + 12 * i: {s32 axisId, float out, float scale} (docs/reference/input.md 2
    // / 3.2)
    const uint32_t rec = cfg_.addrStickMap;
    // A real left stick overrides keyboard synthesis, so retain its native map.
    const bool padSteering =
        cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f);
    const bool want = cfg_.strafeKeys && !padSteering && frame_.focused && !layerIsMenu_ && !dragon && !hostOwned();
    const int32_t axis = static_cast<int32_t>(g.read32(rec));
    if (want) {
        if (axis != 3) { // (re)installed by the options code: remember the game's own mapping, then strafe
            strafeOrigAxis_ = axis;
            strafeOrigScale_ = g.read32(rec + 8);
            strafeActive_ = true;
            g.write32(rec, 3);
            g.writeF(rec + 8, 1.0f);
        }
    } else if (strafeActive_) {
        if (axis == 3) {
            g.write32(rec, static_cast<uint32_t>(strafeOrigAxis_));
            g.write32(rec + 8, strafeOrigScale_);
        }
        strafeActive_ = false;
    }
}

void InputModule::afterPadMgrUpdate(IGuest& g) {
    std::lock_guard<std::mutex> lock(m_);
    // If ownership changed after beginFrame, discard presses and release held
    // commands here on the EE thread. Releases must reach the guest even while
    // the settings overlay owns input.
    const bool blocked = hostOwned() || !frame_.focused;
    // The native walk command selects the walking pace. Reducing the analog
    // stick alone only slows the running profile (Game_OnInputCommand 0x4010).
    const bool padSteering =
        cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f);
    const bool walk = walkActive_ && !blocked && !layerIsMenu_ && !dragon_ && !padSteering;
    if (walk != walkCommandSent_) {
        const uint32_t ms = g.call(GuestCall(cfg_.addrTimerMs));
        GuestCall c(cfg_.addrInvokeCallback);
        c.arg(cfg_.addrPadMgr).arg(0x4010).arg(0).arg(walk ? 1u : 0u).arg(ms);
        g.call(c);
        walkCommandSent_ = walk;
    }
    if (blocked) {
        pending_.erase(
            std::remove_if(pending_.begin(), pending_.end(), [](const CommandEvent& e) { return e.pressed; }),
            pending_.end());
        tracker_.reset(&pending_);
    }
    // ---- direct command events
    if (!pending_.empty()) {
        uint32_t ms = 0;
        bool haveMs = false;
        for (const CommandEvent& e : pending_) {
            uint32_t cmd, actionId;
            if (e.isAction) {
                actionId = e.id;
                cmd = g.read16(cfg_.addrPadMgr + 8 + 2 * (e.id & 0x1ff)); // InputDev keymap: u16[512] at device+8
                if (cmd == 0)
                    continue;
            } else {
                cmd = e.id;
                actionId = 0;
            }
            if (!haveMs) {
                ms = g.call(GuestCall(cfg_.addrTimerMs));
                haveMs = true;
            }
            GuestCall c(cfg_.addrInvokeCallback);
            c.arg(cfg_.addrPadMgr).arg(cmd).arg(actionId).arg(e.pressed ? 1u : 0u).arg(ms);
            g.call(c);
        }
        pending_.clear();
    }

    // Native Circle toggles the bow sight (camera bit 5). Mouse aiming is held:
    // a second native press closes only a sight acquired by held RMB. Keep its
    // release on a separate tick so the controller can consume the close press.
    auto sightCommand = [&](bool down) {
        const uint32_t ms = g.call(GuestCall(cfg_.addrTimerMs));
        g.call(
            GuestCall(cfg_.addrInvokeCallback).arg(cfg_.addrPadMgr).arg(0x4007).arg(0x125).arg(down ? 1u : 0u).arg(ms));
    };
    if (mouseBowClosePulse_) {
        sightCommand(false);
        mouseBowClosePulse_ = false;
    }
    const uint32_t sightCtx = g.read32(cfg_.addrCtxPtr);
    const uint32_t sightHandle = sightCtx ? g.read32(sightCtx + 0x4b4) : 0;
    const uint32_t sightInner = sightHandle ? g.read32(sightHandle + 0xfc) : 0;
    const uint32_t sightCam = sightInner ? g.read32(sightInner + 0x10) : 0;
    const bool sightOpen = sightCam && (g.read32(sightCam + 0x48) & 0x20u);
    if (!sightOpen || sightCam != mouseBowCamera_ || !mouseKeyboardOwner_)
        mouseBowCamera_ = 0;
    if (!blocked && !layerIsMenu_ && frame_.captured && !dragon_ && mouseKeyboardOwner_) {
        const bool bound = std::any_of(cfg_.game.begin(), cfg_.game.end(), [](const Binding& b) {
            return b.src == Binding::Mouse && b.code == MBTN_RIGHT && b.act == Binding::PadBtn &&
                   b.value == Pad::CIRCLE;
        });
        const bool held = bound && ((frame_.mouseDown | frame_.mousePressed) & MBTN_RIGHT);
        if (held && sightOpen)
            mouseBowCamera_ = sightCam;
        else if (!held && mouseBowCamera_) {
            sightCommand(true);
            mouseBowClosePulse_ = true;
            mouseBowCamera_ = 0;
        }
    }

    if (blocked) {
        mm_.reset();
        return;
    }

    // ---- mouse on the game's own menus (hover / click / wheel)
    if (!layerIsMenu_) {
        mm_.reset();
    } else if (cfg_.menuMouse) {
        MenuMouseAddrs a;
        a.activeScreenVar = cfg_.addrActiveScreenVar;
        a.screenObjVar = cfg_.addrScreenPtr;
        a.ctxVar = cfg_.addrCtxPtr;
        a.mouseCallback = cfg_.addrMouseCallback;
        a.uiEntry = cfg_.addrUiEntry;
        a.capturedWidgetVar = cfg_.addrCapturedWidgetVar;
        mm_.setAddrs(a);
        MenuMouseOptions mo;
        mo.inventoryMouse = cfg_.inventoryMouse;
        mo.inventoryClickSquare = cfg_.inventoryClickSquare;
        mm_.setOptions(mo);
        MenuMouseSample ms;
        ms.focused = frame_.focused;
        ms.clientW = static_cast<float>(frame_.clientW);
        ms.clientH = static_cast<float>(frame_.clientH);
        ms.cx = static_cast<float>(frame_.cursorX);
        ms.cy = static_cast<float>(frame_.cursorY);
        ms.inside = frame_.clientW > 0 && frame_.clientH > 0 && frame_.cursorX >= 0 && frame_.cursorY >= 0 &&
                    frame_.cursorX < frame_.clientW && frame_.cursorY < frame_.clientH;
        ms.down = frame_.mouseDown;
        ms.pressed = frame_.mousePressed;
        ms.released = frame_.mouseReleased;
        ms.wheel = frame_.wheel;
        MenuView v;
        {
            std::lock_guard<std::mutex> lk(viewM_);
            v = view_;
        }
        const MenuMouseResult r = mm_.update(g, ms, v, cfg_.menuCursorIdleMs);
        if (r.padTap)
            injectPadTap(r.padTap, 1);
    }
    // ---- legacy menu cursor / clicks (UNVERIFIED, behind config flags)
    if (!layerIsMenu_)
        return;
    if (cfg_.menuCursor && frame_.clientW > 0 && frame_.clientH > 0) {
        uint32_t scr = g.read32(cfg_.addrScreenPtr);
        if (scr) {
            int fbW = static_cast<int>(g.read32(scr + 0xf8)), fbH = static_cast<int>(g.read32(scr + 0xfc));
            if (fbW > 0 && fbH > 0) {
                int x = std::clamp(frame_.cursorX, 0, frame_.clientW - 1) * fbW / frame_.clientW;
                int y = std::clamp(frame_.cursorY, 0, frame_.clientH - 1) * fbH / frame_.clientH;
                uint32_t dev = cfg_.addrPadMgr;
                uint32_t vt = g.read32(dev + 0x434);
                if (vt) {
                    GuestCall c(g.read32(vt + 0x10c)); // InputDev_SetCursorPos via vslot +0x10c
                    c.arg(dev + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x108))))
                        .arg(static_cast<uint32_t>(x))
                        .arg(static_cast<uint32_t>(y));
                    if (c.addr)
                        g.call(c);
                }
            }
        }
    }
    if (cfg_.menuClicks) {
        uint32_t gui = g.read32(cfg_.addrGuiPtr);
        uint32_t vt = gui ? g.read32(gui + cfg_.guiVtableOffset) : 0;
        if (vt) {
            auto send = [&](uint32_t slot, uint32_t id) {
                GuestCall c(g.read32(vt + slot));
                c.arg(gui + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + slot - 4)))).arg(id);
                if (c.addr)
                    g.call(c);
            };
            const struct {
                uint8_t bit;
                uint32_t id;
            } btn[2] = {{MBTN_LEFT, 0x100}, {MBTN_RIGHT, 0x101}};
            uint32_t bits = g.read32(cfg_.addrPadMgr + 0x444);
            for (int i = 0; i < 2; ++i) {
                if (frame_.mousePressed & btn[i].bit)
                    send(0x4c, btn[i].id);
                if (frame_.mouseReleased & btn[i].bit)
                    send(0x54, btn[i].id | 0x10000);
                if (frame_.mouseDown & btn[i].bit)
                    bits |= (1u << i);
                else
                    bits &= ~(1u << i);
            }
            g.write32(cfg_.addrPadMgr + 0x444, bits);
        }
    }
}

// ---- global wrappers
InputModule& module() {
    static InputModule m;
    return m;
}
bool init(void* hwnd, const char* path) {
    return module().init(hwnd, path ? path : "drakan_input.ini");
}
void shutdown() {
    module().shutdown();
}
bool onWindowMessage(void* h, unsigned m, uintptr_t w, intptr_t l, intptr_t* r) {
    return module().onWindowMessage(h, m, w, l, r);
}
void beginFrame() {
    module().beginFrame();
}
bool fillPadSample(int port, int slot, uint8_t out[32]) {
    return module().fillPadSample(port, slot, out);
}
bool afterCameraUpdateLook(IGuest& g, uint32_t p, uint32_t l, bool d) {
    return module().afterCameraUpdateLook(g, p, l, d);
}
void beforeCameraUpdateLook(IGuest& g, bool d, uint32_t p) {
    module().beforeCameraUpdateLook(g, d, p);
}
void beforeCameraFollow(IGuest& g, uint32_t cam) {
    module().beforeCameraFollow(g, cam);
}
void beforePadMgrUpdate(IGuest& g, bool d) {
    module().beforePadMgrUpdate(g, d);
}
void afterPadMgrUpdate(IGuest& g) {
    module().afterPadMgrUpdate(g);
}
void setMenuActive(bool a) {
    module().setMenuActive(a);
}
bool reloadConfig(const char* path) {
    return path && module().reloadConfig(path);
}

} // namespace drakan_input
