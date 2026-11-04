#include "hle/ArenaLevel.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace hle {
namespace {
using Bytes = std::vector<u8>;
u16 u16at(const Bytes& b, size_t p) {
    if (p > b.size() || b.size() - p < 2)
        throw std::runtime_error("truncated u16");
    return u16(b[p]) | u16(b[p + 1]) << 8;
}
u32 u32at(const Bytes& b, size_t p) {
    return u32(u16at(b, p)) | u32(u16at(b, p + 2)) << 16;
}
void put16(Bytes& b, u16 v) {
    b.push_back(u8(v));
    b.push_back(u8(v >> 8));
}
void put32(Bytes& b, u32 v) {
    put16(b, u16(v));
    put16(b, u16(v >> 16));
}
void set32(Bytes& b, size_t p, u32 v) {
    if (p > b.size() || b.size() - p < 4)
        throw std::runtime_error("bad output offset");
    for (unsigned i = 0; i < 4; ++i)
        b[p + i] = u8(v >> (8 * i));
}
struct Reader {
    const Bytes& b;
    size_t p = 0;
    void skip(size_t n) {
        if (p > b.size() || n > b.size() - p)
            throw std::runtime_error("truncated record");
        p += n;
    }
    u16 word() {
        const u16 v = u16at(b, p);
        skip(2);
        return v;
    }
    u32 dword() {
        const u32 v = u32at(b, p);
        skip(4);
        return v;
    }
    std::string text() {
        const u16 n = word();
        const size_t at = p;
        skip(n);
        std::string s(reinterpret_cast<const char*>(b.data() + at), n);
        if (const auto z = s.find('\0'); z != std::string::npos)
            s.resize(z);
        return s;
    }
};
struct Entry {
    u16 type, id, sub;
    Bytes bytes;
};
std::vector<Entry> entries(const Bytes& b) {
    if (b.size() < 16 || std::memcmp(b.data(), "SRSC", 4))
        throw std::runtime_error("not SRSC");
    const u32 index = u32at(b, 6), count = u32at(b, 10);
    if (index < 16 || index > b.size() || count > (b.size() - index) / 14)
        throw std::runtime_error("bad SRSC index");
    std::vector<Entry> out;
    for (u32 i = 0; i < count; ++i) {
        const size_t p = index + size_t(i) * 14;
        const u32 at = u32at(b, p + 6), size = u32at(b, p + 10);
        if (at < 16 || at > index || size > index - at)
            throw std::runtime_error("bad SRSC entry");
        out.push_back({u16at(b, p), u16at(b, p + 2), u16at(b, p + 4), Bytes(b.begin() + at, b.begin() + at + size)});
    }
    return out;
}
std::string dependencyPath(const std::string& level, const std::string& dependency) {
    std::string joined = level.substr(0, level.find_last_of('\\') + 1) + dependency + ".ODB";
    std::replace(joined.begin(), joined.end(), '/', '\\');
    std::vector<std::string> parts;
    for (size_t p = 0; p < joined.size();) {
        const size_t end = joined.find('\\', p);
        const std::string part = joined.substr(p, end == std::string::npos ? end : end - p);
        if (part == "..") {
            if (parts.empty())
                throw std::runtime_error("dependency escaped root");
            parts.pop_back();
        } else if (!part.empty() && part != ".")
            parts.push_back(part);
        if (end == std::string::npos)
            break;
        p = end + 1;
    }
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty())
            result += '\\';
        result += part;
    }
    return result;
}
struct Object {
    u32 id;
    u16 cls, db;
    size_t start = 0, end = 0, linksAt = 0, linksEnd = 0;
    bool keep = false;
};
Bytes filterObjects(const Bytes& bytes, const std::map<u16, std::map<u16, u32>>& classes, const std::set<u16>& npcBanks,
                    size_t& removed) {
    Reader r{bytes};
    const u16 count = r.word();
    std::vector<Object> objects;
    for (unsigned i = 0; i < count; ++i)
        objects.push_back({r.dword(), r.word(), r.word()});
    std::set<u32> retained;
    bool player = false, camera = false;
    for (auto& object : objects) {
        const auto db = classes.find(object.db);
        const auto cls = db == classes.end() ? std::map<u16, u32>::const_iterator{} : db->second.find(object.cls);
        const u32 runtimeClass = db != classes.end() && cls != db->second.end() ? cls->second : 0;
        if (!runtimeClass)
            throw std::runtime_error("unresolved placed actor class");
        object.keep = arenaEnvironmentClass(runtimeClass) && !npcBanks.contains(object.db);
        player |= runtimeClass == 0x10009;
        camera |= runtimeClass == 0x1001b;
        if (object.keep)
            retained.insert(object.id);
        else
            ++removed;
        object.start = r.p;
        r.skip(20); // lighting / reserved / position
        const u32 flags = r.dword();
        r.skip(2); // initial event mask (kept environment still needs its normal init)
        if (flags & 0x10)
            r.skip(4);
        object.linksAt = r.p;
        const u16 links = r.word();
        r.skip(size_t(links) * 4);
        object.linksEnd = r.p;
        r.skip(6); // orientation
        if (flags & 0x100)
            r.skip(12);
        r.skip(4);
        if (r.dword() != 0xffffffffu)
            throw std::runtime_error("bad actor sentinel");
        const u32 words = r.dword(), params = r.dword();
        r.skip(size_t(words) * 4);
        r.skip(size_t(params) * 2);
        for (u32 i = 0; i < params; ++i) {
            r.skip(4);
            r.text();
        }
        object.end = r.p;
    }
    if (r.p != bytes.size() || !player || !camera)
        throw std::runtime_error("invalid arena player / camera / object stream");
    Bytes out;
    put16(out, static_cast<u16>(retained.size()));
    for (const auto& object : objects)
        if (object.keep) {
            put32(out, object.id);
            put16(out, object.cls);
            put16(out, object.db);
        }
    for (const auto& object : objects)
        if (object.keep) {
            out.insert(out.end(), bytes.begin() + object.start, bytes.begin() + object.linksAt);
            std::vector<u32> links;
            for (size_t p = object.linksAt + 2; p < object.linksEnd; p += 4)
                if (retained.contains(u32at(bytes, p)))
                    links.push_back(u32at(bytes, p));
            put16(out, static_cast<u16>(links.size()));
            for (u32 link : links)
                put32(out, link);
            out.insert(out.end(), bytes.begin() + object.linksEnd, bytes.begin() + object.end);
        }
    return out;
}
} // namespace

