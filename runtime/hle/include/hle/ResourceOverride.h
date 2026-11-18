// Resource override layer: substitutes / patches resource payloads before the ResourceService delivers them to the
// game. Generic on purpose (UI prompt remapping, HD texture packs, translations, other mods).
//
//   ResourceService::addOverride(std::shared_ptr<IResourceOverride>)  -- consulted in registration order for every
//   request
//
// An override sees the COMPLETE entry payload (`data` = bytes of the SRSC entry as stored on the disc, or as already
// modified by an earlier override) and may replace it with a blob of any size; offset/length of the request are applied
// to the result afterwards. Overrides are called on the 0x1202 worker thread only; register them before the first
// request.
//
// Provided implementations:
//   DirectoryOverride  overrides/<FILE>/<type>_<id>_<sub>.bin   (whole-entry replacement, e.g. an HD texture)
//   XdbTextOverride    patches the localised strings of .XDB text entries (type 0x400) by rule callback or by
//                      overrides/<FILE>/text.ini lines   "<id>[.<lang>] = text"
#pragma once
#include "Interfaces.h"
#include "IsoFs.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hle {

struct ResourceId {
    std::string file;              // upper-case file name only, e.g. "INTERFCE.TXD"
    std::string path;              // canonical key, e.g. "ENGLISH\COMMON\INTERFCE\INTERFCE.TXD"
    u16 type = 0, id = 0, sub = 0; // sub is the sub id of the matched index entry (never the 0xFFFF wildcard)
};

class IResourceOverride {
public:
    virtual ~IResourceOverride() = default;
    // Cheap predicate, called for every request. Returning true makes the service read the whole entry and call
    // apply().
    virtual bool wants(const ResourceId& id) = 0;
    // Modify `data` in place (it holds the current payload). Return true if modified.
    virtual bool apply(const ResourceId& id, std::vector<u8>& data) = 0;
};

// Parses "<type hex>_<id dec>_<sub dec|x>" (no extension). sub "x" = 0xFFFF (any). Shared by DirectoryOverride and the
// mod packs (ModPack.h).
bool parseEntryName(const std::string& stem, u16& type, u16& id, u16& sub);

// ---- whole-entry replacement from a folder
// -------------------------------------------------------------------------------------
// <root>/<FILE>/<type>_<id>_<sub>.bin ; FILE = file name (INTERFCE.TXD, case-insensitive), type = hex (0040, 0400),
// id/sub decimal, sub may be "x" (any). The file content replaces the entry payload verbatim.
class DirectoryOverride final : public IResourceOverride {
public:
    // Scans `root` (missing folder = no overrides, returns 0). Returns the number of replacement files found.
    size_t load(const std::string& root);
    size_t size() const { return files_.size(); }
    bool wants(const ResourceId& id) override;
    bool apply(const ResourceId& id, std::vector<u8>& data) override;

private:
    struct Key {
        std::string file;
        u16 type, id, sub;
        bool operator<(const Key& o) const;
    };
    std::map<Key, std::string> files_; // key -> host path
    const std::string* find(const ResourceId& id) const;
};

// ---- broken character LODs on the disc
// ----------------------------------------------------------------------------------------------------------------
// Two NPC models of NPCS.MOD ("BigBrownGuy" and "BigBlueGuy", the fat male villagers) have a last LOD whose polygons
// all name the torso texture with UVs that fit none of the model's sheets, in the 0x0204 polygons and in the 0x0211
// strips alike, so beyond 12000 units they draw as a brown or blue smear. This raises the switch distance of that LOD
// in the model's character record (0x0208) so the engine keeps the previous, correctly mapped LOD. The models are
// recognised by the shape of their LOD table, not by entry id, and any other record is left alone. Evidence:
// docs/journal/2026-11-10/issue6-distant-npc-textures.md.
class CharacterLodFixOverride final : public IResourceOverride {
public:
    static constexpr u16 kType = 0x0208;
    // Distance written for a disabled LOD (the engine squares it; far beyond any draw distance).
    static constexpr float kNever = 1.0e8f;
    bool wants(const ResourceId& id) override;
    bool apply(const ResourceId& id, std::vector<u8>& data) override;
    // The patch itself: true when `data` is one of the known broken records and was changed.
    static bool patch(std::vector<u8>& data);
};

