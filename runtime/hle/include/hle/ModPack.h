// Mod packs: a folder or .zip with a manifest (mod.toml or mod.json) and overrides/<FILE>/<type>_<id>_<sub>.bin
// entries (plus optional overrides/<FILE>/text.ini). The ModManager scans a mods folder, applies the enabled list and
// load order from mods.ini, validates every pack against the running disc profile and merges the packs into two
// resource overrides (later mods win per entry; conflicts are logged). Format: docs/reference/modding.md.
//
// Beyond entry overrides a pack may also ship whole files (`files/<disc path>`, served by the HLE file service as if on
// the disc), new SRSC entries (`new/<FILE>/<type>_<id>_<sub>.bin`) and register new levels (manifest `levels`, merged
// into LEVELLST.TXT, see ModManager::extraLevels()).
//
// Std-only public header: no platform APIs.
#pragma once
#include "Interfaces.h"
#include "ResourceOverride.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hle {
class IsoFs;
}
namespace hle::mods {

// The disc profile a pack is pinned to. `id` is the build's profile id ("pal", "usa"); `hash` identifies the release
// (profileHash() of serial and save id).
struct DiscProfile {
    std::string id;
    u64 hash = 0;
};
// FNV-1a 64 of "<serial>|<saveId>" (never 0). Same value the netplay handshake uses.
u64 profileHash(const std::string& serial, const std::string& saveId);
std::string hex64(u64 v); // 16 lower-case hex digits

constexpr int kManifestFormat = 1;

struct Manifest {
    int format = 1;
    std::string id, name, version, author, description;
    std::vector<std::string> profiles;                            // target.profile ("pal", "usa", "*")
    std::vector<std::string> profileHashes;                       // target.hash (hex), optional
    std::vector<std::string> loadAfter, loadBefore, dependencies; // dependencies: "id" or "id>=1.2.0"
    std::vector<std::string> levels; // top-level `levels = ["Level 9X - Name"]`: new levels for LEVELLST.TXT
    std::vector<std::string> levelCopies; // top-level `level_copies = ["Copy = Source level"]`: which disc level a copy is
    // top-level `campaign = "ootf"`: the pack's `levels` are a whole game's campaign (Drakan: Order of the Flame); the
    // title menu then offers it. `campaign_name` = menu label (default by id), `campaign_start` = the level New Game
    // opens (default the first of `levels`).
    std::string campaign, campaignName, campaignStart;
};
// Parses mod.toml (json=false) or mod.json (json=true). Returns false with `err` on a syntax or schema error.
bool parseManifest(const std::string& text, bool json, Manifest& out, std::string& err);
// Dotted-number comparison ("1.10" > "1.9"). Returns -1/0/1.
int compareVersions(const std::string& a, const std::string& b);

// Read-only view of a pack (folder or zip). Paths are relative, '/' separated, relative to the pack root.
class ModArchive {
public:
    virtual ~ModArchive() = default;
    struct Item {
        std::string path;
        u64 size = 0;
    };
    virtual std::vector<Item> list() const = 0;
    virtual bool read(const std::string& path, std::vector<u8>& out) const = 0;
    virtual std::string describe() const = 0; // "folder X" / "zip X"
};
// Opens a folder or a .zip. Zip support: stored and deflate, no encryption, no zip64.
std::shared_ptr<ModArchive> openArchive(const std::string& path, std::string& err);
// Raw deflate (RFC 1951). Exposed for tests.
bool inflateRaw(const u8* in, size_t inSize, std::vector<u8>& out, size_t expectedSize);

// One edit of an SRSC container (header 'SRSC', index of {type,id,sub,off,size}).
struct SrscPatch {
    u16 type = 0, id = 0, sub = 0; // sub 0xFFFF on a replacement = every entry with (type,id)
    std::vector<u8> data;
    bool add = false; // true: add the entry (an existing exact (type,id,sub) is replaced); false: replace only
};
// Rebuilds `in` with the patches applied. A replacement that fits and whose bytes no other index entry shares is
// written in place (index size updated); everything else is appended to the data region and the index is rewritten at
// the end. Replacements that match no entry are skipped with a log line. False with `err` when `in` is not a valid
// container.
bool patchSrsc(const std::vector<u8>& in, const std::vector<SrscPatch>& patches, std::vector<u8>& out,
               std::string& err);

// A level a pack registered. `name` is the LEVELLST.TXT title ("Level 9X - Name"), `path` the game path line
// ("\Levels\Level 9X - Name\Level 9X - Name.lvl"), `listIndex` its 0-based position in the merged list.
struct ExtraLevel {
    std::string name, modId, path;
    int listIndex = -1;
};

enum class ModState { Loaded, Disabled, Skipped };
struct ModStatus {
    std::string key;    // folder or zip stem, the name used in mods.ini
    std::string source; // host path
    Manifest manifest;  // valid when hasManifest
    bool hasManifest = false;
    bool enabled = false;
    ModState state = ModState::Disabled;
    std::string message; // why disabled / skipped, or a summary when loaded
    size_t entries = 0, texts = 0, rejected = 0;
    size_t files = 0, added = 0, levels = 0; // whole files, new SRSC entries, registered levels (files/new/levels)
};
struct Conflict {
    std::string what;          // "INTERFCE.TXD 0040/97/0" or "INTERFCE.XDB text 156.en"
    std::string winner, loser; // mod ids
};

struct ModLimits {
    // Whole-entry requests longer than this fail with -9 in ResourceService (RSRCIOP limit). A replacement that grows
    // an entry across the limit is rejected; entries that were already larger are streamed in chunks and stay allowed.
    u32 maxWholeEntry = 0x40800;
    u64 maxFileBytes = 64u << 20; // sanity cap per replacement file
    // Whole-file replacements and new files (`files/`) are held in memory; the 0x1201 file service sizes files with
    // u32, and 64 file slots limit simultaneously open files, not the number of files.
    u64 maxWholeFile = 256u << 20;
};

// A file or entry provided by a pack.
struct PackSource {
    std::shared_ptr<ModArchive> archive;
    std::string path; // path inside the pack
    size_t modIndex = 0;
    std::string modId;
    u64 size = 0;
};
struct NewEntryKey {
    std::string file; // upper-case file name only
    u16 type, id, sub;
    bool operator<(const NewEntryKey& o) const;
};

class ModOverride; // merged entry replacements

class ModManager {
public:
    explicit ModManager(DiscProfile profile, ModLimits limits = {});
    ~ModManager();
    // Scans `modsDir`, reads/extends `<modsDir>/mods.ini` (new packs are appended as "off"), loads the enabled packs in
    // list order. Returns the number of packs loaded. Safe to call once, before the first resource request.
    size_t load(const std::string& modsDir);
    // Same, with an explicit order/enabled list instead of mods.ini (key, enabled). Used by tests and the editor.
    size_t load(const std::string& modsDir, const std::vector<std::pair<std::string, bool>>& order);

