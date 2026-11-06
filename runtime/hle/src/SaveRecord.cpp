#include "hle/SaveRecord.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace hle::mods {
namespace {
std::string asciiLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}
std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
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
            if (have->build != want.build) {
                r.verdict = SaveVerdict::DifferentBuild;
                r.pack = *have;
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
        return r;
    }
    return r;
}

std::vector<PackStamp> recordFor(const ActivePacks& active) {
    return active.packs;
}

} // namespace hle::mods
