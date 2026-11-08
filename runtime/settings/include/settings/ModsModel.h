// ModsModel: the list behind the "Mods" page of the PC settings. Pure data and logic, std only: the host fills it from
// the mod manager at start-up (one ModInfo per pack, in load order), the UI toggles / moves packs, and every change
// rewrites <mods folder>/mods.ini at once. Packs are loaded once at start-up (the override table is registered before
// the first resource request and the guest caches what it loaded), so every change waits for a restart:
// restartRequired() says whether the list differs from what the running game loaded. Format: docs/reference/modding.md.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
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
    // Discovery (docs/reference/modding.md "New packs"): the pack was found at start-up without a decision yet
    // (mods.ini `new`, or appended this start). Stays true for the session so the Mods page can mark it; `decided`
    // turns true when the player enables it, keeps it off or moves it, and mods.ini then gets `on` / `off` instead of
    // `new`.
    bool isNew = false;
    bool decided = false;
    std::string campaign; // manifest `campaign` id ("ootf", a custom id), empty for a plain pack
    std::string file;     // file or folder name in the mods folder ("ootf-levels.zip"), falls back to key
    // The pack is in the mods folder but its zip or manifest cannot be read (hle ModStatus::readError). Such a pack is
    // not offered on the new packs card; the title screen names it instead (settings/ModDiscovery.h).
    std::string readError;
    // The pack's picture on the Mods page (gameui::ImageKind::LevelPreview id the host resolved from the pack's
    // previews/pack.png); 0 = the pack has none.
    uint32_t picture = 0;
};

// One overlap between two packs (both are pack keys), as ModManager::conflicts() reports it.
struct ModConflict {
    std::string what; // "INTERFCE.TXD 0040/97/0"
    std::string winnerKey, loserKey;
};

// What the title's game card made of the campaign packs at start-up (the host fills it, settings/ModDiscovery.h
// explains it to the player).
struct GameChoiceState {
    std::string setting = "ask";                // the [general] game value the game started with
    bool settingFromEnv = false;                // DRAKAN_GAME set it
    bool offered = false;                       // drakan_game::offerChoice()
    std::vector<std::string> cardCampaigns;     // campaign ids on the card (valid, loaded)
    std::map<std::string, std::string> refused; // manifest id -> why its campaign is not on the card
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

    // Packs found at start-up that the player has not decided on (new, found, readable, still off, not decided this
    // session).
    std::vector<const ModInfo*> undecided() const;
    // The title notice's answers: enable every undecided pack, or keep them off (both are decisions: mods.ini gets
    // `on` / `off`). In memory; save() writes. Return the number of packs changed.
    size_t enableUndecided();
    size_t keepUndecidedOff();

    // The list differs from what the game loaded (enabled flags or order of the packs that matter).
    bool restartRequired() const;
    // One line for the UI: "Active", "Disabled", "New: not enabled yet", "Skipped: <reason>", "Enabled, loads after
    // restart" ...
    std::string stateText(const ModInfo& m) const;
    // Overlaps this pack had at start-up, one line each, grouped per other pack:
    // "Overrides 'B' on 3 entries (INTERFCE.TXD 0040/97/0, ...)" / "Overridden by 'B' on 1 entry (...)".
    std::vector<std::string> conflictLines(const std::string& key) const;

    // mods.ini text for the current list, keeping the comment / unknown lines of `existing`.
    std::string iniText(const std::string& existing) const;
    // Writes <folder>/mods.ini (temp file + rename). False with `err` on failure.
    bool save(std::string* err = nullptr) const;

    // The game card's start-up state, for the "why is my campaign not offered" line (ModDiscovery.h).
    void setGameChoice(GameChoiceState s) { gameChoice_ = std::move(s); }
    const GameChoiceState& gameChoice() const { return gameChoice_; }

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
    GameChoiceState gameChoice_;
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
