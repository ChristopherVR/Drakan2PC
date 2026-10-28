// smoke test: replay PS2X_VU1_DUMP files (entry data only) from candidate entry pcs and compare the XGKICK'd packet.
#include "vu1ref.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char** argv) {
    std::vector<uint8_t> code(0x4000, 0);
    {
        FILE* f = fopen(argv[1], "rb");
        fread(code.data(), 1, 0x4000, f);
        fclose(f);
    }
    int ok = 0, tot = 0;
    for (int a = 2; a < argc; ++a) {
        FILE* f = fopen(argv[a], "rb");
        if (!f)
            continue;
        uint32_t hdr[5];
        fread(hdr, 4, 5, f);
        std::vector<uint8_t> vfvi(128 * 4 + 16 * 4), data(hdr[2]), c(hdr[3]);
        fread(vfvi.data(), 1, vfvi.size(), f);
        fread(data.data(), 1, hdr[2], f);
        fread(c.data(), 1, hdr[3], f);
        uint32_t ex[2];
        fread(ex, 4, 2, f);
        std::vector<uint8_t> entry(hdr[2]);
        fread(entry.data(), 1, hdr[2], f);
        fclose(f);
        ++tot;
        const uint32_t cands[] = {0, 0x26e0, 0x1f0, 0xe70, 0x1018, 0x1580};
        bool found = false;
        for (uint32_t pc : cands) {
            Vu1Ref r;
            memcpy(r.code, code.data(), 0x4000);
            memcpy(r.mem, entry.data(), 0x4000);
            r.top = ex[0];
            r.itop = ex[1];
            r.run(pc);
            for (size_t k = 0; k < r.kicks.size(); ++k)
                if ((uint32_t)r.kicks[k] == hdr[4]) {
                    // compare packet bytes against dump data at xg (size of ref packet)
                    if (!memcmp(r.packets[k].data(), data.data() + hdr[4] * 16,
                                std::min<size_t>(r.packets[k].size(), 0x4000 - hdr[4] * 16))) {
                        found = true;
                    }
                }
            if (found) {
                printf("%s: ok from pc %04x (stalls %llu)\n", argv[a], pc, (unsigned long long)r.nStalls);
                break;
            }
        }
        if (found)
            ++ok;
        else
            printf("%s: no match (xg=%u pc=%04x)\n", argv[a], hdr[4], hdr[1]);
    }
    printf("%d/%d\n", ok, tot);
}