// ---- campaign credits
// ----------------------------------------------------------------------------------------------------------------
// The end credits are two plain text entries of DRAKAN2.RRC (type 0x401, ids 8807 and 8808; "=Heading" lines, names, a
// blank line) the engine reads by id. This serves a campaign pack's own text in place of both while `activeMod()` names
// that pack (the current game is its campaign), so the disc game's credits are untouched.
class CampaignCreditsOverride final : public IResourceOverride {
public:
    static constexpr u16 kType = 0x401, kFirstId = 8807, kSecondId = 8808;
    void add(const std::string& modId, std::string text) { texts_[modId] = std::move(text); }
    void setActive(std::function<std::string()> f) { active_ = std::move(f); }
    bool wants(const ResourceId& id) override;
    bool apply(const ResourceId& id, std::vector<u8>& data) override;
    // The engine reads the credits with plain file reads of the opened DRAKAN2.RRC, not through the entry layer: a
    // FileService read overlay (setReadOverlay) calls this to rewrite the bytes of the two entries in place. The text
    // is padded with spaces to the entry's size (a longer text is cut at a line end); the second entry becomes one
    // blank line.
    void patchRead(const IsoFs& fs, const IsoFs::Node& node, u64 offset, u8* data, size_t n);

private:
    const std::string* current() const;
    struct Range {
        u16 id;
        u64 off, size;
    };
    std::map<std::string, std::vector<Range>> ranges_; // file key -> the entries' byte ranges
    const std::vector<Range>& rangesOf(const IsoFs& fs, const IsoFs::Node& node);
    std::string bodyFor(u16 id, u64 size);
    std::map<std::string, std::string> texts_;
    std::function<std::string()> active_;
};

// ---- campaign title music
// ----------------------------------------------------------------------------------------------------------------
// The title menu plays one PS2MUSIC.SDB track: INTERFCE.ODB class 1 "Background Music File" = 0x100c4 (db 1 of
// INTERFCE.DB = PS2 Music, id 196 "SeaHigh GUI" on the PAL disc). A campaign pack names its own title track in the
// manifest
// (`campaign_title_music = <id>`, an id the pack adds to PS2MUSIC.SDB with `new/` entries); while that campaign is the
// current game, requests for the disc's title track are served from it. ResourceService consults `map` before its index
// lookup (setIdRedirect). The two tracks differ in length, so the choice is latched when the guest reads the track's
// descriptor (0x302, at the start of each play) and the sample (0x304) requests of that play follow it (music has no
// 0x305); a disc play switches to the campaign track at its next chunk when the campaign is chosen during it (`game =
// ask`).
class CampaignMusicRedirect {
public:
    static constexpr u16 kDiscTitleMusic = 196;
    void add(const std::string& modId, u16 id) { tracks_[modId] = id; }
    void setActive(std::function<std::string()> f) { active_ = std::move(f); }
    // The id to serve for a request of `type` / `id` in the file `fileKey` (upper-case canonical key).
    u16 map(const std::string& fileKey, u16 type, u16 id);
    // The id the current game would play now (the disc id when no campaign track applies).
    u16 wanted() const;
    size_t size() const { return tracks_.size(); }
    // A campaign with its own title track is the current game.
    bool campaignActive() const { return active_ && tracks_.count(active_()) != 0; }

private:
    std::map<std::string, u16> tracks_;
    std::function<std::string()> active_;
    u16 latched_ = 0; // 0 = no descriptor read yet: follow the current game
    std::mutex mu_;
};

// ---- music descriptors in the music category
// ----------------------------------------------------------------------------------------------------------------
// The u32 at +16 after the name of a 0x302 sound descriptor is the sound's category in the sound manager (0 effects,
// 2 music, 3 / 4 interface and speech; every PS2MUSIC.SDB track on the disc has 2). The music category plays one track
// at a time: a new track fades the playing one out with the category's fade times (a cut scene's music action sets
// them), the Music Volume option scales it, and a scene's music level and the category stop (0x1b6ac8) act on it.
// Order of the Flame packs converted before 15 November 2026 wrote 0 (effects), so their tracks played on top of each
// other at full volume and nothing could stop them. This serves such a PS2MUSIC.SDB descriptor with category 2.
class MusicCategoryOverride final : public IResourceOverride {
public:
    static constexpr u16 kType = 0x302;
    static constexpr u32 kMusicCategory = 2;
    bool wants(const ResourceId& id) override;
    bool apply(const ResourceId& id, std::vector<u8>& data) override;
    // The patch itself: true when `data` is a descriptor with category 0 and was changed.
    static bool patch(std::vector<u8>& data);
};

