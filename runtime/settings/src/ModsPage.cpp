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
    pg.description = "Turn installed mod packs on or off and set their load order. Changes apply after a restart.";
    pg.note = "Packs load from top to bottom; a later pack wins when two change the same file. Every change is saved "
              "to mods/mods.ini at once and applies the next time the game starts.";

    const bool restart = model.restartRequired();
    if (restart)
        pg.description = "Restart pending: the mod list changed since the game started. Restart to apply it.";
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
        const std::string why = gameChoiceNoteText(gameChoiceNote(model, model.gameChoice()), false);
        if (!why.empty())
            sec.rows.push_back(info("Game choice", why));
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

        std::string help = m.description.empty() ? std::string("No description.") : m.description;
        if (!m.author.empty())
            help += " By " + m.author + ".";
        help += " Target disc: " + (m.target.empty() ? std::string("unknown") : m.target) + ". Status: " + state + ".";
        for (const std::string& c : conflicts)
            help += " " + c + ".";

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
            const auto& card = model.gameChoice().cardCampaigns;
            const bool onCard = std::find(card.begin(), card.end(), m.campaign) != card.end();
            sec.rows.push_back(info("Campaign", !m.enabled ? "Enable it and restart to play it from Choose your game."
                                                : !m.bootEnabled ? "Restart to play it from Choose your game."
                                                : onCard         ? "On the title's Choose your game screen."
                                                                 : "Not offered: see Game choice above and the log."));
        }
        if (!m.author.empty())
            sec.rows.push_back(info("Author", shorten(m.author, 48)));
        sec.rows.push_back(info("Target disc", shorten(m.target.empty() ? std::string("unknown") : m.target, 48)));
        if (!m.description.empty())
            sec.rows.push_back(info("About", shorten(m.description, 56)));
        for (const std::string& c : conflicts)
            sec.rows.push_back(info("Conflict", shorten(c, 64)));
        pg.sections.push_back(std::move(sec));
    }
    return pg;
}

} // namespace dsettings
