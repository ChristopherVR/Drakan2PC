// Drakan: co-op prototype, host-authoritative enemies (docs/journal/2026-10-31/coop-design.md).
// The host reads its creatures (registry types 6 and 7: position, yaw, HP) and sends them at 10 Hz over its own UDP
// socket. On the client the matching creatures stop thinking (behaviour event 4 is swallowed by a host handler, the
// ghost technique) and follow the host's state: they become kinematic proxies. Own transport, independent of
// drakan_netplay (which keeps the player ghosts), so both can run together.
//
// Opt in by environment:
//   DRAKAN_COOP_HOST=<port>          host: listen on UDP <port>, send creature states to every connected client
//   DRAKAN_COOP_CONNECT=<host:port>  client: connect, apply the host's creature states
//   DRAKAN_COOP_MODE=follow|mark     client behaviour (default follow). mark = measure only: nothing is written to the
//                                    guest, the log reports how far the local creatures are from the host's
//   DRAKAN_COOP_SWALLOW=<hex>        behaviour events (value mask) a proxy no longer handles (default 4 = per-tick)
// Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_world. PAL disc profile.
#pragma once
#include <cstdint>

class PS2Runtime;

namespace drakan_coop {

void install(PS2Runtime& runtime);
bool enabled();
// EE thread, at the PadMgr_Update boundary.
void beforePadMgr(uint8_t* rdram);

} // namespace drakan_coop
