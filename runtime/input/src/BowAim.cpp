// Default OotF bow: press fires, alternate attack toggles the native sight; camera-centred native target acquisition
// uses the original PC aiming model. Optional modern aiming
// (`[mouse] bow_aim = modern`): with the bow out on foot, holding the left button aims (a slight
// zoom through the camera's own FOV ramp and a crosshair drawn by the host) and releasing it sends one Square tap; the
// arrow of that shot is pointed at the crosshair just before the native launch. Static analysis and measurements:
// docs/reference/input.md section 16.
#include "input/DrakanInput.h"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace drakan_input {

namespace {
constexpr int kBowShotFrames = 120;          // a launch this long after the release still belongs to the aimed shot
constexpr float kBowAimDistance = 8000.0f;   // the arrow converges on the camera's centre ray this far ahead
constexpr uint32_t kBowMode = 5;             // player+0xe90 while the bow is the active weapon
constexpr uint32_t kCameraSightFlag = 0x20u; // cam+0x48 bit5: the native bow sight (FUN_0032a608)
// Over the shoulder while aiming: the camera target moves this far along the camera's right axis (world-space target
// offset cam+0x10c / +0x114, read by FUN_0041f848; the native easing slides the camera there). Without it the screen
// centre, and the crosshair, sit on Rynn's back.
constexpr float kBowShoulder = 200.0f;

// Object table lookup the engine uses for 16-bit handles: *(ctx+0x7c) + (handle - 1) * 8, valid up to ctx+0x84 + 1.
uint32_t objectFromHandle(IGuest& g, uint32_t ctx, uint16_t handle) {
    if (!handle || !ctx)
        return 0;
    const uint32_t count = g.read32(ctx + 0x84), base = g.read32(ctx + 0x7c);
    if (count > 100000 || handle > count + 1 || base < 0x100000u)
        return 0;
    return g.read32(base + (handle - 1u) * 8u);
}

uint32_t contextCamera(IGuest& g, uint32_t ctx) {
    const uint32_t handle = ctx ? g.read32(ctx + 0x4b4) : 0;
    const uint32_t inner = handle ? g.read32(handle + 0xfc) : 0;
    return inner ? g.read32(inner + 0x10) : 0;
}

uint32_t contextPlayer(IGuest& g, uint32_t ctx) {
    const uint32_t handle = ctx ? g.read32(ctx + 0x4b0) : 0;
    const uint32_t inner = handle ? g.read32(handle + 0xfc) : 0;
    return inner ? g.read32(inner + 0x10) : 0;
}

bool finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

} // namespace

bool InputModule::bowLmbIsSquare() const {
    return std::any_of(cfg_.game.begin(), cfg_.game.end(), [](const Binding& b) {
        return b.src == Binding::Mouse && b.code == MBTN_LEFT && b.act == Binding::PadBtn && b.value == Pad::SQUARE;
    });
}

void InputModule::bowAimFrame() {
    if (bowShotFrames_ > 0)
        --bowShotFrames_;
    const bool ready = cfg_.bowAimModern && bowReady_.load() && frame_.captured && !layerIsMenu_ &&
                       !menuActive_.load() && !hostOwned() && bowLmbIsSquare();
    if (!ready) {
        bowAiming_.store(false); // a sight, weapon change or menu cancels the aim without a shot
        return;
    }
    const uint8_t left = MBTN_LEFT;
    const bool down = (frame_.mouseDown | frame_.mousePressed) & left;
    const bool released = (frame_.mouseReleased & left) != 0;
    if (!bowAiming_.load() && (frame_.mousePressed & left))
        bowAiming_.store(true);
    if (bowAiming_.load() && (released || !down)) {
        bowAiming_.store(false);
        injectPadTap(Pad::SQUARE, 2); // the native draw-and-shoot animation; the arrow leaves on its event
        bowShotFrames_ = kBowShotFrames;
    }
    // The button belongs to the aim while the bow is out: it never reaches the pad as Square.
    frame_.mouseDown &= static_cast<uint8_t>(~left);
    frame_.mousePressed &= static_cast<uint8_t>(~left);
    frame_.mouseReleased &= static_cast<uint8_t>(~left);
}

