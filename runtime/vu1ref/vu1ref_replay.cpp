// vu1ref_replay: replays DRAKAN_VUREC records through the reference simulator and compares with what the runtime
// produced. usage: vu1ref_replay <micromem.bin> <vurec.bin> [-n max] [-v] [-flag N] [-nostall] [-trace idx] [-showdiff
// N]
#include "vu1ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

struct VU1StateRec { // must match runtime/ps2_vu1.h VU1State
    float vf[32][4];
    int32_t vi[16];
    float acc[4];
    float q, p, i;
    uint32_t r, pc, mac, clip, status;
    uint64_t cycles;
    bool ebit, haltAfterDelaySlot, dBitEnabled, tBitEnabled, stoppedByD, stoppedByT;
    uint32_t top, itop;
    bool branchPending;
    uint32_t branchTarget, branchDelay;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage\n");
        return 1;
    }
    std::vector<uint8_t> code(0x4000, 0);
    {
        FILE* f = fopen(argv[1], "rb");
        if (!f)
            return 2;
        fread(code.data(), 1, 0x4000, f);
        fclose(f);
    }
    FILE* f = fopen(argv[2], "rb");
    if (!f)
        return 3;
    int maxN = 1 << 30;
    bool verbose = false;
    int unfIdx = -1;
    uint32_t unfPairs = 400;
    int dumpIdx = -1;
    int geomE = -1;
    bool clipOnly = false;
    int nClipKick = 0;
    int skipN = 0;
    int traceIdx = -1;
    int showDiff = 6;
    Vu1RefOptions opt;
    for (int a = 3; a < argc; ++a) {
        std::string s = argv[a];
        if (s == "-n")
            maxN = atoi(argv[++a]);
        else if (s == "-v")
            verbose = true;
        else if (s == "-flag")
            opt.flagLatency = atoi(argv[++a]);
        else if (s == "-div")
            opt.divLatency = atoi(argv[++a]);
        else if (s == "-nostall")
            opt.stallModel = false;
        else if (s == "-nohazard")
            opt.branchHazard = false;
        else if (s == "-trace")
            traceIdx = atoi(argv[++a]);
        else if (s == "-showdiff")
            showDiff = atoi(argv[++a]);
        else if (s == "-maxmac")
            opt.macFromMaxMini = true;
        else if (s == "-unf") {
            unfIdx = atoi(argv[++a]);
            unfPairs = atoi(argv[++a]);
        } else if (s == "-dump")
            dumpIdx = atoi(argv[++a]);
        else if (s == "-clip")
            clipOnly = true;
        else if (s == "-geom")
            geomE = atoi(argv[++a]);
        else if (s == "-skip")
            skipN = atoi(argv[++a]);
    }
    int n = 0, bad = 0, unfinished = 0, memBad = 0, regBad = 0, shown = 0;
    uint64_t stalls = 0, pairs = 0, hazards = 0;
    std::vector<uint8_t> in(16384), out(16384);
    for (;;) {
        uint32_t hdr[8];
        if (fread(hdr, 4, 8, f) != 8)
            break;
        VU1StateRec es, xs;
        uint32_t xg[64];
        if (hdr[6] != sizeof(VU1StateRec)) {
            fprintf(stderr, "state size mismatch %u vs %zu\n", hdr[6], sizeof(VU1StateRec));
            return 4;
        }
        fread(&es, sizeof es, 1, f);
        fread(&xs, sizeof xs, 1, f);
        fread(in.data(), 1, 16384, f);
        fread(out.data(), 1, 16384, f);
        fread(xg, 4, 64, f);
        if (n >= maxN)
            break;
        ++n;
        if (n <= skipN)
            continue;
        if (!hdr[4] && unfIdx == n - 1) {
            Vu1Ref r;
            r.opt = opt;
            r.opt.trace = true;
            memcpy(r.code, code.data(), 0x4000);
            memcpy(r.mem, in.data(), 0x4000);
            for (int k = 0; k < 32; ++k)
                memcpy(r.vf[k], es.vf[k], 16);
            r.vf[0][3] = 0x3f800000u;
            r.vf[0][0] = r.vf[0][1] = r.vf[0][2] = 0;
            for (int k = 0; k < 16; ++k)
                r.vi[k] = (uint16_t)es.vi[k];
            memcpy(r.acc, es.acc, 16);
            memcpy(&r.q, &es.q, 4);
            memcpy(&r.p, &es.p, 4);
            memcpy(&r.i, &es.i, 4);
            r.mac = es.mac;
            r.clip = es.clip & 0xffffff;
            r.clipRun = r.clip;
            r.top = es.top;
            r.itop = es.itop;
            bool ok = r.run(hdr[2], unfPairs);
            printf("forced run: ok=%d err=%s pc=%04x kicks=%zu\n", ok, r.error.c_str(), r.pc, r.kicks.size());
            for (int k = 1; k < 16; ++k)
                printf(" vi%d=%d", k, r.vi[k]);
            printf("\n");
            return 0;
        }
        if (!hdr[4]) {
            ++unfinished;
            if (unfinished <= 8)
                printf("unfinished rec %d: start=%04x maxCycles=%u exit pc=%04x flags=%08x cycles %llu->%llu "
                       "header9=%u,%u,%u,%u\n",
                       n - 1, hdr[2], hdr[3], xs.pc, *(uint32_t*)(in.data() + 7 * 16), (unsigned long long)es.cycles,
                       (unsigned long long)xs.cycles, ((uint32_t*)in.data())[36], ((uint32_t*)in.data())[37],
                       ((uint32_t*)in.data())[38], ((uint32_t*)in.data())[39]);
            continue;
        }
        Vu1Ref r;
        r.opt = opt;
        if (n - 1 == traceIdx)
            r.opt.trace = true;
        memcpy(r.code, code.data(), 0x4000);
        memcpy(r.mem, in.data(), 0x4000);
        for (int k = 0; k < 32; ++k)
            memcpy(r.vf[k], es.vf[k], 16);
        r.vf[0][3] = 0x3f800000u;
        r.vf[0][0] = r.vf[0][1] = r.vf[0][2] = 0;
        for (int k = 0; k < 16; ++k)
            r.vi[k] = (uint16_t)es.vi[k];
        memcpy(r.acc, es.acc, 16);
        memcpy(&r.q, &es.q, 4);
        memcpy(&r.p, &es.p, 4);
        memcpy(&r.i, &es.i, 4);
        r.r = es.r;
        r.mac = es.mac;
        r.clip = es.clip & 0xffffff;
        r.clipRun = r.clip;
        r.status = es.status;
        r.top = es.top;
        r.itop = es.itop;
        bool ok = r.run(hdr[2]);
        stalls += r.nStalls;
        pairs += r.nPairs;
        hazards += r.nHazard;
        if (!ok) {
            printf("rec %d: ref error: %s\n", n - 1, r.error.c_str());
            ++bad;
            continue;
        }
        for (auto kp : r.kickPcs)
            if (kp == 0x2578 || kp == 0x26b0)
                ++nClipKick;
        if (geomE >= 0) {
            for (size_t k = 0; k < r.packets.size(); ++k) {
                const auto& pk = r.packets[k];
                uint64_t tag[2];
                memcpy(tag, pk.data(), 16);
                uint32_t nloop = tag[0] & 0x7fff, nreg = (tag[0] >> 60) & 15;
                uint32_t prim = (tag[0] >> 47) & 0x7ff;
                if (nreg != 4)
                    continue;
                if (clipOnly && !(r.kickPcs[k] == 0x2578 || r.kickPcs[k] == 0x26b0))
                    continue;
                int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
                int adc = 0;
                uint32_t zmax = 0;
                for (uint32_t v = 0; v < nloop; ++v) {
                    uint64_t q[2];
                    memcpy(q, pk.data() + 16 + (v * 4 + 3) * 16, 16);
                    int x = (int)(q[0] & 0xffff) - 0x8000, y = (int)((q[0] >> 32) & 0xffff) - 0x8000;
                    uint32_t z = (uint32_t)q[1];
                    bool a = (q[1] >> 47) & 1;
                    adc += a;
                    minx = std::min(minx, x);
                    maxx = std::max(maxx, x);
                    miny = std::min(miny, y);
                    maxy = std::max(maxy, y);
                    zmax = std::max(zmax, z);
                }
                int ext = std::max(std::max(-minx, maxx), std::max(-miny, maxy)) / 16;
                if (ext >= geomE)
                    printf("rec %d start=%04x flags=%04x pk%zu prim=%x nv=%u adc=%d x[%d,%d] y[%d,%d] (px) ext=%d\n",
                           n - 1, hdr[2], *(uint32_t*)(in.data() + 7 * 16), k, prim & 7, nloop, adc, minx / 16,
                           maxx / 16, miny / 16, maxy / 16, ext);
            }
        }
        if (dumpIdx == n - 1) {
            for (size_t k = 0; k < r.packets.size(); ++k) {
                const auto& pk = r.packets[k];
                uint64_t tag[2];
                memcpy(tag, pk.data(), 16);
                uint32_t nloop = tag[0] & 0x7fff, nreg = (tag[0] >> 60) & 15;
                uint32_t prim = (tag[0] >> 47) & 0x7ff;
                printf("XGKICK #%zu at pc %04x addr qw%u: NLOOP=%u NREG=%u PRIM=%x%s\n", k, r.kickPcs[k], r.kicks[k],
                       nloop, nreg, prim, nreg == 4 ? "" : " (not a ST/RGBA/FOG/XYZ packet)");
                if (nreg != 4)
                    continue;
                for (uint32_t v = 0; v < nloop && v < 8; ++v) {
                    uint32_t st[4], col[4];
                    uint64_t q[2];
                    memcpy(st, pk.data() + 16 + v * 64, 16);
                    memcpy(col, pk.data() + 16 + v * 64 + 16, 16);
                    memcpy(q, pk.data() + 16 + v * 64 + 48, 16);
                    float s0, s1;
                    memcpy(&s0, &st[0], 4);
                    memcpy(&s1, &st[1], 4);
                    printf("   v%u st=(%g,%g) rgba=(%u,%u,%u,%u) xyz2: x=%.2f y=%.2f z=%u adc=%d\n", v, s0, s1, col[0],
                           col[1], col[2], col[3], ((int)(q[0] & 0xffff) - 0x8000) / 16.0,
                           ((int)((q[0] >> 32) & 0xffff) - 0x8000) / 16.0, (uint32_t)q[1], (int)((q[1] >> 47) & 1));
                }
            }
        }
        // compare memory
        int mdiff = 0;
        std::vector<int> dq;
        for (int k = 0; k < 1024; ++k)
            if (memcmp(r.mem + k * 16, out.data() + k * 16, 16)) {
                ++mdiff;
                if (dq.size() < 8)
                    dq.push_back(k);
            }
        int rdiff = 0;
        std::string rd;
        for (int k = 1; k < 16; ++k)
            if (r.vi[k] != (uint16_t)xs.vi[k]) {
                ++rdiff;
                rd += " vi" + std::to_string(k) + ":" + std::to_string(r.vi[k]) + "/" +
                      std::to_string((uint16_t)xs.vi[k]);
            }
        for (int k = 1; k < 32; ++k) {
            uint32_t a[4], b[4];
            memcpy(a, r.vf[k], 16);
            memcpy(b, xs.vf[k], 16);
            for (int c = 0; c < 4; ++c)
                if (a[c] != b[c]) {
                    ++rdiff;
                    char t[96];
                    snprintf(t, sizeof t, " vf%d.%c:%08x/%08x", k, "xyzw"[c], a[c], b[c]);
                    rd += t;
                }
        }
        if (r.pc != xs.pc) {
            char t[64];
            snprintf(t, sizeof t, " pc:%04x/%04x", r.pc, xs.pc);
            rd += t;
            ++rdiff;
        }
        if (mdiff || rdiff) {
            ++bad;
            if (mdiff)
                ++memBad;
            if (rdiff)
                ++regBad;
            if (shown < showDiff || verbose) {
                ++shown;
                printf("rec %d start=%04x flags=%x: %d qw differ (first:", n - 1, hdr[2],
                       *(uint32_t*)(in.data() + 7 * 16), mdiff);
                for (int k : dq)
                    printf(" %d", k);
                printf("), regs:%s\n", rd.c_str());
                for (int k : dq) {
                    uint32_t a[4], b[4];
                    memcpy(a, r.mem + k * 16, 16);
                    memcpy(b, out.data() + k * 16, 16);
                    printf("   qw%d ref %08x %08x %08x %08x | rt %08x %08x %08x %08x\n", k, a[0], a[1], a[2], a[3],
                           b[0], b[1], b[2], b[3]);
                }
            }
        }
    }
    printf("clipper XGKICKs %d\n", nClipKick);
    printf("records %d  finished %d  mismatching %d (mem %d, regs %d)  ref stalls %llu / %llu pairs, branch hazards "
           "used %llu\n",
           n, n - unfinished, bad, memBad, regBad, (unsigned long long)stalls, (unsigned long long)pairs,
           (unsigned long long)hazards);
    return 0;
}
