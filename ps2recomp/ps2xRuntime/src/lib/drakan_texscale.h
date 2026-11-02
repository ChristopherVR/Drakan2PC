// Fast texture downscale (drakan_texscale.cpp): the guest's surface resampler at PAL 0x1c0f28 averages 2x2 / 4x4 blocks
// one channel at a time through recompiled code (about 30x slower than the host); 256 px truecolor textures made a
// level load take ten times as long. The hook runs the same filter natively on the surface memory.
#pragma once

class PS2Runtime;

namespace drakan_texscale {
// The native filter is on by default (DRAKAN_TEXSCALE=0 restores the guest loop); DRAKAN_TEXSCALE_LOG=1 times the guest
// calls (2: logs the lock step); DRAKAN_TEXSCALE_VERIFY=1 checks every native result against a reference;
// DRAKAN_TEXSCALE_CHARGE=<cycles per pixel>.
void install(PS2Runtime& runtime);
} // namespace drakan_texscale