void InputModule::updateBowAim(IGuest& g) {
    std::lock_guard<std::mutex> lock(m_);
    const uint32_t ctx = g.read32(cfg_.addrCtxPtr);
    const uint32_t cam = contextCamera(g, ctx);
    const uint32_t player = contextPlayer(g, ctx);
    if (!player || !g.read32(0x4fb730)) {
        bowSightJumpPlayer_ = 0;
        bowSightLocomotionPlayer_ = bowSightLocomotionMotion_ = 0;
    }
    const bool camOk = cam && g.read32(cam + 0x14c) == 6 && !(g.read32(cam + 0x48) & kCameraSightFlag);
    const bool ready = cfg_.bowAimModern && camOk && player && !dragon_ && mouseKeyboardOwner_ &&
                       g.read32(player + 0xe90) == kBowMode && g.read32(0x4fb730) && !g.read32(0x4fbafc);
    bowReady_.store(ready);
    const bool sightOpen = bowSightCrosshair(g); // the OotF "targeted first-person view" shows the crosshair
    if (sightOpen != bowSightOpen_.exchange(sightOpen) && std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] sight crosshair %s\n", sightOpen ? "shown" : "hidden");
    // Zoom: the camera tick eases its FOV (cam+0x5c) towards cam+0x60; borrow that target while aiming.
    const bool want = ready && bowAiming_.load() && cfg_.bowZoom > 0.2f && cfg_.bowZoom < 1.0f;
    if (bowZoomActive_) {
        const bool sameCam = cam == bowZoomCam_;
        if (!want || !sameCam || g.readF(cam + 0x60) != bowZoomTarget_) {
            // Put the game's value back unless the game itself has set a new one meanwhile (the sight, a cut scene).
            if (sameCam && g.readF(cam + 0x60) == bowZoomTarget_)
                g.writeF(cam + 0x60, bowZoomNative_);
            bowZoomActive_ = false;
        }
    }
    // Shoulder offset, recomputed every frame from the camera's right axis (column 0 of *(cam+0x148)+0x20).
    const uint32_t camObj = cam ? g.read32(cam + 0x148) : 0;
    const bool shoulder = ready && bowAiming_.load() && camObj;
    if (bowShoulderActive_ && (!shoulder || cam != bowShoulderCam_)) {
        if (bowShoulderCam_ == cam) {
            g.writeF(cam + 0x10c, bowShoulderNative_[0]);
            g.writeF(cam + 0x114, bowShoulderNative_[1]);
        }
        bowShoulderActive_ = false;
    }
    if (shoulder) {
        const float rx = g.readF(camObj + 0x20), rz = g.readF(camObj + 0x38);
        const float l = std::sqrt(rx * rx + rz * rz);
        if (std::isfinite(l) && l > 0.1f) {
            if (!bowShoulderActive_) {
                bowShoulderNative_[0] = g.readF(cam + 0x10c);
                bowShoulderNative_[1] = g.readF(cam + 0x114);
                bowShoulderCam_ = cam;
                bowShoulderActive_ = true;
            }
            g.writeF(cam + 0x10c, bowShoulderNative_[0] + rx / l * kBowShoulder);
            g.writeF(cam + 0x114, bowShoulderNative_[1] + rz / l * kBowShoulder);
        }
    }
    if (want && !bowZoomActive_) {
        const float fov = g.readF(cam + 0x60);
        if (std::isfinite(fov) && fov > 10.0f && fov < 170.0f) {
            bowZoomNative_ = fov;
            bowZoomTarget_ = fov * cfg_.bowZoom;
            bowZoomCam_ = cam;
            bowZoomActive_ = true;
            g.writeF(cam + 0x60, bowZoomTarget_);
        }
    }
}

