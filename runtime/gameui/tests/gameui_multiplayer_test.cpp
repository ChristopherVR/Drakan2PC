// MultiplayerScreen / drawHud: navigation, forms, text entry, the actions the glue receives. Headless, std only.
#include <cstdio>
#include <cstdlib>

#include "gameui/Multiplayer.h"
#include "gameui/TitleScreen.h"

using namespace gameui;
using namespace gameui::mp;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

namespace {

const Viewport kVp{0, 0, 1920, 1080};

UiInput key(UiKey k) {
    UiInput in;
    in.keys.push_back(k);
    return in;
}

struct Rig {
    MultiplayerScreen s;
    MpState st;
    MpFrameOutput last;
    std::vector<MpAction> actions;
    Rig() {
        st.defaultName = "Tester";
        s.open(st);
    }
    void send(const UiInput& in) {
        last = s.frame(st, in, kVp);
        for (auto& a : last.actions)
            actions.push_back(a);
    }
    void press(UiKey k) { send(key(k)); }
    int rowOf(RowId id) const {
        for (size_t i = 0; i < s.rows().size(); ++i)
            if (s.rows()[i].id == id)
                return static_cast<int>(i);
        return -1;
    }
    void focusRow(RowId id) {
        for (int guard = 0; guard < 40 && s.rows()[static_cast<size_t>(s.focus())].id != id; ++guard)
            press(UiKey::Down);
        for (int guard = 0; guard < 40 && s.rows()[static_cast<size_t>(s.focus())].id != id; ++guard)
            press(UiKey::Up);
        if (s.rows()[static_cast<size_t>(s.focus())].id != id)
            std::fprintf(stderr, "focusRow(%d): stuck at %d page %d\n", int(id), int(focused().id), int(s.page()));
        CHECK(s.rows()[static_cast<size_t>(s.focus())].id == id);
    }
    const Row& focused() const { return s.rows()[static_cast<size_t>(s.focus())]; }
};

LanServer lan(const char* name, bool pw = false) {
    LanServer l;
    l.host = "192.168.1.7";
    l.port = 47800;
    l.name = name;
    l.level = "Level 1B - Kragmor";
    l.players = 2;
    l.maxPlayers = 8;
    l.passwordProtected = pw;
    return l;
}

} // namespace

