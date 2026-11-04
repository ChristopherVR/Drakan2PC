// Synthetic pre-init arena filtering and optional smoke test against the player's own ISO. No game content embedded.
#include "hle/ArenaLevel.h"
#include "hle/FileService.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace hle;
using Bytes = std::vector<u8>;
void word(Bytes& b, u16 v) {
    b.push_back(u8(v));
    b.push_back(u8(v >> 8));
}
void dword(Bytes& b, u32 v) {
    word(b, u16(v));
    word(b, u16(v >> 16));
}
void text(Bytes& b, const std::string& s) {
    word(b, static_cast<u16>(s.size() + 1));
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}
u16 get16(const Bytes& b, size_t p) {
    return u16(b.at(p)) | u16(b.at(p + 1)) << 8;
}
u32 get32(const Bytes& b, size_t p) {
    return u32(get16(b, p)) | u32(get16(b, p + 2)) << 16;
}
void set32(Bytes& b, size_t p, u32 v) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(p + i) = u8(v >> (8 * i));
}
struct Entry {
    u16 type, id;
    Bytes bytes;
};
Bytes srsc(const std::vector<Entry>& entries) {
    Bytes b(16);
    std::memcpy(b.data(), "SRSC", 4);
    b[4] = 1;
    std::vector<u32> offsets;
    for (const auto& e : entries) {
        offsets.push_back(static_cast<u32>(b.size()));
        b.insert(b.end(), e.bytes.begin(), e.bytes.end());
    }
    set32(b, 6, static_cast<u32>(b.size()));
    set32(b, 10, static_cast<u32>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        word(b, entries[i].type);
        word(b, entries[i].id);
        word(b, 0);
        dword(b, offsets[i]);
        dword(b, static_cast<u32>(entries[i].bytes.size()));
    }
    return b;
}
Bytes entry(const Bytes& b, u16 type) {
    const u32 p = get32(b, 6), n = get32(b, 10);
    for (u32 i = 0; i < n; ++i)
        if (get16(b, p + i * 14) == type) {
            const u32 a = get32(b, p + i * 14 + 6), s = get32(b, p + i * 14 + 10);
            return {b.begin() + a, b.begin() + a + s};
        }
    return {};
}
Bytes cls(u32 rfl, bool model = false) {
    Bytes b;
    text(b, "synthetic");
    const size_t body = b.size();
    b.resize(body + (model ? 96 : 32));
    if (model)
        b[body + 2] = 1;
    set32(b, body + (model ? 80 : 16), rfl);
    return b;
}
void body(Bytes& b, std::initializer_list<u32> links = {}) {
    for (unsigned i = 0; i < 6; ++i)
        dword(b, i == 5 ? 1 : 0); // lighting / reserved / position / flags
    word(b, 1);
    word(b, static_cast<u16>(links.size()));
    for (u32 link : links)
        dword(b, link);
    word(b, 0);
    word(b, 0);
    word(b, 0);
    dword(b, 0);
    dword(b, 0xffffffff);
    dword(b, 0);
    dword(b, 0);
}
struct Memory final : IEeMemory {
    bool read(u32, void*, size_t) override { return false; }
    bool write(u32, const void*, size_t) override { return true; }
};
int main(int argc, char** argv) {
    for (u32 rfl : {0x1009du, 0x1003du, 0x1003eu, 0x10033u, 0x100b4u, 0x1001cu})
        assert(!arenaEnvironmentClass(rfl));
    for (u32 rfl : {0x10009u, 0x1001bu, 0x100d3u, 0x10084u, 0x10036u})
        assert(arenaEnvironmentClass(rfl));
    IsoFs fs;
    fs.setLanguageRoot("ENGLISH");
    fs.addMemoryFile("ENGLISH\\COMMON\\SYSTEM\\SYSTEM.ODB", srsc({{0x102, 10, cls(0x10009)},
                                                                  {0x102, 11, cls(0x1001b)},
                                                                  {0x102, 12, cls(0x10011, true)},
                                                                  {0x102, 14, cls(0x1003d)},
                                                                  {0x102, 15, cls(0x10033)},
                                                                  {0x102, 16, cls(0x1003e)}}));
    fs.addMemoryFile("ENGLISH\\COMMON\\NPC\\NPCS.ODB",
                     srsc({{0x102, 13, cls(0x1009d, true)}, {0x102, 17, cls(0x10011, true)}}));
    Bytes header;
    text(header, "Test");
    dword(header, 512);
    dword(header, 512);
    dword(header, 2);
    word(header, 2);
    word(header, 0);
    text(header, "..\\..\\Common\\System\\System");
    word(header, 5);
    word(header, 0);
    text(header, "..\\..\\Common\\NPC\\NPCs");
    Bytes objects;
    word(objects, 8);
    for (u16 i = 0; i < 8; ++i) {
        dword(objects, i + 1);
        word(objects, u16(i + 10));
        word(objects, (i == 3 || i == 7) ? 5 : 2);
    }
    body(objects, {2, 4});
    for (unsigned i = 1; i < 8; ++i)
        body(objects);
    const Bytes terrain = {1, 2, 3, 4, 5};
    const Bytes original = srsc({{0, 0, header}, {0x20, 0, objects}, {0x30, 0, terrain}});
    const std::string path = "ENGLISH\\LEVELS\\TEST\\TEST.LVL";
    fs.addMemoryFile(path, original);
    IsoFs::Node node;
    assert(fs.lookup(path, node));
    std::string error;
    assert(filterArenaLevel(fs, node, error));
    const Bytes filtered = fs.readAll(node);
    const Bytes kept = entry(filtered, 0x20);
    assert(get16(kept, 0) == 3);
    assert(get32(kept, 2) == 1 && get32(kept, 10) == 2 && get32(kept, 18) == 3);
    assert(entry(filtered, 0x30) == terrain);
    assert(get16(kept, 26 + 26) == 1); // player links contain only the retained camera, not the removed NPC
    assert(get32(kept, 26 + 28) == 2);
    IsoFs::Node untouched;
    assert(fs.lookup(path, untouched) && fs.readAll(untouched) == original);
    Memory memory;
    FileService files(fs, memory);
    bool arena = true;
    files.setOpenFilter([&](const IsoFs& source, IsoFs::Node& view, std::string& why) {
        return !arena || filterArenaLevel(source, view, why);
    });
    Bytes request(8);
    request.insert(request.end(), path.begin(), path.end());
    request.push_back(0);
    u32 reply[2] = {};
    assert(files.handle(0, request.data(), static_cast<u32>(request.size()), reply, sizeof(reply)) == 8);
    FileService::Open opened;
    assert(files.get(static_cast<s32>(reply[0]), opened));
    assert(fs.readAll(opened.node) == filtered);
    arena = false;
    assert(files.handle(0, request.data(), static_cast<u32>(request.size()), reply, sizeof(reply)) == 8);
    FileService::Open campaign;
    assert(files.get(static_cast<s32>(reply[0]), campaign));
    assert(fs.readAll(campaign.node) == original);
    assert(fs.readAll(opened.node) == filtered); // switching mode cannot change an already-open handle
    auto broken = node;
    broken.mem = std::make_shared<const Bytes>(Bytes{1, 2, 3});
    broken.size = 3;
    assert(!filterArenaLevel(fs, broken, error) && !error.empty());
    if (argc > 1) {
        IsoFs disc;
        assert(disc.openIso(argv[1], &error));
        disc.setLanguageRoot("ENGLISH");
        for (const char* level :
             {"Level 1A - Surdana", "Level 1D - City of the Damned", "Level 1E - Inquisitor's Lair"}) {
            const std::string file = "ENGLISH\\Levels\\" + std::string(level) + "\\" + level + ".LVL";
            IsoFs::Node real;
            assert(disc.lookup(file, real));
            if (!filterArenaLevel(disc, real, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            std::cout << level << ": " << get16(entry(disc.readAll(real), 0x20), 0)
                      << " environment/player/camera actors retained\n";
        }
    }
}