void InputModule::beforeBowLaunch(IGuest& g, uint32_t bow) {
    std::lock_guard<std::mutex> lock(m_);
    if (!cfg_.bowAimModern || bowShotFrames_ <= 0 || !bow)
        return;
    const uint32_t ctx = g.read32(cfg_.addrCtxPtr);
    const uint32_t cam = contextCamera(g, ctx);
    const uint32_t obj = cam ? g.read32(cam + 0x148) : 0;
    const uint32_t arrow = objectFromHandle(g, ctx, g.read16(bow + 0x158));
    const uint32_t owner = objectFromHandle(g, ctx, g.read16(bow + 0x60));
    if (!obj || !arrow || !owner || (g.read32(cam + 0x48) & kCameraSightFlag))
        return;
    const uint32_t ownerObj = contextPlayer(g, ctx);
    // Launch hooks also run for other actors' bows. Their shots must neither use our camera nor consume our pending
    // shot.
    if (!ownerObj || owner != g.read32(ctx + 0x4b0) || g.read16(ownerObj + 0x4da)) // native ballistic aim
        return;
    bowShotFrames_ = 0;
    // Camera centre ray: position *(cam+0x148)+0x14, back axis = column 2 of the matrix at +0x20 (+0x28/+0x34/+0x40).
    float eye[3], fwd[3], from[3];
    for (int i = 0; i < 3; ++i) {
        eye[i] = g.readF(obj + 0x14 + 4 * static_cast<uint32_t>(i));
        fwd[i] = -g.readF(obj + 0x28 + 12 * static_cast<uint32_t>(i));
        from[i] = g.readF(arrow + 0x14 + 4 * static_cast<uint32_t>(i));
    }
    if (!finite3(eye) || !finite3(fwd) || !finite3(from))
        return;
    float d[3];
    for (int i = 0; i < 3; ++i)
        d[i] = eye[i] + fwd[i] * kBowAimDistance - from[i];
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dl > 1.0f))
        return;
    for (float& v : d)
        v /= dl;
    // The native retarget writes column 0 = -dir (FUN_0020e6c8); keep the arrow's other axes as close to their
    // current pose as possible and keep the matrix's handedness.
    float m[9];
    for (int i = 0; i < 9; ++i)
        m[i] = g.readF(arrow + 0x20 + 4 * static_cast<uint32_t>(i));
    const auto col = [&](int j, float out[3]) {
        out[0] = m[j];
        out[1] = m[3 + j];
        out[2] = m[6 + j];
    };
    float c0[3] = {-d[0], -d[1], -d[2]}, c1[3], c2old[3], c0old[3];
    col(1, c1);
    col(2, c2old);
    col(0, c0old);
    const auto cross = [](const float a[3], const float b[3], float out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    };
    float t[3];
    cross(c0old, c1, t);
    const float handed = (t[0] * c2old[0] + t[1] * c2old[1] + t[2] * c2old[2]) < 0.0f ? -1.0f : 1.0f;
    float dot = c1[0] * c0[0] + c1[1] * c0[1] + c1[2] * c0[2];
    for (int i = 0; i < 3; ++i)
        c1[i] -= dot * c0[i];
    float l1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    if (!(l1 > 1.0e-3f)) { // degenerate: any axis perpendicular to the flight direction
        const float up[3] = {0, 1, 0};
        cross(up, c0, c1);
        l1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
        if (!(l1 > 1.0e-3f))
            return;
    }
    for (float& v : c1)
        v /= l1;
    float c2[3];
    cross(c0, c1, c2);
    for (float& v : c2)
        v *= handed;
    for (int i = 0; i < 3; ++i) {
        g.writeF(arrow + 0x20 + 4 * static_cast<uint32_t>(3 * i), c0[i]);
        g.writeF(arrow + 0x24 + 4 * static_cast<uint32_t>(3 * i), c1[i]);
        g.writeF(arrow + 0x28 + 4 * static_cast<uint32_t>(3 * i), c2[i]);
    }
    std::fprintf(stderr, "[bow] aimed shot: arrow 0x%x dir (%.3f %.3f %.3f) from (%.0f %.0f %.0f)\n", arrow, d[0], d[1],
                 d[2], from[0], from[1], from[2]);
}

// PC Bow launch100b0360 bypasses third-person target selection in manual sight and launches the projectile pose.
// The native running/jumping rig tilts that pose away from the sight; rotate its basis only, retaining every
// axis magnitude and the native launch origin. No convergence distance, flight property or velocity is introduced.
bool InputModule::beforeBowSightLaunch(IGuest& g, uint32_t bow) {
    std::lock_guard<std::mutex> lock(m_);
    const auto ctx = g.read32(cfg_.addrCtxPtr), player = contextPlayer(g, ctx);
    const auto cam = ootfBowSightCamera(g, player);
    if (!bow || !cam || g.read16(player + 0x4da) ||
        objectFromHandle(g, ctx, g.read16(bow + 0x60)) != g.read32(ctx + 0x4b0))
        return false;
    const auto arrow = objectFromHandle(g, ctx, g.read16(bow + 0x158));
    const auto registry = g.read32(ctx + 0x320), object = g.read32(cam + 0x148);
    if (!arrow || !registry || !object)
        return false;
    const uint32_t index = g.read16(arrow + 0x10), records = g.read32(registry + 8);
    const auto valid = [](uint32_t p, uint32_t bytes) { return p >= 0x100000u && p <= 0x2000000u - bytes; };
    if (index == 0xffff || index > g.read32(registry + 0x10) || !valid(records, (index + 1u) * 0x24u) ||
        g.read32(records + index * 0x24 + 8) != 0x1005fu)
        return false;
    // Native Arrow class95 uses field16 (0x10) for SeekType. The audited 192e48 reader with default=-1
    // returns *(reader+4)+field*4 directly. Read only this proven flat layout, never modify its shared properties.
    const auto reader = g.read32(arrow + 0x54), readerVt = valid(reader, 12) ? g.read32(reader + 8) : 0;
    if (!valid(readerVt, 0x20) || g.read32(readerVt + 0x1c) != 0x192e48u || g.readS16(readerVt + 0x18) != 0)
        return false;
    const auto fields = g.read32(reader + 4);
    if (!valid(fields, 0x44) || g.read32(fields + 0x40) != 0)
        return false; // intentional tracking/homing arrows retain their native solver
    float old[3][3], length[3], back[3];
    for (uint32_t axis = 0; axis < 3; ++axis) {
        for (uint32_t row = 0; row < 3; ++row)
            old[axis][row] = g.readF(arrow + 0x20 + row * 12 + axis * 4);
        length[axis] =
            std::sqrt(old[axis][0] * old[axis][0] + old[axis][1] * old[axis][1] + old[axis][2] * old[axis][2]);
        if (!finite3(old[axis]) || !std::isfinite(length[axis]) || length[axis] < 1e-4f)
            return false;
        back[axis] = g.readF(object + 0x28 + axis * 12);
    }
    const float norm = std::sqrt(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
    if (!finite3(back) || !std::isfinite(norm) || norm < 1e-4f)
        return false;
    const auto cross = [](const float* a, const float* b, float* v) {
        v[0] = a[1] * b[2] - a[2] * b[1];
        v[1] = a[2] * b[0] - a[0] * b[2];
        v[2] = a[0] * b[1] - a[1] * b[0];
    };
    float axes[3][3], priorCross[3];
    cross(old[0], old[1], priorCross);
    const float handed =
        priorCross[0] * old[2][0] + priorCross[1] * old[2][1] + priorCross[2] * old[2][2] < 0 ? -1.0f : 1.0f;
    float dot = 0;
    for (unsigned i = 0; i < 3; ++i) {
        axes[0][i] = back[i] / norm;
        dot += old[1][i] * axes[0][i];
    }
    float sideLength = 0;
    for (unsigned i = 0; i < 3; ++i) {
        axes[1][i] = old[1][i] - dot * axes[0][i];
        sideLength += axes[1][i] * axes[1][i];
    }
    sideLength = std::sqrt(sideLength);
    if (sideLength < 1e-4f) {
        const float up[3] = {0, 1, 0};
        cross(up, axes[0], axes[1]);
        sideLength = std::sqrt(axes[1][0] * axes[1][0] + axes[1][1] * axes[1][1] + axes[1][2] * axes[1][2]);
        if (sideLength < 1e-4f)
            return false;
    }
    for (float& value : axes[1])
        value /= sideLength;
    cross(axes[0], axes[1], axes[2]);
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned axis = 0; axis < 3; ++axis)
            g.writeF(arrow + 0x20 + row * 12 + axis * 4, axes[axis][row] * length[axis] * (axis == 2 ? handed : 1.0f));
    if (std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] sight shot basis: arrow=%x direction=(%g,%g,%g) magnitudes=(%g,%g,%g)\n", arrow,
                     -back[0] / norm, -back[1] / norm, -back[2] / norm, length[0], length[1], length[2]);
    return true;
}

