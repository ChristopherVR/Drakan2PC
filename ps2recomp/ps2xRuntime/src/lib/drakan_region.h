// Build-time disc profile generated from regions.json and configs/regions/*.json.
// Addresses use PAL as their canonical key; object offsets belong to typed layouts.
// Only audited features are enabled. Missing translations return zero, never PAL.
#pragma once

#include "input/CameraProfile.h"
#include <array>
#include <cstdint>

namespace drakan_region {

enum class Feature : uint8_t {
    ControlsMenu,
    FrameRate,
    DualWield,
    SoundDebug,
    AdvancedInput,
    Widescreen,
    InventoryTypography,
    SceneHooks,
    UiPrompts,
    OptionsSettings,
    Menus,
    Subtitles,
    StereoSync,
    BasicInput,
    MoviePacing,
    MovieGuard,
    InputFlow,
    DirectMouseLook,
    MovementInput
};

constexpr uint32_t featureBit(Feature feature) {
    return 1u << static_cast<unsigned>(feature);
}

struct MovieLayout {
    uint32_t fpsIndex;
    uint32_t frameCount;
    uint32_t screenEntries;
    uint32_t screenCount;
    uint32_t fpsNumerator;
    uint32_t fpsDenominator;
};

struct AudioLayout {
    uint32_t optionStereo;
    uint32_t managerStereo;
};

struct InputFlowLayout {
    uint32_t screenId;
    uint32_t titleScreen;
    uint32_t pauseScreen;
    uint32_t menuScreens;
    uint32_t cursorScreens;
    uint32_t contextCamera;
    uint32_t handleInner;
    uint32_t innerData;
    uint32_t cameraMode;
    uint32_t gameplayModes;
};

struct SettingsLayout {
    uint32_t widgetId;
    uint32_t adjustScreenWidget;
    uint32_t adjustScreenText;
};

struct Profile {
    const char* id;
    const char* bootElf;
    const char* serial;
    const char* saveId;
    const char* label;
    bool ntsc;
    bool identityAddresses;
    uint32_t features;
    MovieLayout movie;
    AudioLayout audio;
    InputFlowLayout inputFlow;
    SettingsLayout settings;
    uint32_t languageCount;
    drakan_input::CameraLayout camera;
};

struct AddressPair {
    uint32_t pal;
    uint32_t region;
};

} // namespace drakan_region

// Generated per target by drakan_configure_disc_profile; no implicit PAL fallback.
#include "drakan_disc_profile.generated.h"

namespace drakan_region {

constexpr const Profile& build() {
    return kProfile;
}
constexpr bool supports(Feature feature) {
    return (build().features & featureBit(feature)) != 0;
}

uint32_t translateGuestAddress(uint32_t palAddress);
inline uint32_t guestAddress(uint32_t palAddress) {
    if constexpr (build().identityAddresses)
        return palAddress;
    else
        return translateGuestAddress(palAddress);
}

inline drakan_input::CameraProfile inputCameraProfile() {
    return {build().camera,
            guestAddress(0x5565d4),
            guestAddress(0x4fbafc),
            guestAddress(0x13ee90),
            guestAddress(0x4e8c48),
            guestAddress(0x4e9594),
            guestAddress(0x4e9558),
            guestAddress(0x32cda8),
            guestAddress(0x276e40),
            supports(Feature::AdvancedInput),
            supports(Feature::MovementInput),
            guestAddress(0x4e90c0),
            guestAddress(0x131fa8),
            guestAddress(0x4af7f8)};
}

} // namespace drakan_region
