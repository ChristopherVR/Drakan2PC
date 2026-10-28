// fmv_test <file.VDB> <movie id> <max frames> <ref_prefix> <out_dir> [bmp frame indices...]
// Decodes frames with the native decoder and compares with FFmpeg raw dumps:
//   <ref_prefix>.yuv  (ffmpeg -pix_fmt yuv420p -f rawvideo)   and   <ref_prefix>.rgba (-pix_fmt rgba); either optional.
// Prints PSNR summary, writes BMPs, and measures decode speed.
//
// fmv_test --synthetic   self-contained checks (bit reader, colour conversion, bad input); needs no game data.
// fmv_test --from-env    as above, but the arguments come from FMV_VDB (required; exits 77 = skipped when unset),
//                        FMV_MOVIE_ID (0), FMV_MAX_FRAMES (30), FMV_REF_PREFIX ("none") and FMV_OUT_DIR (".").
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "fmv/fmv.h"
#include "fmv/ipu_adapter.h"

using namespace fmv;
static std::vector<uint8_t> read_at(std::ifstream& f, uint64_t off, uint64_t n) {
    std::vector<uint8_t> v(n);
    f.seekg((std::streamoff)off);
    f.read((char*)v.data(), (std::streamsize)n);
    return v;
}
static uint32_t u16(const uint8_t* p) {
    return p[0] | p[1] << 8;
}
static uint32_t u32(const uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write_bmp(const std::string& path, const uint8_t* rgba, int w, int h) {
    uint32_t rs = ((uint32_t)w * 3 + 3) & ~3u;
    uint32_t sz = 54 + rs * h;
    std::ofstream o(path, std::ios::binary);
    uint8_t hd[54] = {'B', 'M'};
    uint32_t off = 54, dib = 40;
    uint16_t pl = 1, bpp = 24;
    memcpy(hd + 2, &sz, 4);
    memcpy(hd + 10, &off, 4);
    memcpy(hd + 14, &dib, 4);
    memcpy(hd + 18, &w, 4);
    memcpy(hd + 22, &h, 4);
    memcpy(hd + 26, &pl, 2);
    memcpy(hd + 28, &bpp, 2);
    o.write((char*)hd, 54);
    std::vector<uint8_t> line(rs);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            line[x * 3] = p[2];
            line[x * 3 + 1] = p[1];
            line[x * 3 + 2] = p[0];
        }
        o.write((char*)line.data(), (std::streamsize)rs);
    }
}
static double psnr(const uint8_t* a, const uint8_t* b, size_t n, bool skip_alpha = false) {
    double se = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        if (skip_alpha && (i & 3) == 3)
            continue;
        double d = (double)a[i] - b[i];
        se += d * d;
        ++cnt;
    }
    if (se == 0)
        return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 * (double)cnt / se);
}

static int g_synthFail = 0;
#define SYNTH_CHECK(c)                                                                                                 \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            ++g_synthFail;                                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                               \
        }                                                                                                              \
    } while (0)

static int runSynthetic() {
    const uint8_t bytes[] = {0xA5, 0x0F};
    BitReader br(bytes, sizeof bytes);
    SYNTH_CHECK(br.get(4) == 0xA && br.get(4) == 0x5 && br.tell() == 8);
    SYNTH_CHECK(br.peek(8) == 0x0F && br.get(8) == 0x0F && !br.overrun());
    SYNTH_CHECK(br.get(8) == 0 && br.overrun() && br.bits_left() == 0); // reads past the end are zero + flagged

    YuvFrame f;
    f.alloc(20, 18); // padded to whole macroblocks
    SYNTH_CHECK(f.stride_y == 32 && f.stride_c == 16 && f.padded_h == 32);
    SYNTH_CHECK(f.y.size() == 32u * 32 && f.cb.size() == 16u * 16 && f.cb[0] == 128);

    // BT.601 limited range: Y=16 is black, Y=235 white, Y=126 mid grey (neutral chroma)
    std::vector<uint8_t> rgba(20 * 18 * 4);
    const int ys[3] = {16, 126, 235}, want[3] = {0, 128, 255};
    for (int i = 0; i < 3; ++i) {
        std::fill(f.y.begin(), f.y.end(), (uint8_t)ys[i]);
        yuv_to_rgba(f, rgba.data(), 20 * 4, 0x80);
        SYNTH_CHECK(rgba[0] == want[i] && rgba[1] == want[i] && rgba[2] == want[i] && rgba[3] == 0x80);
    }
    std::fill(f.y.begin(), f.y.end(), (uint8_t)126);
    std::fill(f.cr.begin(), f.cr.end(), (uint8_t)255); // strong red chroma at mid luma: R up, B down
    yuv_to_rgba(f, rgba.data(), 20 * 4, 255);
    SYNTH_CHECK(rgba[0] == 255 && rgba[1] < 128 && rgba[2] == 128);

    // Garbage must be rejected without crashing.
    FrameDecoder dec;
    const uint8_t junk[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    YuvFrame out;
    SYNTH_CHECK(!dec.decode(junk, sizeof junk, 32, 32, out));
    SYNTH_CHECK(!dec.decode(junk, 1, 32, 32, out));

    printf("fmv_test --synthetic: %s\n", g_synthFail ? "FAILED" : "ok");
    return g_synthFail ? 1 : 0;
}

static int runMain(int argc, char** argv);

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--synthetic"))
        return runSynthetic();
    if (argc == 2 && !strcmp(argv[1], "--from-env")) {
        const char* vdb = getenv("FMV_VDB");
        if (!vdb || !*vdb) {
            printf("SKIP: FMV_VDB not set (no .VDB movie archive available)\n");
            return 77;
        }
        auto env = [](const char* k, const char* d) {
            const char* v = getenv(k);
            return std::string(v && *v ? v : d);
        };
        std::string v = vdb, id = env("FMV_MOVIE_ID", "0"), mf = env("FMV_MAX_FRAMES", "30"),
                    ref = env("FMV_REF_PREFIX", "none"), od = env("FMV_OUT_DIR", ".");
        char* a[] = {argv[0], v.data(), id.data(), mf.data(), ref.data(), od.data()};
        return runMain(6, a);
    }
    return runMain(argc, argv);
}

