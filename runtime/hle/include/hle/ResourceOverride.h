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

// ---- campaign music replaces the playing music
// ----------------------------------------------------------------------------------------------------------------
// On the PC a new music cue replaces the playing music (DirectMusic switches the primary segment). The PS2 engine plays a
// cue on top of the level's Triggered Music Loops, and a sequence message does not reach those loops, so in a campaign
// the sample chunks (PS2MUSIC.SDB 0x304) of every track but the newest one are served silent (ADPCM blocks with the
// predictor, shift and data zeroed, the loop flags kept). A track starts when its descriptor (0x302) is read; a track
// whose chunk requests stop for a while and begin again at offset 0 starts again too. Drakan 2 is not affected.
class CampaignMusicDucker {
public:
    void setActive(std::function<bool()> f) { active_ = std::move(f); }
    // Observe a request; true = serve this sample chunk silent.
    bool silence(const std::string& fileKey, u16 type, u16 id, u32 offset);
    // Silence the ADPCM blocks of `data` (16-byte blocks, flag byte kept).
    static void silenceAdpcm(std::vector<u8>& data);

private:
    struct Track {
        std::chrono::steady_clock::time_point last{};
        bool muted = false;
    };
    std::function<bool()> active_;
    std::map<u16, Track> tracks_;
    u16 current_ = 0;
    std::mutex mu_;
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

class XdbTextOverride final : public IResourceOverride {
public:
    // Rule callback: may rewrite `text` (raw game bytes: Latin-1/Windows-1252; glyph escapes are the literal characters
    // "\033"). Return true when it changed the text.
    using Rule = std::function<bool(const ResourceId& id, int lang, std::string& text)>;
    void addRule(Rule r) { rules_.push_back(std::move(r)); }
    // <root>/<FILE>/text.ini: "<id>[.<lang>] = text" (lang en/de/fr/it/es or *, default *), ';' / '#' at line start =
    // comment, escapes \\ \n \r \t (write the game's glyph escape \033 as \\033). Returns the number of lines accepted.
    size_t loadDirectory(const std::string& root);
    // Same line syntax from memory, for one <FILE> (upper-cased). Returns the number of lines accepted.
    size_t loadIni(const std::string& file, const std::string& content);
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