int main() {
    // The Multiplayer page is OotF's player setup screen: name, colour, skins, connection speed, Host / Join buttons.
    {
        Rig r;
        CHECK(r.s.page() == MpPage::Menu && r.s.rows().size() == 9);
        const RowId order[] = {RowId::MenuName,       RowId::MenuColour, RowId::MenuHumanSkin,
                               RowId::MenuDragonSkin, RowId::MenuSpeed,  RowId::MenuHost,
                               RowId::MenuJoin,       RowId::MenuMaster, RowId::MenuBack};
        for (size_t i = 0; i < 9; ++i)
            CHECK(r.s.rows()[i].id == order[i]);
        CHECK(!r.s.rows()[7].enabled); // mplayer.com is gone
        CHECK(r.s.setup().name == "Tester");
        CHECK(r.last.draw.size() == 0); // nothing before the first frame
        r.press(UiKey::Down);
        CHECK(r.focused().id == RowId::MenuColour);
        CHECK(r.last.draw.size() > 10);
        r.press(UiKey::Right);
        CHECK(r.s.setup().colour == 1);
        r.focusRow(RowId::MenuSpeed);
        CHECK(r.focused().value == "LAN / T1 1M");
        r.press(UiKey::Left);
        CHECK(r.s.setup().speed == 3);
        // Typing the name.
        r.focusRow(RowId::MenuName);
        r.s.setup().name.clear();
        r.press(UiKey::Confirm);
        UiInput t;
        t.text = "Aria";
        r.send(t);
        CHECK(r.last.wantsText && r.s.setup().name == "Aria");
        r.press(UiKey::Confirm);
        // Back saves the profile and closes the Multiplayer page.
        r.actions.clear();
        r.press(UiKey::Back);
        CHECK(r.last.wantsClose && r.actions.front().kind == MpActionKind::SaveProfile &&
              r.actions.front().setup.name == "Aria" && r.actions.back().kind == MpActionKind::Close);
    }

    // Unavailable networking greys Host / Join; the profile controls still work.
    {
        Rig r;
        r.st.available = false;
        r.st.unavailableWhy = "no UDP";
        r.press(UiKey::Down);
        CHECK(!r.s.rows()[5].enabled && !r.s.rows()[6].enabled);
        CHECK(r.focused().id == RowId::MenuColour);
    }

    // Host form in OotF's order and value lists.
    {
        Rig r;
        r.focusRow(RowId::MenuHost);
        r.press(UiKey::Confirm);
        CHECK(r.s.page() == MpPage::Host);
        const RowId order[] = {RowId::HostName,  RowId::HostPassword, RowId::HostGroup,    RowId::HostLevel,
                               RowId::HostType,  RowId::HostMode,     RowId::HostMax,      RowId::HostFrag,
                               RowId::HostTime,  RowId::HostTeams,    RowId::HostFriendly, RowId::HostRegister,
                               RowId::HostStart, RowId::HostBack};
        CHECK(r.s.rows().size() == 14);
        for (size_t i = 0; i < 14; ++i)
            CHECK(r.s.rows()[i].id == order[i]);
        CHECK(!r.s.rows()[5].enabled);                             // Game Mode: no custom mode fits this map
        CHECK(!r.s.rows()[10].enabled && !r.s.rows()[11].enabled); // friendly fire / master servers: placeholders
        // Game Type: Deathmatch <-> Co-op. Co-op greys the limits and the teams and hosts with the Coop mode.
        r.focusRow(RowId::HostType);
        CHECK(r.focused().value == "Deathmatch" && !r.s.host().coop);
        r.press(UiKey::Right);
        r.focusRow(RowId::HostType);
        CHECK(r.focused().value == "Co-op" && r.s.host().coop && r.s.host().mode == GameMode::Coop);
        for (RowId id : {RowId::HostFrag, RowId::HostTime, RowId::HostTeams})
            CHECK(!r.s.rows()[static_cast<size_t>(r.rowOf(id))].enabled);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled);
        // Begin Game in co-op hosts with the Coop mode.
        {
            Rig c;
            c.focusRow(RowId::MenuHost);
            c.press(UiKey::Confirm);
            c.focusRow(RowId::HostType);
            c.press(UiKey::Right);
            c.focusRow(RowId::HostStart);
            c.press(UiKey::Confirm);
            CHECK(!c.actions.empty() && c.actions.back().kind == MpActionKind::HostMatch &&
                  c.actions.back().host.mode == GameMode::Coop);
        }
        r.focusRow(RowId::HostType);
        r.press(UiKey::Right);
        CHECK(!r.s.host().coop && r.s.host().mode == GameMode::Deathmatch);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostFrag))].enabled);
        // Teams: No Teams, 2, then 3 and 4 shown but not startable.
        r.focusRow(RowId::HostTeams);
        CHECK(r.focused().value == "No Teams");
        r.press(UiKey::Right);
        CHECK(r.s.host().teams == 2 && r.s.host().mode == GameMode::TeamDeathmatch);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled);
        r.press(UiKey::Right);
        CHECK(r.s.host().teams == 3 && r.focused().value.find("coming") != std::string::npos);
        CHECK(!r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled);
        r.press(UiKey::Right);
        r.press(UiKey::Right); // stops at 4
        CHECK(r.s.host().teams == 4);
        r.press(UiKey::Left);
        r.press(UiKey::Left);
        r.press(UiKey::Left);
        CHECK(r.s.host().teams == 0 && r.s.host().mode == GameMode::Deathmatch);
        // Max players 2..12.
        r.focusRow(RowId::HostMax);
        for (int i = 0; i < 14; ++i)
            r.press(UiKey::Right);
        CHECK(r.s.host().maxPlayers == 12);
        for (int i = 0; i < 14; ++i)
            r.press(UiKey::Left);
        CHECK(r.s.host().maxPlayers == 2);
        // Kills limit: No Limit, 5, 10, 15, 20, 25, 30, 40 ... 200; time: No Limit, 5 ... 45, 60.
        r.focusRow(RowId::HostFrag);
        CHECK(r.focused().value == "20");
        r.press(UiKey::Right);
        CHECK(r.s.host().fragLimit == 25);
        for (int i = 0; i < 20; ++i)
            r.press(UiKey::Right);
        CHECK(r.s.host().fragLimit == 200);
        for (int i = 0; i < 20; ++i)
            r.press(UiKey::Left);
        CHECK(r.s.host().fragLimit == 0 && r.focused().value == "No Limit");
        r.focusRow(RowId::HostTime);
        r.press(UiKey::Right);
        CHECK(r.s.host().timeLimitMin == 15);
        for (int i = 0; i < 9; ++i)
            r.press(UiKey::Right);
        CHECK(r.s.host().timeLimitMin == 60);
        // Level: the disc's levels, walking wraps.
        r.focusRow(RowId::HostLevel);
        r.press(UiKey::Left);
        CHECK(r.s.host().level == static_cast<int>(vanillaLevels().size()) - 1);
        r.press(UiKey::Right);
        r.press(UiKey::Right);
        CHECK(r.s.host().level == 1);
        // Server name and password typing.
        r.focusRow(RowId::HostName);
        r.s.host().serverName.clear();
        r.press(UiKey::Confirm);
        UiInput t;
        t.text = "LAN Party";
        r.send(t);
        UiInput bs;
        bs.backspaces = 2;
        r.send(bs);
        r.press(UiKey::Confirm);
        CHECK(!r.s.editing() && r.s.host().serverName == "LAN Par");
        r.focusRow(RowId::HostPassword);
        CHECK(r.focused().value == "open");
        r.press(UiKey::Confirm);
        UiInput pw;
        pw.text = "sw0rd";
        r.send(pw);
        r.press(UiKey::Back); // Back ends editing, does not leave the page
        CHECK(!r.s.editing() && r.s.page() == MpPage::Host && r.s.host().password == "sw0rd");
        // Begin Game sends the form and moves to the lobby.
        r.focusRow(RowId::HostStart);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].kind == MpActionKind::HostMatch);
        CHECK(r.actions[0].host.serverName == "LAN Par" && r.actions[0].host.password == "sw0rd" &&
              r.actions[0].host.maxPlayers == 2 && r.actions[0].host.fragLimit == 0 &&
              r.actions[0].host.timeLimitMin == 60 && r.actions[0].level == vanillaLevels()[1].name);
        CHECK(r.actions[0].setup.name == "Tester");
        CHECK(r.s.page() == MpPage::Lobby);
    }

    // Custom maps from mods: the Level Group splits All / disc / custom, custom entries are marked.
    {
        Rig r;
        r.st.levels = vanillaLevels();
        r.st.levels.push_back({"Level 9X - Arena", "9X  Arena", true});
        r.focusRow(RowId::MenuHost);
        r.press(UiKey::Confirm);
        r.focusRow(RowId::HostLevel);
        r.press(UiKey::Left); // All Levels: wraps to the custom map
        CHECK(r.focused().value.find("Arena") != std::string::npos &&
              r.focused().value.find("custom") != std::string::npos);
        r.focusRow(RowId::HostGroup);
        r.press(UiKey::Right); // disc levels only
        CHECK(r.focused().value == "Disc levels");
        r.focusRow(RowId::HostLevel);
        r.press(UiKey::Left);
        CHECK(r.focused().value.find("Arena") == std::string::npos);
        r.focusRow(RowId::HostGroup);
        r.press(UiKey::Right); // custom levels only
        r.focusRow(RowId::HostLevel);
        CHECK(r.focused().value.find("Arena") != std::string::npos);
        r.focusRow(RowId::HostStart);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions[0].level == "Level 9X - Arena");
        // No custom levels installed: the custom group is empty and cannot start.
        Rig e;
        e.focusRow(RowId::MenuHost);
        e.press(UiKey::Confirm);
        e.focusRow(RowId::HostGroup);
        e.press(UiKey::Right);
        e.press(UiKey::Right);
        CHECK(!e.s.rows()[static_cast<size_t>(e.rowOf(RowId::HostStart))].enabled);
    }

    // Join: the table sorted by ping, scrolling, a row joins, Add Server, Server Info, Connect.
    {
        Rig r;
        for (int i = 0; i < 9; ++i) {
            LanServer s = lan(("Srv" + std::to_string(i)).c_str(), i == 3);
            s.pingMs = 90 - i * 10; // Srv8 is the fastest
            r.st.lan.push_back(s);
        }
        r.focusRow(RowId::MenuJoin);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.s.page() == MpPage::Join);
        bool refreshed = false;
        for (const MpAction& a : r.actions)
            refreshed = refreshed || a.kind == MpActionKind::RefreshLan;
        CHECK(refreshed);
        CHECK(r.rowOf(RowId::JoinHeader) >= 0 && r.rowOf(RowId::JoinServer) >= 0);
        r.focusRow(RowId::JoinServer);
        CHECK(r.focused().label == "Srv8"); // lowest ping first
        for (int i = 0; i < 8; ++i)
            r.press(UiKey::Down); // walks through the whole table, scrolling the window
        CHECK(r.focused().id == RowId::JoinServer && r.focused().index == 8 && r.focused().label == "Srv0");
        r.press(UiKey::Up);
        CHECK(r.focused().index == 7);
        r.press(UiKey::Down);
        r.press(UiKey::Down); // leaves the list for Refresh
        CHECK(r.focused().id == RowId::JoinRefresh);
        r.press(UiKey::Up);
        CHECK(r.focused().id == RowId::JoinServer && r.focused().index == 8);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].kind == MpActionKind::JoinMatch &&
              r.actions[0].address == "192.168.1.7:47800");
        CHECK(r.s.page() == MpPage::Lobby);
        // A rejection returns to the Join page.
        r.st.conn = Conn::Rejected;
        r.st.message = "wrong password";
        r.press(UiKey::Up);
        CHECK(r.s.page() == MpPage::Join);
    }
    {
        Rig r;
        r.focusRow(RowId::MenuJoin);
        r.press(UiKey::Confirm);
        CHECK(r.rowOf(RowId::JoinServer) < 0 && !r.s.rows()[static_cast<size_t>(r.rowOf(RowId::JoinConnect))].enabled);
        // Add Server: an address entry, OK keeps it as a "+" row.
        r.focusRow(RowId::JoinAdd);
        r.press(UiKey::Confirm);
        CHECK(r.s.editing() && r.focused().id == RowId::JoinNewAddress);
        UiInput clear;
        clear.backspaces = 30;
        r.send(clear);
        UiInput t;
        t.text = "10.0.0.5:47801";
        r.send(t);
        r.press(UiKey::Confirm);
        CHECK(!r.s.editing() && r.s.customServers().size() == 1 && r.s.customServers()[0] == "10.0.0.5:47801");
        CHECK(r.rowOf(RowId::JoinAdd) >= 0 && r.rowOf(RowId::JoinNewAddress) < 0);
        r.focusRow(RowId::JoinServer);
        CHECK(r.focused().label == "+10.0.0.5:47801");
        // Cancelling an entry drops it.
        r.focusRow(RowId::JoinAdd);
        r.press(UiKey::Confirm);
        r.press(UiKey::Back);
        CHECK(r.s.customServers().size() == 1 && r.rowOf(RowId::JoinNewAddress) < 0);
        // Server Info toggles, Password, Connect joins the selected entry.
        r.focusRow(RowId::JoinInfo);
        r.press(UiKey::Confirm);
        CHECK(r.s.showingInfo());
        r.press(UiKey::Confirm);
        CHECK(!r.s.showingInfo());
        r.focusRow(RowId::JoinPassword);
        r.press(UiKey::Confirm);
        UiInput p;
        p.text = "pw";
        r.send(p);
        r.press(UiKey::Confirm);
        r.focusRow(RowId::JoinConnect);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].address == "10.0.0.5:47801" && r.actions[0].password == "pw");
    }

    // Gamepad text entry: Up cycles the last character, Right appends, Left deletes.
    {
        Rig r;
        r.s.setup().name = "Ry";
        r.press(UiKey::Confirm); // Player Name is the first row
        UiInput pad = key(UiKey::Right);
        pad.device = InputDevice::Gamepad;
        r.send(pad);
        CHECK(r.s.setup().name == "RyA");
        pad = key(UiKey::Up);
        pad.device = InputDevice::Gamepad;
        r.send(pad);
        CHECK(r.s.setup().name == "RyB");
        pad = key(UiKey::Left);
        pad.device = InputDevice::Gamepad;
        r.send(pad);
        CHECK(r.s.setup().name == "Ry");
        r.press(UiKey::Confirm);
    }

    // Lobby: roster, ready toggle, host-only start, leave.
    {
        Rig r;
        r.st.conn = Conn::Connected;
        r.st.hosting = true;
        r.st.serverName = "Keep";
        r.st.level = "Level 2 - Shadowmire";
        r.st.maxPlayers = 8;
        r.st.roster = {{1, "Tester", -1, false, true, true, 0, 0}, {2, "Other", -1, true, false, false, 0, 0}};
        r.s.setPage(MpPage::Lobby, r.st);
        CHECK(r.rowOf(RowId::LobbyStart) >= 0);
        r.focusRow(RowId::LobbyReady);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].kind == MpActionKind::SetReady && r.actions[0].flag);
        r.st.selfReady = true;
        r.press(UiKey::Confirm);
        CHECK(!r.actions.back().flag);
        r.focusRow(RowId::LobbyStart);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].kind == MpActionKind::StartMatch);
        // While the match loads the start button is greyed and a card is drawn.
        r.st.phase = MatchPhaseView::Loading;
        r.press(UiKey::Up);
        CHECK(!r.s.rows()[static_cast<size_t>(r.rowOf(RowId::LobbyStart))].enabled);
        r.focusRow(RowId::LobbyLeave);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.actions.size() == 1 && r.actions[0].kind == MpActionKind::Leave && r.s.page() == MpPage::Menu);
        // A client has no Start row.
        Rig c;
        c.st.conn = Conn::Connected;
        c.st.hosting = false;
        c.s.setPage(MpPage::Lobby, c.st);
        CHECK(c.rowOf(RowId::LobbyStart) < 0 && c.rowOf(RowId::LobbyLeave) >= 0);
    }

    // Focus follows the row when the lobby changes under it (a player joins, the page was entered while connecting).
    {
        Rig r;
        r.st.conn = Conn::Connecting;
        r.st.hosting = true;
        r.s.setPage(MpPage::Lobby, r.st);
        r.press(UiKey::Up);
        CHECK(r.focused().id == RowId::LobbyLeave); // nothing else is enabled yet
        r.st.conn = Conn::Connected;
        r.st.roster = {{1, "Me", -1, false, true, true, 0, 0}};
        r.press(UiKey::Up);
        CHECK(r.focused().id == RowId::LobbyReady); // the lobby came up: focus starts on Ready
        r.press(UiKey::Down);
        CHECK(r.focused().id == RowId::LobbyStart);
        r.st.roster.push_back({2, "Other", -1, false, false, false, 0, 0});
        r.press(UiKey::Left); // a new player row shifts every index: the focus stays on Start Match
        CHECK(r.focused().id == RowId::LobbyStart);
    }

    // Mouse: hover focuses, a click activates, right click goes back.
    {
        Rig r;
        r.press(UiKey::Up);
        const Row row1 = r.s.rows()[static_cast<size_t>(r.rowOf(RowId::MenuJoin))];
        UiInput m;
        m.pointerValid = true;
        m.pointerMoved = true;
        m.pointerX = row1.rect.cx();
        m.pointerY = row1.rect.cy();
        r.send(m);
        CHECK(r.focused().id == RowId::MenuJoin);
        m.leftClick = true;
        r.send(m);
        CHECK(r.s.page() == MpPage::Join);
        UiInput rc;
        rc.rightClick = true;
        r.send(rc);
        CHECK(r.s.page() == MpPage::Menu);
    }

    // HUD: the scoreboard sorts by kills, then deaths; chat lines fade out.
    {
        std::vector<RosterRow> rows = {{1, "A", -1, false, false, false, 2, 5},
                                       {2, "B", -1, false, true, false, 7, 1},
                                       {3, "C", -1, false, false, false, 2, 3}};
        const std::vector<RosterRow> s = sortedRoster(rows);
        CHECK(s[0].id == 2 && s[1].id == 3 && s[2].id == 1);
        HudState hs;
        hs.roster = rows;
        hs.now = 100;
        hs.chat = {{"A", "old", 80}, {"B", "new", 99}};
        CHECK(drawHud(hs, kVp, {}).size() > 0); // the chat line "new" only
        hs.chat = {{"A", "old", 80}};
        CHECK(drawHud(hs, kVp, {}).size() == 0);
        hs.scoreboard = true;
        CHECK(drawHud(hs, kVp, {}).size() > 20);
        hs.scoreboard = false;
        hs.chatOpen = true;
        hs.chatInput = "hi";
        CHECK(drawHud(hs, kVp, {}).size() > 3);
    }

    // HUD: kill feed lines fade out, the death overlay shows while dead, results keep the standings up, the limits
    // chip.
    {
        HudState hs;
        hs.now = 100;
        CHECK(drawHud(hs, kVp, {}).size() == 0);
        hs.kills = {{"A", "B", "Rynn's Dagger", 98, true, false}};
        const size_t withKill = drawHud(hs, kVp, {}).size();
        CHECK(withKill >= 2); // backdrop, outline (own kill) and text
        hs.kills[0].at = 80;  // older than the feed's life
        CHECK(drawHud(hs, kVp, {}).size() == 0);
        hs.alive = false;
        hs.respawnInSec = 2.2;
        hs.deathText = "Killed by A (Rynn's Dagger)";
        const size_t dead = drawHud(hs, kVp, {}).size();
        CHECK(dead >= 4);
        hs.alive = true;
        hs.status = "3:42 left   First to 3";
        CHECK(drawHud(hs, kVp, {}).size() > 0);
        hs.status.clear();
        hs.roster = {{1, "A", -1, false, true, false, 3, 0}, {2, "B", -1, false, false, false, 0, 3}};
        hs.results = true;
        hs.banner = "Match over - A wins (frag limit)";
        const size_t results = drawHud(hs, kVp, {}).size();
        CHECK(results > 15); // standings plus banner without F4 held
        hs.alive = false;    // no death overlay under the results
        CHECK(drawHud(hs, kVp, {}).size() == results);
    }

    // Multiplayer maps (arenas): the Host list uses st.arenas for matches, the campaign levels for Co-op and as the
    // fallback; the preview model follows the selection; a map that lacks the chosen mode cannot start.
    {
        Rig r;
        const auto arena = [](const char* name, unsigned modes, unsigned id, bool custom = false) {
            LevelEntry e;
            e.name = name;
            e.title = name;
            e.arena = true;
            e.modes = modes;
            e.maxPlayers = 6;
            e.spawns = 8;
            e.previewId = id;
            e.custom = custom;
            return e;
        };
        r.st.arenas = {arena("Meadow", 0, 11), arena("Duel Pit", 1u << static_cast<unsigned>(GameMode::Deathmatch), 12),
                       arena("Mod Hall", 0, 0, true)};
        r.s.setPage(MpPage::Host, r.st);
        CHECK(r.s.hostMap(r.st).name == "Meadow" && r.s.hostMode() == GameMode::Deathmatch);
        r.focusRow(RowId::HostGroup);
        CHECK(r.focused().value == "All Maps");
        r.press(UiKey::Right);
        CHECK(r.focused().value == "Arenas");
        r.press(UiKey::Left);
        r.focusRow(RowId::HostLevel);
        r.press(UiKey::Right);
        CHECK(r.s.hostMap(r.st).name == "Duel Pit");
        // The preview request list names the pictures of the visible maps.
        const std::vector<uint32_t> ids = r.s.previewRequests(r.st);
        CHECK(ids.size() == 2 && ids[0] == 11 && ids[1] == 12);
        // Team Deathmatch is not offered on "Duel Pit": Begin Game is greyed until another map is picked.
        CHECK(r.s.selectMode(GameMode::TeamDeathmatch, r.st));
        CHECK(r.s.host().teams == 2 && r.s.hostMode() == GameMode::TeamDeathmatch);
        r.s.host().level = 1;
        r.press(UiKey::Down); // rebuild
        CHECK(!r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled);
        r.s.host().level = 0;
        r.press(UiKey::Down);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled);
        // Unavailable modes ignore the card click.
        CHECK(!r.s.selectMode(GameMode::DragonsDuel, r.st) && r.s.hostMode() == GameMode::TeamDeathmatch);
        // Co-op plays campaign levels: the list switches to them.
        CHECK(r.s.selectMode(GameMode::Coop, r.st));
        CHECK(r.s.hostMap(r.st).name == "Level 1A - Surdana");
        CHECK(r.s.selectMode(GameMode::Deathmatch, r.st) && r.s.host().teams == 0 &&
              r.s.hostMap(r.st).name == "Meadow");
        // Clicking a mode card with the mouse (Team Deathmatch is the second card).
        UiInput click;
        click.pointerValid = true;
        click.pointerMoved = true;
        click.leftClick = true;
        click.pointerX = 520;
        click.pointerY = 200;
        r.send(click);
        CHECK(r.s.hostMode() == GameMode::TeamDeathmatch);
        // Clicking the second map of the list selects it (list rows are 58 design units apart from y = 476).
        UiInput pick;
        pick.pointerValid = true;
        pick.pointerMoved = true;
        pick.leftClick = true;
        pick.pointerX = 300;
        pick.pointerY = 476 + 58 + 20;
        r.send(pick);
        CHECK(r.s.host().level == 1);
        // Without arenas the campaign levels are the fallback and the Level Group names stay the old ones.
        Rig f;
        f.s.setPage(MpPage::Host, f.st);
        CHECK(f.s.hostMap(f.st).name == "Level 1A - Surdana");
        f.focusRow(RowId::HostGroup);
        f.press(UiKey::Right);
        CHECK(f.focused().value == "Disc levels");
    }

    // Lobby: ready states in the roster, chat sends a SendChat action, the countdown draws, team rows go in columns.
    {
        Rig r;
        r.st.conn = Conn::Connected;
        r.st.hosting = true;
        r.st.mode = GameMode::TeamDeathmatch;
        r.st.roster = {{1, "Me", 0, true, true, true, 0, 0},
                       {2, "Other", 1, false, false, false, 0, 0},
                       {3, "Third", 0, false, false, false, 0, 0}};
        r.s.setPage(MpPage::Lobby, r.st);
        r.press(UiKey::Down); // frame: lobby is up
        const Row& a = r.s.rows()[static_cast<size_t>(r.rowOf(RowId::LobbyPlayer))];
        float xFire = a.rect.x, xWater = 0;
        for (const Row& row : r.s.rows())
            if (row.id == RowId::LobbyPlayer && row.index == 1)
                xWater = row.rect.x;
        CHECK(xWater > xFire + 100); // the second team is its own column
        r.focusRow(RowId::LobbyChat);
        r.actions.clear();
        r.press(UiKey::Confirm);
        CHECK(r.s.editing() && r.last.wantsText);
        UiInput typed;
        typed.text = "hello";
        r.send(typed);
        CHECK(r.s.chatInput() == "hello");
        r.press(UiKey::Confirm);
        CHECK(!r.s.editing() && r.actions.size() == 1 && r.actions[0].kind == MpActionKind::SendChat &&
              r.actions[0].text == "hello" && r.s.chatInput().empty());
        const size_t base = r.last.draw.size();
        r.st.countdownSec = 2.4;
        r.press(UiKey::Down);
        CHECK(r.last.draw.size() > base); // the countdown card
    }

    // Join: the selection drives the details model; the table shows mode and mods.
    {
        Rig r;
        r.st.lan = {lan("A"), lan("B")};
        r.st.lan[1].pingMs = 40;
        r.st.lan[1].mods = "Pack";
        r.s.setPage(MpPage::Join, r.st);
        r.focusRow(RowId::JoinServer);
        CHECK(r.s.selectedServer() == 0);
        r.press(UiKey::Down);
        CHECK(r.s.selectedServer() == 1);
    }

    // HUD: team scoreboard, results with a winner, the respawn countdown and chat all draw.
    {
        HudState hs;
        hs.mode = GameMode::TeamDeathmatch;
        hs.teamKills[0] = 5;
        hs.teamKills[1] = 3;
        hs.roster = {{1, "A", 0, false, true, false, 5, 1}, {2, "B", 1, false, false, false, 3, 2}};
        hs.scoreboard = true;
        const size_t board = drawHud(hs, kVp, {}).size();
        CHECK(board > 30);
        hs.scoreboard = false;
        hs.results = true;
        hs.resultsText = "Back to the lobby";
        CHECK(drawHud(hs, kVp, {}).size() > board); // winner banner and the footer line
        hs.nextMatchSec = 4;
        CHECK(drawHud(hs, kVp, {}).size() > board);
        // A 4K viewport lays out the same commands.
        const Viewport k4{0, 0, 3840, 2160};
        CHECK(drawHud(hs, k4, {}).size() == drawHud(hs, kVp, {}).size());
    }

    // Co-op lobby: the host is Rynn (locked), a joiner cycles the other characters and sends SetCharacter.
    {
        Rig h;
        h.st.conn = Conn::Connected;
        h.st.hosting = true;
        h.st.mode = GameMode::Coop;
        h.st.roster = {{1, "Host", -1, false, true, true, 0, 0}};
        h.s.setPage(MpPage::Lobby, h.st);
        h.press(UiKey::Down);
        CHECK(h.rowOf(RowId::LobbyCharacter) >= 0);
        const Row& hr = h.s.rows()[static_cast<size_t>(h.rowOf(RowId::LobbyCharacter))];
        CHECK(!hr.enabled && hr.value.find("Rynn") == 0); // locked to the host
        CHECK(h.s.character(h.st).classId == 0);

        Rig j;
        j.st.conn = Conn::Connected;
        j.st.hosting = false;
        j.st.mode = GameMode::Coop;
        j.st.roster = {{1, "Host", -1, false, false, true, 0, 0}, {2, "Me", -1, false, true, false, 0, 0}};
        j.s.setPage(MpPage::Lobby, j.st);
        j.press(UiKey::Down);
        CHECK(j.s.character(j.st).name != "Rynn"); // joiners never get Rynn
        j.focusRow(RowId::LobbyCharacter);
        j.actions.clear();
        const int before = j.s.character(j.st).classId;
        j.press(UiKey::Right);
        CHECK(j.actions.size() == 1 && j.actions[0].kind == MpActionKind::SetCharacter &&
              j.actions[0].character != before && j.actions[0].character != 0);
        // The session's own choice wins over the local one; a supplied list replaces the placeholder.
        j.st.characters = {{0, "Rynn", "m0", "", true}, {7, "Zed", "m7", "x", false}, {9, "Kai", "m9", "y", false}};
        j.st.selfCharacter = 9;
        j.press(UiKey::Down);
        CHECK(j.s.character(j.st).name == "Kai");
        // Non-co-op lobbies have no selector.
        Rig d;
        d.st.conn = Conn::Connected;
        d.s.setPage(MpPage::Lobby, d.st);
        CHECK(d.rowOf(RowId::LobbyCharacter) < 0);
    }

    // Co-op HUD: partner panel and markers (on screen and edge arrows), co-op scoreboard and the level-complete view.
    {
        HudState hs;
        hs.mode = GameMode::Coop;
        hs.now = 50;
        const size_t base = drawHud(hs, kVp, {}).size();
        for (int i = 0; i < 7; ++i) {
            HudPartner q;
            q.id = i + 2;
            q.name = "Partner " + std::to_string(i);
            q.character = "Wartok";
            q.hpFrac = 0.2f * static_cast<float>(i % 5);
            q.down = i == 3;
            q.respawnInSec = 4.5;
            q.markerValid = true;
            q.onScreen = i % 2 == 0;
            q.markerX = 0.2f + 0.1f * static_cast<float>(i);
            q.markerY = 0.5f;
            q.dirDeg = 40.0f * static_cast<float>(i);
            q.distance = 12.0f * static_cast<float>(i);
            hs.partners.push_back(q);
        }
        const size_t withParty = drawHud(hs, kVp, {}).size();
        CHECK(withParty > base + 7 * 6);
        hs.roster = {{1, "Me", -1, false, true, false, 14, 1, -1, 20, 600}, {2, "B", -1, false, false, false, 9, 0}};
        hs.scoreboard = true;
        const size_t board = drawHud(hs, kVp, {}).size();
        CHECK(board > 30);
        hs.scoreboard = false;
        hs.results = true;
        hs.levelComplete = true;
        const size_t done = drawHud(hs, kVp, {}).size();
        CHECK(done > 20);
        hs.levelComplete = false;
        CHECK(drawHud(hs, kVp, {}).size() == done); // defeated: same structure, other text
        // Partners are not drawn in the deathmatch HUD.
        hs.results = false;
        hs.mode = GameMode::Deathmatch;
        hs.roster.clear();
        CHECK(drawHud(hs, kVp, {}).size() == base);
    }

    // Host controls and lobby data: kick (host only), Rejected leaves the session, the map list steps with Up / Down
    // once Select enters it, a joiner announces the character she is shown with, the party panel without health.
    {
        Rig h;
        h.st.conn = Conn::Connected;
        h.st.hosting = true;
        h.st.mode = GameMode::Deathmatch;
        h.st.roster = {{1, "Me", -1, false, true, true, 0, 0},
                       {2, "Alpha", -1, false, false, false, 0, 0},
                       {3, "Beta", -1, false, false, false, 0, 0}};
        h.s.setPage(MpPage::Lobby, h.st);
        h.press(UiKey::Down);
        CHECK(h.rowOf(RowId::LobbyKick) >= 0 && h.s.rows()[static_cast<size_t>(h.rowOf(RowId::LobbyKick))].enabled);
        h.focusRow(RowId::LobbyKick);
        CHECK(h.focused().value == "Alpha");
        h.press(UiKey::Right);
        CHECK(h.focused().value == "Beta");
        h.actions.clear();
        h.press(UiKey::Confirm);
        CHECK(h.actions.size() == 1 && h.actions[0].kind == MpActionKind::KickPlayer && h.actions[0].player == 3);
        // Guests have no kick row; alone, the host's row is greyed.
        Rig g;
        g.st.conn = Conn::Connected;
        g.st.hosting = false;
        g.st.roster = {{1, "Host", -1, false, false, true, 0, 0}, {2, "Me", -1, false, true, false, 0, 0}};
        g.s.setPage(MpPage::Lobby, g.st);
        CHECK(g.rowOf(RowId::LobbyKick) < 0);
        Rig a;
        a.st.conn = Conn::Connected;
        a.st.hosting = true;
        a.st.roster = {{1, "Me", -1, false, true, true, 0, 0}};
        a.s.setPage(MpPage::Lobby, a.st);
        a.press(UiKey::Down);
        CHECK(!a.s.rows()[static_cast<size_t>(a.rowOf(RowId::LobbyKick))].enabled);
        // A kicked / rejected session: the screen leaves it and returns to Join with the reason.
        Rig k;
        k.st.conn = Conn::Connected;
        k.st.roster = {{1, "Host", -1, false, false, true, 0, 0}, {2, "Me", -1, false, true, false, 0, 0}};
        k.s.setPage(MpPage::Lobby, k.st);
        k.press(UiKey::Down);
        k.st.conn = Conn::Rejected;
        k.st.message = "Kicked by the host";
        k.actions.clear();
        k.press(UiKey::Down);
        CHECK(k.s.page() == MpPage::Join);
        CHECK(!k.actions.empty() && k.actions[0].kind == MpActionKind::Leave);

        // The map list: Select enters it, Up / Down step through the maps, Back leaves it.
        Rig m;
        for (int i = 0; i < 5; ++i) {
            LevelEntry e;
            e.name = e.title = "Map " + std::to_string(i);
            e.arena = true;
            m.st.arenas.push_back(e);
        }
        m.s.setPage(MpPage::Host, m.st);
        m.focusRow(RowId::HostLevel);
        CHECK(!m.s.mapListActive());
        m.press(UiKey::Down); // outside the list mode Down moves on to the next row
        CHECK(m.focused().id != RowId::HostLevel);
        m.focusRow(RowId::HostLevel);
        m.press(UiKey::Confirm);
        CHECK(m.s.mapListActive());
        m.press(UiKey::Down);
        m.press(UiKey::Down);
        CHECK(m.s.hostMap(m.st).name == "Map 2" && m.focused().id == RowId::HostLevel);
        m.press(UiKey::Up);
        CHECK(m.s.hostMap(m.st).name == "Map 1");
        for (int i = 0; i < 9; ++i)
            m.press(UiKey::Down);
        CHECK(m.s.hostMap(m.st).name == "Map 4"); // stops at the end
        m.press(UiKey::Back);
        CHECK(!m.s.mapListActive() && m.s.page() == MpPage::Host);

        // A joiner whose session character is Rynn (the host's) announces the first one she may play.
        Rig j;
        j.st.conn = Conn::Connected;
        j.st.hosting = false;
        j.st.mode = GameMode::Coop;
        j.st.selfCharacter = 0;
        j.st.roster = {{1, "Host", -1, false, false, true, 0, 0}, {2, "Me", -1, false, true, false, 0, 0}};
        j.s.setPage(MpPage::Lobby, j.st);
        j.actions.clear();
        j.press(UiKey::Down);
        CHECK(!j.actions.empty() && j.actions[0].kind == MpActionKind::SetCharacter && j.actions[0].character != 0);
        const size_t announced = j.actions.size();
        j.press(UiKey::Down); // only once
        CHECK(j.actions.size() == announced);

        // Co-op HUD: a partner without a known health draws less (no bar, no percentage) and its edge arrow keeps off
        // the party panel.
        HudState hs;
        hs.mode = GameMode::Coop;
        hs.now = 10;
        HudPartner q;
        q.id = 2;
        q.name = "Pal";
        q.markerValid = true;
        q.onScreen = false;
        q.dirDeg = 225; // up and to the left: the corner the party panel sits in
        q.distance = 40;
        hs.partners = {q};
        const size_t known = drawHud(hs, kVp, {}).size();
        hs.partners[0].hpKnown = false;
        CHECK(drawHud(hs, kVp, {}).size() < known);
        const DrawList arrows = drawHud(hs, kVp, {});
        for (const DrawCmd& c : arrows.commands())
            if (c.kind == DrawKind::Line)                  // no arrow stroke inside the party panel
                CHECK(!(c.rect.x < 464 && c.rect.y < 90)); // the panel is 24..464 wide, one row (24..80) tall
    }

    // Scopes: the Multiplayer menu (Versus) has no Co-op; the campaign's Co-op flow (Coop) has nothing else. Map tags.
    {
        Rig v;
        v.s.open(v.st, MpScope::Versus);
        v.st.arenas = {[] {
            LevelEntry e;
            e.name = e.title = "Air Duel";
            e.arena = true;
            e.game = "Drakan 1";
            return e;
        }()};
        v.s.setPage(MpPage::Host, v.st);
        CHECK(!v.s.host().coop && v.s.hostMap(v.st).game == "Drakan 1");
        CHECK(!v.s.selectMode(GameMode::Coop, v.st) && !v.s.host().coop);
        CHECK(v.s.selectMode(GameMode::TeamDeathmatch, v.st));
        CHECK(!v.s.rows()[static_cast<size_t>(v.rowOf(RowId::HostType))].enabled); // fixed: no way to Co-op from here
        v.press(UiKey::Right);
        CHECK(!v.s.host().coop && v.last.draw.size() > 10);
        Rig c;
        c.s.open(c.st, MpScope::Coop);
        c.s.setPage(MpPage::Host, c.st);
        CHECK(c.s.host().coop && c.s.hostMode() == GameMode::Coop && c.s.hostMap(c.st).name == "Level 1A - Surdana");
        CHECK(!c.s.selectMode(GameMode::Deathmatch, c.st) && c.s.host().coop);
        const Row& type = c.s.rows()[static_cast<size_t>(c.rowOf(RowId::HostType))];
        CHECK(type.value == "Co-op" && !type.enabled);
        c.focusRow(RowId::HostStart);
        c.press(UiKey::Confirm);
        CHECK(!c.actions.empty() && c.actions.back().kind == MpActionKind::HostMatch &&
              c.actions.back().host.mode == GameMode::Coop);
    }

    // New Game asks Solo / Co-op when TitleConfig::showCoop is on; Co-op opens the pages in the Co-op scope.
    {
        TitleConfig cfg;
        cfg.showMultiplayer = cfg.showCoop = true;
        TitleScreen t(cfg);
        TitleState st;
        t.open();
        t.frame(st, UiInput{}, kVp);
        while (t.focus().index != static_cast<int>(TitleRow::NewGame))
            t.frame(st, key(UiKey::Down), kVp);
        TitleFrameOutput o = t.frame(st, key(UiKey::Confirm), kVp);
        CHECK(t.page() == TitlePage::NewGameChoice && o.actions.empty() && o.draw.size() > 10);
        o = t.frame(st, key(UiKey::Back), kVp);
        CHECK(t.page() == TitlePage::Main && t.focus().index == static_cast<int>(TitleRow::NewGame));
        t.frame(st, key(UiKey::Confirm), kVp);
        o = t.frame(st, key(UiKey::Down), kVp); // Solo -> Co-op
        o = t.frame(st, key(UiKey::Confirm), kVp);
        CHECK(t.page() == TitlePage::Multiplayer && t.multiplayer().scope() == MpScope::Coop &&
              t.multiplayer().host().coop);
        o = t.frame(st, key(UiKey::Back), kVp);
        CHECK(t.page() == TitlePage::Main);
        // Solo starts the ordinary New Game.
        t.frame(st, key(UiKey::Confirm), kVp);
        o = t.frame(st, key(UiKey::Confirm), kVp);
        CHECK(!o.actions.empty() && o.actions.front().kind == TitleActionKind::NewGame);
        // The Multiplayer row opens the versus scope.
        TitleScreen m(cfg);
        m.open();
        m.frame(st, UiInput{}, kVp);
        while (m.focus().index != static_cast<int>(TitleRow::Multiplayer))
            m.frame(st, key(UiKey::Down), kVp);
        m.frame(st, key(UiKey::Confirm), kVp);
        CHECK(m.page() == TitlePage::Multiplayer && m.multiplayer().scope() == MpScope::Versus);
    }

    // The title menu: the Multiplayer row sits after New Game, opens the page, Back returns with it focused.
    {
        TitleConfig cfg;
        cfg.showMultiplayer = true;
        TitleScreen t(cfg);
        TitleState st;
        st.canQuit = true;
        t.open();
        const std::vector<TitleRow> rows = t.rows(st);
        size_t at = 0;
        while (at < rows.size() && rows[at] != TitleRow::Multiplayer)
            ++at;
        CHECK(at < rows.size() && rows[at - 1] == TitleRow::NewGame);
        t.frame(st, UiInput{}, kVp);
        for (size_t i = 0; i < at; ++i) { // rows are walked from the first enabled one
            UiInput d;
            d.keys.push_back(UiKey::Down);
            t.frame(st, d, kVp);
        }
        CHECK(t.focus().index == static_cast<int>(TitleRow::Multiplayer));
        TitleFrameOutput o = t.frame(st, key(UiKey::Confirm), kVp);
        CHECK(t.page() == TitlePage::Multiplayer && o.draw.size() > 10);
        for (int i = 0; i < 6; ++i) // Player Name ... Connection Speed, Host Game, Join Game
            o = t.frame(st, key(UiKey::Down), kVp);
        o = t.frame(st, key(UiKey::Confirm), kVp);
        CHECK(o.mpActions.size() == 2 && o.mpActions[1].kind == MpActionKind::RefreshLan);
        o = t.frame(st, key(UiKey::Back), kVp);
        o = t.frame(st, key(UiKey::Back), kVp);
        CHECK(t.page() == TitlePage::Main && t.focus().index == static_cast<int>(TitleRow::Multiplayer));
        CHECK(!t.wantsText());
        TitleScreen plain; // off by default: the existing menu is untouched
        plain.open();
        for (TitleRow r : plain.rows(st))
            CHECK(r != TitleRow::Multiplayer);
    }

    // Game Mode row: Standard or a custom mode that fits the chosen map; the mode fixes the type, the teams and the
    // limits.
    {
        Rig r;
        LevelEntry meadow, pit;
        meadow.name = meadow.title = "Meadow";
        meadow.arena = true;
        meadow.modeIds = {"quick", "trio"};
        pit.name = pit.title = "Pit";
        pit.arena = true; // takes no custom mode
        r.st.arenas = {meadow, pit};
        ModeEntry quick;
        quick.id = "quick";
        quick.name = "Quick Frags";
        quick.fragLimit = 10;
        quick.timeLimitSec = 300;
        ModeEntry trio;
        trio.id = "trio";
        trio.name = "Three Teams";
        trio.teamBase = true;
        trio.teams = 3;
        trio.fragLimit = 30;
        r.st.modes = {quick, trio};
        r.s.setPage(MpPage::Host, r.st);
        r.focusRow(RowId::HostMode);
        CHECK(r.focused().enabled && r.focused().value == "Standard");
        r.press(UiKey::Right);
        CHECK(r.s.host().modeId == "quick" && r.focused().value == "Quick Frags");
        CHECK(r.s.hostMode() == GameMode::Deathmatch);
        CHECK(r.s.host().fragLimit == 10 && r.s.host().timeLimitMin == 5);
        // The limit rows show the mode values and are fixed.
        CHECK(!r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostFrag))].enabled);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostFrag))].label == "Score Limit");
        r.focusRow(RowId::HostMode);
        r.press(UiKey::Right);
        CHECK(r.s.host().modeId == "trio" && r.s.hostMode() == GameMode::TeamDeathmatch && r.s.host().teams == 3);
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostStart))].enabled); // 3 teams start with a mode
        {
            Rig b; // Begin Game carries the mode id, the base game type and the mode team count
            b.st = r.st;
            b.s.setPage(MpPage::Host, b.st);
            b.focusRow(RowId::HostMode);
            b.press(UiKey::Right);
            b.press(UiKey::Right);
            b.focusRow(RowId::HostStart);
            b.actions.clear();
            b.press(UiKey::Confirm);
            CHECK(!b.actions.empty() && b.actions.back().kind == MpActionKind::HostMatch &&
                  b.actions.back().host.modeId == "trio" && b.actions.back().host.mode == GameMode::TeamDeathmatch &&
                  b.actions.back().host.teams == 3);
        }
        // Right wraps to Standard, which frees the rows.
        r.focusRow(RowId::HostMode);
        r.press(UiKey::Right);
        CHECK(r.s.host().modeId.empty());
        CHECK(r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostFrag))].enabled);
        // Another map without the mode drops it.
        r.focusRow(RowId::HostMode);
        r.press(UiKey::Right);
        CHECK(r.s.host().modeId == "quick");
        r.focusRow(RowId::HostLevel);
        r.press(UiKey::Right);
        CHECK(r.s.hostMap(r.st).name == "Pit");
        CHECK(r.s.host().modeId.empty() && !r.s.rows()[static_cast<size_t>(r.rowOf(RowId::HostMode))].enabled);
    }

    // Results buttons: the layout, keyboard / pad / mouse handling, and the HUD draws them.
    {
        ScreenConfig cfg;
        std::vector<ResultsButton> bs = {{ResultsButtonId::BackToLobby, "Back to lobby", true},
                                         {ResultsButtonId::NextMap, "Next map", true},
                                         {ResultsButtonId::Rematch, "Rematch", true},
                                         {ResultsButtonId::Leave, "Leave", true}};
        const std::vector<Rect> rects = resultsButtonRects(bs.size(), kVp, cfg);
        CHECK(rects.size() == 4);
        for (size_t i = 0; i < rects.size(); ++i) {
            CHECK(rects[i].w > 100 && rects[i].h > 30 && rects[i].right() <= 1920 && rects[i].bottom() <= 1080);
            if (i)
                CHECK(rects[i].x > rects[i - 1].right()); // a row, no overlap
        }
        ResultsMenu m;
        CHECK(m.frame(bs, UiInput{}, kVp, cfg) == -1 && m.focus() == 0); // the first enabled button
        CHECK(m.frame(bs, key(UiKey::Right), kVp, cfg) == -1 && m.focus() == 1);
        CHECK(m.frame(bs, key(UiKey::Confirm), kVp, cfg) == 1);
        bs[2].enabled = false; // Rematch greyed: the focus skips it
        CHECK(m.frame(bs, key(UiKey::Right), kVp, cfg) == -1 && m.focus() == 3);
        CHECK(m.frame(bs, key(UiKey::Left), kVp, cfg) == -1 && m.focus() == 1);
        // The pointer: hover moves the focus, a click presses; a disabled button ignores both.
        UiInput hover;
        hover.pointerValid = hover.pointerMoved = true;
        hover.pointerX = rects[0].x + 10;
        hover.pointerY = rects[0].y + 10;
        CHECK(m.frame(bs, hover, kVp, cfg) == -1 && m.focus() == 0);
        UiInput click = hover;
        click.pointerX = rects[3].x + rects[3].w / 2;
        click.pointerY = rects[3].y + rects[3].h / 2;
        click.leftClick = true;
        CHECK(m.frame(bs, click, kVp, cfg) == 3 && m.focus() == 3);
        click.pointerX = rects[2].x + 5;
        click.pointerY = rects[2].y + 5;
        CHECK(m.frame(bs, click, kVp, cfg) == -1);
        // The HUD draws them, and more when they exist.
        HudState hs;
        hs.results = true;
        hs.mode = GameMode::Deathmatch;
        RosterRow a, b;
        a.id = 1;
        a.name = "Aria";
        a.self = true;
        a.kills = 3;
        a.deaths = 1;
        a.score = 3;
        b.id = 2;
        b.name = "Bo";
        b.kills = 1;
        b.deaths = 3;
        b.score = 1;
        hs.roster = {a, b};
        hs.nextMatchSec = 12;
        const size_t plain = drawHud(hs, kVp, cfg).size();
        hs.resultsButtons = bs;
        hs.resultsFocus = 1;
        hs.resultsNote = "Waiting for the host";
        CHECK(drawHud(hs, kVp, cfg).size() > plain);
    }

    // Custom modes in the standings: points instead of kills, sorted by points; four teams get one card each.
    {
        std::vector<RosterRow> rows(3);
        rows[0].id = 1;
        rows[0].kills = 5;
        rows[0].score = 5;
        rows[1].id = 2;
        rows[1].kills = 2;
        rows[1].score = 8; // fewer kills, more points (objectives)
        rows[2].id = 3;
        rows[2].kills = 1;
        rows[2].score = 1;
        const std::vector<RosterRow> sorted = sortedRoster(rows);
        CHECK(sorted[0].id == 2 && sorted[1].id == 1 && sorted[2].id == 3);
        HudState hs;
        hs.results = true;
        hs.mode = GameMode::TeamDeathmatch;
        hs.scoreMode = true;
        hs.modeName = "Flags";
        hs.roster = rows;
        for (int i = 0; i < 3; ++i)
            hs.roster[static_cast<size_t>(i)].team = i;
        hs.teamCount = 2;
        const size_t two = drawHud(hs, kVp, {}).size();
        hs.teamCount = 4;
        hs.teamScore[0] = 4;
        hs.teamScore[1] = 9;
        hs.teamScore[2] = 2;
        hs.teamScore[3] = 9;
        CHECK(drawHud(hs, kVp, {}).size() > 20 && two > 20);
    }

    // A mode with lives (last one standing): the players still in sort first, the LIVES column shows what is left (OUT
    // when eliminated), the eliminated overlay replaces the respawn countdown, the match's winner line and the mode's
    // team names / colours are used.
    {
        const auto hasText = [](const DrawList& d, const std::string& t) {
            for (const DrawCmd& c : d.commands())
                if (c.text.find(t) != std::string::npos)
                    return true;
            return false;
        };
        std::vector<RosterRow> rows(2);
        rows[0].id = 1;
        rows[0].name = "Alpha";
        rows[0].score = 5;
        rows[0].eliminated = true;
        rows[0].lives = 0;
        rows[1].id = 2;
        rows[1].name = "Bravo";
        rows[1].score = 1;
        rows[1].lives = 2;
        CHECK(sortedRoster(rows)[0].id == 2); // the survivor heads the table despite fewer points
        HudState hs;
        hs.scoreboard = true;
        hs.scoreMode = true;
        hs.livesMode = true;
        hs.modeName = "Last Rynn Standing";
        hs.goal = "Last one standing, 3 lives each";
        hs.roster = rows;
        DrawList d = drawHud(hs, kVp, {});
        CHECK(hasText(d, "LIVES") && hasText(d, "OUT") && hasText(d, "Last Rynn Standing - Last one standing"));
        CHECK(!hasText(d, "PING"));
        hs.scoreboard = false;
        hs.alive = false;
        hs.eliminated = true;
        d = drawHud(hs, kVp, {});
        CHECK(hasText(d, "You are out") && !hasText(d, "RESPAWNING IN"));
        hs.results = true;
        hs.winnerText = "Bravo is the last one standing";
        d = drawHud(hs, kVp, {});
        CHECK(hasText(d, "Bravo is the last one standing"));
        // Team names from the mode.
        HudState th;
        th.scoreboard = true;
        th.mode = GameMode::TeamDeathmatch;
        th.teamNames[0] = "Red";
        th.teamNames[1] = "Blue";
        th.teamColors[0] = 0xff0000;
        th.roster = rows;
        th.roster[0].team = 0;
        th.roster[1].team = 1;
        d = drawHud(th, kVp, {});
        CHECK(hasText(d, "Team Red") && hasText(d, "Team Blue") && !hasText(d, "Team Fire"));
        bool red = false;
        for (const DrawCmd& c : d.commands())
            red = red || c.color == rgba(255, 0, 0);
        CHECK(red);
    }

    std::puts("gameui_multiplayer_test ok");
    return 0;
}
