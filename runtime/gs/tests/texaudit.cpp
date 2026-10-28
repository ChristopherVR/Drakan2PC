// texaudit - replay a DRKGSCAP capture through gsr and audit every texture gsr decodes from local memory.
//   texaudit <capture.bin> [--pool DIR] [--out DIR] [--report FILE] [--max-frames N] [--png N] [--max-detail N]
//
// Two independent references for each texture (every mip level, every use):
//  (a) the raw host->local IMAGE stream of the upload that wrote the texture (same base pointer, same PSM, same size),
//  read as a
//      plain raster. Indexed textures are resolved through the raw CLUT upload (CT32 16x16 raster, 8-bit: entry j =
//      raster swap34(j) = the GS CSM1 bit3/bit4 rule, 4-bit: raster order). CT16/CT24 follow the TEXA rules (written
//      here again, not shared with gsr).
//  (b) ground truth from the disc: the pixel bytes of the upload are hashed and looked up in a pool of every ENGLISH
//  TXD texture
//      (texaudit_pool.py -> pool.bin); the TXD image (palette B,G,R,A; alpha 0..0x80; 16 bit ARGB1555; 24 bit RGB; 32
//      bit RGBA) is what the texture is supposed to look like. The game edits palette alpha, so only RGB is compared
//      for this one.
// "stale" = a draw reused a cached texture (cached events) whose content differs from the reference of the CURRENT
// upload.
#include "gsr/gsr.h"
#include "gs_internal.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace gsr;

static uint64_t fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
static uint32_t swap34(uint32_t i) {
    return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}

// ------------------------------------------------------------------------------------------------ TXD pool
struct PoolTex {
    uint32_t file, id, w, h, bpp, palKind;
    std::vector<uint8_t> pal, pix;
    std::vector<uint32_t> rgba; // expected, R low byte, alpha in GS units
};
static std::vector<PoolTex> g_pool;
static std::vector<std::string> g_files;

static uint32_t poolPal32(const PoolTex& t, unsigned i) {
    if (t.palKind == 1) {
        const uint8_t* p = &t.pal[i * 4];
        return p[2] | (p[1] << 8) | (p[0] << 16) | ((uint32_t)p[3] << 24); // stored B,G,R,A
    }
    const uint32_t v = t.pal[i * 2] | (t.pal[i * 2 + 1] << 8);
    return ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) | ((v & 0x8000) ? 0x80000000u : 0);
}
static void decodePool(PoolTex& t) {
    t.rgba.resize((size_t)t.w * t.h);
    for (size_t i = 0; i < t.rgba.size(); ++i) {
        uint32_t c = 0;
        switch (t.bpp) {
            case 8:
                c = poolPal32(t, t.pix[i]);
                break;
            case 16: {
                const uint32_t v = t.pix[i * 2] | (t.pix[i * 2 + 1] << 8);
                c = ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) |
                    ((v & 0x8000) ? 0x80000000u : 0);
                break;
            }
            case 24:
                c = t.pix[i * 3] | (t.pix[i * 3 + 1] << 8) | (t.pix[i * 3 + 2] << 16) | 0x80000000u;
                break;
            case 32:
                c = t.pix[i * 4] | (t.pix[i * 4 + 1] << 8) | (t.pix[i * 4 + 2] << 16) |
                    ((uint32_t)t.pix[i * 4 + 3] << 24);
                break;
        }
        t.rgba[i] = c;
    }
}
static bool loadPool(const std::string& dir) {
    FILE* f = std::fopen((dir + "/pool.bin").c_str(), "rb");
    if (!f)
        return false;
    uint32_t h[8];
    while (std::fread(h, 4, 8, f) == 8) {
        PoolTex t;
        t.file = h[0];
        t.id = h[1];
        t.w = h[2];
        t.h = h[3];
        t.bpp = h[4];
        t.palKind = h[5];
        t.pal.resize(h[6]);
        t.pix.resize(h[7]);
        if (h[6] && std::fread(t.pal.data(), 1, h[6], f) != h[6])
            break;
        if (h[7] && std::fread(t.pix.data(), 1, h[7], f) != h[7])
            break;
        g_pool.push_back(std::move(t));
    }
    std::fclose(f);
    FILE* g = std::fopen((dir + "/pool.txt").c_str(), "r");
    char line[512];
    while (g && std::fgets(line, sizeof line, g)) {
        char* sp = std::strchr(line, ' ');
        if (sp) {
            std::string n = sp + 1;
            while (!n.empty() && (n.back() == '\n' || n.back() == '\r'))
                n.pop_back();
            g_files.push_back(n);
        }
    }
    if (g)
        std::fclose(g);
    return !g_pool.empty();
}

