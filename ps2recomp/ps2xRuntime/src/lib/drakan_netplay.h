// Drakan: networked ghosts, first spike (docs/reference/netcode.md "Playing over the network (spike)",
// docs/journal/2026-10-31/netplay-spike.md). Two game instances see each other's Rynn as a ghost (drakan_ghost).
//
// Opt in by environment (all need DRAKAN_GHOST=1 too, the ghosts are drakan_ghost actors):
//   DRAKAN_NET_HOST=<port>          listen server: a drakan::net MatchServer lives in this process, the game joins it
//   DRAKAN_NET_CONNECT=<host:port>  client of a listen server or of drakan_server (start that with --auto-start 1)
//   DRAKAN_NET_NAME=<name>          player name (default "Rynn", at most 24 characters)
//
// Threads: all sockets, the server and the client live on one network thread that never touches guest memory. The EE
// thread, once per PadMgr_Update (beforePadMgr), publishes the local pose into a mutex-protected slot and applies the
// remote poses it finds in another one. The network thread sends the published PlayerState at 20 Hz.
// Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_ghost.
#pragma once
#include <cstdint>

class PS2Runtime;

namespace drakan_netplay {

void install(PS2Runtime& runtime); // reads the DRAKAN_NET_* variables; starts the network thread; subscribes lifecycle
bool enabled();
// EE thread, at the PadMgr_Update boundary (after drakan_ghost::beforePadMgr).
void beforePadMgr(const uint8_t* rdram);

} // namespace drakan_netplay
