// ModsModel (list, toggle, reorder, mods.ini write, conflict summaries) and the Mods page of the schema.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "settings/ModsModel.h"
#include "settings/ModsPage.h"
#include "settings/Schema.h"

namespace {
int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

using namespace dsettings;

ModInfo mk(const char* key, bool on, ModBoot boot, const char* msg = "") {
    ModInfo m;
    m.key = key;
    m.name = std::string("Pack ") + key;
    m.version = "1.0";
    m.author = "Tester";
    m.description = "Does a thing.";
    m.target = "pal";
    m.hasManifest = true;
    m.enabled = m.bootEnabled = on;
    m.boot = boot;
    m.message = msg;
    return m;
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

ModsModel sample(const std::string& folder) {
    ModsModel m;
    m.setSnapshot(folder,
                  {mk("a", true, ModBoot::Active), mk("b", true, ModBoot::Active), mk("c", false, ModBoot::Disabled),
                   mk("usa", true, ModBoot::Skipped, "built for disc usa, this is pal")},
                  {{"INTERFCE.TXD 0040/97/0", "b", "a"}, {"INTERFCE.XDB text 156.en", "b", "a"}, {"X 1", "c", "b"}});
    return m;
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "drakan_mods_model_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    ModsModel m = sample(dir.string());
    CHECK(m.size() == 4 && !m.restartRequired());
    CHECK(m.stateText(*m.find("a")) == "Active");
    CHECK(m.stateText(*m.find("c")) == "Disabled");
    CHECK(m.stateText(*m.find("usa")).rfind("Skipped: built for disc usa", 0) == 0);

    // toggle
    CHECK(m.setEnabled("C", true)); // keys are case-insensitive
    CHECK(m.restartRequired());
    CHECK(m.stateText(*m.find("c")) == "Enabled, loads after restart");
    CHECK(m.setEnabled("c", false));
    CHECK(!m.restartRequired());
    CHECK(m.setEnabled("a", false));
    CHECK(m.stateText(*m.find("a")) == "Disabled, unloads after restart");
    CHECK(!m.setEnabled("nope", true));
    CHECK(m.setEnabled("a", true));
    CHECK(!m.restartRequired());

    // reorder
    CHECK(m.moveBy("a", 1) && m.indexOf("a") == 1 && m.indexOf("b") == 0);
    CHECK(m.restartRequired()); // two enabled packs swapped
    CHECK(m.moveBy("a", -1) && m.indexOf("a") == 0);
    CHECK(!m.restartRequired());
    CHECK(m.moveTo("a", 99) && m.indexOf("a") == 3);
    CHECK(m.moveTo("a", -5) && m.indexOf("a") == 0);
    CHECK(!m.moveBy("zzz", 1));

    // conflicts as they stood at start-up, per pack
    auto ca = m.conflictLines("a");
    CHECK(ca.size() == 1 && ca[0].find("Overridden by 'Pack b' on 2 entries (INTERFCE.TXD 0040/97/0") == 0);
    auto cb = m.conflictLines("b");
    CHECK(cb.size() == 2);
    CHECK(cb.size() == 2 &&
          cb[0] == "Overrides 'Pack a' on 2 entries (INTERFCE.TXD 0040/97/0, INTERFCE.XDB text 156.en)");
    CHECK(cb.size() == 2 && cb[1] == "Overridden by 'Pack c' on 1 entry (X 1)");
    CHECK(m.conflictLines("usa").empty());

    // ini write through apply(): fresh file gets the header, order and flags
    m = sample(dir.string());
    std::string err;
    CHECK(m.apply("mods.enable.c", "1", &err));
    CHECK(m.apply("mods.order.c", "1", &err));
    CHECK(m.apply("mods.enable.a", "0", &err));
    const std::string ini = slurp(dir / "mods.ini");
    CHECK(ini == "; Drakan mods. One pack per line (folder name or zip name without .zip). Top to bottom is\n"
                 "; the load order; later packs win conflicts. Set a pack to on to enable it, off to disable.\n"
                 "[mods]\nc = on\na = off\nb = on\nusa = on\n");
    CHECK(!fs::exists(dir / "mods.ini.tmp"));
    CHECK(m.restartRequired());
    CHECK(!m.apply("mods.enable.ghost", "1", &err) && !err.empty());
    CHECK(!m.apply("mods.bogus", "1", &err));

    // existing comments and the section header are kept, key lines rewritten
    {
        std::ofstream f(dir / "mods.ini", std::ios::binary);
        f << "; mine\r\n[mods]\r\n; note\r\nold = on\r\nb = off\r\n";
    }
    CHECK(m.save(&err));
    CHECK(slurp(dir / "mods.ini") == "; mine\n[mods]\n; note\nc = on\na = off\nb = on\nusa = on\n");

    // folder action
    int opened = 0;
    m.setOpenFolder([&](const std::string& f) {
        ++opened;
        return f == dir.string();
    });
    CHECK(m.canOpenFolder() && m.apply(kKeyModsFolder, "go") && opened == 1);

    // the schema page
    {
        ModsModel pm = sample(dir.string());
        pm.setOpenFolder([](const std::string&) { return true; });
        SchemaEnv env;
        CHECK(buildSettingsSchema(Settings{}, env).pages.size() == 4); // no mods: no page
        env.mods = &pm;
        const gameui::SettingsSchema sc = buildSettingsSchema(Settings{}, env);
        CHECK(sc.pages.size() == 5 && sc.pages[4].id == "mods");
        const gameui::SettingPage& pg = sc.pages[4];
        CHECK(sc.find("mods.enable.a") && sc.find("mods.enable.a")->on);
        CHECK(sc.find("mods.enable.c") && !sc.find("mods.enable.c")->on);
        const gameui::SettingRow* ord = sc.find("mods.order.b");
        CHECK(ord && ord->choices.size() == 4 && ord->current == 1 && ord->choices[1].value == "2");
        CHECK(sc.find("mods.folder") && sc.find("mods.folder")->enabled);
        CHECK(sc.find("mods.restart") && !sc.find("mods.restart")->enabled); // nothing changed yet
        bool sawConflict = false, sawSkip = false;
        for (const auto& sec : pg.sections)
            for (const auto& r : sec.rows) {
                sawConflict = sawConflict || (r.label == "Conflict" && r.text.find("Pack b") != std::string::npos);
                sawSkip = sawSkip || (r.label == "Status" && r.text.rfind("Skipped:", 0) == 0);
            }
        CHECK(sawConflict && sawSkip);
        pm.setEnabled("c", true);
        const gameui::SettingsSchema sc2 = buildSettingsSchema(Settings{}, env);
        CHECK(sc2.find("mods.restart") && sc2.find("mods.restart")->enabled);
        CHECK(sc2.find("mods.enable.c")->restart);

        ModsModel empty;
        empty.setSnapshot(dir.string(), {}, {});
        CHECK(buildModsPage(empty).sections.size() == 2);
    }

    fs::remove_all(dir);
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