uint32_t InputModule::bowAttackMask(IGuest& g, uint32_t player, uint32_t caller, uint32_t mask) {
    std::lock_guard<std::mutex> lock(m_);
    // PC attack entry 1006f9d0 excludes jumping (actor+68 bit10) from its refusal mask; PS2 31da90 explicitly
    // adds that bit for bows. The PS2 reset callback 307198 returns to 31de5c with the mask in s2. Preserve
    // the optional held-object refusal bit1000, falling bit20, and every other native gate.
    // Applies in modern mode too: its release tap mid-jump would otherwise be refused and the aimed shot lost.
    if (cfg_.bowAimNative || dragon_ || caller != 0x31de5cu || (mask & ~0x1000u) != 0x104b70u || !player ||
        player != contextPlayer(g, g.read32(cfg_.addrCtxPtr)) || g.read32(player + 0xe90) != kBowMode ||
        !(g.read32(player + 0x94) & 0x10u) || !g.read32(0x4fb730) || g.read32(0x4fbafc))
        return mask;
    return mask & ~0x10u;
}

// PS2 attack start 3016e8 clears the shot channel player+aec to 0 and then plays the archer clip on it (motion setter
// 32cf30, return 301b7c) before repeating it on the torso channel (player property 0x52). Channel 0 is the base
// motion: mid-jump it replaces the jump clip and its velocity-driven travel, so a forward jump turns into a vertical
// one. The original PC jump transition 1005a830 instead keeps the bow pose on the torso channel. Play an airborne
// shot there (the sight branch already stores that channel in +aec), so the legs keep the native jump and the
// native attack completion check in 31da90 follows the torso clip.
uint32_t InputModule::bowShotChannel(IGuest& g, uint32_t player, uint32_t caller, uint32_t channel) {
    std::lock_guard<std::mutex> lock(m_);
    if (cfg_.bowAimNative || caller != 0x301b7cu || channel != 0 || dragon_ || !player ||
        player != contextPlayer(g, g.read32(cfg_.addrCtxPtr)) || g.read32(player + 0xe90) != kBowMode ||
        !(g.read32(player + 0x94) & 0x10u) || !g.read32(0x4fb730) || g.read32(0x4fbafc))
        return channel;
    const auto valid = [](uint32_t p, uint32_t bytes) { return p >= 0x100000u && p <= 0x2000000u - bytes; };
    const auto actor = g.read32(player + 0x80);
    const auto reader = valid(actor, 0x58) ? g.read32(actor + 0x54) : 0;
    const auto vt = valid(reader, 12) ? g.read32(reader + 8) : 0;
    const auto get = valid(vt, 0x20) ? g.read32(vt + 0x1c) : 0;
    if (!get)
        return channel;
    // The same property read the native attack start uses for its torso call: (reader + vt[0x18], 0x52, -1).
    const auto field = g.call(GuestCall(get)
                                  .arg(reader + static_cast<uint32_t>(static_cast<int32_t>(g.readS16(vt + 0x18))))
                                  .arg(0x52)
                                  .arg(0xffffffffu));
    const auto torso = valid(field, 4) ? g.read32(field) : 0;
    if (torso == 0 || torso > 15)
        return channel;
    g.write32(player + 0xaec, torso);
    if (std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] airborne shot on torso channel %u: player=%x\n", torso, player);
    return torso;
}

