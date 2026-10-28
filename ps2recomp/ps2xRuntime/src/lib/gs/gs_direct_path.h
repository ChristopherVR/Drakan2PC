#pragma once
// The GIF arbiter whose output PS2Runtime routes to GS::processGIFPacketPath. A PATH1/PATH2 packet that would be
// drained right away from an empty arbiter is handed to the GS directly (same capture hook, same call) instead of
// being copied into the arbiter queue first (PS2Memory::submitGifPacket).
class GS;
class GifArbiter;

namespace gif_direct {
inline GifArbiter* g_arbiter = nullptr;
inline GS* g_gs = nullptr;
} // namespace gif_direct
