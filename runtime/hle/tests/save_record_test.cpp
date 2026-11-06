// save_record_test: pack build ids and the save load policy (SaveRecord.h, docs/reference/modding.md "Saves and pack
// builds"). Synthetic packs and records only; no game data.
#include "hle/ModPack.h"
#include "hle/SaveRecord.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace hle;
using namespace hle::mods;
namespace fs = std::filesystem;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                      \
        }                                                                                                              \
    } while (0)

static void writeFile(const fs::path& p, const std::vector<u8>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}
static std::vector<u8> bytes(const std::string& s) {
    return {s.begin(), s.end()};
}
static void p16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x));
    v.push_back(static_cast<u8>(x >> 8));
}
static void p32(std::vector<u8>& v, u32 x) {
    p16(v, x & 0xFFFF);
    p16(v, x >> 16);
}
// Stored (uncompressed) zip: what the build id reads is the central directory's size and CRC.
static void writeZip(const fs::path& p, const std::vector<std::pair<std::string, std::vector<u8>>>& es) {
    std::vector<u8> out, cd;
    for (const auto& [name, data] : es) {
        const u32 lho = static_cast<u32>(out.size()), crc = crc32Of(data.data(), data.size());
        for (std::vector<u8>* v : {&out, &cd}) {
            const bool central = v == &cd;
            p32(*v, central ? 0x02014b50u : 0x04034b50u);
            if (central)
                p16(*v, 20);
            p16(*v, 20);
            p16(*v, 0);
            p16(*v, 0);
            p32(*v, 0);
            p32(*v, crc);
            p32(*v, static_cast<u32>(data.size()));
            p32(*v, static_cast<u32>(data.size()));
            p16(*v, static_cast<u32>(name.size()));
            p16(*v, 0);
            if (central) {
                p16(*v, 0);
                p16(*v, 0);
                p16(*v, 0);
                p32(*v, 0);
                p32(*v, lho);
            }
            v->insert(v->end(), name.begin(), name.end());
            if (!central)
                v->insert(v->end(), data.begin(), data.end());
        }
    }
    const u32 cdOff = static_cast<u32>(out.size());
    out.insert(out.end(), cd.begin(), cd.end());
    p32(out, 0x06054b50);
    p16(out, 0);
    p16(out, 0);
    p16(out, static_cast<u32>(es.size()));
    p16(out, static_cast<u32>(es.size()));
    p32(out, static_cast<u32>(cd.size()));
    p32(out, cdOff);
    p16(out, 0);
    writeFile(p, out);
}

// The pack of editor/packages/formats/test/packbuild.test.ts ("matches the runtime vector").
static std::vector<std::pair<std::string, std::vector<u8>>> vectorPack(const std::string& manifest) {
    std::vector<u8> lvl = bytes("SRSC");
    lvl.resize(16, 0);
    return {{"mod.toml", bytes(manifest)},
            {"overrides/INTERFCE.TXD/0040_97_0.bin", bytes("abc")},
            {"files/ENGLISH/LEVELS/LEVEL9AP/LEVEL9AP.LVL", lvl},
            {"new/RESOURES.ODB/0103_7_0.bin", {1, 2}},
            {"previews/x.png", bytes("png")}};
}
static const char* kVector = "8d3872dfe99031fa";

static std::string manifest(const std::string& id, const std::string& extra = "") {
    return "id = \"" + id + "\"\nname = \"Pack " + id + "\"\nversion = \"1.2\"\n" + extra +
           "[target]\nprofile = \"pal\"\n";
}
static const ModStatus* statusOf(const ModManager& mm, const std::string& key) {
    for (const ModStatus& s : mm.status())
        if (s.key == key)
            return &s;
    return nullptr;
}

static ActivePacks active(std::initializer_list<PackStamp> packs) {
    ActivePacks a;
    a.packs = packs;
    return a;
}