bool InputModule::bowSightCrosshair(IGuest& g) const {
    const auto ctx = g.read32(cfg_.addrCtxPtr), cam = contextCamera(g, ctx), player = contextPlayer(g, ctx);
    return !cfg_.bowAimNative && cam && player && !dragon_ && g.read32(cam + 0x14c) == 6 &&
           (g.read32(cam + 0x48) & kCameraSightFlag) && g.read32(player + 0xe90) == kBowMode && g.read32(0x4fb730) &&
           !g.read32(0x4fbafc);
}

uint32_t InputModule::ootfBowSightCamera(IGuest& g, uint32_t player) const {
    const bool padSteering = cfg_.gamepadEnabled && frame_.gamepad.connected &&
                             (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f || frame_.gamepad.rx != 0.0f ||
                              frame_.gamepad.ry != 0.0f);
    if (cfg_.bowAimModern || cfg_.bowAimNative || dragon_ || !player || !mouseKeyboardOwner_ || padSteering ||
        cfg_.lookMode != LookMode::Direct || nativeLookControlActive() || !g.read32(0x4fb730) || g.read32(0x4fbafc))
        return 0;
    const auto ctx = g.read32(cfg_.addrCtxPtr), cam = contextCamera(g, ctx);
    if (player != contextPlayer(g, ctx) || g.read32(player + 0xe90) != kBowMode || !cam || g.read32(cam + 0x14c) != 6 ||
        !(g.read32(cam + 0x48) & kCameraSightFlag))
        return 0;
    return cam;
}

void InputModule::captureBowSightJump(IGuest& g, uint32_t player) {
    std::lock_guard<std::mutex> lock(m_);
    // PC 10069500 retains the jump command in its sight path. PS2 Controller C clears +50c at entry. Remember
    // only a genuine grounded jump request until its exact post-clear callback, including across a guest yield.
    bowSightJumpPlayer_ =
        ootfBowSightCamera(g, player) && !g.read32(player + 0x94) && g.read32(player + 0x50c) == 1 ? player : 0;
}

bool InputModule::restoreBowSightJump(IGuest& g, uint32_t player, uint32_t caller) {
    std::lock_guard<std::mutex> lock(m_);
    if (caller != 0x311314u || player != bowSightJumpPlayer_)
        return false;
    bowSightJumpPlayer_ = 0;
    if (!ootfBowSightCamera(g, player) || g.read32(player + 0x94) || g.read32(player + 0x50c))
        return false;
    g.write32(player + 0x50c, 1); // restore the input, never an actor physics/state flag
    return true;
}

bool InputModule::keepBowSightOnJump(IGuest& g, uint32_t player, uint32_t cam, uint32_t caller) {
    std::lock_guard<std::mutex> lock(m_);
    // Native jump start closes the sight here; original PC jump transitions do not. All other closes stay native.
    return caller == 0x2f9610u && cam && cam == ootfBowSightCamera(g, player);
}

bool InputModule::suppressBowSightForwardLook(IGuest& g, uint32_t player, uint32_t caller, uint32_t axis) {
    std::lock_guard<std::mutex> lock(m_);
    // PS2 Camera_UpdateLook reads forward axis2 as pitch in the sight (44e678). Keyboard W/S must remain
    // movement commands; direct mouse displacement already owns pitch. A physical stick retains its native read.
    const bool padSteering =
        cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f);
    return caller == 0x44e680u && axis == 2 && mouseKeyboardOwner_ && !padSteering &&
           cfg_.lookMode == LookMode::Direct && !nativeLookControlActive() && ootfBowSightCamera(g, player);
}

