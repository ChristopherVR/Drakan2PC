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
        const RowId order[] = {RowId::HostName,     RowId::HostPassword, RowId::HostGroup, RowId::HostLevel,
                               RowId::HostMax,      RowId::HostFrag,     RowId::HostTime,  RowId::HostTeams,
                               RowId::HostFriendly, RowId::HostRegister, RowId::HostStart, RowId::HostBack};
        CHECK(r.s.rows().size() == 12);
        for (size_t i = 0; i < 12; ++i)
            CHECK(r.s.rows()[i].id == order[i]);
        CHECK(!r.s.rows()[8].enabled && !r.s.rows()[9].enabled); // friendly fire / master servers: placeholders
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

    std::puts("gameui_multiplayer_test ok");
    return 0;
}
