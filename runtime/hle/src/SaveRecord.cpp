#include "hle/SaveRecord.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "common/Text.h"

namespace hle::mods {

using drakan::common::asciiLower;
using drakan::common::trim;
namespace {
std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}
// One line: record values never span lines.
std::string oneLine(const std::string& s) {
    std::string out;
    for (char c : s)
        out.push_back(c == '\r' || c == '\n' ? ' ' : c);
    return trim(out);
}
bool sameLevel(const std::string& a, const std::string& b) {
    return asciiLower(trim(a)) == asciiLower(trim(b));
}
// A level-scoped pack (save_scope = "levels") that does not register `level`: saves of that level do not depend on it.
// The save's name can be the level title cut short (`OotF 02 - Ruined Village .rsg` for "OotF 02 - Ruined Village
// Book"), so a title that starts with the trimmed name counts as the pack's level: a wrong "in scope" only refuses.
bool outOfScope(const ActivePacks& active, const std::string& packId, const std::string& level) {
    if (std::find(active.levelScoped.begin(), active.levelScoped.end(), packId) == active.levelScoped.end())
        return false;
    const std::string want = asciiLower(trim(level));
    return std::none_of(active.levels.begin(), active.levels.end(), [&](const ActivePacks::LevelOwner& l) {
        return l.packId == packId && asciiLower(trim(l.level)).compare(0, want.size(), want) == 0;
    });
}
} // namespace

bool isContentPath(const std::string& path) {
    const std::string p = asciiLower(slashes(path));
    for (const char* root : {"files/", "new/", "overrides/"}) {
        const std::string r(root);
        if (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p.back() != '/')
            return true;
    }
    return false;
}

u32 crc32Of(const u8* d, size_t n) {
    static const auto table = [] {
        std::vector<u32> t(256);
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        c = table[(c ^ d[i]) & 255] ^ (c >> 8);
    return ~c;
}

u64 packContentHash(std::vector<ContentItem> items) {
    std::vector<ContentItem> lines;
    for (ContentItem& i : items)
        if (isContentPath(i.path)) {
            i.path = asciiLower(slashes(i.path));
            lines.push_back(std::move(i));
        }
    std::sort(lines.begin(), lines.end(), [](const ContentItem& a, const ContentItem& b) { return a.path < b.path; });
    u64 h = 0xcbf29ce484222325ull;
    for (const ContentItem& l : lines) {
        char size[24], crc[12];
        std::snprintf(size, sizeof size, "%llu", static_cast<unsigned long long>(l.size));
        std::snprintf(crc, sizeof crc, "%08x", l.crc);
        std::string line = l.path;
        line.push_back('\0');
        line += size;
        line.push_back('\0');
        line += crc;
        line.push_back('\n');
        for (unsigned char c : line)
            h = (h ^ c) * 0x100000001b3ull;
    }
    return h;
}

std::string formatSaveRecord(const std::vector<PackStamp>& packs) {
    std::string s = "; Mod packs that were active when this save was written. The game refuses the save when one of\n"
                    "; them is missing or is another build of the pack.\nformat = 1\n";
    for (const PackStamp& p : packs) {
        s += "\n[pack " + oneLine(p.id) + "]\n";
        s += "name = " + oneLine(p.name) + "\n";
        s += "version = " + oneLine(p.version) + "\n";
        s += "build = " + oneLine(p.build) + "\n";
        if (p.revision > 0)
            s += "revision = " + std::to_string(p.revision) + "\n";
    }
    return s;
}

bool parseSaveRecord(const std::string& text, std::vector<PackStamp>& out) {
    out.clear();
    int format = 0;
    PackStamp* cur = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t end = std::min(text.find('\n', pos), text.size());
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line.front() == '[') {
            if (line.back() != ']' || line.compare(0, 6, "[pack ") != 0)
                return false;
            out.push_back({});
            cur = &out.back();
            cur->id = trim(line.substr(6, line.size() - 7));
            if (cur->id.empty())
                return false;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return false;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (!cur) {
            if (key == "format")
                format = std::atoi(value.c_str());
            continue;
        }
        if (key == "name")
            cur->name = value;
        else if (key == "version")
            cur->version = value;
        else if (key == "build")
            cur->build = value;
        else if (key == "revision")
            cur->revision = std::max(0, std::atoi(value.c_str()));
    }
    if (format != 1)
        return false;
    for (const PackStamp& p : out)
        if (p.build.empty())
            return false;
    return true;
}

const char* saveVerdictName(SaveVerdict v) {
    switch (v) {
        case SaveVerdict::Ok:
            return "ok";
        case SaveVerdict::DifferentBuild:
            return "different build";
        case SaveVerdict::MissingPack:
            return "missing pack";
        case SaveVerdict::OlderThanPack:
            return "made before pack builds were recorded";
        case SaveVerdict::BadRecord:
            return "unreadable record";
    }
    return "?";
}

SaveCheck checkSave(const SaveCheckInput& save, const ActivePacks& active) {
    SaveCheck r;
    if (save.hasRecord) {
        if (!save.recordReadable) {
            r.verdict = SaveVerdict::BadRecord;
            return r;
        }
        for (const PackStamp& want : save.record) {
            const auto have = std::find_if(active.packs.begin(), active.packs.end(),
                                           [&](const PackStamp& p) { return p.id == want.id; });
            if (have == active.packs.end()) {
                r.verdict = SaveVerdict::MissingPack;
                r.pack = want;
                return r;
            }
            if (outOfScope(active, want.id, save.level))
                continue;
            if (have->build != want.build) {
                const auto compatible = std::find_if(
                    active.compatibleBuilds.begin(), active.compatibleBuilds.end(),
                    [&](const auto& build) { return build.packId == want.id && build.build == want.build; });
                if (compatible != active.compatibleBuilds.end()) {
                    r.needsUpgrade = true;
                    continue;
                }
                r.verdict = SaveVerdict::DifferentBuild;
                r.pack = *have;
                r.overridden = active.loadOtherBuilds;
                return r;
            }
        }
        return r;
    }
    if (save.level.empty())
        return r;
    for (const ActivePacks::LevelOwner& l : active.levels) {
        if (!l.buildDeclared || !sameLevel(l.level, save.level))
            continue;
        r.verdict = SaveVerdict::OlderThanPack;
        const auto pack = std::find_if(active.packs.begin(), active.packs.end(),
                                       [&](const PackStamp& p) { return p.id == l.packId; });
        r.pack = pack != active.packs.end() ? *pack : PackStamp{l.packId, l.packId, {}, {}};
        r.overridden = active.loadOtherBuilds;
        return r;
    }
    return r;
}

std::vector<PackStamp> recordFor(const ActivePacks& active, const std::string& level) {
    std::vector<PackStamp> out;
    for (const PackStamp& p : active.packs)
        if (!outOfScope(active, p.id, level))
            out.push_back(p);
    return out;
}

} // namespace hle::mods
