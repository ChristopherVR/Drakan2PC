// The weapons the player can hold, by resource record id (PlayerState.weaponRight / weaponLeft), harvested from the
// live weapon records of 2875 local RAM dumps with analysis/scripts/weapon_table.py (item properties, see
// docs/reference/world-model.md and docs/journal/2026-10-24/tab-rework-data-model.md): melee class 0x1000e property
// 0x1a "Damage" (the inventory line), 0x1e the stance (0 one handed, 1 two handed, 2 hammer / axe); bow class 0x1004f.
// Only ids, small numbers and the names the dumps had resident are listed (no game data beyond that). Used by the
// server's hit validation (MatchServer.h) and the kill feed.
#pragma once
#include <cstddef>
#include <cstdint>

namespace drakan::net {

struct MeleeWeapon {
    uint32_t record;
    uint16_t damage; // item property 0x1a: "Damage: n" in the inventory
    uint8_t stance;  // item property 0x1e
    const char* name;
};
struct BowWeapon {
    uint32_t record;
    const char* name;
};

inline constexpr MeleeWeapon kMeleeWeapons[] = {
    {0x023cbc, 4, 0, ""},
    {0x030c54, 10, 0, ""},
    {0x031c63, 30, 0, ""},
    {0x031eb8, 14, 2, ""},
    {0x031ebd, 6, 0, "Rusty Short Sword"},
    {0x031ed2, 6, 0, "Iron Short Sword"},
    {0x031ee4, 11, 0, "Mithril Short Sword"},
    {0x031f30, 19, 1, ""},
    {0x031fe5, 10, 0, ""},
    {0x031ff8, 13, 0, ""},
    {0x03200b, 18, 0, ""},
    {0x03208e, 9, 0, "Rusty Axe"},
    {0x0320d1, 18, 2, ""},
    {0x03214f, 20, 0, ""},
    {0x032170, 12, 2, ""},
    {0x032179, 12, 2, ""},
    {0x03217b, 8, 0, "Wood Club"},
    {0x032183, 21, 2, ""},
    {0x03223f, 18, 2, ""},
    {0x03224b, 16, 2, ""},
    {0x03224e, 16, 2, ""},
    {0x032256, 28, 2, ""},
    {0x03227d, 10, 0, ""},
    {0x032284, 20, 0, ""},
    {0x0333bf, 1, 0, ""},
    {0x0334ce, 22, 0, ""},
    {0x033728, 8, 0, ""},
    {0x03372a, 8, 0, "Iron Club"},
    {0x03372b, 11, 0, ""},
    {0x03372c, 14, 0, ""},
    {0x03372d, 7, 0, "Rusty Scimitar"},
    {0x03372e, 7, 0, "Iron Scimitar"},
    {0x03372f, 9, 0, "Steel Scimitar"},
    {0x033732, 9, 0, "Iron Axe"},
    {0x033734, 16, 0, ""},
    {0x033735, 18, 0, ""},
    {0x03374d, 8, 0, "Steel Short Sword"},
    {0x033cbc, 4, 0, "Rynn's Dagger"},
    {0x033e0f, 25, 1, ""},
    {0x033f7a, 4, 0, ""},
};
inline constexpr BowWeapon kBows[] = {
    {0x031bc9, "Short Wood Bow"},
    {0x032264, ""},
    {0x032265, ""},
    {0x032266, "Wood Long Bow"},
    {0x032269, ""},
    {0x03226c, ""},
    {0x03226e, ""},
    {0x03226f, ""},
    {0x032272, ""},
    {0x0331e3, ""},
    {0x0331e5, ""},
    {0x03375f, ""},
    {0x033760, ""},
    {0x033761, ""},
    {0x033762, ""},
    {0x033765, "Short War Bow"},
    {0x033766, ""},
    {0x033767, ""},
    {0x033768, ""},
    {0x03376a, ""},
};

// Arrow damage: "Wooden Arrows" show Damage 5 in the inventory of every bow. Other arrow types and the spells cast from
// scrolls are capped, not tabulated (their records are not weapons the dumps could name): an accepted projectile hit
// is at most kMaxProjectileDamage before the server's damage scale.
inline constexpr uint16_t kArrowDamage = 5;
inline constexpr uint16_t kMaxProjectileDamage = 40;
// The blade multiplier is 1.0 .. 2.0 observed; the server accepts four times the item damage (the dagger: 16).
inline constexpr uint16_t kMeleeLimitFactor = 4;

inline const MeleeWeapon* findMelee(uint32_t record) {
    for (const MeleeWeapon& w : kMeleeWeapons)
        if (w.record == record)
            return &w;
    return nullptr;
}
inline const BowWeapon* findBow(uint32_t record) {
    for (const BowWeapon& w : kBows)
        if (w.record == record)
            return &w;
    return nullptr;
}

// Kill feed text for a hit weapon: `hit` is net::Hit.weapon (0 right hand, 1 left hand, 2 projectile), `record` the
// resource id the killer held in that hand (the bow for a projectile, which sits in the left holder).
inline const char* weaponDisplayName(uint8_t hit, uint32_t record) {
    if (hit >= 2) {
        const BowWeapon* b = findBow(record);
        return b && b->name[0] ? b->name : "Bow";
    }
    if (record == 0)
        return "bare hands";
    if (const MeleeWeapon* m = findMelee(record)) {
        if (m->name[0])
            return m->name;
        return m->stance == 1 ? "two-handed sword" : m->stance == 2 ? "hammer" : "sword";
    }
    return "melee weapon";
}

} // namespace drakan::net