// ---- campaign music level
// ----------------------------------------------------------------------------------------------------------------
// The sound manager gives a sound instance the base gain `category gain * (1 - global level) * (1 - category level) *
// sound gain` (0x1b30b8 from 0x1b7758, 0x1b6e28 and 0x1b6800); the music category's gain is the Music Volume option /
// 100, default 25. Campaign tracks play at half level at the default, with adjustment over the full slider.
struct CampaignMusicLevel {
    // The base gain to give a campaign music instance for the engine's `engineGain` (which includes `categoryGain`).
    static float baseGain(float engineGain, float categoryGain);
};

// Category levels are attenuations (0 = full volume). Overlapping scenes share the underlying music mix.
class CampaignSceneMusic {
public:
    float begin(u32 scene, float savedLevel, float sceneLevel);
    float end(u32 scene, float savedLevel);
    float setLevel(float level);
    bool active(u32 scene) const { return scenes_.count(scene) != 0; }
    void reset();

private:
    float mixedLevel() const;
    float underlying_ = 0;
    std::map<u32, float> scenes_;
};

// ---- XDB text
// ----------------------------------------------------------------------------------------------------------------
namespace xdb {
constexpr int NUM_LANGS = 5; // slots 0..4 = English, German, French, Italian, Spanish (slots 5..7 are empty spares)
constexpr int NUM_SLOTS = 8;
// Decode a type-0x400 payload into 8 byte strings. Returns false when the payload does not have the exact text layout
// (u16 len + len bytes, len = roundup4(chars + 2) - 2, 8 times, consuming the payload exactly).
bool decode(const std::vector<u8>& payload, std::vector<std::string>& slots);
std::vector<u8> encode(const std::vector<std::string>& slots);
// "en" "de" "fr" "it" "es" -> 0..4, "*" or "" -> -1, else -2
int langFromName(const std::string& s);
} // namespace xdb

class GlyphMap;

// text.ini "encoding" directive: returns true and sets `utf8` when `content` declares one ("utf-8"/"utf8" -> true,
// "raw"/"cp1252"/"windows-1252"/"latin-1" -> false); `bad` gets an unknown value (treated as raw).
bool textIniEncoding(const std::string& content, bool& utf8, std::string* bad = nullptr);
// True for text.ini keys that are directives, not string ids (the mod conflict scan skips them).
bool isTextIniDirective(const std::string& key);
// The translation glyph map lives next to the font override: overrides/INTERFCE.TXD/glyphs.ini (one per pack, it
// applies to both fonts 96 and 97, which share the glyph order).
inline constexpr const char* kGlyphMapDir = "INTERFCE.TXD";
inline constexpr const char* kGlyphMapName = "glyphs.ini";
// Parses a glyph map (bad lines are logged with `label`); loadGlyphMapFile returns nullptr when the file is missing.
std::shared_ptr<GlyphMap> parseGlyphMap(const std::string& content, const std::string& label);
std::shared_ptr<GlyphMap> loadGlyphMapFile(const std::string& path, const std::string& label);

class XdbTextOverride final : public IResourceOverride {
public:
    // Rule callback: may rewrite `text` (raw game bytes: Latin-1/Windows-1252; glyph escapes are the literal characters
    // "\033"). Return true when it changed the text.
    using Rule = std::function<bool(const ResourceId& id, int lang, std::string& text)>;
    void addRule(Rule r) { rules_.push_back(std::move(r)); }
    // <root>/<FILE>/text.ini: "<id>[.<lang>] = text" (lang en/de/fr/it/es or *, default *), ';' / '#' at line start =
    // comment, escapes \\ \n \r \t (write the game's glyph escape \033 as \\033). Returns the number of lines accepted.
    // A line "encoding = utf-8" anywhere in the file makes the values UTF-8: they are transcoded to game bytes through
    // `glyphs` (GlyphMap.h) and clamped to kXdbTextMaxBytes. Without it (or "encoding = raw") the bytes are kept as
    // written. loadDirectory reads the glyph map from <root>/INTERFCE.TXD/glyphs.ini when present.
    size_t loadDirectory(const std::string& root);
    // Same line syntax from memory, for one <FILE> (upper-cased). Returns the number of lines accepted.
    size_t loadIni(const std::string& file, const std::string& content, const GlyphMap* glyphs = nullptr);
    void setText(const std::string& file, u16 id, int lang, std::string text); // programmatic patch (lang -1 = all)
    bool wants(const ResourceId& id) override;
    bool apply(const ResourceId& id, std::vector<u8>& data) override;

private:
    struct Patch {
        std::string file;
        u16 id;
        int lang;
        std::string text;
    };
    std::vector<Patch> patches_;
    std::vector<Rule> rules_;
};

} // namespace hle
