// Fast texture downscale (drakan_texscale.cpp): the guest's surface resampler at PAL 0x1c0f28 averages 2x2 / 4x4 blocks
// one channel at a time through recompiled code (about 30x slower than the host); 256 px truecolor textures made a
// level load take ten times as long. The hook runs the same filter natively on the surface memory.
#pragma once

class PS2Runtime;

namespace drakan_texscale {
// Env DRAKAN_TEXSCALE=1 enables the native filter (off by default, experimental); DRAKAN_TEXSCALE_LOG=1 times the guest calls (2: logs the lock step).
void install(PS2Runtime& runtime);
} // namespace drakan_texscale