// Direct OotF sight orientation: keep native position/bob, remove animation-driven look tilt.
// Native 327700 and original PC 100761a0 both use -1.5 times the camera pitch. Apply at the FPS
// bow-sight writer32a608 (mode6+sight, return218794), after its animated basis and native positional bob.
bool InputModule::stabilizeBowSightOrientation(IGuest& g, uint32_t camera, uint32_t caller) {
    std::lock_guard<std::mutex> lock(m_);
    if (caller != 0x218794u)
        return false;
    const bool padSteering = cfg_.gamepadEnabled && frame_.gamepad.connected &&
                             (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f || frame_.gamepad.rx != 0.0f ||
                              frame_.gamepad.ry != 0.0f);
    if (!mouseKeyboardOwner_ || padSteering || cfg_.lookMode != LookMode::Direct || nativeLookControlActive()) {
        if (std::getenv("DRAKAN_BOW_LOG")) {
            static unsigned logged = 0;
            if (logged++ < 8)
                std::fprintf(stderr, "[bow] orientation input guard: owner=%d pad=%d direct=%d native=%d\n",
                             mouseKeyboardOwner_, padSteering, cfg_.lookMode == LookMode::Direct,
                             nativeLookControlActive());
        }
        return false;
    }
    const auto player = contextPlayer(g, g.read32(cfg_.addrCtxPtr));
    const auto cam = ootfBowSightCamera(g, player);
    const auto actor = player ? g.read32(player + 0x80) : 0;
    const auto object = cam ? g.read32(cam + 0x148) : 0;
    if (!cam || camera != cam || !actor || !object) {
        if (std::getenv("DRAKAN_BOW_LOG")) {
            static unsigned logged = 0;
            if (logged++ < 8)
                std::fprintf(stderr, "[bow] orientation context guard: player=%x cam=%x actor=%x object=%x\n", player,
                             cam, actor, object);
        }
        return false;
    }
    const float x = g.readF(actor + 0x28), z = g.readF(actor + 0x40), pitch = g.readF(cam + 0x84);
    const float length = std::sqrt(x * x + z * z);
    if (!std::isfinite(length) || length < 1e-4f || !std::isfinite(pitch))
        return false;
    const float bx = x / length, bz = z / length;
    const float c = std::cos(-1.5f * pitch), s = std::sin(-1.5f * pitch);
    const float matrix[9] = {bz, -bx * s, bx * c, 0, c, s, -bx, -bz * s, bz * c};
    if (std::getenv("DRAKAN_BOW_LOG")) {
        static unsigned logged = 0;
        if (logged < 16 && std::fabs(g.readF(object + 0x34) - s) > 0.01f) {
            ++logged;
            std::fprintf(stderr, "[bow] sight orientation: player=%x pitch=%g backY=%g->%g\n", player, pitch,
                         g.readF(object + 0x34), s);
        }
    }
    for (uint32_t i = 0; i < 9; ++i)
        g.writeF(object + 0x20 + 4 * i, matrix[i]);
    return true;
}

void InputModule::releaseBowSightLocomotion(IGuest& g, uint32_t player) {
    if (!bowSightLocomotionPlayer_)
        return;
    if (player != bowSightLocomotionPlayer_) {
        bowSightLocomotionPlayer_ = bowSightLocomotionMotion_ = 0;
        return;
    }
    if (g.read32(player + 0x94) || g.read32(player + 0x50c))
        return; // retain cleanup ownership until the native jump/shot is finished
    const auto actor = g.read32(player + 0x80), model = actor ? g.read32(actor + 0x50) : 0;
    const auto records = model ? g.read32(model + 0xe8) : 0, channels = model ? g.read32(model + 0xdc) : 0;
    const auto modelVt = model ? g.read32(model) : 0;
    if (!records || !channels || !modelVt || g.read32(modelVt + 0xcc) != 0x11a5a8u)
        return;
    const int channel = g.readS16(channels);
    if (channel < 0 || channel > 255)
        return;
    if (g.read32(records + static_cast<uint32_t>(channel) * 0x80 + 0x50) == bowSightLocomotionMotion_) {
        const auto pose =
            g.call(GuestCall(0x325650u).arg(player).arg(g.read32(player + 0xabc)).arg(0).arg(0xffffffffu));
        if (!pose || pose == 0xffffffffu)
            return;
        GuestCall play(0x3253a0u);
        play.argf(0.2f).argf(-1.0f).argf(1.0f).arg(player).arg(pose).arg(0).arg(0);
        g.call(play);
        g.call(GuestCall(0x11a5a8u).arg(model + static_cast<uint32_t>(g.readS16(modelVt + 0xc8))).arg(0));
    }
    bowSightLocomotionPlayer_ = bowSightLocomotionMotion_ = 0;
}

