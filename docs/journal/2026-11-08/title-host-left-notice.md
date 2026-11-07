# Title notice when the host leaves (issue 1 item 4)

The guest already returned to the title automatically when the host quit, but the reason only showed on the Join Game
page, so a player on the title saw nothing.

Change: `State::endNotice` (drakan_netplay) is set when an established client loses its server, chosen by
`policy::disconnectNotice` from the silence before the event (`MatchClient::lastRxMs`): under 4 s is "The host ended the
match", otherwise "Connection to the server lost". The transport cannot tell a graceful leave from a timeout, hence the
silence heuristic. The notice survives the session teardown, is passed to the title through `MpState::endNotice` and
drawn as `TitleState::connectionNote` on the main page's note plate. The glue shows it for 12 s and dismisses it when the
Multiplayer page opens.

Tests: `netplay_policy` (thresholds), `net_udp_smoke` (`lastRxMs` set), `gameui_saveload_test` (the note draws, shares
the plate with another note).

Two-instance harness: see the end of this note for whether it was run.
Two-instance harness: not run. Linking a private drakan.exe needs the whole runtime tree, and another agent's in-flight change left ps2xRuntime/src/lib/Kernel/Stubs/Deci2.cpp unbuildable in this checkout. The two changed game translation units (drakan_netplay.cpp, drakan_settings_host.cpp) were compiled alone against the final-game project with a private IntDir (build/mp15/obj) and build clean; the unit tests run.
