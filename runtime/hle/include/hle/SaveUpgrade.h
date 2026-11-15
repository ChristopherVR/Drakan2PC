// Save revisions of a content pack (docs/reference/modding.md "Saves and pack builds"). A pack keeps its `build` while
// its save layout only grows (SaveRecord.h), so a save made before a rebuild still loads. Two kinds of rebuild still
// leave such a save with old state, because a save names every object it restores:
//
//  - objects the rebuild added to a level: a save lists the level's untouched objects by id (0x24 table) and leaves out
//    the ones the player destroyed, picked up or killed, so an object no list names is never created. Creating every
//    unnamed object would bring those back too;
//  - objects a level object spawned at run time under the old pack: the save restores them as records without an id
//    (a tree Mover's "Special Effect" flame that the converter later cleared);
//  - a dragon that used up its linked Dragon Power-up while the old pack gave the power-up no breath: its record keeps
//    the "power-ups taken" mark and an empty link list, so it never takes the breath the power-up has now;
//  - a zone-culling Visibility Toggler the old pack left hidden: its record keeps that state, and the toggler hides its
//    scenery again on load although the pack now shows it from the start.
//
// The pack therefore counts its `save_revision` and lists, per revision, what changed. Every save record names the
// revision it was written with (`revision = N` in its pack section, absent = 0). A save or level state of an older
// revision is read with the objects added since appended to its tables, the listed spawned records turned into an inert
// class of the same engine class (the record keeps its slot, so no positional reference moves), the listed dragons'
// power-up links restored and the listed togglers' saved state cleared. Std-only: no file access, no guest memory.
#pragma once
#include "Interfaces.h"
#include <string>
#include <vector>

namespace hle::mods {

struct SaveRevisionRule {
    enum class Kind {
        AddObjects,     // manifest `save_added_objects = ["<rev> | <level title> | <id>[@<required id>] ..."]`
        ReplaceEffects, // manifest `save_replaced_effects = ["<rev> | <level title> | <class hex> <class hex> | <x>
                        // <z>"]`
        RelinkPowerUps, // manifest `save_relinked_powerups = ["<rev> | <level title> | <dragon id> <power-up id> ..."]`
        ResetTogglers,  // manifest `save_reset_togglers = ["<rev> | <level title> | <toggler id> ..."]`
    };
    Kind kind = Kind::AddObjects;
    int revision = 0;     // the pack revision that made the change: applies to saves of an older revision
    std::string level;    // level title ("OotF 01 - Ruined Village")
    std::vector<u32> ids; // AddObjects, ResetTogglers: level object ids; RelinkPowerUps: the dragon, then its power-ups
    // AddObjects: per id, a level object the save must still name for the id to be added (0 = none; `3353@2855`: the
    // blow-up statue prop only while its generator has not burst).
    std::vector<u32> needs;
    // ReplaceEffects: saved records without a level id whose class id (low 16 bits of the record's class word) is
    // `fromClass` and whose position is within kEffectRadius (x / z) of (x, z) get class `toClass` (same database).
    u16 fromClass = 0, toClass = 0;
    float x = 0, z = 0;
};
constexpr float kEffectRadius = 64.0f;

// One manifest line of the given kind. False (and `out` untouched) for a malformed line.
bool parseSaveRevisionRule(const std::string& line, SaveRevisionRule::Kind kind, SaveRevisionRule& out);
std::string formatSaveRevisionRule(const SaveRevisionRule& rule);

// A rule's level against the level a save file names (its stem). The game cuts long titles in save names
// (`OotF 02 - Ruined Village .rsg` for "OotF 02 - Ruined Village Book"), so a stem of 24 or more characters also names
// a title that starts with it (trimmed); shorter stems must equal the title (case-insensitive, trimmed).
bool ruleLevelMatches(const std::string& ruleLevel, const std::string& saveStem);

// The rules of `kind` for a save of `saveStem` written at `saveRevision`.
std::vector<const SaveRevisionRule*> rulesFor(const std::vector<SaveRevisionRule>& rules, SaveRevisionRule::Kind kind,
                                              const std::string& saveStem, int saveRevision);

// The dragon record (the Arokh class, engine 0x1c): at kDragonPowerUpMark a word that is 1 once the dragon has taken
// his linked power-ups, then the counted list of their slots. A new game's save before the awakening has (0, 1, slot);
// the 15 November saves have (1, 0) (docs/journal/2026-11-16/save-state-fix.md).
constexpr size_t kDragonPowerUpMark = 0x64;
// The Visibility Toggler record (engine 121) holds, after its link list, a word (state << 16) | 121: state 0 in a save
// whose toggler shows its links, 2 in one whose toggler hid them last (the 15 November Slot33 camp toggler).
constexpr u32 kTogglerEngine = 121;

struct SaveUpgradeReport {
    std::vector<u32> added; // level object ids appended
    size_t replaced = 0;    // spawned records given their inert class
    size_t relinked = 0;    // dragons whose power-up links were restored
    size_t togglers = 0;    // togglers whose saved state was cleared
    bool changed() const { return !added.empty() || replaced || relinked || togglers; }
};

// Upgrades a save or level state (SRSC container) by the given rules (any kind, already chosen for the save):
//  * AddObjects: level object ids appended to the untouched-object table (0x24) at its end, where no existing slot
//  moves:
//    the table's count and the 0x21 total grow by the number added, and each new (id, slot = old total + i) pair goes
//    into the 0x21 pair list in id order. Ids the save already names (in a pair) are left out, and so are ids whose
//    required object the save does not name;
//  * ReplaceEffects: applied to the saved records (0x102) that have no pair;
//  * RelinkPowerUps: a saved dragon record with the "taken" mark and an empty list gets (0, n, power-up slots) back,
//  for
//    the power-ups the save names (in a pair, or added above); any other layout is left alone;
//  * ResetTogglers: a saved toggler record with exactly one (state << 16) | kTogglerEngine word of a state 1..255 after
//    the common header gets state 0.
// The container keeps every other entry; bytes after the index (an old, longer save the game overwrote in place) are
// dropped. False with `err` when the save does not have the expected tables (`out` is then empty). Nothing to change:
// `out` == `in`. Journal: docs/journal/2026-11-16/save-state-fix.md.
bool upgradeSave(const std::vector<u8>& in, const std::vector<const SaveRevisionRule*>& rules, std::vector<u8>& out,
                 SaveUpgradeReport& report, std::string& err);

} // namespace hle::mods