bool InputModule::updateBowSightLocomotion(IGuest& g, uint32_t player, uint32_t caller) {
    std::lock_guard<std::mutex> lock(m_);
    if (caller != 0x311314u || !player || player != contextPlayer(g, g.read32(cfg_.addrCtxPtr)))
        return false;
    const bool padSteering =
        cfg_.gamepadEnabled && frame_.gamepad.connected && (frame_.gamepad.lx != 0.0f || frame_.gamepad.ly != 0.0f);
    if (!ootfBowSightCamera(g, player) || !mouseKeyboardOwner_ || padSteering) {
        releaseBowSightLocomotion(g, player);
        return false;
    }
    // Let native jump, attack, crouch and other state machines finish. The next grounded callback then selects
    // idle/run again, including after a shot; no actor state flags or movement velocities are synthesized.
    if (g.read32(player + 0x94) & ~0x30u)
        return false;
    const auto actor = g.read32(player + 0x80), vt = g.read32(player + 0x57c);
    const auto model = actor ? g.read32(actor + 0x50) : 0;
    const auto records = model ? g.read32(model + 0xe8) : 0;
    const auto channels = model ? g.read32(model + 0xdc) : 0;
    const auto modelVt = model ? g.read32(model) : 0;
    const auto valid = [](uint32_t p) { return p >= 0x100000u && p < 0x1ff0000u; };
    if (!valid(records) || !valid(channels) || !vt || g.read32(vt + 0x144) != 0x32cf30u)
        return false;
    // Audited native model root-motion predicates/setters used by Controller C immediately after PlayMotion.
    if (!modelVt || g.read32(modelVt + 0xb4) != 0x11a488u || g.read32(modelVt + 0xbc) != 0x11a4e8u ||
        g.read32(modelVt + 0xc4) != 0x11a548u || g.read32(modelVt + 0xcc) != 0x11a5a8u)
        return false;
    const int channel = g.readS16(channels);
    if (channel < 0 || channel > 255)
        return false;
    if (g.read32(player + 0x94) || g.read32(player + 0x50c))
        return false;
    const auto current = g.read32(records + static_cast<uint32_t>(channel) * 0x80 + 0x50);
    const auto forward = g.read32(player + 0x504), strafe = g.read32(player + 0x508);
    const bool otherDirection = (forward & 2u) || strafe;
    if (otherDirection && (bowSightLocomotionPlayer_ != player || current != bowSightLocomotionMotion_)) {
        bowSightLocomotionPlayer_ = bowSightLocomotionMotion_ = 0;
        return false; // native lateral/back handling already owns its motion
    }
    const bool moving = (forward & 1u) && !otherDirection;
    // Native cached Stand / Run / Walk, resolved for the actually equipped bow by the same weapon motion mapper
    // used by Controller C. Its sight branch skips these transitions, which otherwise start AND stop root motion.
    const auto source = g.read32(player + (moving ? (walkActive_ ? 0xac4 : 0xac0) : 0xabc));
    if (!source)
        return false;
    const auto motion = g.call(GuestCall(0x325650u).arg(player).arg(source).arg(0).arg(0xffffffffu));
    if (!motion || motion == 0xffffffffu)
        return false;
    bowSightLocomotionPlayer_ = moving ? player : 0;
    bowSightLocomotionMotion_ = moving ? motion : 0;
    bool changed = motion != current;
    if (changed) {
        GuestCall play(0x32cf30u);              // native base-motion transition, without owning a torso layer
        play.argf(0.2f).argf(-1.0f).argf(1.0f); // Controller C's native transition/default playback arguments
        play.arg(player).arg(motion).arg(0).arg(0).arg(0);
        g.call(play);
    } // otherwise retain the current loop's phase
    const auto rootFlags = g.read32(model + 8);
    const auto modelMode = [&](uint32_t slot, uint32_t enabled) {
        g.call(GuestCall(g.read32(modelVt + slot))
                   .arg(model + static_cast<uint32_t>(g.readS16(modelVt + slot - 4)))
                   .arg(enabled));
        changed = true;
    };
    if (moving) {
        if (!(rootFlags & 0x10u))
            modelMode(0xb4, 1);
        if (!(rootFlags & 0x40u))
            modelMode(0xc4, 1);
        if (rootFlags & 0x20u)
            modelMode(0xbc, 0);
    } else if (rootFlags & 0x70u)
        modelMode(0xcc, 0);
    if (changed && std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] sight locomotion: player=%x moving=%d motion=%x->%x\n", player, moving, current,
                     motion);
    return changed;
}

size_t InputModule::setBowAimFiles(const std::vector<std::pair<std::string, std::string>>& files) {
    std::lock_guard<std::mutex> lock(m_);
    bowAimProfiles_.clear();
    const auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r");
        const auto last = value.find_last_not_of(" \t\r");
        return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
    };
    for (const auto& file : files) {
        if (file.second.size() > (64u << 10))
            continue;
        std::istringstream stream(file.second);
        std::string line;
        BowAimProfile pending{};
        bool version = false, bad = false, cone = false, range = false;
        const auto flush = [&] {
            if (!version || bad || !pending.resource || !cone || !range)
                return;
            const auto it = std::find_if(bowAimProfiles_.begin(), bowAimProfiles_.end(),
                                         [&](const auto& p) { return p.resource == pending.resource; });
            if (it != bowAimProfiles_.end())
                *it = pending;
            else if (bowAimProfiles_.size() < 4096)
                bowAimProfiles_.push_back(pending);
        };
        while (std::getline(stream, line)) {
            line = trim(line.substr(0, line.find_first_of(";#")));
            if (line.empty())
                continue;
            if (line.front() == '[') {
                flush();
                pending = {};
                bad = true;
                cone = range = false;
                if (line.rfind("[bow 0x", 0) != 0 || line.back() != ']')
                    continue;
                const auto text = line.substr(7, line.size() - 8);
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), pending.resource, 16);
                bad = parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                      !(pending.resource & 0xffffu) || (pending.resource & 0xffffu) == 0xffffu;
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                bad = true;
                continue;
            }
            const auto key = trim(line.substr(0, eq)), text = trim(line.substr(eq + 1));
            if (!pending.resource && key == "format") {
                version = text == "1";
                if (!version)
                    break;
                continue;
            }
            float value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
                bad = true;
            } else if (key == "cone_degrees" && !cone && value >= 0 && value <= 180) {
                pending.cone = value;
                cone = true;
            } else if (key == "maximum_range" && !range && value >= 0) {
                pending.range = value;
                range = true;
            } else
                bad = true;
        }
        flush();
    }
    if (!bowAimProfiles_.empty())
        std::fprintf(stderr, "[bow] loaded autoaim metadata for %zu converted bows\n", bowAimProfiles_.size());
    return bowAimProfiles_.size();
}

