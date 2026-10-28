// libsd_test: data-free checks of Note2Pitch / Pitch2Note and of the LIBSD.IRX reverb preset finder.
#include "spu2/Spu2.h"
#include <cstdio>
#include <vector>

using namespace spu2;

static int g_failures = 0;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            ++g_failures;                                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

int main() {
    // Octaves around the centre note are exact powers of two of 0x1000; two octaves up and beyond stay at the
    // two-octave scale (libsd behaviour).
    CHECK(Spu2::note2Pitch(60, 0, 60, 0) == 0x1000, "unity %x", Spu2::note2Pitch(60, 0, 60, 0));
    CHECK(Spu2::note2Pitch(60, 0, 72, 0) == 0x2000, "+1 octave %x", Spu2::note2Pitch(60, 0, 72, 0));
    CHECK(Spu2::note2Pitch(60, 0, 48, 0) == 0x0800, "-1 octave %x", Spu2::note2Pitch(60, 0, 48, 0));
    CHECK(Spu2::note2Pitch(60, 0, 36, 0) == 0x0400, "-2 octaves %x", Spu2::note2Pitch(60, 0, 36, 0));
    CHECK(Spu2::note2Pitch(60, 0, 84, 0) == 0x4000, "+2 octaves %x", Spu2::note2Pitch(60, 0, 84, 0));
    CHECK(Spu2::note2Pitch(60, 0, 96, 0) == 0x4000, "+3 octaves %x", Spu2::note2Pitch(60, 0, 96, 0));
    // A semitone is 2^(1/12): 0x1000 * 1.05946 = 0x10F3.9, rounded up.
    CHECK(Spu2::note2Pitch(60, 0, 61, 0) == 0x10F4, "+1 semitone %x", Spu2::note2Pitch(60, 0, 61, 0));
    // 128 fine steps carry into the next semitone; a negative fine borrows from the note.
    CHECK(Spu2::note2Pitch(60, 0, 60, 128) == Spu2::note2Pitch(60, 0, 61, 0), "fine carry");
    CHECK(Spu2::note2Pitch(60, 0, 61, -128) == 0x1000, "fine borrow %x", Spu2::note2Pitch(60, 0, 61, -128));
    CHECK(Spu2::note2Pitch(60, 64, 60, -64) == 0x1000, "centre fine %x", Spu2::note2Pitch(60, 64, 60, -64));
    // Pitch2Note inverts it to (note << 8) | fine (even fine steps).
    CHECK(Spu2::pitch2Note(60, 0, 0x1000) == ((60 << 8) | 0), "inverse unity %x", Spu2::pitch2Note(60, 0, 0x1000));
    CHECK(Spu2::pitch2Note(60, 0, 0x2000) == ((72 << 8) | 0), "inverse octave %x", Spu2::pitch2Note(60, 0, 0x2000));
    for (int note = 30; note < 84; ++note) {
        const u16 back = Spu2::pitch2Note(60, 0, Spu2::note2Pitch(60, 0, (u16)note, 0));
        CHECK((back >> 8) == note || ((back >> 8) == note - 1 && (back & 0xFE) >= 0x7E), "round trip note %d -> %x",
              note, back);
    }

    // The preset finder must not accept anything but the verified table.
    EffectPresets presets;
    CHECK(!findLibsdEffectPresets(nullptr, 0, presets), "null image accepted");
    std::vector<u8> junk(4096, 0);
    CHECK(!findLibsdEffectPresets(junk.data(), junk.size(), presets), "zero image accepted");
    junk[100] = 2; // looks like the mode-OFF work-area size, but the hash does not match
    CHECK(!findLibsdEffectPresets(junk.data(), junk.size(), presets), "unverified table accepted");

    // Without presets every reverb mode is accepted and leaves the reverb registers clear.
    Spu2 spu;
    spu.sdInit(0);
    CHECK(!spu.hasEffectPresets(), "presets present by default");
    SdEffectAttr hall{0, SD_EFFECT_MODE_HALL | SD_EFFECT_MODE_CLEAR, 0x3000, 0x3000, 0, 0};
    CHECK(spu.setEffectAttr(0, hall) == 0, "setEffectAttr without presets");
    SdEffectAttr bad{0, 10, 0, 0, 0, 0};
    CHECK(spu.setEffectAttr(0, bad) == -1, "invalid mode accepted");

    // Injected presets drive the work area: ESA = EEA end - (blocks * 16 - 2).
    EffectPresets custom{};
    custom[SD_EFFECT_MODE_ROOM].workAreaBlocks = 0x100;
    custom[SD_EFFECT_MODE_ROOM].regs[0] = 0x10;
    spu.setEffectPresets(custom);
    SdEffectAttr room{0, SD_EFFECT_MODE_ROOM, 0, 0, 0, 0};
    CHECK(spu.setEffectAttr(0, room) == 0, "setEffectAttr with presets");
    const u32 eeaEnd = ((spu.getAddr(SD_ADDR_EEA) >> 17) << 17) | 0x1FFFF;
    CHECK(spu.getAddr(SD_ADDR_ESA) == ((eeaEnd - (0x1000 - 2)) & ~1u), "ESA %x (EEA end %x)", spu.getAddr(SD_ADDR_ESA),
          eeaEnd);

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
