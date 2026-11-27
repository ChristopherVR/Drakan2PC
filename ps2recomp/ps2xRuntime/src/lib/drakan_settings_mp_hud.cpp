// Drakan: the in-game multiplayer HUD: scoreboard held on F4 (the Tab key is the inventory), chat on F6, kill feed,
// co-op party panel, the results screen with its buttons and map vote.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
// ---- in-game multiplayer HUD: scoreboard held on F4 (the Tab key is the inventory), chat on F6, kill-feed hooks ----

struct MpHud {
    bool chatOpen = false;
    std::string chatInput;
    bool f6Was = false;
    gameui::DrawList last;
};
MpHud g_mpHud;
} // namespace

// The chat line is open: the screen layer takes the window messages (WM_CHAR is only fed to ImGui then).
std::atomic<bool> g_mpChatOpen{false};

// An active match in the Playing phase: the HUD draws (and the screen layer wakes up for it).
bool mpHudActive() {
    (void)mpResultsUp();        // keeps the input hold in step with the results phase, drawn or not
    static bool played = false; // this process was in a match; the lobby phase that follows is shown in the level
    if (!drakan_netplay::enabled())
        return false;
    const drakan_netplay::State s = drakan_netplay::state();
    if (!s.inSession || s.conn != drakan_netplay::Conn::Connected) {
        played = false;
        return false;
    }
    if (s.phase == drakan_netplay::Phase::Playing)
        played = true;
    else if (s.phase == drakan_netplay::Phase::Loading)
        played = false;
    return s.phase == drakan_netplay::Phase::Playing || s.phase == drakan_netplay::Phase::Results ||
           (s.phase == drakan_netplay::Phase::Lobby && played);
}

// The results screen is up (the match is over and the session is still connected): the overlay takes the input and the
// buttons work. Rynn is held (the guest input is neutral) while it is.
bool mpResultsUp() {
    const drakan_netplay::State s = drakan_netplay::state();
    const bool up =
        s.inSession && s.conn == drakan_netplay::Conn::Connected && s.phase == drakan_netplay::Phase::Results;
    static bool was = false; // the single owner of the guest input hold: set with the results, released with them
    if (up != was) {
        was = up;
        drakan_input::module().setHostScreen(up);
    }
    return up;
}

namespace {
// DRAKAN_MP_CAPTURE=1 only: capture the current view for a missing map thumbnail. Moving the live player to the
// authored preview point would send that movement to the other clients and return the player to a different spawn.
// Off by default: the current view is whatever the player looks at, and the menu kept showing it as the map's picture.
bool mpPreviewCapture(const drakan_netplay::State& ns, const gameui::mp::HudState& hs) {
    static int hiding = 0;           // frames still drawn without the HUD
    static std::string requestedFor; // the map a capture was asked for
    static double playingSince = -1, lastPhaseAt = 0;
    const bool off = !mapAutoCapture(); // a current-view capture is a random frame, not a map picture (issue #1)
    const double now = ImGui::GetTime();
    if (hiding > 0) {
        --hiding;
        return true;
    }
    if (off || ns.phase != drakan_netplay::Phase::Playing || ns.level.empty() || hs.scoreboard || hs.chatOpen ||
        !hs.alive) {
        playingSince = -1;
        return false;
    }
    if (now - lastPhaseAt > 1.0 || playingSince < 0)
        playingSince = now;
    lastPhaseAt = now;
    const bool arena = drakan_arena::active() && ns.mode != 2;
    if (now - playingSince < (arena ? 12.0 : 9.0) || requestedFor == ns.level || mapPreviewId(ns.level) != 0)
        return false;
    requestedFor = ns.level;
    const std::filesystem::path dir = capturedPreviewDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string file = (dir / (mapSafeName(ns.level) + ".png")).string();
    std::fprintf(stderr, "[menu-screens] map picture: capturing %s (current view)\n", file.c_str());
    gsr::requestWindowShot(file);
    hiding = 4;
    return true;
}

constexpr const char* kTeamNames[4] = {"Fire", "Water", "Earth", "Air"};
} // namespace

void mpHudDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    MpHud& h = g_mpHud;
    TabUi& tu = g_tabUi;
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    ImGuiIO& io = ImGui::GetIO();
    // The global key state only counts while this window has the focus (two instances on one PC share the keyboard).
    const bool f6 = ImGui::IsKeyDown(ImGuiKey_F6) || (drakan_win::focused() && drakan_win::keyDown(0x75 /* VK_F6 */));
    if (f6 && !h.f6Was && !h.chatOpen) {
        h.chatOpen = true;
        g_mpChatOpen.store(true);
        h.chatInput.clear();
        drakan_input::module().setSuspended(true); // typed letters must not drive Rynn
    }
    h.f6Was = f6;
    if (h.chatOpen) {
        static int dbgFrames = 0;
        if (std::getenv("DRAKAN_NET_CHAT_DEBUG") && (io.InputQueueCharacters.Size > 0 || (++dbgFrames % 90) == 0))
            std::fprintf(stderr, "[chat] input=%d chars=%d enter=%d focused=%d\n", f.input ? 1 : 0,
                         io.InputQueueCharacters.Size, ImGui::IsKeyPressed(ImGuiKey_Enter, false) ? 1 : 0,
                         drakan_win::focused() ? 1 : 0);
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const ImWchar c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c < 0x7f && h.chatInput.size() < 120)
                h.chatInput.push_back(static_cast<char>(c));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !h.chatInput.empty())
            h.chatInput.pop_back();
        const bool enter =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (enter || esc) {
            if (enter && !h.chatInput.empty())
                drakan_netplay::sendChat(h.chatInput);
            h.chatOpen = false;
            g_mpChatOpen.store(false);
            h.chatInput.clear();
            drakan_input::module().setSuspended(false);
        }
    }
    const drakan_netplay::State ns = drakan_netplay::state();
    const drakan_netplay::Scoreboard sb = drakan_netplay::scoreboard(); // rows, kill feed, limits, results
    gameui::mp::HudState hs;
    // DRAKAN_NET_SCOREBOARD=<seconds> (test aid, scripted input cannot press F4): show it from that many seconds after
    // the match started playing.
    static const double forceAfter = [] {
        const char* e = std::getenv("DRAKAN_NET_SCOREBOARD");
        return e && *e ? std::atof(e) : -1.0;
    }();
    static double playingSince = -1;
    if (playingSince < 0)
        playingSince = ImGui::GetTime();
    hs.scoreboard = ImGui::IsKeyDown(ImGuiKey_F4) || (drakan_win::focused() && drakan_win::keyDown(0x73 /* VK_F4 */)) ||
                    (forceAfter >= 0 && ImGui::GetTime() - playingSince >= forceAfter);
    hs.chatOpen = h.chatOpen;
    hs.chatInput = h.chatInput;
    hs.serverName = ns.serverName;
    hs.level = ns.level;
    hs.mode = sb.mode == 2   ? gameui::mp::GameMode::Coop
              : sb.mode == 1 ? gameui::mp::GameMode::TeamDeathmatch
                             : gameui::mp::GameMode::Deathmatch;
    hs.fragLimit = sb.fragLimit;
    hs.timeLeftSec = sb.timeLeftSec;
    hs.nextMatchSec = sb.nextMatchSec;
    hs.teamKills[0] = sb.teamKills[0];
    hs.teamKills[1] = sb.teamKills[1];
    hs.now = sb.now;
    // The standings come from the scoreboard (kills / deaths as the server scored them, most kills first).
    hs.scoreMode = !sb.modeId.empty();
    const drakan::net::ModeDef* hudMode = hs.scoreMode ? drakan_modes::customModes().find(sb.modeId) : nullptr;
    if (hudMode) {
        hs.modeName = hudMode->name;
        hs.goal = drakan::net::modeGoal(*hudMode);
        hs.livesMode = hudMode->lives > 0;
        for (size_t t = 0; t < 4; ++t) {
            if (t < hudMode->teamNames.size())
                hs.teamNames[t] = hudMode->teamNames[t];
            if (t < hudMode->teamColors.size())
                hs.teamColors[t] = static_cast<int>(hudMode->teamColors[t]);
        }
    }
    const auto hudTeamName = [&](int t) {
        return t >= 0 && t < 4 && !hs.teamNames[t].empty() ? hs.teamNames[t]
                                                           : std::string(t >= 0 && t < 4 ? kTeamNames[t] : "?");
    };
    hs.teamCount = std::clamp(sb.teamCount, 2, 4);
    for (int t = 0; t < 4; ++t)
        hs.teamScore[t] = sb.teamScore[t];
    for (const auto& r : sb.rows) {
        gameui::mp::RosterRow row;
        row.id = r.id;
        row.name = r.name;
        row.team = r.team;
        row.self = r.self;
        row.kills = r.kills;
        row.deaths = r.deaths;
        row.score = r.score;
        row.pingMs = r.pingMs;
        row.timeSec = r.timeSec;
        row.lives = r.lives;
        row.eliminated = r.eliminated;
        hs.roster.push_back(std::move(row));
    }
    // OotF has no results screen: a banner with the winner while the server shows the results phase, the final
    // standings stay up under it, then the lobby returns.
    if (sb.phase == drakan_netplay::Phase::Results) {
        hs.results = true;
        std::string who;
        if (sb.tie)
            who = "Draw";
        else if (sb.mode == 1)
            who = "Team " + hudTeamName(sb.winnerTeam) + " wins";
        else
            who = (sb.winnerName.empty() ? std::string("?") : sb.winnerName) + " wins";
        hs.banner = std::string("Match over - ") + who + " (" + sb.reason + ")";
        if (sb.reason == "last one standing" && !sb.tie) // the standings alone cannot tell: say what the match decided
            hs.winnerText =
                sb.mode == 1 ? "Team " + hudTeamName(sb.winnerTeam) + " is the last team standing"
                             : (sb.winnerName.empty() ? std::string("?") : sb.winnerName) + " is the last one standing";
        hs.resultsText = "Choose what happens next";
    } else if (sb.phase == drakan_netplay::Phase::Lobby) {
        hs.banner = "Match finished";
        hs.resultsText = "Esc > Quit to Title returns to the lobby for the next match";
    } else {
        std::string st;
        if (sb.timeLeftSec >= 0) {
            char t[24];
            std::snprintf(t, sizeof t, "%d:%02d left", sb.timeLeftSec / 60, sb.timeLeftSec % 60);
            st = t;
        }
        // A custom mode: its goal in words (a mode with lives: "Last one standing" and the lives left, the full goal is
        // on the scoreboard).
        if (hudMode && hs.livesMode)
            st +=
                std::string(st.empty() ? "" : "   ") +
                (hudMode->teamCount() >= 2 ? "Last team standing" : "Last one standing") +
                (sb.selfLives >= 0 && !sb.selfEliminated ? "   Lives " + std::to_string(sb.selfLives) : std::string());
        else if (hudMode)
            st += std::string(st.empty() ? "" : "   ") + hs.goal;
        else if (sb.fragLimit > 0)
            st += std::string(st.empty() ? "" : "   ") + "First to " + std::to_string(sb.fragLimit);
        hs.status = st;
    }
    if (hs.mode == gameui::mp::GameMode::Coop) { // the party panel: the other players of the session
        bool complete = false;
        const std::vector<drakan_netplay::CoopPartnerInfo> party = drakan_netplay::coopParty(complete);
        hs.levelComplete = complete;
        for (const auto& pl : ns.roster) {
            if (pl.self)
                continue;
            gameui::mp::HudPartner q;
            q.id = pl.id;
            q.name = pl.name;
            q.character = drakan_coop_character::entry(static_cast<uint16_t>(pl.character)).name;
            q.hpKnown = false; // set from the Vitals message below (drakan_coop publishes it with setCoopParty)
            for (const auto& info : party)
                if (info.player == pl.id) {
                    q.hpKnown = info.hpKnown;
                    q.hpFrac = info.hpFrac;
                    q.hp = info.hp;
                    q.hpMax = info.hpMax;
                    q.dragonKnown = info.dragonKnown;
                    q.dragonFrac = info.dragonFrac;
                    q.down = info.down;
                    q.respawnInSec = info.respawnInSec;
                }
            hs.partners.push_back(std::move(q));
        }
        hs.banner.clear();
    }
    hs.alive = sb.selfAlive || sb.respawnInSec <= 0.0; // the overlay only runs while the countdown does
    hs.eliminated = sb.selfEliminated && sb.phase == drakan_netplay::Phase::Playing; // out of lives: watching
    if (hs.eliminated)
        hs.alive = false;
    hs.respawnInSec = sb.respawnInSec;
    for (const auto& k : sb.killFeed) {
        gameui::mp::HudKill hk;
        hk.killer = k.killer == 0 ? std::string() : k.killerName;
        hk.victim = k.victimName;
        hk.weapon = k.weaponName;
        hk.at = k.at;
        for (const auto& r : sb.rows) {
            hk.selfKiller = hk.selfKiller || (r.self && r.id == k.killer);
            hk.selfVictim = hk.selfVictim || (r.self && r.id == k.victim);
        }
        if (hk.selfVictim)
            hs.deathText = k.killer == 0 ? std::string() : "Killed by " + k.killerName + " (" + k.weaponName + ")";
        hs.kills.push_back(std::move(hk));
    }
    for (const auto& c : drakan_netplay::chatLog())
        hs.chat.push_back({c.who, c.text, c.at});
    gameui::ScreenConfig sc;
    sc.measure = tu.renderer.measure();
    // The results buttons: the host chooses (back to the lobby, next map, rematch), everybody can leave. The overlay
    // owns the keyboard / pad / mouse while the results are up (the game input is held, so Rynn stands still).
    static gameui::mp::ResultsMenu resultsMenu;
    static bool resultsWas = false, resultsSent = false;
    gameui::UiInput resultsIn;
    {
        const bool up = hs.results && ns.conn == drakan_netplay::Conn::Connected;
        if (up != resultsWas) {
            resultsWas = up;
            resultsSent = false;
            resultsMenu.reset();
            tu.gate.reset();
        }
        if (up) {
            using gameui::mp::ResultsButton;
            using gameui::mp::ResultsButtonId;
            const bool host = sb.selfHost && !resultsSent;
            if (hs.mode == gameui::mp::GameMode::Coop)
                hs.resultsButtons = {{ResultsButtonId::BackToLobby, "Back to lobby", host},
                                     {ResultsButtonId::Rematch, hs.levelComplete ? "Play again" : "Try again", host},
                                     {ResultsButtonId::Leave, "Leave", true}};
            else
                hs.resultsButtons = {{ResultsButtonId::BackToLobby, "Back to lobby", host},
                                     {ResultsButtonId::NextMap, "Next map", host},
                                     {ResultsButtonId::Rematch, "Rematch", host},
                                     {ResultsButtonId::Leave, "Leave", true}};
            for (size_t i = 0; i < sb.voteNames.size(); ++i)
                hs.vote.push_back({sb.voteNames[i], mapPreviewId(sb.voteNames[i]), sb.voteTally[i]});
            hs.voteMine = sb.voteMine;
            hs.voteWinner = sb.voteWinner;
            hs.resultsNote = sb.selfHost ? (resultsSent ? "Waiting for the server ..." : std::string())
                                         : "Waiting for the host to choose";
            const bool take = f.input;
            resultsIn = hostScreenInput(take, false, f.w, f.h);
            static int dbg = 0;
            if (std::getenv("DRAKAN_MP_RESULTS_DEBUG") &&
                (++dbg % 60 == 0 || !resultsIn.keys.empty() || resultsIn.leftClick))
                std::fprintf(stderr, "[results] input take=%d pointer=%d (%.0f,%.0f) keys=%zu click=%d hostScreen=%d\n",
                             take ? 1 : 0, resultsIn.pointerValid ? 1 : 0, resultsIn.pointerX, resultsIn.pointerY,
                             resultsIn.keys.size(), resultsIn.leftClick ? 1 : 0,
                             drakan_input::module().hostScreen() ? 1 : 0);
        }
    }
    if (hs.scoreboard || hs.results || !hs.partners.empty())
        drakan_fpsmeter::setAvoidTop(true);
    // The first picture of an arena: a window capture a few seconds into a match on a map without one, taken with the
    // HUD hidden for those frames (previews/captured/<map>.png next to the exe, the player's own file).
    if (mpPreviewCapture(ns, hs)) {
        h.last = {};
        return;
    }
    if (!hs.vote.empty())
        updateMapPreviews(); // the candidates' pictures (ImageKind::LevelPreview)
    if (!hs.vote.empty() && hs.voteWinner < 0) {
        const int pick = gameui::mp::mapVoteFrame(hs.vote.size(), hs.voteMine, resultsIn, {0, 0, f.w, f.h}, sc);
        if (pick >= 0 && pick != hs.voteMine) {
            std::fprintf(stderr, "[results] vote for map %d (%s)\n", pick,
                         hs.vote[static_cast<size_t>(pick)].name.c_str());
            drakan_netplay::voteMap(pick);
            hs.voteMine = pick; // shown at once, the server's tally follows
        }
    }
    if (!hs.resultsButtons.empty()) {
        const int pressed = resultsMenu.frame(hs.resultsButtons, resultsIn, {0, 0, f.w, f.h}, sc);
        hs.resultsFocus = resultsMenu.focus();
        if (pressed >= 0) {
            using gameui::mp::ResultsButtonId;
            switch (hs.resultsButtons[static_cast<size_t>(pressed)].id) {
                case ResultsButtonId::BackToLobby:
                    resultsSent = true;
                    std::fprintf(stderr, "[results] Back to lobby\n");
                    drakan_netplay::resultsAction(1);
                    break;
                case ResultsButtonId::NextMap:
                    resultsSent = true;
                    std::fprintf(stderr, "[results] Next map\n");
                    drakan_netplay::resultsAction(2);
                    break;
                case ResultsButtonId::Rematch:
                    resultsSent = true;
                    std::fprintf(stderr, "[results] Rematch\n");
                    drakan_netplay::resultsAction(3);
                    break;
                case ResultsButtonId::Leave:
                    resultsSent = true;
                    std::fprintf(stderr, "[results] Leave\n");
                    drakan_netplay::leave();
                    drakan_netplay::requestReturnToTitle();
                    (void)mpResultsUp(); // the session is gone: the input hold ends now
                    break;
            }
        }
    }
    h.last = gameui::mp::drawHud(hs, {0, 0, f.w, f.h}, sc);
    drawHostList(f, h.last, ImGui::GetTime(), 1.0f, &previewCrop); // the map vote's pictures
    if (!hs.resultsButtons.empty() && resultsIn.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse &&
        f.input)
        dsettings::drawMenuCursor(resultsIn.pointerX, resultsIn.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
