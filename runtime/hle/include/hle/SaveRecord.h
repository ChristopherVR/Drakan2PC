// Saves and pack builds (docs/reference/modding.md "Saves and pack builds"). A save restores objects by class id and
// field layout, so a save written with one build of a mod pack can stall or corrupt the game when it is loaded with
// another build (class ids, logic defaults or level data changed). The game therefore writes, next to every save
// made while content packs are active, a small record of those packs and their builds (`<save>.rsg.mods`), and checks
// it before a load: a save whose packs are missing or have another build is refused with a message.
//
// Std-only public header: no platform APIs, no file access (the host reads and writes the record files).
#pragma once
#include "Interfaces.h"
#include <string>
#include <vector>

namespace hle::mods {

// One content file of a pack as the build hash sees it: path inside the pack (relative to the pack root), size and
// CRC-32 (the zip one).
struct ContentItem {
    std::string path;
    u64 size = 0;
    u32 crc = 0;
};
// True for a path under files/, new/ or overrides/ (case-insensitive, '\' or '/'): the bytes the game loads.
bool isContentPath(const std::string& path);
// The build hash of a pack: FNV-1a 64 over `<lower-case path>\0<size>\0<crc as 8 hex digits>\n` of every content
// item, sorted by lower-case path. Same algorithm as the editor's `packContentHash` (editor/packages/formats
// packbuild.ts), so a pack without a `build` key gets the id the editor would have written.
u64 packContentHash(std::vector<ContentItem> items);
// CRC-32 (IEEE, the zip one).
u32 crc32Of(const u8* data, size_t size);

// A content pack as a save records it.
struct PackStamp {
    std::string id, name, version, build;
    bool operator==(const PackStamp&) const = default;
};

// The record text (`<save>.rsg.mods`, UTF-8, LF): a comment, `format = 1`, then one `[pack <id>]` section per pack
// with `name`, `version`, `build`.
std::string formatSaveRecord(const std::vector<PackStamp>& packs);
// Parses a record. False for an unreadable one (no `format`, a newer format, a pack without id or build); the caller
// then treats the save as unloadable, never as "no record".
bool parseSaveRecord(const std::string& text, std::vector<PackStamp>& out);

enum class SaveVerdict {
    Ok,
    DifferentBuild, // a recorded pack is active with another build
    MissingPack,    // a recorded pack is not active (not installed, disabled or skipped)
    OlderThanPack,  // no record, the save's level comes from a pack that declares a build: written by an earlier game
                    // version with an unknown pack build
    BadRecord,      // the record cannot be read
};
const char* saveVerdictName(SaveVerdict v);

struct SaveCheckInput {
    bool hasRecord = false;
    bool recordReadable = true;
    std::vector<PackStamp> record; // the save's packs (hasRecord)
    std::string level;             // the save's level title (the .rsg file name without extension)
};
// What the running game provides.
struct ActivePacks {
    std::vector<PackStamp> packs; // loaded content packs in load order
    struct LevelOwner {
        std::string level, packId;
        bool buildDeclared = false; // the pack's manifest names a `build`
    };
    std::vector<LevelOwner> levels; // levels registered by loaded packs
};
struct SaveCheck {
    SaveVerdict verdict = SaveVerdict::Ok;
    PackStamp pack; // the pack the verdict is about (recorded or active stamp); empty for Ok / BadRecord
    bool ok() const { return verdict == SaveVerdict::Ok; }
};
// The load policy. A save with a record loads when every recorded pack is active with the same build (packs added
// since do not matter). A save without a record (stock saves, saves made without content packs, saves of earlier
// game versions) loads unless its level is registered by an active pack whose manifest declares a build.
SaveCheck checkSave(const SaveCheckInput& save, const ActivePacks& active);

// The record to write for a save made now: the active content packs (empty = write none, remove an old one).
std::vector<PackStamp> recordFor(const ActivePacks& active);

} // namespace hle::mods
