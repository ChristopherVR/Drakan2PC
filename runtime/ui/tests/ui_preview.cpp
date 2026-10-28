// ui_preview <iso-or-folder> <outdir> [drakan_input.ini]
// Generates the keyboard/mouse prompt textures from the real game data and writes before/after PNGs (upright, alpha
// applied over magenta).
#include "hle/IsoFs.h"
#include "ui/Prompts.h"
#include "ui/Txd.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace drakan_ui;

static std::vector<hle::u8> entry(hle::IsoFs& fs, const char* path, unsigned type, unsigned id) {
    hle::IsoFs::Node n;
    if (!fs.lookup(path, n))
        return {};
    auto d = fs.readAll(n);
    if (d.size() < 16)
        return {};
    uint32_t idx, cnt;
    std::memcpy(&idx, &d[6], 4);
    std::memcpy(&cnt, &d[10], 4);
    for (uint32_t i = 0; i < cnt; ++i) {
        const hle::u8* e = &d[idx + i * 14];
        uint16_t t, k;
        uint32_t off, sz;
        std::memcpy(&t, e, 2);
        std::memcpy(&k, e + 2, 2);
        std::memcpy(&off, e + 6, 4);
        std::memcpy(&sz, e + 10, 4);
        if (t == type && k == id)
            return std::vector<hle::u8>(d.begin() + off, d.begin() + off + sz);
    }
    return {};
}

static void save(const std::vector<hle::u8>& blob, const std::string& path, int scale) {
    TxdEdit t;
    if (!t.load(blob)) {
        std::printf("cannot decode %s\n", path.c_str());
        return;
    }
    auto px = toRgba(t);
    int w = t.width(), h = t.height();
    std::vector<Rgba> out((size_t)w * scale * h * scale);
    for (int y = 0; y < h * scale; ++y)
        for (int x = 0; x < w * scale; ++x) {
            Rgba c = px[(size_t)(y / scale) * w + x / scale];
            float a = std::min(1.0f, c.a / 128.0f);
            Rgba m{(hle::u8)(c.r * a + 40 * (1 - a)), (hle::u8)(c.g * a + 0 * (1 - a)),
                   (hle::u8)(c.b * a + 60 * (1 - a)), 0x80};
            out[(size_t)y * w * scale + x] = m;
        }
    writePng(path, w * scale, h * scale, out);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: ui_preview <iso|folder> <outdir> [ini]\n");
        return 2;
    }
    hle::IsoFs fs;
    std::string err;
    bool ok = std::filesystem::is_directory(argv[1]) ? fs.openFolder(argv[1], &err) : fs.openIso(argv[1], &err);
    if (!ok) {
        std::printf("cannot open %s: %s\n", argv[1], err.c_str());
        return 2;
    }
    fs.setLanguageRoot("\\ENGLISH");
    std::filesystem::create_directories(argv[2]);
    Bindings b = argc > 3 ? Bindings::load(argv[3]) : Bindings::defaults();
    std::string out = argv[2];
    const char* tx = "COMMON\\INTERFCE\\INTERFCE.TXD";
    struct J {
        unsigned id;
        int scale;
        std::vector<hle::u8> (*fn)(const std::vector<hle::u8>&, const Bindings&);
    };
    const J jobs[] = {{97, 3, PromptOverride::patchFont},
                      {124, 3, PromptOverride::patchControllerDiagram},
                      {94, 4, PromptOverride::patchPageTile}};
    for (const J& j : jobs) {
        auto orig = entry(fs, tx, 0x40, j.id);
        if (orig.empty()) {
            std::printf("tex %u missing\n", j.id);
            continue;
        }
        auto patched = j.fn(orig, b);
        std::printf("tex %u: %zu -> %zu bytes%s\n", j.id, orig.size(), patched.size(),
                    patched.empty() ? "  (FAILED)" : "");
        save(orig, out + "/tex" + std::to_string(j.id) + "_orig.png", j.scale);
        if (!patched.empty())
            save(patched, out + "/tex" + std::to_string(j.id) + "_new.png", j.scale);
    }
    if (argc > 4 && std::string(argv[4]) == "text") { // list every rewritten string of every ENGLISH\*.XDB
        for (auto& kv : fs.entries()) {
            const std::string& k = kv.first;
            if (k.size() < 4 || k.compare(k.size() - 4, 4, ".XDB") != 0 || k.rfind("ENGLISH\\", 0) != 0)
                continue;
            auto x = fs.readAll(kv.second);
            uint32_t xi, xc;
            std::memcpy(&xi, &x[6], 4);
            std::memcpy(&xc, &x[10], 4);
            std::string fname = k.substr(k.find_last_of('\\') + 1);
            for (uint32_t i = 0; i < xc; ++i) {
                const hle::u8* e = &x[xi + i * 14];
                uint16_t t, id;
                uint32_t off, sz;
                std::memcpy(&t, e, 2);
                std::memcpy(&id, e + 2, 2);
                std::memcpy(&off, e + 6, 4);
                std::memcpy(&sz, e + 10, 4);
                if (t != 0x400)
                    continue;
                std::vector<hle::u8> p(x.begin() + off, x.begin() + off + sz);
                std::vector<std::string> sl;
                if (!hle::xdb::decode(p, sl))
                    continue;
                for (int l = 0; l < 5; ++l) {
                    std::string tt = sl[l];
                    if (rewriteText(b, fname, id, l, tt))
                        std::printf("%s %u/%d: %s\n      -> %s\n", fname.c_str(), id, l, sl[l].c_str(), tt.c_str());
                }
            }
        }
        return 0;
    }
    // text samples
    const char* samples[][2] = {{"COMMON\\INTERFCE\\INTERFCE.XDB", ""}};
    (void)samples;
    std::string t;
    auto show = [&](const char* file, unsigned id, int lang, const char* in) {
        t = in;
        rewriteText(b, file, id, lang, t);
        std::printf("  [%s %u/%d] %s  ->  %s\n", file, id, lang, in, t.c_str());
    };
    show("INTERFCE.XDB", 73, 0, "Press \\033 to continue.");
    show("INTERFCE.XDB", 85, 0,
         "Press \\033 to load the last saved game.  Press the START button to go to the main menu.");
    show("INTERFCE.XDB", 148, 0, "Press L1 and R1 to access other sections of the Inventory.");
    show("LEVEL1ON.XDB", 25, 0, "Lock onto a target with the R2 button.");
    show("LEVEL1ON.XDB", 49, 0, "Access Rynn's inventory, journal, and map by pressing the SELECT.  ");
    show("LEVEL1ON.XDB", 22, 0, "Move Arokh with the left analog stick.  ");
    show("LEVEL1ON.XDB", 39, 1, "Mit der R1-Taste oder \\031 feuern Sie Arokhs Atemwaffe ab.");
    show("LEVEL1ON.XDB", 21, 2, "Appuyez sur Select pour ouvrir l'inventaire et prendre l'arc.");
    show("INTERFCE.XDB", 41, 0, "Select Skill");
    return 0;
}
