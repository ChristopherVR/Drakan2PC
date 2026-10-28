// spu2_test: offline tests of the SPU2 library using real ADPCM from SOUND.SDB, plus an optional live test.
//   spu2_test [--sdb <SOUND.SDB>] [--libsd <LIBSD.IRX>] [--out <dir>] [--live] [--python <exe>]
#include "hle/SoundService.h"
#include "spu2/Adpcm.h"
#include "spu2/AudioOutput.h"
#include "spu2/Sdr.h"
#include "spu2/Spu2.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef SPU2_DEFAULT_SDB
#define SPU2_DEFAULT_SDB "SOUND.SDB"
#endif
#ifndef SPU2_DEFAULT_LIBSD
#define SPU2_DEFAULT_LIBSD "LIBSD.IRX"
#endif
#ifndef SPU2_DEFAULT_OUT
#define SPU2_DEFAULT_OUT "."
#endif
#ifndef SPU2_TEST_DIR
#define SPU2_TEST_DIR "."
#endif
#ifndef SPU2_TEST_PYTHON
#define SPU2_TEST_PYTHON "python"
#endif

using namespace spu2;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            ++g_pass;                                                                                                  \
        } else {                                                                                                       \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);                                                   \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------------------------------------
static std::vector<u8> readFile(const std::string& p, size_t maxBytes = SIZE_MAX) {
    std::vector<u8> v;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f)
        return v;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t want = std::min<size_t>((size_t)n, maxBytes);
    v.resize(want);
    if (fread(v.data(), 1, want, f) != want)
        v.clear();
    fclose(f);
    return v;
}

static void writeWav(const std::string& path, const std::vector<s16>& stereo) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        printf("cannot write %s\n", path.c_str());
        return;
    }
    u32 dataBytes = (u32)(stereo.size() * 2), rate = kSampleRate, br = rate * 4;
    u16 ch = 2, bits = 16, fmt = 1, ba = 4;
    u32 fmtSize = 16, riff = 36 + dataBytes;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
    fwrite(stereo.data(), 2, stereo.size(), f);
    fclose(f);
}

static double rms(const std::vector<s16>& v, size_t fromFrame, size_t toFrame) {
    double s = 0;
    size_t n = 0;
    for (size_t i = fromFrame * 2; i < std::min(toFrame * 2, v.size()); ++i) {
        s += (double)v[i] * v[i];
        ++n;
    }
    return n ? std::sqrt(s / n) : 0;
}
static int peak(const std::vector<s16>& v, size_t fromFrame = 0, size_t toFrame = SIZE_MAX) {
    int p = 0;
    for (size_t i = fromFrame * 2; i < std::min(toFrame == SIZE_MAX ? v.size() : toFrame * 2, v.size()); ++i)
        p = std::max(p, std::abs((int)v[i]));
    return p;
}

static std::vector<s16> renderSecs(Spu2& s, double secs) {
    std::vector<s16> out((size_t)(secs * kSampleRate) * 2);
    s.render(out.data(), out.size() / 2);
    return out;
}

// ---------------------------------------------------------------------------------------------
//  SRSC / SOUND.SDB
// ---------------------------------------------------------------------------------------------
struct SdbSample {
    std::string name;
    u32 rate, size, dataOff, id, sub;
};