static void savePng(const std::string& path, const uint32_t* rgba, uint32_t w, uint32_t h) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const uint32_t c = rgba[i];
        img.rgba[i * 4 + 0] = (uint8_t)c;
        img.rgba[i * 4 + 1] = (uint8_t)(c >> 8);
        img.rgba[i * 4 + 2] = (uint8_t)(c >> 16);
        img.rgba[i * 4 + 3] = (uint8_t)std::min<uint32_t>(255, (c >> 24) * 2);
    }
    writePng(path.c_str(), img);
}

// ------------------------------------------------------------------------------------------------ uploads
struct Up {
    uint32_t dbp = 0, dbw = 0, dpsm = 0, x = 0, y = 0, w = 0, h = 0;
    std::vector<uint8_t> data;
    uint64_t hash = 0, seq = 0;
    int pixPool = -1, palPool = -1;
};

// ------------------------------------------------------------------------------------------------ reference decode
static uint32_t refExpand16(uint32_t v, uint64_t texa) {
    const uint32_t ta0 = (uint32_t)(texa & 0xFF), ta1 = (uint32_t)((texa >> 32) & 0xFF);
    const bool aem = (texa >> 15) & 1;
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    uint32_t a = (v & 0x8000) ? ta1 : ta0;
    if (aem && (v & 0x7FFF) == 0)
        a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}
static uint32_t refExpand24(uint32_t v, uint64_t texa) {
    uint32_t a = (uint32_t)(texa & 0xFF);
    if (((texa >> 15) & 1) && (v & 0xFFFFFF) == 0)
        a = 0;
    return (v & 0xFFFFFF) | (a << 24);
}
// stream element i of an upload in its own format
static bool streamValue(const Up& u, uint32_t psm, size_t i, uint32_t& v) {
    switch (psm) {
        case PSM_CT32:
            if ((i + 1) * 4 > u.data.size())
                return false;
            std::memcpy(&v, &u.data[i * 4], 4);
            return true;
        case PSM_CT24:
            if ((i + 1) * 3 > u.data.size())
                return false;
            v = u.data[i * 3] | (u.data[i * 3 + 1] << 8) | (u.data[i * 3 + 2] << 16);
            return true;
        case PSM_CT16:
        case PSM_CT16S:
            if ((i + 1) * 2 > u.data.size())
                return false;
            v = u.data[i * 2] | (u.data[i * 2 + 1] << 8);
            return true;
        case PSM_T8:
            if (i >= u.data.size())
                return false;
            v = u.data[i];
            return true;
        case PSM_T4:
            if (i / 2 >= u.data.size())
                return false;
            v = (u.data[i / 2] >> ((i & 1) * 4)) & 15;
            return true;
        default:
            return false;
    }
}

struct Verdict {
    enum Kind { Ok, Mismatch, NoRef } kind = NoRef;
    std::string why;
    size_t firstBad = 0;
    uint32_t got = 0, exp = 0;
    size_t nBad = 0;
};