bool arenaEnvironmentClass(uint32_t cls) {
    switch (cls) {
        case 0x10009:
        case 0x1001b: // actual player and camera
        case 0x10036: // level lighting / fog / physics / cache properties
        case 0x10011:
        case 0x10013:
        case 0x10014:
        case 0x1001a: // scenery, doors, torches, sky
        case 0x10006:
        case 0x1002b:
        case 0x1003f:
        case 0x10063:
        case 0x10092: // ambient sound / visual effects
        case 0x10084:
        case 0x10085: // lighting
        case 0x100d3:
        case 0x100d4:
        case 0x100d8:
        case 0x100e2: // vegetation / buildings / lit props / flags
            return true;
        default:
            return false; // no campaign NPC, detector, timer, sequence, exit, quest, shop, pickup or dragon init
    }
}

bool filterArenaLevel(const IsoFs& fs, IsoFs::Node& level, std::string& error) {
    try {
        const Bytes original = fs.readAll(level);
        auto records = entries(original);
        auto header = std::find_if(records.begin(), records.end(), [](const Entry& e) { return e.type == 0; });
        if (header == records.end())
            throw std::runtime_error("missing level header");
        Reader r{header->bytes};
        r.text();
        r.skip(8);
        const u32 count = r.dword();
        if (count > 256)
            throw std::runtime_error("too many dependencies");
        std::map<u16, std::map<u16, u32>> classes;
        std::set<u16> npcBanks;
        for (u32 i = 0; i < count; ++i) {
            const u16 index = r.word();
            r.skip(2);
            const std::string path = dependencyPath(level.key, r.text());
            IsoFs::Node odb;
            if (!fs.lookup(path, odb))
                continue; // dependencies may contain no ODB (music / textures)
            if (odb.key.ends_with("\\NPCS.ODB"))
                npcBanks.insert(index);
            for (const Entry& e : entries(fs.readAll(odb)))
                if (e.type == 0x102) {
                    Reader c{e.bytes};
                    c.text();
                    const size_t body = c.p;
                    const bool model = u16at(e.bytes, body + 2) != 0;
                    const size_t at = body + (model ? 80 : 16);
                    classes[index][e.id] = u32at(e.bytes, at);
                }
        }
        size_t removed = 0, streams = 0;
        for (Entry& e : records)
            if (e.type == 0x20) {
                e.bytes = filterObjects(e.bytes, classes, npcBanks, removed);
                ++streams;
            }
        if (!streams)
            throw std::runtime_error("missing placed actors");
        Bytes out(original.begin(), original.begin() + 16);
        std::vector<u32> offsets;
        for (const Entry& e : records) {
            offsets.push_back(static_cast<u32>(out.size()));
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
        }
        set32(out, 6, static_cast<u32>(out.size()));
        set32(out, 10, static_cast<u32>(records.size()));
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& e = records[i];
            put16(out, e.type);
            put16(out, e.id);
            put16(out, e.sub);
            put32(out, offsets[i]);
            put32(out, static_cast<u32>(e.bytes.size()));
        }
        level.mem = std::make_shared<const Bytes>(std::move(out));
        level.size = level.mem->size();
        level.host.clear();
        level.lba = 0;
        logf("arena LVL %s: stripped %zu placed campaign actors before initialization", level.key.c_str(), removed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
} // namespace hle
