// ModsModel: the list behind the "Mods" page of the PC settings. Pure data and logic, std only: the host fills it from
// the mod manager at start-up (one ModInfo per pack, in load order), the UI toggles / moves packs, and every change
// rewrites <mods folder>/mods.ini at once. Packs are loaded once at start-up (the override table is registered before
// the first resource request and the guest caches what it loaded), so every change waits for a restart:
// restartRequired() says whether the list differs from what the running game loaded. Format: docs/reference/modding.md.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace dsettings {

// What the running game made of a pack at start-up.
enum class ModBoot { Active, Disabled, Skipped };

struct ModInfo {
    std::string key;  // folder or zip stem, the name used in mods.ini
    std::string name; // manifest name (falls back to key)
    std::string id, version, author, description;
    std::string target; // "pal", "usa, pal", "any disc" (manifest target profiles)
    bool hasManifest = false;
    bool found = true;                // false: listed in mods.ini but not on disk
    bool enabled = false;             // wanted: the mods.ini flag now
    ModBoot boot = ModBoot::Disabled; // result at start-up
    bool bootEnabled = false;         // the flag the game started with
    std::string message;              // why skipped, else a summary
};

// One overlap between two packs (both are pack keys), as ModManager::conflicts() reports it.
struct ModConflict {
    std::string what; // "INTERFCE.TXD 0040/97/0"
    std::string winnerKey, loserKey;
};

class ModsModel {
public:
    // Start-up snapshot: packs in load order. Resets every pending change.
    void setSnapshot(std::string folder, std::vector<ModInfo> mods, std::vector<ModConflict> conflicts);
    void setOpenFolder(std::function<bool(const std::string&)> fn) { openFolder_ = std::move(fn); }

    const std::string& folder() const { return folder_; }
    const std::vector<ModInfo>& mods() const { return mods_; }
    size_t size() const { return mods_.size(); }
    int indexOf(const std::string& key) const; // -1 when unknown
    const ModInfo* find(const std::string& key) const;

    // Edits (in memory only; save() writes). False for an unknown key / out of range.
    bool setEnabled(const std::string& key, bool on);
    bool moveTo(const std::string& key, int index); // clamped to [0, size)
    bool moveBy(const std::string& key, int delta);

    // The list differs from what the game loaded (enabled flags or order of the packs that matter).
    bool restartRequired() const;
    // One line for the UI: "Active", "Disabled", "Skipped: <reason>", "Will load after restart" ...
    std::string stateText(const ModInfo& m) const;
    // Overlaps this pack had at start-up, one line each, grouped per other pack:
    // "Overrides 'B' on 3 entries (INTERFCE.TXD 0040/97/0, ...)" / "Overridden by 'B' on 1 entry (...)".
    std::vector<std::string> conflictLines(const std::string& key) const;

    // mods.ini text for the current list, keeping the comment / unknown lines of `existing`.
    std::string iniText(const std::string& existing) const;
    // Writes <folder>/mods.ini (temp file + rename). False with `err` on failure.
    bool save(std::string* err = nullptr) const;

    bool canOpenFolder() const { return static_cast<bool>(openFolder_); }
    bool openFolder() const { return openFolder_ && openFolder_(folder_); }

    // The settings page's actions. Keys: "mods.enable.<key>" (value "1" / "0"), "mods.order.<key>" (value = the new
    // 1-based position), "mods.folder" (open the folder). A list change is saved at once. Returns false with `err`
    // when the key is unknown or the file could not be written.
    bool apply(const std::string& rowKey, const std::string& value, std::string* err = nullptr);

private:
    std::string folder_;
    std::vector<ModInfo> mods_, boot_;
    std::vector<ModConflict> conflicts_;
    std::function<bool(const std::string&)> openFolder_;
};

inline constexpr const char* kKeyModsEnable = "mods.enable.";
inline constexpr const char* kKeyModsOrder = "mods.order.";
inline constexpr const char* kKeyModsFolder = "mods.folder";
inline constexpr const char* kKeyModsRestart = "mods.restart";
inline bool isModsKey(const std::string& k) {
    return k.rfind("mods.", 0) == 0;
}

} // namespace dsettings