// reference for one mip level; false when the level has no matching upload / unsupported format
static bool refLevel(const Up& u, uint64_t tex0, uint64_t texa, const Up* palUp, uint32_t w, uint32_t h,
                     std::vector<uint32_t>& out, std::string& why) {
    const uint32_t psm = (uint32_t)bitsOf(tex0, 20, 6);
    if (u.dpsm != psm) {
        why = "upload psm differs";
        return false;
    }
    if (u.w != w || u.h != h) {
        why = "upload size differs";
        return false;
    }
    const bool indexed = isIndexed(psm);
    const uint32_t csa = (uint32_t)bitsOf(tex0, 56, 5), csm = (uint32_t)bitsOf(tex0, 55, 1),
                   cpsm = (uint32_t)bitsOf(tex0, 51, 4);
    std::vector<uint32_t> pal;
    if (indexed) {
        if (psm != PSM_T8 && psm != PSM_T4) {
            why = "unsupported indexed psm";
            return false;
        }
        if (csm != 0 || cpsm != 0) {
            why = "CSM2 / 16-bit CLUT not supported by the audit";
            return false;
        }
        if (!palUp || palUp->dpsm != PSM_CT32) {
            why = "no CT32 CLUT upload at CBP";
            return false;
        }
        const size_t n = palUp->data.size() / 4;
        if (psm == PSM_T8) {
            if (n < 256 || palUp->w != 16 || palUp->h != 16) {
                why = "CLUT upload is not 16x16";
                return false;
            }
            pal.resize(256);
            for (uint32_t j = 0; j < 256; ++j)
                std::memcpy(&pal[j], &palUp->data[swap34(j) * 4], 4);
        } else {
            if (csa != 0 || n < 16) {
                why = "4-bit CLUT with CSA != 0 not supported by the audit";
                return false;
            }
            pal.resize(16);
            for (uint32_t j = 0; j < 16; ++j)
                std::memcpy(&pal[j], &palUp->data[j * 4], 4);
        }
    }
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); ++i) {
        uint32_t v;
        if (!streamValue(u, psm, i, v)) {
            why = "upload shorter than the texture";
            return false;
        }
        switch (psm) {
            case PSM_CT32:
                out[i] = v;
                break;
            case PSM_CT24:
                out[i] = refExpand24(v, texa);
                break;
            case PSM_CT16:
            case PSM_CT16S:
                out[i] = refExpand16(v, texa);
                break;
            default:
                out[i] = pal[v];
                break;
        }
    }
    return true;
}

