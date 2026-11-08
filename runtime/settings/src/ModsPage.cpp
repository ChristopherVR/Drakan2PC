#include "settings/ModsPage.h"

#include <algorithm>
#include <string>

#include "settings/ModDiscovery.h"

namespace dsettings {

namespace {

gameui::SettingRow info(const std::string& label, const std::string& text) {
    gameui::SettingRow r;
    r.kind = gameui::SettingKind::Info;
    r.label = label;
    r.text = text;
    return r;
}

// One-line text for the rows that cannot wrap (the modern pages).
std::string shorten(const std::string& s, size_t n) {
    if (s.size() <= n)
        return s;
    return s.substr(0, n - 3) + "...";
}

gameui::SettingRow action(const std::string& key, const std::string& label, const std::string& help,
                          const std::string& choice, bool enabled) {
    gameui::SettingRow r;
    r.key = key;
    r.kind = gameui::SettingKind::Choice;
    r.label = label;
    r.help = help;
    r.choices = {{"go", choice, true}};
    r.current = -1;
    r.currentLabel = "Select";
    r.enabled = enabled;
    r.unavailable = "Not available on this platform.";
    return r;
}

} // namespace

gameui::SettingPage buildModsPage(const ModsModel& model) {
    gameui::SettingPage pg;
    pg.id = "mods";
    pg.title = "Mods";
    pg.description = "A mod pack changes the game: levels, textures, menus. Select a pack to see its picture and "
                     "details. 1. Turn it on. 2. Order the packs: a later one wins when two change the same file. "
                     "3. Restart the game to apply.";
    pg.note = "Packs load from top to bottom; a later pack wins when two change the same file. Every change is saved "
              "to mods/mods.ini at once and applies the next time the game starts.";

    const bool restart = model.restartRequired();
    if (restart)
        pg.description = "Restart pending: the mod list changed since the game started. Use Restart game now (or "
                         "close and start the game) to apply it. " +
                         pg.description;
    {
        gameui::SettingSection sec{"Mods", {}};
        // Always present so the rows below do not shift when the first change is made.
        sec.rows.push_back(info("Restart", restart ? "Required: mod changes apply when the game restarts."
                                                   : "Not needed: the list matches what is loaded."));
        sec.rows.push_back(action(kKeyModsRestart, "Restart game now",
                                  "Close the game and start it again so the new mod list takes effect.", "Restart now",
                                  restart));
        sec.rows.back().unavailable = "Nothing to apply: the mod list has not changed since the game started.";
        sec.rows.push_back(action(kKeyModsFolder, "Open mods folder",
                                  "Show the mods folder in the file browser: " + model.folder(), "Open folder",
                                  model.canOpenFolder()));
        sec.rows.push_back(info("Installed", std::to_string(model.size()) + (model.size() == 1 ? " pack" : " packs")));
        size_t fresh = 0;
        for (const ModInfo& m : model.mods())
            fresh += m.isNew ? 1 : 0;
        if (fresh)
            sec.rows.push_back(info("New", std::to_string(fresh) + (fresh == 1 ? " pack" : " packs") +
                                               " found since the last start. New packs stay off until enabled."));
        // Info rows are one line and cannot be focused for help, so the loader's reason gets its own row instead of
        // being cut off at the end of the Game choice line.
        GameChoiceNote note = gameChoiceNote(model, model.gameChoice());
        const std::string reason = note.detail;
        note.detail.clear();
        const std::string why = gameChoiceNoteText(note, false);
        if (!why.empty()) {
            sec.rows.push_back(info("Game choice", why));
            if (!reason.empty())
                sec.rows.push_back(info("Reason", reason));
        }
        const std::string unreadable = unreadablePacksText(model, false);
        if (!unreadable.empty())
            sec.rows.push_back(info("Unreadable", unreadable));
        const std::string missing = missingPacksText(model, false);
        if (!missing.empty())
            sec.rows.push_back(info("Missing", missing));
        pg.sections.push_back(std::move(sec));
    }
    if (model.size() == 0) {
        gameui::SettingSection sec{"No mods installed", {}};
        sec.rows.push_back(info("Add packs", "Copy a mod folder or .zip into the mods folder, then restart."));
        pg.sections.push_back(std::move(sec));
        return pg;
    }

    const int n = static_cast<int>(model.size());
    for (int i = 0; i < n; ++i) {
        const ModInfo& m = model.mods()[static_cast<size_t>(i)];
        std::string title = (m.name.empty() ? m.key : m.name) + (m.version.empty() ? "" : "  v" + m.version);
        if (m.isNew)
            title += "  [New]";
        if (m.enabled != m.bootEnabled)
            title += "  [Restart pending]";
        gameui::SettingSection sec{title, {}};
        const std::vector<std::string> conflicts = model.conflictLines(m.key);
        const std::string state = model.stateText(m);

        const std::string help = "Turn this pack on or off. The details beside the list say what it is and whether it "
                                 "overlaps another pack. Takes effect after a restart.";

        gameui::SettingRow en;
        en.key = std::string(kKeyModsEnable) + m.key;
        en.kind = gameui::SettingKind::Toggle;
        en.label = "Enabled";
        en.help = help;
        en.on = m.enabled;
        en.restart = true;
        en.currentValue = m.enabled ? "1" : "0";
        sec.rows.push_back(std::move(en));

        gameui::SettingRow ord;
        ord.key = std::string(kKeyModsOrder) + m.key;
        ord.kind = gameui::SettingKind::Choice;
        ord.label = "Load position";
        ord.help = "Where this pack sits in the load order. Move it down to let it win conflicts with the packs "
                   "above it. Left / Right changes it.";
        ord.restart = true;
        for (int p = 1; p <= n; ++p) {
            std::string label = std::to_string(p) + " of " + std::to_string(n);
            if (n > 1 && p == 1)
                label += " (first)";
            else if (n > 1 && p == n)
                label += " (last)";
            ord.choices.push_back({std::to_string(p), label, true});
        }
        ord.current = i;
        ord.currentValue = std::to_string(i + 1);
        sec.rows.push_back(std::move(ord));

        sec.rows.push_back(info("Status", state));
        if (!m.campaign.empty()) {
            const bool onCard = campaignOffered(m, model.gameChoice());
            sec.rows.push_back(info("Campaign", !m.enabled ? "Enable it and restart to play it from Choose your game."
                                                : !m.bootEnabled ? "Restart to play it from Choose your game."
                                                : onCard         ? "On the title's Choose your game screen."
                                                                 : "Not offered: see Game choice above and the log."));
        }
        for (const std::string& c : conflicts)
            sec.rows.push_back(info("Conflict", shorten(c, 64)));

        // What the help panel shows beside the list for this pack: the picture, the facts and what to do next.
        sec.picture = m.picture;
        if (!m.author.empty())
            sec.details.push_back({"By", m.author});
        sec.details.push_back({"Status", state});
        sec.details.push_back({"Target disc", m.target.empty() ? std::string("unknown") : m.target});
        if (!m.campaign.empty())
            sec.details.push_back({"Adds a game", "Campaign \"" + m.campaign + "\" on Choose your game"});
        if (!m.description.empty())
            sec.details.push_back({"About", m.description});
        for (const std::string& c : conflicts)
            sec.details.push_back({"Conflict", c});
        sec.hint = !m.found               ? "This pack is not in the mods folder any more."
                   : !m.readError.empty() ? "This pack cannot be read: " + m.readError
                   : m.enabled            ? "On. Select Enabled and press Enter to turn it off."
                                          : "Off. Select Enabled and press Enter to turn it on.";
        pg.sections.push_back(std::move(sec));
    }
    return pg;
}

} // namespace dsettings
