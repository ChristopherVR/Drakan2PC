// libsd_test: data-free checks of Note2Pitch / Pitch2Note and of the LIBSD.IRX reverb preset finder.
#include "spu2/Spu2.h"
#include <algorithm>
#include <array>
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

static void testParkingSilence() {
    Spu2 spu;
    // Drakan parks completed voices on the libsd init block without clearing their pitch, gain or ADSR.
    // Exercise cold/warm/repeated init, both cores and pitches from the issue #1 gameplay trace.
    for (int init : {0, 1, 0}) {
        spu.sdInit(init);
        for (int core = 0; core < 2; ++core) {
            for (int c = 0; c < 2; ++c) {
                spu.setParam(SD_PARAM_MVOLL | c, 0x3fff);
                spu.setParam(SD_PARAM_MVOLR | c, 0x3fff);
            }
            const u16 voice = SD_VOICE(core, 23);
            spu.setParam(SD_VPARAM_VOLL | voice, 0x3fff);
            spu.setParam(SD_VPARAM_VOLR | voice, 0x3fff);
            spu.setParam(SD_VPARAM_ADSR1 | voice, 0x000f);
            spu.setParam(SD_VPARAM_ADSR2 | voice, 0x1fc0);
            for (u16 pitch : {0x6ff, 0x7e7, 0x6cc, 0x75a, 0x1000, 0x3fff}) {
                spu.setParam(SD_VPARAM_PITCH | voice, pitch);
                // A deliberately audible looping block at an ordinary upload address. This must stay audible:
                // the fix must initialize the parking block, not suppress flag=7 blocks or high frequencies.
                std::array<u8, 16> tone;
                tone.fill(0x07);
                spu.ramWrite(0x6000, tone.data(), tone.size());
                spu.setAddr(SD_VADDR_SSA | voice, 0x6000);
                spu.setAddr(SD_VADDR_LSAX | voice, 0x6000);
                spu.setSwitch(SD_SWITCH_KON | core, 1u << 23);
                std::vector<s16> pcm(4800 * 2);
                spu.render(pcm.data(), pcm.size() / 2);
                CHECK(std::any_of(pcm.begin() + 128, pcm.end(), [](s16 s) { return s != 0; }),
                      "uploaded tone suppressed init=%d core=%d pitch=%x", init, core, pitch);
                spu.setAddr(SD_VADDR_SSA | voice, 0x5000);
                spu.setAddr(SD_VADDR_LSAX | voice, 0x5000);
                spu.setSwitch(SD_SWITCH_KON | core, 1u << 23);
                spu.render(pcm.data(), pcm.size() / 2);
                CHECK(std::all_of(pcm.begin() + 128, pcm.end(), [](s16 s) { return s == 0; }),
                      "parking tone init=%d core=%d pitch=%x", init, core, pitch);
                const u32 nax = spu.getAddr(SD_VADDR_NAX | voice);
                CHECK(nax >= 0x5000 && nax < 0x5010, "parking loop escaped: %x", nax);
                CHECK(spu.getSwitch(SD_SWITCH_ENDX | core) & (1u << 23), "parking loop did not reach ENDX");
            }
            spu.setParam(SD_VPARAM_VOLL | voice, 0);
            spu.setParam(SD_VPARAM_VOLR | voice, 0);
        }
    }
}

int main() {
    testParkingSilence();
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