uint16_t InputModule::bowAutoTarget(IGuest& g, uint32_t bow, uint32_t scratch) {
    std::lock_guard<std::mutex> lock(m_);
    if (std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] autoaim entry: bow=%x modern=%d dragon=%d mouseOwner=%d scratch=%x\n", bow,
                     cfg_.bowAimModern, dragon_, mouseKeyboardOwner_, scratch);
    if (cfg_.bowAimModern || cfg_.bowAimNative || dragon_ || !mouseKeyboardOwner_ || !bow || !scratch)
        return 0;
    const uint32_t ctx = g.read32(cfg_.addrCtxPtr), cam = contextCamera(g, ctx), player = contextPlayer(g, ctx);
    if (std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr,
                     "[bow] autoaim state: ctx=%x cam=%x player=%x cameraMode=%u sight=%x weapon=%u target=%u "
                     "gameplay=%u scripted=%u\n",
                     ctx, cam, player, cam ? g.read32(cam + 0x14c) : 0, cam ? g.read32(cam + 0x48) : 0,
                     player ? g.read32(player + 0xe90) : 0, player ? g.read16(player + 0x4da) : 0, g.read32(0x4fb730),
                     g.read32(0x4fbafc));
    if (!ctx || !cam || !player || g.read32(cam + 0x14c) != 6 || (g.read32(cam + 0x48) & kCameraSightFlag) ||
        g.read32(player + 0xe90) != kBowMode || g.read16(player + 0x4da) || !g.read32(0x4fb730) || g.read32(0x4fbafc))
        return 0;
    const uint32_t owner = objectFromHandle(g, ctx, g.read16(bow + 0x60));
    const uint32_t obj = g.read32(cam + 0x148);
    if (!owner || owner != g.read32(ctx + 0x4b0) || !obj)
        return 0;
    float eye[3], direction[3];
    for (uint32_t i = 0; i < 3; ++i) {
        eye[i] = g.readF(obj + 0x14 + 4 * i);
        direction[i] = -g.readF(obj + 0x28 + 12 * i);
    }
    if (!finite3(eye) || !finite3(direction))
        return 0;
    const float length =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (!(length > 0.1f))
        return 0;
    for (uint32_t i = 0; i < 3; ++i) {
        g.writeF(scratch + 4 * i, eye[i]);
        g.writeF(scratch + 16 + 4 * i, direction[i] / length);
    }
    // Original Bow21/23 are cone degrees and maximum range, consumed by its automatic target selector.
    // Imported classes retain these extra properties in a host sidecar; native/unmapped bows keep the baseline.
    float cone = 25.0f, range = 16000.0f;
    const auto actor = g.read32(bow + 0xc), registry = g.read32(ctx + 0x320);
    const auto valid = [](uint32_t p, uint32_t bytes) { return p >= 0x100000u && p <= 0x2000000u - bytes; };
    uint32_t resource = 0;
    if (valid(actor, 0x14) && valid(registry, 0x14)) {
        const uint32_t index = g.read16(actor + 0x10), records = g.read32(registry + 8);
        if (index != 0xffff && index <= g.read32(registry + 0x10) && valid(records, (index + 1u) * 0x24u) &&
            g.read32(records + index * 0x24u + 8) == 0x1004fu) {
            resource = g.read32(records + index * 0x24u);
            const auto it = std::find_if(bowAimProfiles_.begin(), bowAimProfiles_.end(),
                                         [&](const auto& p) { return p.resource == resource; });
            if (it != bowAimProfiles_.end()) {
                cone = it->cone;
                range = it->range;
            }
        }
    }
    if (range == 0)
        return 0;
    const uint32_t target =
        g.call(GuestCall(0x49ec60).arg(owner).arg(scratch).arg(scratch + 16).argf(cone).argf(range));
    if (std::getenv("DRAKAN_BOW_LOG"))
        std::fprintf(stderr, "[bow] autoaim properties: resource=%x cone=%g range=%g\n", resource, cone, range);
    const uint16_t handle = target ? g.read16(target + 0x12) : 0;
    std::fprintf(stderr, "[bow] autoaim query: owner=%x target=%x handle=%u\n", owner, target, handle);
    return handle && objectFromHandle(g, ctx, handle) == target ? handle : 0;
}

void updateBowAim(IGuest& g) {
    module().updateBowAim(g);
}
void beforeBowLaunch(IGuest& g, uint32_t bow) {
    module().beforeBowLaunch(g, bow);
}

} // namespace drakan_input