static int runMain(int argc, char** argv) {
    if (argc < 6) {
        fprintf(
            stderr,
            "usage: fmv_test file.VDB id maxframes ref_prefix out_dir [bmp frames...] | --synthetic | --from-env\n");
        return 2;
    }
    std::string vdb = argv[1];
    int id = atoi(argv[2]);
    uint32_t maxf = (uint32_t)atoi(argv[3]);
    std::string refp = argv[4], outd = argv[5];
    std::ifstream f(vdb, std::ios::binary);
    if (!f) {
        fprintf(stderr, "cannot open %s\n", vdb.c_str());
        return 2;
    }
    auto hd = read_at(f, 0, 16);
    if (memcmp(hd.data(), "SRSC", 4)) {
        fprintf(stderr, "not SRSC\n");
        return 2;
    }
    uint32_t idx = u32(&hd[6]), cnt = u32(&hd[10]);
    auto ix = read_at(f, idx, 14ull * cnt);
    uint64_t o342 = 0, s342 = 0, o343 = 0, s343 = 0;
    for (uint32_t i = 0; i < cnt; ++i) {
        const uint8_t* e = &ix[i * 14];
        if ((int)u16(e + 2) != id)
            continue;
        if (u16(e) == 0x342) {
            o342 = u32(e + 6);
            s342 = u32(e + 10);
        }
        if (u16(e) == 0x343) {
            o343 = u32(e + 6);
            s343 = u32(e + 10);
        }
    }
    if (!s342 || !s343) {
        fprintf(stderr, "movie id %d not found\n", id);
        return 2;
    }
    auto h342 = read_at(f, o342, s342);
    // Load only the needed prefix of the 0x343 blob; truncate the header's frame table accordingly.
    uint32_t nl = u16(h342.data());
    uint32_t nfr = u32(&h342[2 + nl + 8]);
    uint32_t want = std::min(nfr, maxf);
    uint32_t need = u32(&h342[2 + nl + 12 + 4 * (size_t)want]);
    auto blob = read_at(f, o343, need);
    for (uint32_t i = want + 1; i < nfr + 2; ++i)
        memcpy(&h342[2 + nl + 12 + 4 * (size_t)i], &need, 4);
    memcpy(&h342[2 + nl + 8], &want, 4);
    Movie m;
    if (!m.open_srsc(h342.data(), h342.size(), blob.data(), blob.size())) {
        fprintf(stderr, "open: %s\n", m.error().c_str());
        return 1;
    }
    int W = (int)m.width(), H = (int)m.height();
    printf("movie '%s' %dx%d, %u frames in file, testing %u (frame 0 flags byte = 0x%02x)\n", m.name().c_str(), W, H,
           nfr, m.frame_count(), m.frame(0).data[0]);

    std::ifstream ry(refp + ".yuv", std::ios::binary), rr(refp + ".rgba", std::ios::binary);
    size_t ysz = (size_t)W * H, csz = (size_t)(W / 2) * (H / 2), fsz = ysz + 2 * csz, rsz = (size_t)W * H * 4;
    YuvFrame yf;
    std::vector<uint8_t> rgba(rsz), refy(fsz), refr(rsz), mine_yuv(fsz);
    double miny = 1e9, sumy = 0, minr = 1e9, sumr = 0;
    int ny = 0, nr = 0, bad = 0, maxdiff = 0;
    uint32_t worst = 0;
    std::vector<uint32_t> bmps;
    for (int a = 6; a < argc; ++a)
        bmps.push_back((uint32_t)atoi(argv[a]));
    if (bmps.empty())
        bmps = {0, m.frame_count() / 2, m.frame_count() - 1};
    std::string tag = m.name().substr(0, 3);
    for (char& c : tag)
        if (!isalnum((unsigned char)c))
            c = '_';
    for (uint32_t n = 0; n < m.frame_count(); ++n) {
        if (!m.decode_yuv(n, yf)) {
            printf("frame %u: decode error: %s\n", n, m.error().c_str());
            ++bad;
            continue;
        }
        yuv_to_rgba(yf, rgba.data(), (size_t)W * 4, 255);
        if (ry) {
            ry.read((char*)refy.data(), (std::streamsize)fsz);
            if (ry) {
                for (int y = 0; y < H; ++y)
                    memcpy(&mine_yuv[(size_t)y * W], &yf.y[(size_t)y * yf.stride_y], (size_t)W);
                for (int y = 0; y < H / 2; ++y) {
                    memcpy(&mine_yuv[ysz + (size_t)y * (W / 2)], &yf.cb[(size_t)y * yf.stride_c], (size_t)W / 2);
                    memcpy(&mine_yuv[ysz + csz + (size_t)y * (W / 2)], &yf.cr[(size_t)y * yf.stride_c], (size_t)W / 2);
                }
                if (const char* dp = getenv("FMV_DUMP")) { // debug: append our YUV420 output to a raw file
                    std::ofstream dd(dp, std::ios::binary | std::ios::app);
                    dd.write((const char*)mine_yuv.data(), (std::streamsize)fsz);
                }
                double p = psnr(mine_yuv.data(), refy.data(), fsz);
                for (size_t i = 0; i < fsz; ++i)
                    maxdiff = std::max(maxdiff, std::abs((int)mine_yuv[i] - refy[i]));
                if (p < 40 && getenv("FMV_VERBOSE"))
                    printf("  frame %u psnr %.2f\n", n, p);
                if (p < miny) {
                    miny = p;
                    worst = n;
                }
                sumy += p;
                ++ny;
            }
        }
        if (rr) {
            rr.read((char*)refr.data(), (std::streamsize)rsz);
            if (rr) {
                double p = psnr(rgba.data(), refr.data(), rsz, true);
                minr = std::min(minr, p);
                sumr += p;
                ++nr;
            }
        }
        for (uint32_t b : bmps)
            if (b == n)
                write_bmp(outd + "/" + tag + "_" + std::to_string(id) + "_f" + std::to_string(n) + ".bmp", rgba.data(),
                          W, H);
    }
    if (ny)
        printf("YUV420 vs FFmpeg: %d frames, PSNR mean %.2f dB, min %.2f dB (frame %u), max abs diff %d\n", ny,
               sumy / ny, miny, worst, maxdiff);
    if (nr)
        printf("RGBA   vs FFmpeg(swscale): %d frames, PSNR mean %.2f dB, min %.2f dB\n", nr, sumr / nr, minr);
    printf("decode errors: %d\n", bad);

    { // IPU adapter smoke test: frame 0 through IpuEmu must equal the frame decoder's RGBA (alpha 0x80) in MB order
        FrameSpan fs = m.frame(0);
        IpuEmu ipu;
        ipu.feed(fs.data + 1, fs.size - 1); // the flags byte is consumed by FDEC 8 on the real IPU
        ipu.write_cmd(0x40000000u | 0);
        ipu.set_ctrl((uint32_t)fs.data[0] << 16);
        ipu.set_picture_size(W, H);
        ipu.write_cmd(0x10010000u);
        std::vector<uint32_t> mbs((size_t)W * H);
        size_t q = ipu.drain_out(mbs.data(), (size_t)W * H / 4);
        m.decode_rgba(0, rgba.data(), (size_t)W * 4, 0x80);
        size_t diff = 0;
        const uint32_t* ref = (const uint32_t*)rgba.data();
        for (int my = 0; my < H / 16; ++my)
            for (int mx = 0; mx < W / 16; ++mx)
                for (int k = 0; k < 256; ++k)
                    diff += mbs[((size_t)my * (W / 16) + mx) * 256 + k] !=
                            ref[(size_t)(my * 16 + k / 16) * W + mx * 16 + k % 16];
        printf("IPU adapter: %zu qwords out (expect %zu), mismatching pixels %zu\n", q, (size_t)W * H / 4, diff);
    }
    for (int pass = 0; pass < 2; ++pass) {
        auto t0 = std::chrono::steady_clock::now();
        uint32_t cntf = 0;
        for (int rep = 0; rep < 3; ++rep)
            for (uint32_t n = 0; n < m.frame_count(); ++n) {
                if (pass == 0)
                    m.decode_yuv(n, yf);
                else
                    m.decode_rgba(n, rgba.data(), (size_t)W * 4);
                ++cntf;
            }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("speed %s %dx%d: %u frames in %.3f s = %.1f fps (%.2f ms/frame)\n", pass ? "decode+RGBA" : "decode YUV",
               W, H, cntf, s, cntf / s, 1000 * s / cntf);
    }
    return bad ? 1 : 0;
}