static Verdict compare(const uint32_t* got, const std::vector<uint32_t>& ref, int tolRgb, int tolA) {
    Verdict v;
    v.kind = Verdict::Ok;
    for (size_t i = 0; i < ref.size(); ++i) {
        int d = 0;
        for (int k = 0; k < 24; k += 8)
            d = std::max(d, std::abs((int)((got[i] >> k) & 255) - (int)((ref[i] >> k) & 255)));
        const int da = std::abs((int)(got[i] >> 24) - (int)(ref[i] >> 24));
        if (d > tolRgb || da > tolA) {
            if (v.nBad++ == 0) {
                v.firstBad = i;
                v.got = got[i];
                v.exp = ref[i];
            }
            v.kind = Verdict::Mismatch;
        }
    }
    return v;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: texaudit <capture.bin> [--pool DIR] [--out DIR] [--report FILE] [--max-frames N] [--png N] "
                    "[--max-detail N]\n");
        return 1;
    }
    std::string in = argv[1], poolDir, outDir = GSR_OUT_DIR "\\texaudit", reportPath;
    int maxFrames = 1 << 30, maxPng = 0, maxDetail = 60;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--pool") && i + 1 < argc)
            poolDir = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--report") && i + 1 < argc)
            reportPath = argv[++i];
        else if (!std::strcmp(argv[i], "--max-frames") && i + 1 < argc)
            maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--png") && i + 1 < argc)
            maxPng = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-detail") && i + 1 < argc)
            maxDetail = std::atoi(argv[++i]);
    }
    if (poolDir.empty())
        poolDir = outDir;
    const bool havePool = loadPool(poolDir);
    std::unordered_map<uint64_t, int> pixHash, palHash;
    if (havePool) {
        for (auto& t : g_pool)
            decodePool(t);
        for (size_t i = 0; i < g_pool.size(); ++i) {
            pixHash.emplace(fnv(g_pool[i].pix.data(), g_pool[i].pix.size()), (int)i);
            if (!g_pool[i].pal.empty())
                palHash.emplace(fnv(g_pool[i].pal.data(), g_pool[i].pal.size()), (int)i);
        }
    } else
        std::printf("note: no TXD pool in %s (run texaudit_pool.py); only the raw-upload reference is used\n",
                    poolDir.c_str());

    FILE* rep = reportPath.empty() ? stdout : std::fopen(reportPath.c_str(), "w");
    if (!rep)
        rep = stdout;

    std::map<uint32_t, Up> latest; // by DBP
    uint64_t seq = 0, nUploads = 0, nUpPix = 0, nUpPal = 0;
    int curFrame = 0, texN = 0, detail = 0;
    // statistics
    uint64_t decodeEvents = 0, cachedEvents = 0;
    uint64_t lvlOk = 0, lvlBad = 0, lvlNoRef = 0, lvlStale = 0;
    uint64_t poolOk = 0, poolBad = 0, poolAlphaEdit = 0, poolNone = 0;
    uint64_t palIdentity = 0, palSwap34 = 0, palRgbSwap34 = 0, palRgbIdentity = 0, palOther = 0, palNone = 0;
    std::map<std::string, std::array<uint64_t, 4>> byFormat; // ok, mismatch, noref, stale
    std::map<std::string, uint64_t> noRefWhy;
    std::set<std::string> done; // (tbp,psm,w,h,cbp,clut seq,tex seq) already verified (cached uses)

    Config cfg;
    cfg.vsync = false;
    cfg.onUpload = [&](const UploadEvent& e) {
        Up u;
        u.dbp = e.dbp;
        u.dbw = e.dbw;
        u.dpsm = e.dpsm;
        u.x = e.x;
        u.y = e.y;
        u.w = e.w;
        u.h = e.h;
        u.data.assign(e.data, e.data + e.bytes);
        u.hash = fnv(e.data, e.bytes);
        u.seq = seq++;
        if (havePool) {
            auto it = pixHash.find(u.hash);
            if (it != pixHash.end()) {
                u.pixPool = it->second;
                ++nUpPix;
            }
            auto it2 = palHash.find(u.hash);
            if (it2 != palHash.end()) {
                u.palPool = it2->second;
                ++nUpPal;
            }
        }
        ++nUploads;
        latest[e.dbp] = std::move(u);
    };

    auto fmtName = [](uint64_t tex0, uint32_t mxl) {
        char b[96];
        const uint32_t psm = (uint32_t)bitsOf(tex0, 20, 6);
        const char* n = psm == PSM_CT32    ? "CT32"
                        : psm == PSM_CT24  ? "CT24"
                        : psm == PSM_CT16  ? "CT16"
                        : psm == PSM_CT16S ? "CT16S"
                        : psm == PSM_T8    ? "T8"
                        : psm == PSM_T4    ? "T4"
                        : psm == PSM_T8H   ? "T8H"
                        : psm == PSM_T4HL  ? "T4HL"
                        : psm == PSM_T4HH  ? "T4HH"
                                           : "?";
        if (isIndexed(psm))
            std::snprintf(b, sizeof b, "%s clut cpsm%u csm%u %s", n, (uint32_t)bitsOf(tex0, 51, 4),
                          (uint32_t)bitsOf(tex0, 55, 1), mxl ? "mip" : "nomip");
        else
            std::snprintf(b, sizeof b, "%s %s", n, mxl ? "mip" : "nomip");
        return std::string(b);
    };

    cfg.onTexture = [&](const TextureEvent& e) {
        ++texN;
        if (e.cached)
            ++cachedEvents;
        else
            ++decodeEvents;
        const uint32_t tbp = (uint32_t)bitsOf(e.tex0, 0, 14), tbw = (uint32_t)bitsOf(e.tex0, 14, 6),
                       psm = (uint32_t)bitsOf(e.tex0, 20, 6);
        const uint32_t cbp = (uint32_t)bitsOf(e.tex0, 37, 14);
        const uint32_t mxl = e.mipLevels - 1;
        const std::string fmt = fmtName(e.tex0, mxl);
        // upload bookkeeping key: only verify a cached use once per (texture, upload) pair
        auto itU = latest.find(tbp);
        auto itP = latest.find(cbp);
        if (e.cached) {
            char key[160];
            std::snprintf(key, sizeof key, "%u/%u/%ux%u/%u/%llu/%llu/%u", tbp, psm, e.width, e.height, cbp,
                          itU == latest.end() ? 0ull : (unsigned long long)itU->second.seq,
                          itP == latest.end() || !isIndexed(psm) ? 0ull : (unsigned long long)itP->second.seq, mxl);
            if (!done.insert(key).second)
                return;
        }
        auto& stat = byFormat[fmt];
        bool anyBad = false, anyOk = false, anyNoRef = false;
        for (uint32_t l = 0; l <= mxl && l < 7; ++l) {
            const uint32_t w = e.levelW[l], h = e.levelH[l];
            uint32_t lbp = tbp;
            if (l > 0) {
                const uint64_t m = (l <= 3) ? e.miptbp1 : e.miptbp2;
                lbp = (uint32_t)bitsOf(m, 20 * ((l - 1) % 3), 14);
            }
            auto it = latest.find(lbp);
            std::vector<uint32_t> ref;
            std::string why;
            if (it == latest.end()) {
                ++lvlNoRef;
                anyNoRef = true;
                ++noRefWhy["no upload at that base (render target / memory-to-memory copy / written by draw)"];
                continue;
            }
            const Up* palUp = itP != latest.end() ? &itP->second : nullptr;
            if (!refLevel(it->second, e.tex0, e.texa, palUp, w, h, ref, why)) {
                ++lvlNoRef;
                anyNoRef = true;
                ++noRefWhy[why];
                continue;
            }
            const bool is16 = (psm == PSM_CT16 || psm == PSM_CT16S);
            Verdict v = compare(e.level[l], ref, is16 ? 0 : 0, 0);
            if (v.kind == Verdict::Ok) {
                ++lvlOk;
                anyOk = true;
            } else {
                anyBad = true;
                if (e.cached) {
                    ++lvlStale;
                    ++stat[3];
                } else
                    ++lvlBad;
                if (detail++ < maxDetail)
                    std::fprintf(rep,
                                 "%s f%d tbp=%u tbw=%u psm=%02x %ux%u level %u (base %u) cbp=%u fmt=[%s]: %zu/%zu "
                                 "texels differ, first at %zu got %08x expected %08x\n",
                                 e.cached ? "STALE   " : "MISMATCH", curFrame, tbp, tbw, psm, w, h, l, lbp, cbp,
                                 fmt.c_str(), v.nBad, ref.size(), v.firstBad, v.got, v.exp);
            }
            if (l == 0 && havePool && it->second.pixPool >= 0 && !e.cached) {
                const PoolTex& pt = g_pool[it->second.pixPool];
                if (pt.w == w && pt.h == h) {
                    const int atol = (pt.bpp == 24) ? 255 : 255;
                    Verdict vr = compare(e.level[0], pt.rgba, is16 ? 7 : 0, atol);
                    if (vr.kind == Verdict::Ok) {
                        Verdict va = compare(e.level[0], pt.rgba, is16 ? 7 : 0, 0);
                        if (va.kind == Verdict::Ok)
                            ++poolOk;
                        else
                            ++poolAlphaEdit;
                    } else {
                        ++poolBad;
                        if (detail++ < maxDetail)
                            std::fprintf(rep,
                                         "TXD-MISMATCH f%d tbp=%u psm=%02x %ux%u [%s id %u]: %zu texels differ, first "
                                         "%zu got %08x expected %08x\n",
                                         curFrame, tbp, psm, w, h, g_files[pt.file].c_str(), pt.id, vr.nBad,
                                         vr.firstBad, vr.got, vr.exp);
                    }
                    // CLUT relation: how does the uploaded palette relate to the TXD palette?
                    if (pt.bpp == 8 && pt.palKind == 1 && palUp && palUp->w == 16 && palUp->h == 16 && psm == PSM_T8) {
                        auto rawE = [&](uint32_t j) {
                            uint32_t c;
                            std::memcpy(&c, &palUp->data[j * 4], 4);
                            return c;
                        };
                        bool id = true, sw = true, idRgb = true, swRgb = true;
                        for (uint32_t j = 0; j < 256; ++j) {
                            const uint32_t t = poolPal32(pt, j);
                            if (rawE(j) != t)
                                id = false;
                            if (rawE(swap34(j)) != t)
                                sw = false;
                            if ((rawE(j) & 0xFFFFFF) != (t & 0xFFFFFF))
                                idRgb = false;
                            if ((rawE(swap34(j)) & 0xFFFFFF) != (t & 0xFFFFFF))
                                swRgb = false;
                        }
                        if (id)
                            ++palIdentity;
                        else if (sw)
                            ++palSwap34;
                        else if (idRgb)
                            ++palRgbIdentity;
                        else if (swRgb)
                            ++palRgbSwap34;
                        else
                            ++palOther;
                    } else
                        ++palNone;
                } else
                    ++poolNone;
            }
        }
        if (anyBad) {
            if (!e.cached)
                ++stat[1];
        } else if (anyOk)
            ++stat[0];
        else if (anyNoRef)
            ++stat[2];
        if (!e.cached && texN <= maxPng) {
            char nm[128];
            std::snprintf(nm, sizeof nm, "/tex_%04d_f%d_tbp%u_psm%02x_%ux%u.png", texN, curFrame, tbp, psm, e.width,
                          e.height);
            savePng(outDir + nm, e.level[0], e.width, e.height);
        }
    };

    auto gs = Renderer::create(cfg);
    if (!gs) {
        std::printf("renderer creation failed\n");
        return 2;
    }

    FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) {
        std::printf("cannot open %s\n", in.c_str());
        return 1;
    }
    char hdr[32];
    if (std::fread(hdr, 1, 32, f) != 32 || std::memcmp(hdr, "DRKGSCAP", 8))
        return 1;
    static const uint32_t privOff[20] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50,       0x60,       0x70,   0x80,   0x90,
                                         0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xFFFFFFFF, 0xFFFFFFFF, 0x1010, 0x1040, 0x1080};
    std::vector<uint8_t> buf;
    uint32_t type = 0, size = 0;
    while (std::fread(&type, 4, 1, f) == 1 && std::fread(&size, 4, 1, f) == 1) {
        buf.resize(size);
        if (size && std::fread(buf.data(), 1, size, f) != size)
            break;
        if (type == 2 && size >= 8) {
            uint32_t psize;
            std::memcpy(&psize, buf.data() + 4, 4);
            const uint8_t path = buf[0];
            gs->submitGifPacket(path == 1   ? GifPath::Path1
                                : path == 2 ? GifPath::Path2
                                            : GifPath::Path3,
                                buf.data() + 8, std::min<uint32_t>(psize, size - 8));
        } else if (type == 3 && size >= 160) {
            uint64_t regs[20];
            std::memcpy(regs, buf.data(), sizeof(regs));
            for (int i = 0; i < 20; ++i)
                if (privOff[i] != 0xFFFFFFFFu)
                    gs->writePriv(privOff[i], regs[i]);
        } else if (type == 4) {
            gs->vsync(false);
            if (++curFrame >= maxFrames)
                break;
        }
    }
    std::fclose(f);

    std::fprintf(rep, "\n==== texture audit: %s ====\n", in.c_str());
    std::fprintf(rep, "frames %d, uploads %llu (pixel data = TXD texture: %llu, palette = raw TXD palette: %llu)\n",
                 curFrame, (unsigned long long)nUploads, (unsigned long long)nUpPix, (unsigned long long)nUpPal);
    std::fprintf(rep, "texture decodes %llu, cached uses seen %llu (each distinct texture/upload pair checked once)\n",
                 (unsigned long long)decodeEvents, (unsigned long long)cachedEvents);
    std::fprintf(rep,
                 "mip levels checked against the raw upload stream: ok %llu, MISMATCH %llu, STALE (cached texture "
                 "differs from current upload) %llu, no reference %llu\n",
                 (unsigned long long)lvlOk, (unsigned long long)lvlBad, (unsigned long long)lvlStale,
                 (unsigned long long)lvlNoRef);
    std::fprintf(rep,
                 "level-0 textures checked against the TXD image from the disc: exact %llu, RGB exact but alpha edited "
                 "by the game %llu, MISMATCH %llu, size differs %llu\n",
                 (unsigned long long)poolOk, (unsigned long long)poolAlphaEdit, (unsigned long long)poolBad,
                 (unsigned long long)poolNone);
    std::fprintf(rep,
                 "CLUT upload vs TXD palette (T8): identical bytes %llu, swap34 order %llu, RGB-identical (alpha "
                 "edited) %llu, RGB swap34 (alpha edited) %llu, other %llu, n/a %llu\n",
                 (unsigned long long)palIdentity, (unsigned long long)palSwap34, (unsigned long long)palRgbIdentity,
                 (unsigned long long)palRgbSwap34, (unsigned long long)palOther, (unsigned long long)palNone);
    std::fprintf(rep, "by format (decoded textures: ok / mismatch / no reference; stale cached uses):\n");
    for (auto& kv : byFormat)
        std::fprintf(rep, "  %-32s ok %-5llu mismatch %-5llu noref %-5llu stale %llu\n", kv.first.c_str(),
                     (unsigned long long)kv.second[0], (unsigned long long)kv.second[1],
                     (unsigned long long)kv.second[2], (unsigned long long)kv.second[3]);
    for (auto& kv : noRefWhy)
        std::fprintf(rep, "  no reference (%llu): %s\n", (unsigned long long)kv.second, kv.first.c_str());
    std::fprintf(rep, "TOTAL checked levels %llu, mismatches %llu (incl. stale %llu)\n",
                 (unsigned long long)(lvlOk + lvlBad + lvlStale), (unsigned long long)(lvlBad + lvlStale + poolBad),
                 (unsigned long long)lvlStale);
    if (rep != stdout)
        std::fclose(rep);
    return (lvlBad + lvlStale + poolBad) ? 3 : 0;
}
