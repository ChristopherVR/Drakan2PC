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

} // namespace drakan_netplay::policy
