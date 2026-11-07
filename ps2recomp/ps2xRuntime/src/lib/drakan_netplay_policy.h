#pragma once

namespace drakan_netplay::policy {

// Returning from a lost session must run the lobby's error/Leave transition as well as displaying the title.
constexpr bool resumeLobby(bool inSession, bool rejected, bool connected, bool matchOver) {
    return inSession && (rejected || (connected && matchOver));
}

// Native Quit confirmation is box screen 1 -> title screen 8. Results-driven returns preserve the live lobby.
constexpr bool leaveAfterTitle(int previousScreen, int screen, bool automaticReturn) {
    return previousScreen == 1 && screen == 8 && !automaticReturn;
}

// The arena world mode (drakan_arena: no saving, the campaign stripped, the arena .LVL filter) is on only while this
// client is in a running deathmatch match (loading, playing or showing results). `sessionRunning` is false once the
// session ended (Quit to Title, Leave): the mode must go off then, or a later Continue / Load / New Game from the title
// loads the arena version of the level (issue #1, journal 2026-11-07/mp-to-sp-reset.md).
constexpr bool arenaWorldWanted(bool sessionRunning, bool connected, bool coop, bool matchRunning) {
    return sessionRunning && connected && !coop && matchRunning;
}

// The title screen notice after a connected client lost its server. The transport reports a graceful leave (the host
// quit or ended the match) and a timeout alike, so the silence before the event tells them apart: the server was heard
// moments ago = it left on purpose; silent for `kHostLeftSilenceMs` or longer = the connection dropped.
constexpr unsigned long long kHostLeftSilenceMs = 4000;
constexpr const char* kHostEndedNotice = "The host ended the match";
constexpr const char* kConnectionLostNotice = "Connection to the server lost";
constexpr const char* disconnectNotice(unsigned long long silenceMs) {
    return silenceMs < kHostLeftSilenceMs ? kHostEndedNotice : kConnectionLostNotice;
}

} // namespace drakan_netplay::policy
