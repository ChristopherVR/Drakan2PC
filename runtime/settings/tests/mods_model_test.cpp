// ModsModel (list, toggle, reorder, mods.ini write, conflict summaries) and the Mods page of the schema.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gameui/MenuText.h"
#include "settings/ModDiscovery.h"
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

    // ---- discovery policy (ModDiscovery.h): new packs are offered, never enabled by themselves
    {
        ModInfo fresh = mk("fresh", false, ModBoot::Disabled);
        fresh.isNew = true;
        ModInfo camp = mk("camp", false, ModBoot::Disabled);
        camp.isNew = true;
        camp.campaign = "test-valley";
        ModInfo gone = mk("gone", false, ModBoot::Skipped); // listed as new, then removed from the folder
        gone.isNew = true;
        gone.found = false;
        ModsModel d;
        d.setSnapshot(dir.string(), {mk("a", true, ModBoot::Active), fresh, camp, gone}, {});
        CHECK(d.undecided().size() == 2 && d.undecided()[0]->key == "fresh" && d.undecided()[1]->key == "camp");
        CHECK(d.stateText(*d.find("fresh")) == "New: not enabled yet");
        CHECK(!d.find("fresh")->enabled && !d.restartRequired()); // discovery alone changes nothing
        const ModNoticeContent nc = modNoticeContent(d);
        CHECK(nc.names.size() == 2 && nc.names[0] == "Pack fresh" && nc.campaigns == 1);

        // when the notice shows
        CHECK(showModNotice(d, ModNoticeMode::Auto, false, false));
        CHECK(!showModNotice(d, ModNoticeMode::Auto, true, false)); // scripted input (harness)
        CHECK(showModNotice(d, ModNoticeMode::Force, true, false));
        CHECK(!showModNotice(d, ModNoticeMode::Off, false, false));
        CHECK(!showModNotice(d, ModNoticeMode::Force, false, true)); // once per run
        CHECK(modNoticeModeFrom(nullptr) == ModNoticeMode::Auto && modNoticeModeFrom("") == ModNoticeMode::Auto);
        CHECK(modNoticeModeFrom("0") == ModNoticeMode::Off && modNoticeModeFrom("OFF") == ModNoticeMode::Off);
        CHECK(modNoticeModeFrom("1") == ModNoticeMode::Force && modNoticeModeFrom("maybe") == ModNoticeMode::Auto);

        // undecided packs stay `new` in mods.ini; decided ones get on / off
        fs::remove(dir / "mods.ini");
        CHECK(d.save(&err));
        CHECK(slurp(dir / "mods.ini").find("a = on\nfresh = new\ncamp = new\ngone = new\n") != std::string::npos);
        ModsModel keep = d;
        CHECK(keep.keepUndecidedOff() == 2 && keep.undecided().empty());
        CHECK(!showModNotice(keep, ModNoticeMode::Force, false, false));
        CHECK(keep.iniText("").find("fresh = off\ncamp = off\ngone = new\n") != std::string::npos);
        CHECK(keep.stateText(*keep.find("fresh")) == "Disabled" && keep.find("fresh")->isNew); // still marked
        ModsModel en = d;
        CHECK(en.enableUndecided() == 2 && en.undecided().empty());
        CHECK(en.find("fresh")->enabled && en.find("camp")->enabled && !en.find("gone")->enabled);
        CHECK(en.restartRequired() && en.stateText(*en.find("camp")) == "Enabled, loads after restart");
        CHECK(en.iniText("").find("fresh = on\ncamp = on\n") != std::string::npos);
        ModsModel one = d; // toggling one pack on the Mods page decides that pack only
        CHECK(one.apply("mods.enable.fresh", "0", &err));
        CHECK(one.undecided().size() == 1 && one.undecided()[0]->key == "camp");
        CHECK(slurp(dir / "mods.ini").find("fresh = off\ncamp = new\n") != std::string::npos);

        // the Mods page marks new packs and a pending restart
        const gameui::SettingPage pg = buildModsPage(en);
        bool sawNew = false, sawPending = false, sawNewRow = false, sawCampaign = false;
        for (const auto& sec : pg.sections) {
            sawNew = sawNew || sec.title.find("[New]") != std::string::npos;
            sawPending = sawPending || sec.title.find("[Restart pending]") != std::string::npos;
            for (const auto& r : sec.rows) {
                sawNewRow = sawNewRow || r.label == "New";
                sawCampaign = sawCampaign || (r.label == "Campaign" && r.text.find("Restart") == 0);
            }
        }
        CHECK(sawNew && sawPending && sawNewRow && sawCampaign);
        CHECK(pg.description.find("Restart pending") == 0);

        // why an enabled campaign is not on the game card
        GameChoiceState gs;
        CHECK(gameChoiceNote(d, gs).reason == GameChoiceReason::None); // the campaign pack is off: nothing to say
        GameChoiceNote n = gameChoiceNote(en, gs);
        CHECK(n.reason == GameChoiceReason::PendingRestart && n.pack == "Pack camp");
        CHECK(gameChoiceNoteText(n, false) ==
              "Pack camp is enabled: restart the game to choose it on the title screen.");

        ModInfo live = mk("live", true, ModBoot::Active);
        live.id = "live.pack";
        live.campaign = "valley";
        ModsModel lm;
        lm.setSnapshot(dir.string(), {live}, {});
        gs.cardCampaigns = {"valley"};
        gs.offered = true;
        CHECK(gameChoiceNote(lm, gs).reason == GameChoiceReason::None); // offered: fine
        gs.offered = false;
        gs.setting = "d2";
        n = gameChoiceNote(lm, gs);
        CHECK(n.reason == GameChoiceReason::Pinned && n.setting == "d2" && n.pack == "Pack live");
        CHECK(gameChoiceNoteText(n, false) ==
              "Choose your game is not shown: drakan.ini sets game = d2. Set game = ask to choose Pack live.");
        gs.settingFromEnv = true;
        CHECK(gameChoiceNoteText(gameChoiceNote(lm, gs), false).find("DRAKAN_GAME is set to d2") != std::string::npos);
        gs = {};
        gs.refused["live.pack"] = "campaign start level 'X' is not one of its levels";
        n = gameChoiceNote(lm, gs);
        CHECK(n.reason == GameChoiceReason::Rejected && n.detail.find("start level") != std::string::npos);
        CHECK(gameChoiceNoteText(n, false).find("Reason: campaign start level") != std::string::npos);
        CHECK(gameChoiceNoteText(n, true).find("Reason") == std::string::npos); // the title line stays short
        // A second pack with a campaign id that is already on the card is refused: explained, not "on the card".
        {
            ModInfo first = live, second = mk("second", true, ModBoot::Active);
            second.id = "second.pack";
            second.campaign = "valley";
            ModsModel dm;
            dm.setSnapshot(dir.string(), {first, second}, {});
            GameChoiceState ds;
            ds.offered = true;
            ds.cardCampaigns = {"valley"};
            ds.refused["second.pack"] = "campaign 'valley' is already given by another pack";
            CHECK(campaignOffered(first, ds) && !campaignOffered(second, ds));
            n = gameChoiceNote(dm, ds);
            CHECK(n.reason == GameChoiceReason::Rejected && n.pack == "Pack second" &&
                  n.detail.find("already given") != std::string::npos);
            dm.setGameChoice(ds);
            int onCardRows = 0, notOffered = 0;
            for (const auto& sec : buildModsPage(dm).sections)
                for (const auto& r : sec.rows)
                    if (r.label == "Campaign") {
                        onCardRows += r.text.rfind("On the title", 0) == 0;
                        notOffered += r.text.rfind("Not offered", 0) == 0;
                    }
            CHECK(onCardRows == 1 && notOffered == 1);
        }
        ModInfo bad = mk("bad", true, ModBoot::Skipped, "built for disc usa, this is pal");
        bad.campaign = "valley";
        lm.setSnapshot(dir.string(), {bad}, {});
        n = gameChoiceNote(lm, gs);
        CHECK(n.reason == GameChoiceReason::Skipped && n.detail == "built for disc usa, this is pal");
        lm.setGameChoice(gs);
        bool sawChoice = false, sawReason = false, reasonInline = false;
        for (const auto& sec : buildModsPage(lm).sections)
            for (const auto& r : sec.rows) {
                sawChoice =
                    sawChoice || (r.label == "Game choice" && r.text.find("was not loaded") != std::string::npos);
                reasonInline = reasonInline || (r.label == "Game choice" && r.text.find("Reason") != std::string::npos);
                sawReason = sawReason || (r.label == "Reason" && r.text == "built for disc usa, this is pal");
            }
        CHECK(sawChoice && sawReason && !reasonInline); // the reason has its own row: one-line rows cut long text
    }

    // ---- unreadable packs (a broken zip or manifest): named on the title, not offered on the new packs card
    {
        auto broken = [](const char* key, bool on, bool isNew) {
            ModInfo m;
            m.key = m.name = key;
            m.file = std::string(key) + ".zip";
            m.enabled = m.bootEnabled = on;
            m.boot = on ? ModBoot::Skipped : ModBoot::Disabled;
            m.message = on ? "invalid pack: not a zip (too small)" : "new pack, not enabled yet";
            m.readError = "not a zip (too small)";
            m.isNew = isNew;
            return m;
        };
        ModInfo fresh = mk("fresh", false, ModBoot::Disabled);
        fresh.isNew = true;
        ModsModel u;
        u.setSnapshot(dir.string(), {broken("newbad", false, true), fresh}, {});
        // the new broken pack is not undecided: the card offers only the readable one, and enabling skips it
        CHECK(u.undecided().size() == 1 && u.undecided()[0]->key == "fresh");
        CHECK(modNoticeContent(u).names.size() == 1);
        CHECK(u.stateText(*u.find("newbad")) == "Disabled, cannot be read: not a zip (too small)");
        CHECK(unreadablePacks(u).size() == 1 && unreadablePacks(u)[0]->key == "newbad");
        CHECK(unreadablePacksText(u, false) == "A pack in the mods folder could not be read: newbad.zip.");
        ModsModel en = u;
        CHECK(en.enableUndecided() == 1 && !en.find("newbad")->enabled);
        // a broken pack alone does not open the card, but it is still named
        ModsModel only;
        only.setSnapshot(dir.string(), {broken("newbad", false, true)}, {});
        CHECK(!showModNotice(only, ModNoticeMode::Force, false, false) && !unreadablePacksText(only, false).empty());

        // enabled broken packs are named too (the loader skipped them); one turned off is not
        ModsModel many;
        many.setSnapshot(dir.string(),
                         {broken("on1", true, false), broken("off1", false, false), broken("new2", false, true)}, {});
        CHECK(unreadablePacks(many).size() == 2);
        CHECK(unreadablePacksText(many, false) == "2 packs in the mods folder could not be read: on1.zip, new2.zip.");
        CHECK(many.apply("mods.enable.on1", "0", &err)); // the player turns it off: no longer named
        CHECK(many.apply("mods.enable.new2", "0", &err));
        CHECK(unreadablePacks(many).empty() && unreadablePacksText(many, false).empty());
        // a missing pack is not a read error; a readable pack has none
        ModInfo gone = broken("gone", true, false);
        gone.found = false;
        many.setSnapshot(dir.string(), {gone, fresh}, {});
        CHECK(unreadablePacks(many).empty());

        // translated on the title screen; the Mods page has its own row
        gameui::setMenuLanguage("german");
        CHECK(unreadablePacksText(u, true) == "Ein Paket im Mod-Ordner konnte nicht gelesen werden: newbad.zip.");
        gameui::setMenuLanguage("english");
        bool sawRow = false;
        for (const auto& sec : buildModsPage(u).sections)
            for (const auto& r : sec.rows)
                sawRow = sawRow || (r.label == "Unreadable" && r.text.find("newbad.zip") != std::string::npos);
        CHECK(sawRow);
    }

    fs::remove_all(dir);
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
