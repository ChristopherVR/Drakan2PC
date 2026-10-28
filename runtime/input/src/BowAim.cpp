// Modern bow aiming (`[mouse] bow_aim = modern`): with the bow out on foot, holding the left button aims (a slight
// zoom through the camera's own FOV ramp and a crosshair drawn by the host) and releasing it sends one Square tap; the
// arrow of that shot is pointed at the crosshair just before the native launch. Static analysis and measurements:
// docs/reference/input.md section 16.
#include "input/DrakanInput.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    const bool camOk = cam && g.read32(cam + 0x14c) == 6 && !(g.read32(cam + 0x48) & kCameraSightFlag);
    const bool ready = cfg_.bowAimModern && camOk && player && !dragon_ && mouseKeyboardOwner_ &&
                       g.read32(player + 0xe90) == kBowMode && g.read32(0x4fb730) && !g.read32(0x4fbafc);
    bowReady_.store(ready);
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
    if (!ownerObj || g.read16(ownerObj + 0x4da)) // a locked target keeps the game's own ballistic aim
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

void updateBowAim(IGuest& g) {
    module().updateBowAim(g);
}
void beforeBowLaunch(IGuest& g, uint32_t bow) {
    module().beforeBowLaunch(g, bow);
}

} // namespace drakan_input
