// Disc region profile of this build (docs/reference/regions.md).
//
// A drakan.exe runs exactly one recompiled executable, so the region is fixed when the runtime is compiled:
// CMake's DRAKAN_GAME_REGION=usa defines DRAKAN_GAME_REGION_USA for the runtime sources; PAL defines nothing.
//
// All guest addresses in the runtime are written as PAL SCES_500.06 addresses. guestAddress() converts one to the
// build's executable: identity for PAL (a constant expression, so PAL code is unchanged), and for another region a
// lookup in that region's table of hand-verified equivalents. An address without a verified equivalent yields 0 and the
// feature that needs it must stay off. Struct offsets are not translated: the USA build is five months older and some
// object layouts differ (regions.md "Structure layouts"), so a feature is enabled for a region only after its offsets
// were checked too. Until then a non-PAL build installs only the boot baseline (drakan_overrides.cpp, kPcFeatures).
#pragma once

#include <cstdint>

namespace drakan_region {

enum class Region : uint8_t { Pal, Usa };

struct Profile {
    Region region;
    const char* id;      // "pal" / "usa" (scripts/buildtool/regions.json)
    const char* bootElf; // ISO9660 name of the boot executable
    const char* serial;  // "SCES-50006"
    const char* saveId;  // memory-card directory prefix the game writes ("BESCES-50006" + "Slot1")
    const char* label;   // game-override name (logs)
    bool ntsc;           // VMODE in SYSTEM.CNF
};

#if defined(DRAKAN_GAME_REGION_USA)
inline constexpr Region kBuildRegion = Region::Usa;
#else
inline constexpr Region kBuildRegion = Region::Pal;
#endif
inline constexpr bool kIsPal = kBuildRegion == Region::Pal;

inline constexpr Profile kProfiles[] = {
    {Region::Pal, "pal", "SCES_500.06", "SCES-50006", "BESCES-50006", "Drakan: The Ancients' Gates (SCES-500.06)",
     false},
    {Region::Usa, "usa", "SCUS_971.28", "SCUS-97128", "BASCUS-97128", "Drakan: The Ancients' Gates (SCUS-971.28)",
     true},
};

constexpr const Profile& profile(Region region) {
    return kProfiles[static_cast<unsigned>(region)];
}
constexpr const Profile& build() {
    return profile(kBuildRegion);
}

// PAL guest address -> the build executable's address, 0 when this region has no verified equivalent.
uint32_t translateGuestAddress(uint32_t palAddress);
inline uint32_t guestAddress(uint32_t palAddress) {
    if constexpr (kIsPal)
        return palAddress;
    else
        return translateGuestAddress(palAddress);
}

} // namespace drakan_region
