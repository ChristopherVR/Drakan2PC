// smoke test: replay PS2X_VU1_DUMP files (entry data only) from candidate entry pcs and compare the XGKICK'd packet.
#include "vu1ref.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t kVuMem = 0x4000;      // VU1 data memory: the entry snapshot and the packet window
constexpr uint32_t kMaxBlob = 16u << 20; // header sizes above this are a corrupt dump, not data
bool readAll(FILE* f, void* p, size_t n) {
    return n == 0 || fread(p, 1, n, f) == n;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <vu1 microcode> <dump>...\n", argv[0]);
        return 2;
    }
    std::vector<uint8_t> code(0x4000, 0);
    {
        FILE* f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[1]);
            return 2;
        }
        fread(code.data(), 1, 0x4000, f); // a shorter image leaves the rest zero
        fclose(f);
    }
    int ok = 0, tot = 0;
    for (int a = 2; a < argc; ++a) {
        FILE* f = fopen(argv[a], "rb");
        if (!f)
            continue;
        uint32_t hdr[5];
        // hdr[2] = data / entry size (both copied as a full VU memory), hdr[3] = code size, hdr[4] = XGKICK qword
        if (!readAll(f, hdr, sizeof hdr) || hdr[2] < kVuMem || hdr[2] > kMaxBlob || hdr[3] > kMaxBlob ||
            hdr[4] >= kVuMem / 16) {
            printf("%s: bad header\n", argv[a]);
            fclose(f);
            continue;
        }
        std::vector<uint8_t> vfvi(128 * 4 + 16 * 4), data(hdr[2]), c(hdr[3]);
        uint32_t ex[2];
        std::vector<uint8_t> entry(hdr[2]);
        const bool read = readAll(f, vfvi.data(), vfvi.size()) && readAll(f, data.data(), hdr[2]) &&
                          readAll(f, c.data(), hdr[3]) && readAll(f, ex, sizeof ex) && readAll(f, entry.data(), hdr[2]);
        fclose(f);
        if (!read) {
            printf("%s: truncated\n", argv[a]);
            continue;
        }
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
