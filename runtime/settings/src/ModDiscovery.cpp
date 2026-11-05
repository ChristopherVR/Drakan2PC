#include "settings/ModDiscovery.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string_view>

#include "gameui/MenuText.h"

namespace dsettings {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string displayName(const ModInfo& m) {
    return m.name.empty() ? m.key : m.name;
}

// {0} {1} substitution without translation (the English Mods page).
std::string formatPlain(std::string_view pattern, std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> args(arguments);
    std::string out;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' &&
            pattern[i + 1] <= '9') {
            const size_t index = static_cast<size_t>(pattern[i + 1] - '0');
            if (index < args.size())
                out += args[index];
            i += 2;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::string format(bool translate, std::string_view pattern, std::initializer_list<std::string_view> arguments) {
    return translate ? gameui::menuFormat(pattern, arguments) : formatPlain(pattern, arguments);
}

} // namespace

ModNoticeMode modNoticeModeFrom(const char* value) {
    if (!value || !*value)
        return ModNoticeMode::Auto;
    const std::string v = lower(value);
    if (v == "0" || v == "off" || v == "no" || v == "false")
        return ModNoticeMode::Off;
    if (v == "1" || v == "on" || v == "yes" || v == "true")
        return ModNoticeMode::Force;
    return ModNoticeMode::Auto;
}

bool showModNotice(const ModsModel& model, ModNoticeMode mode, bool scriptedInput, bool shownThisRun) {
    if (shownThisRun || mode == ModNoticeMode::Off)
        return false;
    if (mode == ModNoticeMode::Auto && scriptedInput)
        return false;
    return !model.undecided().empty();
}

ModNoticeContent modNoticeContent(const ModsModel& model) {
    ModNoticeContent c;
    for (const ModInfo* m : model.undecided()) {
        c.names.push_back(displayName(*m));
        if (!m->campaign.empty())
            ++c.campaigns;
    }
    return c;
}

bool campaignOffered(const ModInfo& m, const GameChoiceState& state) {
    if (m.campaign.empty() || state.refused.count(m.id))
        return false;
    return std::any_of(state.cardCampaigns.begin(), state.cardCampaigns.end(),
                       [&](const std::string& id) { return lower(id) == lower(m.campaign); });
}

GameChoiceNote gameChoiceNote(const ModsModel& model, const GameChoiceState& state) {
    GameChoiceNote n;
    // A pack's campaign is on the card when its id is there and the pack itself was not refused: a second pack with
    // an id that is already taken shares the id with the card but is not offered.
    auto onCard = [&](const ModInfo& m) { return campaignOffered(m, state); };
    // 1. Campaigns are installed and valid, but the setting picks the game without asking.
    if (!state.cardCampaigns.empty() && !state.offered) {
        n.reason = GameChoiceReason::Pinned;
        n.setting = state.setting;
        n.fromEnv = state.settingFromEnv;
        for (const ModInfo& m : model.mods())
            if (!m.campaign.empty() && m.bootEnabled && onCard(m)) {
                n.pack = displayName(m);
                break;
            }
        return n;
    }
    for (const ModInfo& m : model.mods()) {
        if (m.campaign.empty() || !m.enabled)
            continue;
        if (!m.bootEnabled) { // 2. turned on in this session
            n.reason = GameChoiceReason::PendingRestart;
            n.pack = displayName(m);
            return n;
        }
        if (m.boot == ModBoot::Skipped || !m.found) { // 3. enabled, not loaded
            n.reason = GameChoiceReason::Skipped;
            n.pack = displayName(m);
            n.detail = !m.found ? std::string("not found in the mods folder") : m.message;
            return n;
        }
        if (m.boot == ModBoot::Active && !onCard(m)) { // 4. loaded, campaign refused
            n.reason = GameChoiceReason::Rejected;
            n.pack = displayName(m);
            auto it = state.refused.find(m.id);
            n.detail = it == state.refused.end() ? std::string() : it->second;
            return n;
        }
    }
    return n;
}

std::string gameChoiceNoteText(const GameChoiceNote& n, bool translate) {
    switch (n.reason) {
        case GameChoiceReason::None:
            return {};
        case GameChoiceReason::Pinned:
            return n.fromEnv ? format(translate,
                                      "Choose your game is not shown: DRAKAN_GAME is set to {1}. Clear it to choose "
                                      "{0}.",
                                      {n.pack, n.setting})
                             : format(translate,
                                      "Choose your game is not shown: drakan.ini sets game = {1}. Set game = ask to "
                                      "choose {0}.",
                                      {n.pack, n.setting});
        case GameChoiceReason::PendingRestart:
            return format(translate, "{0} is enabled: restart the game to choose it on the title screen.", {n.pack});
        case GameChoiceReason::Skipped: {
            std::string s =
                format(translate, "{0} is enabled but was not loaded, so its campaign is not offered.", {n.pack});
            if (!translate && !n.detail.empty())
                s += " Reason: " + n.detail + ".";
            return s;
        }
        case GameChoiceReason::Rejected: {
            std::string s = format(translate, "{0} is loaded, but its campaign could not be offered.", {n.pack});
            if (!translate && !n.detail.empty())
                s += " Reason: " + n.detail + ".";
            return s;
        }
    }
    return {};
}

} // namespace dsettings
