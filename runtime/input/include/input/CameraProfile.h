// Camera ABI supplied by the selected disc profile. Defaults preserve the standalone PAL input API.
#pragma once
#include <cstdint>

namespace drakan_input {
struct CameraLayout {
    uint32_t contextCamera = 0x4b4;
    uint32_t contextActor = 0x4b0;
    uint32_t contextResources = 0x320;
    uint32_t contextTargets = 0x7c;
    uint32_t contextTargetCount = 0x84;
    uint32_t handleInner = 0xfc;
    uint32_t innerData = 0x10;
    uint32_t handleResource = 0x10;
    uint32_t resourceCount = 0x10;
    uint32_t resourceTable = 0x8;
    uint32_t resourceStride = 0x24;
    uint32_t resourceClass = 0x8;
    uint32_t dragonClass = 0x1001c;
    uint32_t cameraFlags = 0x48;
    uint32_t cameraPitch = 0x84;
    uint32_t cameraAlternatePitch = 0x94;
    uint32_t cameraAlternateYaw = 0x98;
    uint32_t cameraPitchRate = 0x9c;
    uint32_t cameraYawRate = 0xa0;
    uint32_t cameraRollRate = 0xa4;
    uint32_t cameraTransform = 0x148;
    uint32_t cameraMode = 0x14c;
    uint32_t cameraSwing = 0x168;
    uint32_t cameraSwingTarget = 0x170;
    uint32_t cameraAlternateView = 0x1c4;
    uint32_t cameraFollowRate = 0x210;
    uint32_t cameraBlendTimer = 0x21c;
    uint32_t playerTransform = 0x80;
    uint32_t playerFlags = 0x90;
    uint32_t playerFlags2 = 0x94;
    uint32_t playerTarget = 0x4da;
    uint32_t playerAimAnchor = 0x114;
    uint32_t playerAnchorY = 0x520;
    uint32_t playerVtable = 0x57c;
    uint32_t vtableTurn = 0x174;
    uint32_t vtableTurnThis = 0x170;
    uint32_t vtablePitch = 0x17c;
    uint32_t vtablePitchThis = 0x178;
    uint32_t transformPosition = 0x14;
    uint32_t transformRight = 0x20;
    uint32_t transformBack = 0x28;
    uint32_t cameraOrbit = 0x90;
};
struct CameraProfile {
    CameraLayout layout;
    uint32_t context = 0x5565d4;
    uint32_t scripted = 0x4fbafc;
    uint32_t turn = 0x13ee90;
    uint32_t turnUp = 0x4e8c48;
    uint32_t stickMode = 0x4e9594;
    uint32_t stickMap = 0x4e9558;
    uint32_t rynnPitch = 0x32cda8;
    uint32_t dragonPitch = 0x276e40;
    bool advancedInput = true;
    bool movementInput = true;
    uint32_t padManager = 0x4e90c0;
    uint32_t invokeCallback = 0x131fa8;
    uint32_t timerMs = 0x4af7f8;
};
} // namespace drakan_input