    const std::vector<ModStatus>& status() const { return status_; }
    const std::vector<Conflict>& conflicts() const { return conflicts_; }
    size_t entryCount() const;
    size_t textCount() const { return textCount_; }
    // One line per pack and conflict, ready for a log or a UI list.
    std::vector<std::string> report() const;
    // Serves what the packs put on the disc: `files/<path>` whole files (replacement or new, later pack wins), entry
    // overrides of containers the guest reads with plain file reads (.LVL, the Known gap of earlier versions), `new/`
    // entries, and the merged LEVELLST.TXT. Call once after the IsoFs is open and its language root is set, before
    // the game reads files. Returns the number of files served from memory. Problems are logged as `mods: ...`
    // (e.g. a replacement past a guest limit is rejected with the reason).
    size_t installFiles(IsoFs& fs);
    // Levels registered by packs (valid after installFiles; only those whose LVL exists on the merged disc).
    // For the Multiplayer map list: `name` is what the game's level list holds, `path` the file the game opens.
    const std::vector<ExtraLevel>& extraLevels() const { return extraLevels_; }
    size_t fileCount() const { return files_.size(); }
    // Multiplayer map definitions (docs/reference/netcode.md "Multiplayer maps"): the text of every `maps/*.ini` of every
    // loaded pack, in load order. `first` is "mod <id>", `second` the file content (the game parses it).
    const std::vector<std::pair<std::string, std::string>>& mapFiles() const { return mapFiles_; }
    // Map pictures: every `previews/<name>.png` (<= 8 MiB) of every loaded pack, `first` = <name> without extension
    // (the safe form of the map's display name), `second` = the PNG bytes.
    const std::vector<std::pair<std::string, std::vector<u8>>>& previewFiles() const { return previewFiles_; }

    // Register in this order (entries first, then texts) before the first resource request. Null when empty.
    std::shared_ptr<IResourceOverride> entries() const;
    std::shared_ptr<XdbTextOverride> texts() const { return textCount_ ? texts_ : nullptr; }
    // Convenience: registers both on a service. Returns false when nothing was loaded.
    template <class Service>
    bool install(Service& res) const {
        if (auto e = entries())
            res.addOverride(e);
        if (auto t = texts())
            res.addOverride(t);
        return e_nonEmpty();
    }

private:
    bool e_nonEmpty() const { return entryCount() != 0 || textCount_ != 0; }
    DiscProfile profile_;
    ModLimits limits_;
    std::vector<ModStatus> status_;
    std::vector<Conflict> conflicts_;
    std::shared_ptr<ModOverride> merged_;
    std::map<std::string, PackSource> files_; // manglePath(files/<path>) -> source
    std::map<NewEntryKey, PackSource> newEntries_;
    std::vector<ExtraLevel> levelDefs_, extraLevels_;
    std::vector<std::pair<std::string, std::string>> mapFiles_;
    std::vector<std::pair<std::string, std::vector<u8>>> previewFiles_;
    std::shared_ptr<XdbTextOverride> texts_;
    size_t textCount_ = 0;
};

} // namespace hle::mods