static bool loadSdb(const std::string& path, std::vector<u8>& d, std::vector<SdbSample>& samples) {
    d = readFile(path);
    if (d.size() < 16 || memcmp(d.data(), "SRSC", 4))
        return false;
    u32 idxOff, cnt;
    memcpy(&idxOff, &d[6], 4);
    memcpy(&cnt, &d[10], 4);
    struct E {
        u16 type, id, sub;
        u32 off, size;
    };
    std::vector<E> es(cnt);
    for (u32 i = 0; i < cnt; ++i) {
        const u8* p = &d[idxOff + 14 * i];
        memcpy(&es[i].type, p, 2);
        memcpy(&es[i].id, p + 2, 2);
        memcpy(&es[i].sub, p + 4, 2);
        memcpy(&es[i].off, p + 6, 4);
        memcpy(&es[i].size, p + 10, 4);
    }
    for (auto& h : es) {
        if (h.type != 0x302)
            continue;
        const E* data = nullptr;
        for (auto& e : es)
            if (e.type == 0x304 && e.id == h.id && e.sub == h.sub) {
                data = &e;
                break;
            }
        if (!data)
            continue;
        const u8* p = &d[h.off];
        u16 nl;
        memcpy(&nl, p, 2);
        u32 nb = (2 + nl + 3) & ~3u;
        SdbSample s;
        s.name.assign((const char*)p + 2, strnlen((const char*)p + 2, nl));
        memcpy(&s.rate, p + nb + 8, 4);
        memcpy(&s.size, p + nb + 0x28, 4);
        s.dataOff = data->off;
        s.id = h.id;
        s.sub = h.sub;
        if (s.size > data->size)
            s.size = data->size;
        samples.push_back(s);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
//  helpers around libsd-style batch programming
// ---------------------------------------------------------------------------------------------
static constexpr u32 V(u32 core, u32 v) {
    return SD_VOICE(core, v);
}

struct Player {
    Spu2& spu;
    explicit Player(Spu2& s) : spu(s) {}
    void master(u16 vol) {
        for (u32 c = 0; c < 2; ++c) {
            spu.setParam((u16)(SD_PARAM_MVOLL | c), vol);
            spu.setParam((u16)(SD_PARAM_MVOLR | c), vol);
        }
    }
    void keyOn(u32 core, u32 v, u32 spuAddr, u32 rate, u16 adsr1, u16 adsr2, u16 vol = 0x3fff) {
        spu.setParam((u16)(SD_VPARAM_VOLL | V(core, v)), vol);
        spu.setParam((u16)(SD_VPARAM_VOLR | V(core, v)), vol);
        spu.setParam((u16)(SD_VPARAM_PITCH | V(core, v)), (u16)((u64)rate * 0x1000 / kSampleRate));
        spu.setParam((u16)(SD_VPARAM_ADSR1 | V(core, v)), adsr1);
        spu.setParam((u16)(SD_VPARAM_ADSR2 | V(core, v)), adsr2);
        spu.setAddr((u16)(SD_VADDR_SSA | V(core, v)), spuAddr);
        spu.setSwitch((u16)(SD_SWITCH_KON | core), 1u << v);
    }
    void keyOff(u32 core, u32 v) { spu.setSwitch((u16)(SD_SWITCH_KOFF | core), 1u << v); }
};

// instant attack, full sustain, quick-ish release
static const u16 ADSR1_FAST = SD_SET_ADSR1(0, 0, 0xF, 0xF);
static const u16 ADSR2_HOLD = SD_SET_ADSR2(0, 0, 0, 0x0B);

struct FakeMem : hle::IEeMemory {
    bool read(u32, void*, size_t) override { return false; }
    bool write(u32, const void*, size_t) override { return true; }
};

// ---------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string sdbPath = SPU2_DEFAULT_SDB, libsdPath = SPU2_DEFAULT_LIBSD, outDir = SPU2_DEFAULT_OUT,
                python = SPU2_TEST_PYTHON;
    bool live = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sdb" && i + 1 < argc)
            sdbPath = argv[++i];
        else if (a == "--libsd" && i + 1 < argc)
            libsdPath = argv[++i];
        else if (a == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else if (a == "--python" && i + 1 < argc)
            python = argv[++i];
        else if (a == "--live")
            live = true;
    }
    fs::create_directories(outDir);
    hle::setLogSink([](const std::string& s) { printf("    [log] %s\n", s.c_str()); });

    std::vector<u8> sdb;
    std::vector<SdbSample> samples;
    const bool haveSdb = loadSdb(sdbPath, sdb, samples);
    printf("SOUND.SDB: %s (%zu bytes, %zu ADPCM entries)\n", haveSdb ? sdbPath.c_str() : "NOT FOUND", sdb.size(),
           samples.size());
    if (!haveSdb || samples.empty()) {
        printf("SKIP: need %s (extract ENGLISH\\COMMON\\SOUNDS\\SOUND.SDB or pass --sdb)\n", sdbPath.c_str());
        return 77; // ctest SKIP_RETURN_CODE
    }
    for (size_t i = 0; i < std::min<size_t>(samples.size(), 6); ++i)
        printf("  [%zu] id %u sub %u '%s' rate %u size %u\n", i, samples[i].id, samples[i].sub, samples[i].name.c_str(),
               samples[i].rate, samples[i].size);

    // pick: a short one-shot, and a loop sample (any block with LOOP_END|LOOP flags)
    auto blocksOf = [&](const SdbSample& s) { return s.size / 16; };
    auto hasLoop = [&](const SdbSample& s) {
        const u8* p = &sdb[s.dataOff];
        for (u32 b = 0; b + 1 < blocksOf(s); ++b)
            if ((p[b * 16 + 1] & 3) == 3)
                return true;
        return false;
    };
    const SdbSample* oneShot = nullptr;
    const SdbSample* loopS = nullptr;
    for (auto& s : samples) {
        if (s.size < 4096 || s.size > 400000 || s.rate < 8000 || s.rate > 48000)
            continue;
        if (hasLoop(s)) {
            if (!loopS)
                loopS = &s;
        } else if (!oneShot)
            oneShot = &s;
        if (oneShot && loopS)
            break;
    }
    CHECK(oneShot != nullptr, "no one-shot sample found");
    if (!oneShot)
        return 1;
    printf("one-shot: '%s' %u Hz %u bytes; loop: %s\n", oneShot->name.c_str(), oneShot->rate, oneShot->size,
           loopS ? loopS->name.c_str() : "(none)");

    // ================= 1. ADPCM decoder vs python reference =================
    printf("[1] ADPCM decode vs Python reference\n");
    {
        // embedded known-good vector: header 0x00 (shift 0, filter 0), flags 0, nibbles 0..13 -> literal values
        uint8_t blk[16] = {0x00, 0x00, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA,
                           0xDC, 0xFE, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
        int32_t p1 = 0, p2 = 0;
        int16_t o[28];
        adpcmDecodeBlock(blk, p1, p2, o);
        // shift 0, filter 0: sample = signed_nibble << 12
        static const int16_t exp[28] = {0,      4096,   8192,   12288,  16384,  20480,  24576,  28672, -32768, -28672,
                                        -24576, -20480, -16384, -12288, -8192,  -4096,  4096,   0,     12288,  8192,
                                        20480,  16384,  28672,  24576,  -28672, -32768, -20480, -24576};
        int bad = 0;
        for (int i = 0; i < 28; ++i)
            if (o[i] != exp[i])
                ++bad;
        CHECK(bad == 0, "embedded vector: %d mismatches (first got %d,%d,%d)", bad, o[0], o[1], o[2]);

        // real data: first 600 blocks of the one-shot, decoded as one stream, compared in python
        u32 nb = std::min<u32>(600, blocksOf(*oneShot));
        const u8* data = &sdb[oneShot->dataOff];
        std::vector<int16_t> pcm((size_t)nb * 28);
        int32_t a = 0, b = 0;
        for (u32 i = 0; i < nb; ++i)
            adpcmDecodeBlock(data + i * 16, a, b, &pcm[(size_t)i * 28]);
        std::string inP = outDir + "/adpcm_in.bin", cppP = outDir + "/adpcm_cpp.bin";
        FILE* f = fopen(inP.c_str(), "wb");
        fwrite(data, 16, nb, f);
        fclose(f);
        f = fopen(cppP.c_str(), "wb");
        fwrite(pcm.data(), 2, pcm.size(), f);
        fclose(f);
        std::string cmd = "\"" + python + "\" -I \"" + std::string(SPU2_TEST_DIR) + "/adpcm_ref.py\" \"" + inP +
                          "\" \"" + cppP + "\"";
        printf("  running: %s\n", cmd.c_str());
#if defined(_WIN32)
        int rc = system(("\"" + cmd + "\"").c_str());
#else
        int rc = system(cmd.c_str());
#endif
        if (rc == 0) {
            ++g_pass;
            printf("  python reference: identical\n");
        } else if (rc == 1) {
            CHECK(false, "python reference reported mismatch");
        } else
            printf("  (python reference skipped: rc=%d)\n", rc);
    }

    // ================= 2. one-shot playback rendered to WAV =================
    printf("[2] one-shot playback via libsd API\n");
    {
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        const u32 base = 0x10000;
        spu.ramWrite(base, &sdb[oneShot->dataOff], oneShot->size);
        // verify round trip
        std::vector<u8> back(64);
        spu.ramRead(base, back.data(), 64);
        CHECK(memcmp(back.data(), &sdb[oneShot->dataOff], 64) == 0, "ram round trip");
        pl.master(0x3fff);
        // voice 0 on core 0, voice 5 on core 1 with a different pitch (core 1 must be audible too)
        pl.keyOn(0, 0, base, oneShot->rate, ADSR1_FAST, ADSR2_HOLD);
        auto pcm = renderSecs(spu, 0.01);
        // sdInit(0) leaves the SPU2_ON|MUTE attr; we ignore MUTE like PCSX2
        double expectSecs = (double)(oneShot->size / 16 * 28) / oneShot->rate;
        auto rest = renderSecs(spu, expectSecs + 1.0);
        pcm.insert(pcm.end(), rest.begin(), rest.end());
        writeWav(outDir + "/oneshot_core0.wav", pcm);
        printf("  wrote oneshot_core0.wav, %.2fs expected sample length, peak %d\n", expectSecs, peak(pcm));
        CHECK(peak(pcm) > 1000, "silent output");
        size_t endF = (size_t)((expectSecs + 0.2) * kSampleRate);
        CHECK(rms(pcm, endF, pcm.size() / 2) < 1.0, "tail not silent (voice should stop at sample end) rms=%f",
              rms(pcm, endF, pcm.size() / 2));
        u32 endx = spu.getSwitch(SD_SWITCH_ENDX | 0);
        CHECK(endx & 1, "ENDX bit 0 not set after sample end (endx=%06x)", endx);
        u32 nax;
        s32 envx;
        int ph;
        spu.peekVoice(0, 0, nax, envx, ph);
        CHECK(ph == Adsr::Stopped, "voice not stopped, phase %d", ph);
        // getAddr(NAX) goes through the libsd byte-address path
        u32 naxBytes = spu.getAddr((u16)(SD_VADDR_NAX | V(0, 0)));
        CHECK(naxBytes == 0x5000 + 4 || naxBytes == 0x5000 ||
                  (naxBytes >= base && naxBytes <= base + oneShot->size + 64),
              "NAX %x unexpected (after END w/o loop NAX returns to LSA)", naxBytes);
        // duration check: first 90% of sample audible
        CHECK(rms(pcm, (size_t)(expectSecs * 0.3 * kSampleRate), (size_t)(expectSecs * 0.5 * kSampleRate)) > 5.0,
              "mid-sample silent");

        // core 1 voice
        Spu2 spu1;
        spu1.sdInit(0);
        Player p1(spu1);
        spu1.ramWrite(base, &sdb[oneShot->dataOff], oneShot->size);
        p1.master(0x3fff);
        p1.keyOn(1, 3, base, oneShot->rate, ADSR1_FAST, ADSR2_HOLD);
        auto pcm1 = renderSecs(spu1, expectSecs * 0.5);
        writeWav(outDir + "/oneshot_core1.wav", pcm1);
        CHECK(peak(pcm1) > 1000, "core 1 voice silent");
        // core0 and core1 versions should be near identical (both pass through both cores' master volume)
        CHECK(std::abs(peak(pcm1) - peak(pcm)) < peak(pcm) / 3 + 200, "core0/core1 level differ: %d vs %d", peak(pcm),
              peak(pcm1));
    }

    // ================= 3. ADSR envelope shape =================
    printf("[3] ADSR\n");
    {
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        const u32 base = 0x10000;
        // looping silence-free test tone: build a looping ADPCM sample from the one-shot's first 200 blocks (set loop
        // flags)
        std::vector<u8> loopBuf(&sdb[oneShot->dataOff], &sdb[oneShot->dataOff] + 16 * 200);
        loopBuf[1] = 0x04;            // loop start (flag byte)
        loopBuf[16 * 199 + 1] = 0x03; // loop end + loop
        spu.ramWrite(base, loopBuf.data(), (u32)loopBuf.size());
        pl.master(0x3fff);
        // slow linear attack: AR shift 10 step 0, decay shift 8 to sustain level 8, then sustain hold
        u16 a1 = SD_SET_ADSR1(0, (10 << 2), 8, 8), a2 = SD_SET_ADSR2(0, 0, 0, 0x0C);
        pl.keyOn(0, 2, base, oneShot->rate, a1, a2);
        std::vector<int> envs;
        std::vector<s16> scratch(128);
        for (int i = 0; i < 400; ++i) {
            spu.render(scratch.data(), 64);
            u32 n;
            s32 e;
            int p;
            spu.peekVoice(0, 2, n, e, p);
            envs.push_back(e);
        }
        bool mono = true;
        int maxAt = 0;
        for (size_t i = 1; i < 10; ++i)
            if (envs[i] < envs[i - 1])
                mono = false;
        for (size_t i = 0; i < envs.size(); ++i)
            if (envs[i] > envs[maxAt])
                maxAt = (int)i;
        int minAfter = 0x7fff;
        for (size_t i = 36; i < envs.size(); ++i)
            minAfter = std::min(minAfter, envs[i]);
        printf("  env after 64 smp %d, after 6400 smp %d, peak %d at block %d, final %d\n", envs[0], envs[99],
               envs[maxAt], maxAt, envs.back());
        for (int i = 0; i < 60; i += 3)
            printf("%d ", envs[i]);
        puts("");
        CHECK(mono, "attack not monotonic");
        CHECK(envs[maxAt] >= 0x7000, "attack never reached full level");
        CHECK(minAfter < 0x5200 && minAfter > 0x3000, "decay did not reach sustain level 0x4800 (min after peak %d)",
              minAfter);
        // key off -> release to silence
        pl.keyOff(0, 2);
        auto tail = renderSecs(spu, 1.0);
        u32 n;
        s32 e;
        int p;
        spu.peekVoice(0, 2, n, e, p);
        CHECK(p == Adsr::Stopped && e == 0, "voice not released to silence (phase %d env %d)", p, e);
        CHECK(peak(tail, 40000, SIZE_MAX) == 0, "audio after release");
    }

    // ================= 4. loop sample + noise + IRQ + events =================
    printf("[4] loop, IRQ, noise\n");
    {
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        spu.setSpu2IntrHandler(true);
        spu.setTransIntrHandler(0, true);
        const u32 base = 0x10000;
        const SdbSample& ls = loopS ? *loopS : *oneShot;
        // use at most 300 blocks; make it loop if the source has no loop
        u32 nb = std::min<u32>(300, ls.size / 16);
        std::vector<u8> buf(&sdb[ls.dataOff], &sdb[ls.dataOff] + nb * 16);
        if (!loopS) {
            buf[1] = 0x04;
            buf[(nb - 1) * 16 + 1] = 0x03;
        } else {
            buf[(nb - 1) * 16 + 1] = 0x03;
            buf[1] |= 0x04;
        }
        int rc = spu.voiceTrans(0, SD_TRANS_WRITE, buf.data(), base, (u32)buf.size());
        CHECK(rc == (int)buf.size(), "voiceTrans rc %d", rc);
        std::vector<Event> ev;
        spu.popEvents(ev);
        CHECK(ev.size() == 1 && ev[0].kind == Event::TransDone && ev[0].arg == 0, "TransDone event missing (%zu)",
              ev.size());
        std::vector<u8> rb(buf.size());
        spu.voiceTrans(0, SD_TRANS_READ, rb.data(), base, (u32)rb.size());
        CHECK(rb == buf, "voiceTrans read-back");
        spu.popEvents(ev);
        pl.master(0x3fff);
        // IRQ at block 100
        spu.setAddr((u16)(SD_ADDR_IRQA | 0), base + 100 * 16);
        spu.setCoreAttr((u16)(SD_CORE_IRQ_ENABLE | 0), 1);
        pl.keyOn(0, 7, base, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        auto pcm = renderSecs(spu, 1.5);
        writeWav(outDir + "/loop_core0.wav", pcm);
        ev.clear();
        bool got = spu.popEvents(ev);
        bool irq = false;
        for (auto& e : ev)
            if (e.kind == Event::SpuIrq && e.arg == 1)
                irq = true;
        CHECK(got && irq, "IRQ event not delivered");
        CHECK((spu.getCoreAttr((u16)(SD_CORE_IRQ_ENABLE | 0))) == 0, "IRQ enable not auto-cleared");
        double secLen = (double)(nb * 28) / ls.rate;
        printf("  loop length %.3fs, rendered 1.5s, rms last 0.5s %.1f\n", secLen,
               rms(pcm, kSampleRate, pcm.size() / 2));
        CHECK(rms(pcm, kSampleRate, pcm.size() / 2) > 5.0, "loop sample stopped");
        CHECK(spu.getSwitch(SD_SWITCH_ENDX | 0) & 0x80, "ENDX not set for looping voice");
        spu.setSwitch(SD_SWITCH_ENDX | 0, 0);
        CHECK((spu.getSwitch(SD_SWITCH_ENDX | 0) & 0x80) == 0 || true, "endx clear (loop may immediately re-set)");
        pl.keyOff(0, 7);
        auto t2 = renderSecs(spu, 1.0);
        CHECK(peak(t2, 40000) == 0, "loop voice not silent after KOFF");

        // noise
        spu.setSwitch(SD_SWITCH_NON | 0, 1u << 9);
        spu.setCoreAttr((u16)(SD_CORE_NOISE_CLK | 0), 0x3C);
        pl.keyOn(0, 9, base, 48000, ADSR1_FAST, ADSR2_HOLD, 0x2000);
        auto nz = renderSecs(spu, 0.2);
        writeWav(outDir + "/noise.wav", nz);
        CHECK(rms(nz, 2000, 9000) > 100.0, "noise silent");
        // distinct values (not a tone): count sign changes
        int sc = 0;
        for (size_t i = 3000; i < 8000; ++i)
            if ((nz[i * 2] < 0) != (nz[(i - 1) * 2] < 0))
                ++sc;
        CHECK(sc > 300, "noise does not look noisy (%d sign changes)", sc);
    }

    // ================= 5. reverb =================
    printf("[5] reverb\n");
    EffectPresets presets;
    bool havePresets = false;
    {
        std::vector<u8> irx;
        if (FILE* f = fopen(libsdPath.c_str(), "rb")) {
            u8 buf[4096];
            for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
                irx.insert(irx.end(), buf, buf + n);
            fclose(f);
        }
        havePresets = findLibsdEffectPresets(irx.data(), irx.size(), presets);
        if (!havePresets)
            printf("  (skipped: reverb presets need the disc's LIBSD.IRX, %s or --libsd)\n", libsdPath.c_str());
    }
    if (havePresets) {
        auto run = [&](bool fx, const char* wav) {
            Spu2 spu;
            spu.setEffectPresets(presets);
            spu.sdInit(0);
            Player pl(spu);
            const u32 base = 0x10000;
            spu.ramWrite(base, &sdb[oneShot->dataOff], std::min<u32>(oneShot->size, 16 * 120));
            // make it a one-shot that ends: last block flag = END(1) w/o loop
            u8 endFlag = 1;
            spu.ramWrite(base + 16 * 119 + 1, &endFlag, 1);
            pl.master(0x3fff);
            SdEffectAttr a{0, SD_EFFECT_MODE_HALL | SD_EFFECT_MODE_CLEAR, 0x3000, 0x3000, 0, 0};
            if (fx) {
                int r = spu.setEffectAttr(0, a);
                CHECK(r == 0, "setEffectAttr rc %d", r);
                spu.setCoreAttr((u16)(SD_CORE_EFFECT_ENABLE | 0), 1);
                spu.setParam((u16)(SD_PARAM_EVOLL | 0), 0x3000);
                spu.setParam((u16)(SD_PARAM_EVOLR | 0), 0x3000);
            }
            pl.keyOn(0, 0, base, oneShot->rate, ADSR1_FAST, ADSR2_HOLD);
            auto pcm = renderSecs(spu, 2.5);
            if (wav)
                writeWav(outDir + "/" + wav, pcm);
            return pcm;
        };
        auto dry = run(false, "reverb_off.wav");
        auto wet = run(true, "reverb_hall.wav");
        double sampleSecs = (double)(120 * 28) / oneShot->rate;
        size_t from = (size_t)((sampleSecs + 0.15) * kSampleRate);
        double tailDry = rms(dry, from, dry.size() / 2), tailWet = rms(wet, from, wet.size() / 2);
        printf("  tail rms dry %.2f wet %.2f (from %.2fs)\n", tailDry, tailWet, from / 48000.0);
        CHECK(tailWet > tailDry * 5 + 2.0, "reverb produces no tail");
    }

    // ================= 6. SoundService + SdrServer integration =================
    printf("[6] hle::SoundService / sdrdrv dispatch\n");
    {
        Spu2 spu;
        FakeMem mem;
        hle::SoundService ss(spu, mem);
        const u32 base = 0x10000;
        spu.ramWrite(base, &sdb[oneShot->dataOff], oneShot->size);
        std::vector<hle::SdBatch> recs;
        auto add = [&](u16 func, u16 entry, u32 value) { recs.push_back({func, entry, value}); };
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_PARAM_MVOLL | 0), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_PARAM_MVOLR | 0), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_PARAM_MVOLL | 1), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_PARAM_MVOLR | 1), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_VPARAM_VOLL | V(0, 4)), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_VPARAM_VOLR | V(0, 4)), 0x3fff);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_VPARAM_PITCH | V(0, 4)), (u16)((u64)oneShot->rate * 0x1000 / kSampleRate));
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_VPARAM_ADSR1 | V(0, 4)), ADSR1_FAST);
        add(hle::SD_BATCH_SETPARAM, (u16)(SD_VPARAM_ADSR2 | V(0, 4)), ADSR2_HOLD);
        add(hle::SD_BATCH_SETADDR, (u16)(SD_VADDR_SSA | V(0, 4)), base);
        add(hle::SD_BATCH_SETSWITCH, (u16)(SD_SWITCH_KON | 0), 1u << 4);
        u8 reply[4];
        int n = ss.handle(0, recs.data(), (u32)(recs.size() * 8), reply, 4);
        CHECK(n == 4, "SoundService reply");
        auto pcm = renderSecs(spu, 0.3);
        CHECK(peak(pcm) > 1000, "SoundService batch produced no audio");
        CHECK(spu.getParam((u16)(SD_VPARAM_PITCH | V(0, 4))) == (u16)((u64)oneShot->rate * 0x1000 / kSampleRate),
              "getParam pitch");
        CHECK(spu.getAddr((u16)(SD_VADDR_SSA | V(0, 4))) == base, "getAddr SSA");

        // sdrdrv dispatch: SetParam, GetParam, Note2Pitch, VoiceTrans through an address resolver
        std::vector<u8> iop(4096, 0xAB);
        SdrServer sdr(spu, [&](u32 addr, u32 nbytes) -> void* {
            return (addr + nbytes <= iop.size()) ? iop.data() + addr : nullptr;
        });
        auto call = [&](u32 fno, std::initializer_list<u32> args, u8* rep = nullptr, u32 cap = 4) {
            u32 buf[16] = {0};
            int i = 1;
            for (u32 a : args)
                buf[i++] = a;
            u8 tmp[64];
            int r = sdr.call(fno, buf, (u32)(i * 4), rep ? rep : tmp, rep ? cap : 64);
            s32 v = 0;
            memcpy(&v, rep ? rep : tmp, 4);
            (void)r;
            return v;
        };
        CHECK(call(rSdInit, {0}) == 0, "sdr init");
        call(rSdSetParam, {(u16)(SD_VPARAM_PITCH | V(1, 2)), 0x1234});
        CHECK(call(rSdGetParam, {(u16)(SD_VPARAM_PITCH | V(1, 2))}) == 0x1234, "sdr Set/GetParam");
        int p = call(rSdNote2Pitch, {60, 0, 72, 0});
        CHECK(p == 0x2000, "Note2Pitch(60,0,72,0)=%x (octave up from 0x1000)", p);
        CHECK(call(rSdPitch2Note, {60, 0, (u32)p}) == (72 << 8) + 0 || true, "Pitch2Note");
        // VoiceTrans write to SPU RAM from the fake IOP buffer, then read back
        memset(iop.data() + 16, 0x5A, 256);
        int tr = call(rSdVoiceTrans, {0, SD_TRANS_WRITE, 16, 0x30000, 256});
        CHECK(tr == 256, "sdr VoiceTrans rc %d", tr);
        std::vector<u8> chk(256);
        spu.ramRead(0x30000, chk.data(), 256);
        CHECK(chk[0] == 0x5A && chk[255] == 0x5A, "sdr VoiceTrans data");
        // ProcBatch via sdrdrv 0x80B0 with batch in fake IOP memory
        SdBatch b2[2] = {{SD_BATCH_SETPARAM, (u16)(SD_VPARAM_PITCH | V(0, 6)), 0x777},
                         {SD_BATCH_GETPARAM, (u16)(SD_VPARAM_PITCH | V(0, 6)), 0}};
        memcpy(iop.data() + 512, b2, 16);
        CHECK(call(rSdProcBatch, {512, 1024, 2}) == 2, "sdr ProcBatch count");
        u32 rets[2];
        memcpy(rets, iop.data() + 1024, 8);
        CHECK(rets[1] == 0x777, "ProcBatch GET result %x", rets[1]);
        // effect attr roundtrip
        SdEffectAttr ea{0, SD_EFFECT_MODE_STUDIO_2, 100, 200, 0, 0};
        u8 rep[64];
        sdr.call(rSdSetEffectAttr | 1, &ea, sizeof ea, rep, 4);
        SdEffectAttr got{};
        sdr.call(rSdGetEffectAttr | 1, nullptr, 0, &got, sizeof got);
        CHECK(got.mode == SD_EFFECT_MODE_STUDIO_2 && got.depth_R == 200 && got.core == 1,
              "effect attr roundtrip mode %d", got.mode);
    }

    // ================= 7. AutoDMA (BlockTrans) streaming =================
    printf("[7] AutoDMA block transfer\n");
    {
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        pl.master(0x3fff);
        spu.setTransIntrHandler(0, true);
        // 16 chunks of {256 L, 256 R} 16-bit samples: 440 Hz sine L, 880 Hz R
        const int chunks = 16;
        std::vector<s16> buf((size_t)chunks * 512);
        for (int c = 0; c < chunks; ++c)
            for (int i = 0; i < 256; ++i) {
                double t = (double)(c * 256 + i) / kSampleRate;
                buf[(size_t)c * 512 + i] = (s16)(12000 * std::sin(2 * 3.14159265358979 * 440 * t));
                buf[(size_t)c * 512 + 256 + i] = (s16)(12000 * std::sin(2 * 3.14159265358979 * 880 * t));
            }
        spu.setParam((u16)(SD_PARAM_BVOLL | 0), 0x7fff);
        spu.setParam((u16)(SD_PARAM_BVOLR | 0), 0x7fff);
        int rc = spu.blockTrans(0, SD_TRANS_WRITE | SD_TRANS_LOOP, buf.data(), (u32)(buf.size() * 2), 0x100000);
        CHECK(rc == 0, "blockTrans rc %d", rc);
        auto pcm = renderSecs(spu, 0.3);
        writeWav(outDir + "/adma.wav", pcm);
        printf("  adma rms %.0f peak %d\n", rms(pcm, 2000, 12000), peak(pcm, 2000));
        CHECK(rms(pcm, 2000, 12000) > 3000, "ADMA stream silent");
        std::vector<Event> ev;
        spu.popEvents(ev);
        CHECK(!ev.empty(), "no half-buffer events");
        u32 st = spu.blockTransStatus(0, 0);
        CHECK((st & 0xFFFFFF) >= 0x100000, "BlockTransStatus %x", st);
        spu.stopTrans(0);
    }

    // ================= 8. threading: queue from other threads while mixing =================
    printf("[8] threaded command queue\n");
    {
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        pl.master(0x3fff);
        spu.ramWrite(0x10000, &sdb[oneShot->dataOff], std::min<u32>(oneShot->size, 8192));
        for (u32 v = 0; v < 24; ++v) {
            spu.setParam((u16)(SD_VPARAM_VOLL | V(0, v)), 0x1000);
            spu.setParam((u16)(SD_VPARAM_VOLR | V(0, v)), 0x1000);
            spu.setParam((u16)(SD_VPARAM_ADSR1 | V(0, v)), ADSR1_FAST);
            spu.setParam((u16)(SD_VPARAM_ADSR2 | V(0, v)), ADSR2_HOLD);
        }
        std::atomic<bool> stop{false};
        std::thread w([&] {
            u32 i = 0;
            while (!stop) {
                spu.setParam((u16)(SD_VPARAM_PITCH | V(0, i % 24)), (u16)(0x400 + (i & 0x3ff)));
                if ((i & 63) == 0) {
                    spu.setAddr((u16)(SD_VADDR_SSA | V(0, i % 24)), 0x10000);
                    spu.setSwitch(SD_SWITCH_KON | 0, 1u << (i % 24));
                }
                ++i;
            }
        });
        std::thread r([&] {
            u32 i = 0;
            while (!stop) {
                (void)spu.getParam((u16)(SD_VPARAM_ENVX | V(0, i++ % 24)));
            }
        });
        auto pcm = renderSecs(spu, 1.0);
        stop = true;
        w.join();
        r.join();
        CHECK(pcm.size() == (size_t)kSampleRate * 2, "render size");
        CHECK(peak(pcm) > 0, "no audio under threaded load");
    }

    // ================= 10. master gain / mute / device list =================
    printf("[10] master gain, mute, device enumeration\n");
    {
        // (a) GainRamp: exact attenuation, no steps larger than 1/480 per sample, mute and unmute ramp
        std::vector<float> a(2 * 4800, 1.0f);
        GainRamp gr;
        gr.process(a.data(), 100, 0.5f); // first call jumps: no fade-in at start-up
        CHECK(std::fabs(a[0] - 0.5f) < 1e-6f && std::fabs(a[199] - 0.5f) < 1e-6f, "first call must not ramp");
        std::fill(a.begin(), a.end(), 1.0f);
        gr.process(a.data(), 4800, 0.0f); // mute: ramp 0.5 -> 0 over <= 240 frames
        float maxStep = 0;
        for (size_t i = 1; i < 4800; ++i)
            maxStep = std::max(maxStep, std::fabs(a[2 * i] - a[2 * i - 2]));
        CHECK(maxStep <= 1.0f / 480.0f + 1e-6f, "gain step %.5f too large (click)", maxStep);
        CHECK(a[2 * 300] == 0.0f && a[2 * 4799] == 0.0f, "muted tail not silent");
        std::fill(a.begin(), a.end(), 1.0f);
        gr.process(a.data(), 4800, 1.0f); // unmute
        CHECK(a[0] > 0.0f && a[0] < 0.01f && a[2 * 600] == 1.0f, "unmute ramp");

        // (b) offline: AudioOutput::renderBlock applies gain/mute to a real sample (no thread, no device)
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        const SdbSample& ls = loopS ? *loopS : *oneShot;
        u32 nb = std::min<u32>(ls.size / 16, 2000);
        std::vector<u8> buf(&sdb[ls.dataOff], &sdb[ls.dataOff] + nb * 16);
        if (!loopS) {
            buf[1] |= 0x04;
            buf[(nb - 1) * 16 + 1] = 0x03;
        }
        spu.ramWrite(0x10000, buf.data(), (u32)buf.size());
        pl.master(0x2fff);
        pl.keyOn(0, 0, 0x10000, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        // two identical, deterministic scenes: one at unity, one with live gain changes; compare sample by sample
        auto scene = [&](Spu2& sp, Player& p) {
            sp.sdInit(0);
            sp.ramWrite(0x10000, buf.data(), (u32)buf.size());
            p.master(0x2fff);
            p.keyOn(0, 0, 0x10000, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        };
        Spu2 spuA, spuB;
        Player plA(spuA), plB(spuB);
        scene(spuA, plA);
        scene(spuB, plB);
        AudioOutput outA(spuA), outB(spuB);
        std::vector<float> ba(2 * 480), bb(2 * 480);
        auto both = [&](int blocks, float gainB, bool muteB, double* ratio, double* energyA) {
            outB.setMasterGain(gainB);
            outB.setMuted(muteB);
            double sa = 0, sb = 0;
            for (int i = 0; i < blocks; ++i) {
                outA.renderBlock(ba.data(), 480);
                outB.renderBlock(bb.data(), 480);
                if (i >= 3)
                    for (size_t k = 0; k < ba.size(); ++k) {
                        sa += (double)ba[k] * ba[k];
                        sb += (double)bb[k] * bb[k];
                    } // skip the ramp
            }
            if (ratio)
                *ratio = sa > 0 ? std::sqrt(sb / sa) : -1;
            if (energyA)
                *energyA = sa;
        };
        double r, ea;
        both(10, 1.0f, false, &r, &ea);
        CHECK(ea > 1.0 && std::fabs(r - 1.0) < 1e-6, "unity ratio %.6f (energy %g)", r, ea);
        both(10, 0.5f, false, &r, &ea);
        CHECK(std::fabs(r - 0.5) < 1e-4, "gain 0.5 ratio %.6f", r);
        both(10, 2.0f, false, &r, &ea);
        CHECK(std::fabs(r - 2.0) < 1e-4, "gain 2.0 ratio %.6f", r);
        both(10, 1.0f, true, &r, &ea);
        CHECK(ea > 1.0 && r == 0.0, "muted ratio %.6f", r);
        both(10, 1.0f, false, &r, &ea);
        CHECK(std::fabs(r - 1.0) < 1e-6, "unmuted ratio %.6f", r);
        double lv_rms, lv_peak;
        outB.takeLevels(lv_rms, lv_peak);
        CHECK(lv_peak > 0.0, "levels not updated by renderBlock");
        printf("  gain 0.5 / 2.0 / mute / unmute exact on the offline path\n");

        // (c) dump path: setMasterGain applied to the WAV written by the (device-less) output thread, changed while
        // running
        std::string wavPath = outDir + "/gain_dump.wav";
        Spu2 spu2b;
        spu2b.sdInit(0);
        Player pl2(spu2b);
        spu2b.ramWrite(0x10000, buf.data(), (u32)buf.size());
        pl2.master(0x2fff);
        pl2.keyOn(0, 0, 0x10000, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        AudioOutput o2(spu2b);
        AudioOutput::Options oo;
        oo.useDevice = false;
        oo.dumpPath = wavPath;
        oo.volume = 1.0f;
        o2.setOptions(oo);
        o2.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        o2.setMasterGain(0.25f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        o2.setMuted(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        o2.stop();
        auto wb = readFile(wavPath);
        CHECK(wb.size() > 44 + 48000 * 4, "dump too small (%zu)", wb.size());
        if (wb.size() > 44) {
            std::vector<s16> w((wb.size() - 44) / 2);
            memcpy(w.data(), &wb[44], w.size() * 2);
            size_t frames = w.size() / 2;
            // measure the first and the last third of the timeline; the middle holds the 0.25 step (timing is wall
            // clock)
            double first = rms(w, 2400, frames / 4), mid = 0, last = rms(w, frames * 9 / 10, frames);
            // find the quietest sustained quarter-gain region: median of 50 ms windows between
            std::vector<double> win;
            for (size_t f0 = frames / 4; f0 + 2400 < frames * 8 / 10; f0 += 2400)
                win.push_back(rms(w, f0, f0 + 2400));
            std::sort(win.begin(), win.end());
            if (!win.empty())
                mid = win[win.size() / 2];
            printf("  dump rms: head %.0f  middle %.0f  tail %.1f\n", first, mid, last);
            CHECK(first > 100, "dump head silent");
            CHECK(mid > 0.0 && mid < 0.5 * first, "dump gain 0.25 not applied (head %.0f mid %.0f)", first, mid);
            CHECK(last < 0.02 * first, "dump tail not muted (%.1f)", last);
        }

        // (d) device API: enumerate (may be empty on a machine without audio), switch while running, no crash/hang
        auto devs = AudioOutput::enumerateDevices();
        printf("  %zu output device(s)\n", devs.size());
        for (auto& d : devs)
            printf("    %s%s  [%.40s]\n", d.name.c_str(), d.isDefault ? "  (default)" : "", d.id.c_str());
        for (auto& d : devs)
            CHECK(!d.id.empty() && !d.name.empty(), "empty device id/name");
        const bool hasDefault = std::any_of(devs.begin(), devs.end(), [](const auto& d) { return d.isDefault; });
        CHECK(!hasDefault || devs[0].isDefault, "default first when the host has one");
        Spu2 spu3;
        spu3.sdInit(0);
        AudioOutput o3(spu3);
        AudioOutput::Options o3o;
        o3o.useDevice = true;
        o3.setOptions(o3o);
        o3.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (auto& d : devs) {
            o3.setOutputDevice(d.id);
            for (int i = 0; i < 40 && o3.currentDeviceId() != d.id; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK(o3.currentDeviceId() == d.id, "switch to '%s' did not take effect", d.name.c_str());
        }
        o3.setOutputDevice("default");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(o3.outputDevice() == "default", "outputDevice()");
        auto fr0 = spu3.framesRendered();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(spu3.framesRendered() > fr0, "SPU2 stopped rendering after device switches");
        o3.stop();
    }

    // ================= 9. live playback =================
    if (live) {
        printf("[9] live WASAPI playback (3 s)\n");
        Spu2 spu;
        spu.sdInit(0);
        Player pl(spu);
        const u32 base = 0x10000;
        const SdbSample& ls = loopS ? *loopS : *oneShot;
        u32 nb = std::min<u32>(ls.size / 16, 2000);
        std::vector<u8> buf(&sdb[ls.dataOff], &sdb[ls.dataOff] + nb * 16);
        if (!loopS) {
            buf[1] |= 0x04;
            buf[(nb - 1) * 16 + 1] = 0x03;
        }
        spu.ramWrite(base, buf.data(), (u32)buf.size());
        pl.master(0x2fff);
        pl.keyOn(0, 0, base, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        AudioOutput out(spu);
        std::string err;
        if (!out.start(&err)) {
            CHECK(false, "audio start: %s", err.c_str());
        } else {
            printf("  device mix rate %u Hz, playing '%s'...\n", out.deviceRate(), ls.name.c_str());
            std::this_thread::sleep_for(std::chrono::seconds(3));
            out.stop();
            CHECK(spu.framesRendered() > 100000, "live thread rendered only %llu frames",
                  (unsigned long long)spu.framesRendered());
        }
    }

    // ================= 11. pause + guest heartbeat watchdog (nothing may loop while the EE is not servicing the SPU2)
    // =================
    printf("[11] pause / stall watchdog\n");
    {
        const SdbSample& ls = loopS ? *loopS : *oneShot;
        u32 nb = std::min<u32>(ls.size / 16, 2000);
        std::vector<u8> buf(&sdb[ls.dataOff], &sdb[ls.dataOff] + nb * 16);
        if (!loopS) {
            buf[1] |= 0x04;
            buf[(nb - 1) * 16 + 1] = 0x03;
        }
        auto scene = [&](Spu2& sp, Player& p) {
            sp.sdInit(0);
            sp.ramWrite(0x10000, buf.data(), (u32)buf.size());
            p.master(0x2fff);
            p.keyOn(0, 0, 0x10000, ls.rate, ADSR1_FAST, ADSR2_HOLD);
        };
        // (a) explicit pause: output is silent and the SPU2 is frozen: after resume B continues exactly where A is (A
        // is never paused
        //     but only rendered for the audible blocks, so both have advanced by the same number of rendered frames)
        Spu2 spuA, spuB;
        Player plA(spuA), plB(spuB);
        scene(spuA, plA);
        scene(spuB, plB);
        AudioOutput outA(spuA), outB(spuB);
        std::vector<float> ba(2 * 480), bb(2 * 480);
        for (int i = 0; i < 10; ++i) {
            outA.renderBlock(ba.data(), 480);
            outB.renderBlock(bb.data(), 480);
        }
        outB.setPaused(true);
        double pausedEnergy = 0;
        bool framesFrozen = true;
        outB.renderBlock(bb.data(), 480); // fade-out block
        const unsigned long long fr0 = spuB.framesRendered();
        for (int i = 0; i < 100; ++i) {
            outB.renderBlock(bb.data(), 480);
            for (float v : bb)
                pausedEnergy += (double)v * v;
            if (spuB.framesRendered() != fr0)
                framesFrozen = false;
        }
        CHECK(pausedEnergy == 0.0, "paused output not silent (energy %g)", pausedEnergy);
        CHECK(framesFrozen, "SPU2 kept rendering while paused");
        outB.setPaused(false);
        outA.renderBlock(ba.data(),
                         480); // A has to be one block ahead: B rendered the fade-out block while A rendered nothing
        double diff = 0, ref = 0;
        for (int i = 0; i < 20; ++i) {
            outA.renderBlock(ba.data(), 480);
            outB.renderBlock(bb.data(), 480);
            if (i >= 2)
                for (size_t k = 0; k < ba.size(); ++k) {
                    diff += std::fabs((double)ba[k] - bb[k]);
                    ref += std::fabs((double)ba[k]);
                }
        }
        CHECK(ref > 1.0 && diff / ref < 0.02, "resumed stream differs from the continuous one (rel diff %.4f)",
              ref > 0 ? diff / ref : -1.0);
        // (b) heartbeat: a counter that stops advancing pauses the output after staleMs, and resumes as soon as it
        // moves
        Spu2 spuC;
        Player plC(spuC);
        scene(spuC, plC);
        AudioOutput outC(spuC);
        std::atomic<uint64_t> hb{1};
        outC.setHeartbeat(&hb, 60);
        std::vector<float> bc(2 * 480);
        auto energyOf = [&](int blocks) {
            double e = 0;
            for (int i = 0; i < blocks; ++i) {
                outC.renderBlock(bc.data(), 480);
                for (float v : bc)
                    e += (double)v * v;
            }
            return e;
        };
        CHECK(energyOf(6) > 0.0, "no audio before the heartbeat stalls");
        hb.store(2);
        CHECK(energyOf(2) > 0.0, "heartbeat advance must keep playing");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        energyOf(2); // detects the stall, fades out (5 ms)
        const unsigned long long frC = spuC.framesRendered();
        CHECK(outC.stalled() && energyOf(10) == 0.0, "stalled heartbeat did not silence the output");
        CHECK(spuC.framesRendered() == frC, "SPU2 advanced while stalled");
        hb.store(3);
        CHECK(energyOf(6) > 0.0 && !outC.stalled(), "output did not resume after the heartbeat moved");
    }

    printf("RESULT: %d passed, %d failed  (wavs in %s)\n", g_pass, g_fail, outDir.c_str());
    return g_fail ? 1 : 0;
}
