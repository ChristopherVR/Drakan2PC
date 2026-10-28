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
#include <functional>
#include <map>
#include <memory>
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