int main() {
    setVerbose(false);
    const DiscProfile pal{"pal", profileHash("SCES-50006", "BESCES-50006")};
    const fs::path root = fs::temp_directory_path() / "drakan_save_record_test";
    fs::remove_all(root);

    // ---- the content hash (same algorithm and vector as the editor)
    {
        CHECK(crc32Of(reinterpret_cast<const u8*>("abc"), 3) == 891568578u);
        std::vector<ContentItem> items;
        for (const auto& [path, data] : vectorPack("id = \"x\"\n"))
            items.push_back({path, data.size(), crc32Of(data.data(), data.size())});
        CHECK(hex64(packContentHash(items)) == kVector);
        std::vector<ContentItem> reordered(items.rbegin(), items.rend());
        for (ContentItem& i : reordered)
            for (char& c : i.path)
                c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        CHECK(hex64(packContentHash(reordered)) == kVector); // order, case and separators do not matter
        CHECK(hex64(packContentHash({})) == "cbf29ce484222325");
        CHECK(isContentPath("files/A.LVL") && isContentPath("Overrides\\X\\0040_1_0.bin") && isContentPath("new/a"));
        CHECK(!isContentPath("mod.toml") && !isContentPath("previews/a.png") && !isContentPath("maps/a.ini") &&
              !isContentPath("files/") && !isContentPath("ui/menus.ini"));
        items[1].crc ^= 1;
        CHECK(hex64(packContentHash(items)) != kVector);
    }

    // ---- the manifest key
    {
        Manifest m;
        std::string err;
        CHECK(parseManifest(manifest("a", "build = \"0123456789abcdef\"\n"), false, m, err) &&
              m.build == "0123456789abcdef");
        CHECK(parseManifest(manifest("a"), false, m, err) && m.build.empty());
        CHECK(parseManifest("{\"id\":\"j\",\"build\":\"r-2\",\"target\":{\"profile\":\"pal\"}}", true, m, err) &&
              m.build == "r-2");
        CHECK(!parseManifest(manifest("a", "build = \"has space\"\n"), false, m, err) &&
              err.find("build") != std::string::npos);
    }

    // ---- ModManager: declared build, computed build (zip directory and folder agree with the editor), no content
    {
        const fs::path mods = root / "mods";
        writeZip(mods / "computed.zip", vectorPack(manifest("computed")));
        writeZip(mods / "declared.zip", vectorPack(manifest("declared", "build = \"pinned-1\"\n")));
        for (const auto& [path, data] : vectorPack(manifest("folder")))
            writeFile(mods / "folder" / path, data);
        writeFile(mods / "maponly/mod.toml", bytes(manifest("maponly")));
        writeFile(mods / "maponly/maps/a.ini", bytes("[map]\n"));
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"computed", true}, {"declared", true}, {"folder", true}, {"maponly", true}}) ==
              4);
        const ModStatus *c = statusOf(mm, "computed"), *d = statusOf(mm, "declared"), *f = statusOf(mm, "folder"),
                        *o = statusOf(mm, "maponly");
        CHECK(c && c->content && !c->buildDeclared && c->build == kVector);
        CHECK(d && d->content && d->buildDeclared && d->build == "pinned-1");
        CHECK(f && f->content && !f->buildDeclared && f->build == kVector);
        CHECK(o && !o->content);
    }

    // ---- record text
    {
        const std::vector<PackStamp> packs{{"ootf", "Drakan: Order of the Flame", "1.0", "0123456789abcdef"},
                                           {"tex.pack", "Line\nbreak", "2", "b-2"}};
        const std::string text = formatSaveRecord(packs);
        CHECK(text.find("format = 1") != std::string::npos);
        std::vector<PackStamp> back;
        CHECK(parseSaveRecord(text, back) && back.size() == 2);
        CHECK(back.size() == 2 && back[0] == packs[0] && back[1].name == "Line break" && back[1].build == "b-2");
        CHECK(parseSaveRecord("format = 1\n", back) && back.empty());
        CHECK(parseSaveRecord("format = 1\r\n[pack a]\r\nbuild = x\r\n", back) && back.size() == 1 &&
              back[0].build == "x");
        CHECK(!parseSaveRecord("", back));                                 // no format
        CHECK(!parseSaveRecord("format = 2\n", back));                     // newer format
        CHECK(!parseSaveRecord("format = 1\n[pack a]\nname = A\n", back)); // no build
        CHECK(!parseSaveRecord("format = 1\n[pack ]\nbuild = x\n", back)); // no id
        CHECK(!parseSaveRecord("format = 1\n[other]\nbuild = x\n", back)); // unknown section
        CHECK(!parseSaveRecord("format = 1\n[pack a]\nbuild\n", back));    // not key = value
    }

    // ---- the load policy
    {
        const PackStamp ootf{"ootf", "Order of the Flame", "1.0", "aaaa"},
            ootfNew{"ootf", "Order of the Flame", "1.1", "bbbb"};
        const PackStamp tex{"tex", "Textures", "1", "t1"};
        SaveCheckInput save;
        save.hasRecord = true;
        save.record = {ootf};
        save.level = "OotF 11 - Volcanic";
        // Same build: loads, also with packs added since.
        CHECK(checkSave(save, active({ootf})).ok());
        CHECK(checkSave(save, active({tex, ootf})).ok());
        // Another build of the pack: refused, names the installed pack.
        SaveCheck r = checkSave(save, active({ootfNew}));
        CHECK(r.verdict == SaveVerdict::DifferentBuild && r.pack.version == "1.1");
        // The pack is gone: refused, names the recorded pack.
        r = checkSave(save, active({tex}));
        CHECK(r.verdict == SaveVerdict::MissingPack && r.pack.name == "Order of the Flame");
        CHECK(checkSave(save, active({})).verdict == SaveVerdict::MissingPack);
        // Every recorded pack counts.
        save.record = {tex, ootf};
        CHECK(checkSave(save, active({ootf})).verdict == SaveVerdict::MissingPack);
        CHECK(checkSave(save, active({ootf, tex})).ok());
        // An unreadable record never loads.
        save.recordReadable = false;
        CHECK(checkSave(save, active({ootf, tex})).verdict == SaveVerdict::BadRecord);

        // No record: stock saves and saves made without content packs load, with or without packs.
        SaveCheckInput stock;
        stock.level = "Level 1A - Surdana";
        ActivePacks withLevels = active({ootfNew});
        withLevels.levels = {{"OotF 11 - Volcanic", "ootf", true}};
        CHECK(checkSave(stock, active({})).ok());
        CHECK(checkSave(stock, withLevels).ok());
        // No record and the level comes from a pack that declares a build: made by an earlier game version with an
        // unknown build of that pack (the old Volcanic sanctum quicksave), refused.
        SaveCheckInput legacy;
        legacy.level = "ootf 11 - volcanic"; // level titles compare without case
        r = checkSave(legacy, withLevels);
        CHECK(r.verdict == SaveVerdict::OlderThanPack && r.pack.id == "ootf" && r.pack.name == "Order of the Flame");
        // ... but a pack without a declared build cannot tell: such saves keep loading as before.
        withLevels.levels[0].buildDeclared = false;
        CHECK(checkSave(legacy, withLevels).ok());

        // The record written now: the active content packs.
        CHECK(recordFor(active({ootf, tex})) == (std::vector<PackStamp>{ootf, tex}));
        CHECK(recordFor(active({})).empty());
        CHECK(std::string(saveVerdictName(SaveVerdict::DifferentBuild)) == "different build");
    }

    fs::remove_all(root);
    printf("save_record_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
