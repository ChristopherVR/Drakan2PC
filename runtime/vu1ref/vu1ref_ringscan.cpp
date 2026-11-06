// vu1ref_ringscan: replays a DRAKAN_VU_RING dump (DRAKAN_VU_RECORD format) through the reference simulator and lists the
// XGKICK vertices that have a negative Q (behind the camera). usage: vu1ref_ringscan <micromem.bin> <ring.rec> [-v]
#include "vu1ref.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
struct RecHeader {
    uint32_t magic, count, codeSize, stateSize;
};
struct RecEntry {
    uint32_t resume, startPC, top, itop, dEn, tEn;
};

int main(int argc, char** argv) {
    if (argc < 3)
        return 1;
    bool verbose = argc > 3 && std::string(argv[3]) == "-v";
    std::vector<uint8_t> code(0x4000, 0);
    FILE* f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fread(code.data(), 1, 0x4000, f);
    fclose(f);
    f = fopen(argv[2], "rb");
    if (!f)
        return 3;
    RecHeader h;
    fread(&h, sizeof h, 1, f);
    std::vector<uint8_t> rcode(h.codeSize);
    fread(rcode.data(), 1, h.codeSize, f);
    VU1StateRec init;
    fread(&init, sizeof init, 1, f);
    Vu1Ref r;
    memcpy(r.code, rcode.data(), std::min<size_t>(rcode.size(), 0x4000));
    for (int k = 0; k < 32; ++k)
        memcpy(r.vf[k], init.vf[k], 16);
    for (int k = 0; k < 16; ++k)
        r.vi[k] = (uint16_t)init.vi[k];
    std::vector<uint8_t> in(16384);
    int badRuns = 0;
    std::vector<uint8_t> prevMem;
    std::vector<uint32_t> prevKicks;
    long cmpSame = 0, cmpDiff = 0;
    std::map<unsigned long long, long> hist;
    FILE* fs = fopen((std::string(argv[2]) + ".states").c_str(), "rb");
    if (!fs) {
        fprintf(stderr, "missing .states file\n");
        return 4;
    }
    for (uint32_t n = 0; n < h.count; ++n) {
        RecEntry e;
        if (fread(&e, sizeof e, 1, f) != 1 || fread(in.data(), 1, 16384, f) != 16384)
            break;
        if (!prevMem.empty())
            for (uint32_t kq : prevKicks)
                for (uint32_t q = kq; q < kq + 24 && q < 1024; ++q) {
                    if (!memcmp(prevMem.data() + q * 16, in.data() + q * 16, 16))
                        ++cmpSame;
                    else
                        ++cmpDiff;
                }
        memcpy(r.mem, in.data(), 16384);
        VU1StateRec es;
        if (fread(&es, sizeof es, 1, fs) != 1)
            break;
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
        r.top = e.top;
        r.itop = e.itop;
        r.packets.clear();
        r.kicks.clear();
        r.kickPcs.clear();
        const uint32_t pc = e.startPC;
        r.run(pc);
        prevMem.assign(r.mem, r.mem + 16384);
        prevKicks = r.kicks;
        for (size_t k = 0; k < r.packets.size(); ++k) {
            const auto& pk = r.packets[k];
            uint64_t tag[2];
            memcpy(tag, pk.data(), 16);
            uint32_t nloop = tag[0] & 0x7fff, nreg = (tag[0] >> 60) & 15;
            int stIdx = -1, xyIdx = -1;
            for (uint32_t i = 0; i < nreg; ++i) {
                const uint32_t d = (tag[1] >> (4 * i)) & 15;
                if (d == 2)
                    stIdx = i;
                if (d == 5 || d == 4)
                    xyIdx = i;
            }
            hist[tag[1] & 0xffffffffffffull]++;
            if (stIdx < 0 || xyIdx < 0 || ((tag[0] >> 58) & 3) != 0)
                continue;
            int neg = 0, corner = 0, bigz = 0;
            for (uint32_t v = 0; v < nloop; ++v) {
                uint32_t st[4];
                uint64_t q[2];
                memcpy(st, pk.data() + 16 + (v * nreg + stIdx) * 16, 16);
                memcpy(q, pk.data() + 16 + (v * nreg + xyIdx) * 16, 16);
                float qf;
                memcpy(&qf, &st[2], 4);

                if ((q[0] & 0xffff) == 0xffff && ((q[0] >> 32) & 0xffff) == 0xffff)
                    ++corner;
                if ((uint32_t)q[1] >= 0x80000000u)
                    ++bigz;
            }
            {
                // triangles the GS would draw (strip/list/fan; a vertex with ADC set is queued but does not kick)
                const uint32_t pt = (tag[0] >> 47) & 7;
                std::vector<float> qs(nloop);
                std::vector<int> ad(nloop);
                for (uint32_t v = 0; v < nloop; ++v) {
                    uint32_t st[4];
                    uint64_t q[2];
                    memcpy(st, pk.data() + 16 + (v * nreg + stIdx) * 16, 16);
                    memcpy(q, pk.data() + 16 + (v * nreg + xyIdx) * 16, 16);
                    memcpy(&qs[v], &st[2], 4);
                    ad[v] = (int)((q[1] >> 47) & 1);
                }
                for (uint32_t v = 2; v < nloop; ++v) {
                    if (ad[v] || pt < 3)
                        continue;
                    uint32_t a = v - 2, b = v - 1;
                    if (pt == 3 && (v % 3) != 2)
                        continue;
                    if (pt == 5) {
                        a = 0;
                    }
                    {
                        const int np = (qs[a] > 0) + (qs[b] > 0) + (qs[v] > 0);
                        if (np > 0 && np < 3)
                            ++neg; // mixed-sign Q: a triangle straddling the camera plane
                    }
                }
            }
            if (neg || corner || bigz) {
                ++badRuns;
                printf("regs=%016llx ", (unsigned long long)tag[1]);
                printf("run %u (%s start=%04x kick pc=%04x flags[7].x=%08x top=%u itop=%u) pk%zu prim=%llx nv=%u neg-q=%d corner=%d z>=2^31=%d\n",
                       n, e.resume ? "mscnt" : "mscal", pc, r.kickPcs[k], *(uint32_t*)(in.data() + 112), e.top, e.itop, k,
                       (unsigned long long)((tag[0] >> 47) & 0x7ff), nloop, neg, corner, bigz);
                if (verbose && neg)
                    for (uint32_t v = 0; v < nloop; ++v) {
                        uint32_t st[4];
                        uint64_t q[2];
                        memcpy(st, pk.data() + 16 + (v * nreg + stIdx) * 16, 16);
                        memcpy(q, pk.data() + 16 + (v * nreg + xyIdx) * 16, 16);
                        float a, b, c;
                        memcpy(&a, &st[0], 4);
                        memcpy(&b, &st[1], 4);
                        memcpy(&c, &st[2], 4);
                        printf("   v%u stq=(%g,%g,%g) x=%.1f y=%.1f z=%u adc=%d\n", v, a, b, c,
                               ((int)(q[0] & 0xffff) - 0x8000) / 16.0, ((int)((q[0] >> 32) & 0xffff) - 0x8000) / 16.0,
                               (uint32_t)q[1], (int)((q[1] >> 47) & 1));
                    }
            }
        }
    }
    printf("exit-vs-next-entry kick region qw same %ld diff %ld\n", cmpSame, cmpDiff);
    for (auto& kv : hist)
        printf("packet regs %016llx: %ld packets\n", kv.first, kv.second);
    printf("scanned %u runs, %d runs with suspicious vertices\n", h.count, badRuns);
}
