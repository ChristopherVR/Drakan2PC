// mod_test: synthetic mod packs (folder and zip): manifest parsing, merge order, conflicts, profile mismatch,
// dependencies, oversize rejection, text.ini, mods.ini handling, zip stored/deflate, zip-slip. No game data needed.
#include "hle/IsoFs.h"
#include "hle/ModPack.h"
#include "hle/Srsc.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <iterator>
#include <thread>

using namespace hle;
using namespace hle::mods;
namespace fs = std::filesystem;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                      \
        }                                                                                                              \
    } while (0)

static std::vector<std::string> g_log;
static void sink(const std::string& line) {
    g_log.push_back(line);
}
static bool logHas(const char* needle) {
    for (auto& l : g_log)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

static void writeFile(const fs::path& p, const std::vector<u8>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o.write((const char*)d.data(), (std::streamsize)d.size());
}
static void writeText(const fs::path& p, const std::string& s) {
    writeFile(p, std::vector<u8>(s.begin(), s.end()));
}
static std::vector<u8> fill(size_t n, u8 v) {
    return std::vector<u8>(n, v);
}

// ---- tiny zip writer (stored or pre-deflated payloads)
static u32 crcOf(const std::vector<u8>& d) {
    u32 c = 0xFFFFFFFFu;
    for (u8 b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
struct ZEnt {
    std::string name;
    std::vector<u8> plain, packed; // packed = bytes as written (deflated when method 8)
    int method;
    u32 crc;
};
static void p16(std::vector<u8>& v, u32 x) {
    v.push_back((u8)x);
    v.push_back((u8)(x >> 8));
}
static void p32(std::vector<u8>& v, u32 x) {
    p16(v, x & 0xFFFF);
    p16(v, x >> 16);
}
static void writeZip(const fs::path& p, const std::vector<ZEnt>& es, bool flagEncrypted = false) {
    std::vector<u8> out, cd;
    for (auto& e : es) {
        u32 lho = (u32)out.size();
        p32(out, 0x04034b50);
        p16(out, 20);
        p16(out, flagEncrypted ? 1 : 0);
        p16(out, (u32)e.method);
        p32(out, 0);
        p32(out, e.crc);
        p32(out, (u32)e.packed.size());
        p32(out, (u32)e.plain.size());
        p16(out, (u32)e.name.size());
        p16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.packed.begin(), e.packed.end());
        p32(cd, 0x02014b50);
        p16(cd, 20);
        p16(cd, 20);
        p16(cd, flagEncrypted ? 1 : 0);
        p16(cd, (u32)e.method);
        p32(cd, 0);
        p32(cd, e.crc);
        p32(cd, (u32)e.packed.size());
        p32(cd, (u32)e.plain.size());
        p16(cd, (u32)e.name.size());
        p16(cd, 0);
        p16(cd, 0);
        p16(cd, 0);
        p16(cd, 0);
        p32(cd, 0);
        p32(cd, lho);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    u32 cdOff = (u32)out.size();
    out.insert(out.end(), cd.begin(), cd.end());
    p32(out, 0x06054b50);
    p16(out, 0);
    p16(out, 0);
    p16(out, (u32)es.size());
    p16(out, (u32)es.size());
    p32(out, (u32)cd.size());
    p32(out, cdOff);
    p16(out, 0);
    writeFile(p, out);
}
static ZEnt stored(const std::string& n, const std::vector<u8>& d) {
    return {n, d, d, 0, crcOf(d)};
}
static ZEnt stored(const std::string& n, const std::string& s) {
    return stored(n, std::vector<u8>(s.begin(), s.end()));
}

// kDeflated = raw deflate (zlib level 9, dynamic Huffman) of plainBlob(), generated with python zlib
static std::vector<u8> plainBlob() {
    std::string s;
    for (int i = 0; i < 3; ++i)
        s += "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 200; ++i)
        s += (char)i;
    for (int i = 0; i < 5; ++i)
        s += "Drakan mod pack inflate test ";
    return std::vector<u8>(s.begin(), s.end());
}
static const u8 kDeflated[] = {
    11,  201, 72,  85,  40,  44,  205, 76,  206, 86,  72,  42,  202, 47,  207, 83,  72,  203, 175, 80,  200, 42,  205,
    45,  40,  86,  200, 47,  75,  45,  82,  40,  1,   74,  231, 36,  86,  85,  42,  164, 228, 167, 235, 41,  132, 208,
    76,  49,  3,   35,  19,  51,  11,  43,  27,  59,  7,   39,  23,  55,  15,  47,  31,  191, 128, 160, 144, 176, 136,
    168, 152, 184, 132, 164, 148, 180, 140, 172, 156, 188, 130, 162, 146, 178, 138, 170, 154, 186, 134, 166, 150, 182,
    142, 174, 158, 190, 129, 161, 145, 177, 137, 169, 153, 185, 133, 165, 149, 181, 141, 173, 157, 189, 131, 163, 147,
    179, 139, 171, 155, 187, 135, 167, 151, 183, 143, 175, 159, 127, 64,  96,  80,  112, 72,  104, 88,  120, 68,  100,
    84,  116, 76,  108, 92,  124, 66,  98,  82,  114, 74,  106, 90,  122, 70,  102, 86,  118, 78,  110, 94,  126, 65,
    97,  81,  113, 73,  105, 89,  121, 69,  101, 85,  117, 77,  109, 93,  125, 67,  99,  83,  115, 75,  107, 91,  123,
    71,  103, 87,  119, 79,  111, 95,  255, 132, 137, 147, 38,  79,  153, 58,  109, 250, 140, 153, 179, 102, 207, 153,
    59,  111, 254, 130, 133, 139, 22,  47,  89,  186, 108, 249, 138, 149, 171, 86,  175, 89,  187, 110, 253, 134, 141,
    155, 54,  111, 217, 186, 109, 251, 142, 157, 187, 118, 239, 217, 187, 111, 255, 129, 131, 135, 14,  31,  57,  122,
    236, 184, 75,  81,  98,  118, 98,  158, 66,  110, 126, 138, 66,  65,  34,  208, 235, 153, 121, 105, 57,  137, 37,
    169, 10,  37,  169, 197, 37,  10,  3,   33,  9,   0};

// More raw deflate streams (python zlib) for the table-driven inflater. Their plain data is regenerated here.
//   kSkewDeflated: level 9, two dynamic blocks around a sync flush (an empty stored block in the middle of the bit
//                  stream) of skewBlob(1500, 12345) + skewBlob(1500, 777); the skewed symbol frequencies give Huffman
//                  codes longer than the decoder's 10 bit lookup
//   kRunsFixed:    fixed Huffman (Z_FIXED) of runsBlob(): matches at distance 1 and 3 that overlap their own output
//   kStored:       level 0 (one stored block) of skewBlob(300, 99)
static std::vector<u8> skewBlob(size_t n, u32 seed) {
    std::vector<u8> out;
    u32 x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        const u32 v = (x >> 8) & 0xFFFF;
        u32 k = 0;
        while (k < 16 && !(v & (0x8000u >> k)))
            ++k;
        out.push_back((u8)(k * 13 + ((x >> 24) & 7)));
    }
    return out;
}
static std::vector<u8> runsBlob() {
    std::vector<u8> out;
    for (int i = 0; i < 40; ++i) {
        out.insert(out.end(), (size_t)(3 + i * 5), (u8)(65 + i % 7));
        for (int k = 0; k < i + 2; ++k)
            out.insert(out.end(), {(u8)'a', (u8)'b', (u8)'c'});
        for (int k = 0; k < 9; ++k)
            out.push_back((u8)(i + k));
    }
    return out;
}
static const u8 kSkewDeflated[] = {
    0x14, 0x94, 0x39, 0x73, 0xdc, 0x46, 0x10, 0x85, 0xfb, 0xee, 0x68, 0x06, 0x73, 0x45, 0x58, 0x2c, 0x76, 0x11, 0xed,
    0x89, 0x8d, 0x48, 0x5a, 0x24, 0x23, 0x51, 0x22, 0xc5, 0x4c, 0x2e, 0x1f, 0xa1, 0xab, 0x6c, 0x47, 0x0e, 0xec, 0x72,
    0xee, 0xdc, 0xb1, 0xff, 0xb2, 0x47, 0x85, 0x2a, 0x00, 0xc9, 0x74, 0xbf, 0xf7, 0xfa, 0xeb, 0xe1, 0xf7, 0xaa, 0x27,
    0xbb, 0x36, 0x70, 0x5c, 0xd7, 0xe1, 0x5a, 0x6b, 0x92, 0x83, 0x9f, 0xb8, 0x9c, 0x5e, 0xc8, 0xd6, 0xef, 0xe7, 0x92,
    0xdb, 0x82, 0xd4, 0x88, 0x9f, 0x33, 0xe1, 0x74, 0xd0, 0x8a, 0x4c, 0xb4, 0x9c, 0xfc, 0x8b, 0xb7, 0xad, 0x6d, 0x68,
    0x43, 0xd5, 0x46, 0x79, 0x68, 0xc1, 0xbf, 0x16, 0x0f, 0x44, 0x59, 0x7c, 0xa6, 0x16, 0xcd, 0x58, 0x9a, 0xbd, 0x19,
    0xce, 0x79, 0xac, 0x2e, 0x1c, 0x54, 0x81, 0x71, 0xb1, 0x74, 0xa1, 0x27, 0x81, 0x74, 0x6c, 0x75, 0x09, 0x2e, 0x92,
    0x1f, 0x2b, 0x88, 0xc0, 0x23, 0xb8, 0x27, 0x96, 0xea, 0x93, 0xb2, 0x5e, 0x51, 0x9b, 0x38, 0xcc, 0x0b, 0xc3, 0xba,
    0xbd, 0x65, 0x4a, 0x1c, 0xb9, 0xd8, 0xca, 0x73, 0x74, 0xe0, 0xb3, 0x35, 0x1c, 0xcc, 0xd7, 0x1b, 0x9f, 0xe1, 0xd3,
    0x90, 0xa1, 0x3a, 0x59, 0x94, 0x19, 0x93, 0xcc, 0x5e, 0xbb, 0xae, 0xe5, 0x87, 0xcb, 0xdb, 0x76, 0xd6, 0x0b, 0xe0,
    0x01, 0xb4, 0xb8, 0x16, 0xa5, 0xab, 0x1f, 0xe5, 0xc1, 0xd8, 0x83, 0x20, 0x33, 0x99, 0xf2, 0x3c, 0x14, 0x90, 0x97,
    0xd4, 0x34, 0x72, 0x20, 0x23, 0x9c, 0x8f, 0x6e, 0x4f, 0x8d, 0x19, 0x71, 0x2f, 0x83, 0x42, 0xbd, 0x47, 0xa7, 0x7d,
    0xda, 0x04, 0x45, 0x47, 0x00, 0xe5, 0x98, 0x44, 0xb6, 0x9e, 0x3e, 0x68, 0xbe, 0x45, 0xcb, 0xcb, 0x84, 0x0d, 0x73,
    0x36, 0x4c, 0xa9, 0xf6, 0x73, 0x17, 0x52, 0x19, 0x9e, 0x52, 0x82, 0xd9, 0xd6, 0x16, 0x76, 0x09, 0xd5, 0x89, 0x5f,
    0x79, 0xab, 0x71, 0xa6, 0x81, 0xef, 0x65, 0x5f, 0x2a, 0x3c, 0x0e, 0x3d, 0x99, 0x5e, 0x48, 0xbc, 0x21, 0xac, 0x39,
    0x6d, 0xbf, 0x3b, 0xa3, 0x8e, 0xf4, 0xa0, 0xdd, 0xd5, 0xa1, 0xa7, 0xa1, 0xb9, 0x97, 0xb4, 0xc6, 0x1b, 0x63, 0x85,
    0xbf, 0x82, 0x9b, 0xdf, 0x73, 0x1c, 0xba, 0xa5, 0x78, 0x2b, 0xc6, 0xbc, 0x12, 0x55, 0xd4, 0xa3, 0xd8, 0x7b, 0x82,
    0x20, 0x0c, 0xa1, 0x07, 0x30, 0x5a, 0x28, 0x28, 0x16, 0xbd, 0xab, 0x5e, 0x7a, 0x5f, 0x25, 0xbd, 0x4c, 0xdc, 0x48,
    0xd9, 0x2b, 0xdc, 0x40, 0xcf, 0xfe, 0x9b, 0x30, 0x5b, 0x55, 0xc6, 0x29, 0xf4, 0x89, 0xf5, 0xda, 0x8b, 0x41, 0x18,
    0xf7, 0x30, 0xac, 0x10, 0x07, 0xe5, 0x81, 0x5f, 0xf0, 0x20, 0xe9, 0x27, 0x67, 0x3f, 0xcc, 0xa3, 0xa3, 0x4c, 0xdd,
    0xa7, 0x4b, 0x3d, 0x07, 0xc8, 0x27, 0xb9, 0xea, 0x8b, 0xda, 0xa2, 0xf7, 0x48, 0x32, 0x80, 0x2b, 0x43, 0x54, 0xba,
    0x9c, 0x95, 0x47, 0xd3, 0x94, 0x58, 0x67, 0x1b, 0x31, 0x0b, 0xe5, 0x9b, 0x61, 0x9f, 0x9e, 0x6e, 0xe2, 0x9b, 0x43,
    0x55, 0xd8, 0x69, 0xe9, 0x31, 0x57, 0xca, 0x78, 0xb4, 0xca, 0xfe, 0xbe, 0x18, 0xf7, 0xa7, 0xce, 0xe2, 0x68, 0xd2,
    0x26, 0x4e, 0xdd, 0x4b, 0x86, 0x67, 0x3c, 0x63, 0x8d, 0x3b, 0xf6, 0xb4, 0xa6, 0xda, 0xd8, 0xe6, 0x40, 0x78, 0x4b,
    0x7d, 0x40, 0xa0, 0x8b, 0xe1, 0x10, 0xbb, 0xb6, 0x5f, 0x85, 0xc6, 0xe8, 0x24, 0xd2, 0xcd, 0x84, 0x28, 0xa4, 0xb1,
    0x59, 0x0d, 0xa1, 0xaa, 0x7a, 0xe1, 0x3b, 0xbc, 0x85, 0x77, 0xd2, 0xd9, 0xf3, 0xb0, 0x3c, 0x4e, 0x8f, 0x04, 0xad,
    0x1a, 0xdc, 0x8d, 0x12, 0x0f, 0x03, 0xf2, 0xe5, 0xf5, 0xc7, 0x7d, 0x8b, 0x72, 0xee, 0x00, 0x65, 0x22, 0xf4, 0xf2,
    0x16, 0x01, 0x73, 0x9a, 0x6e, 0xbc, 0x79, 0x84, 0x74, 0x17, 0x1a, 0xd6, 0xab, 0xac, 0xc6, 0x77, 0x6c, 0x22, 0x1a,
    0xca, 0x8a, 0x67, 0x21, 0xaf, 0x05, 0x75, 0xba, 0x6f, 0x37, 0xb2, 0x7a, 0x2d, 0x7a, 0x55, 0xb3, 0x20, 0x5b, 0x51,
    0x23, 0xbb, 0x41, 0x4a, 0x67, 0x33, 0xc3, 0x82, 0x8b, 0xdc, 0xdc, 0x8e, 0x3d, 0x91, 0xa3, 0xa0, 0x15, 0x50, 0x91,
    0x5d, 0xff, 0xa1, 0x4d, 0xa7, 0xa1, 0x5a, 0xee, 0x35, 0x14, 0x20, 0xcd, 0xf5, 0x4f, 0x96, 0x3c, 0xe2, 0x29, 0x52,
    0xc7, 0x7b, 0x75, 0xdf, 0x1d, 0xdc, 0x4d, 0x4e, 0xdb, 0x0f, 0xa8, 0x5c, 0xb7, 0x1c, 0xea, 0x50, 0x5b, 0x58, 0xa8,
    0x93, 0x56, 0x5f, 0xcb, 0xa0, 0x0e, 0xcf, 0xb7, 0x19, 0x04, 0xf8, 0x67, 0x2a, 0xdc, 0x3f, 0x08, 0xb4, 0x57, 0x6c,
    0x1f, 0x77, 0xbb, 0x0d, 0xef, 0x3f, 0x1d, 0x77, 0x03, 0x45, 0xe9, 0x13, 0x31, 0x83, 0x64, 0xdd, 0xfd, 0x04, 0x60,
    0x7f, 0x2c, 0xa5, 0xa4, 0x0a, 0x06, 0x13, 0xd5, 0x54, 0x2e, 0x7b, 0x1a, 0xd0, 0x1e, 0x49, 0xa2, 0x1f, 0xfe, 0x8e,
    0x34, 0xbc, 0x25, 0xf9, 0xb8, 0x45, 0x2e, 0x78, 0x37, 0xd2, 0x67, 0xc4, 0xd4, 0x4f, 0x5c, 0x90, 0xfa, 0xe6, 0x38,
    0xd2, 0x94, 0xb2, 0xdd, 0x23, 0x8c, 0x23, 0x12, 0x76, 0x6a, 0x6d, 0x30, 0xfd, 0x57, 0x21, 0x7e, 0xdb, 0x8a, 0x1a,
    0x79, 0x9b, 0xa4, 0x11, 0x72, 0xeb, 0x3a, 0x4a, 0xb3, 0xe7, 0x78, 0x46, 0x29, 0x50, 0x73, 0x48, 0x54, 0x4b, 0x13,
    0x85, 0x32, 0x04, 0x2e, 0xd7, 0x50, 0x96, 0x93, 0xf6, 0x45, 0x62, 0x9d, 0xa6, 0xfb, 0x8d, 0x67, 0xf3, 0xce, 0x60,
    0x27, 0x40, 0x7f, 0xa8, 0x42, 0x9b, 0x25, 0x2b, 0x2c, 0x1d, 0xa9, 0x2f, 0x52, 0xad, 0x17, 0x3c, 0xe5, 0xe5, 0x84,
    0x94, 0xd3, 0x00, 0x6b, 0x18, 0xef, 0xf8, 0x9f, 0xbd, 0x87, 0xb6, 0x9d, 0x64, 0xcb, 0x00, 0xd2, 0xd1, 0xe8, 0x3c,
    0xf4, 0x15, 0x45, 0x33, 0xad, 0xe3, 0x90, 0x46, 0x29, 0x79, 0x28, 0x1c, 0xbf, 0x8a, 0xd8, 0xf0, 0x39, 0x60, 0xb0,
    0xac, 0x6d, 0xf3, 0xc1, 0x4a, 0x6f, 0x69, 0x5d, 0x3f, 0xb8, 0xf4, 0x05, 0x31, 0xe9, 0xf7, 0xc6, 0xe3, 0x8e, 0xc7,
    0xd0, 0xce, 0x4f, 0x2c, 0x08, 0x0c, 0x1e, 0xbf, 0xc6, 0x35, 0x70, 0x9b, 0xb0, 0x47, 0x6b, 0x14, 0xa7, 0xec, 0x4d,
    0x3b, 0x1f, 0x7c, 0x51, 0x01, 0x82, 0x7e, 0x41, 0x60, 0x02, 0x40, 0xbd, 0xca, 0xc3, 0xcc, 0x4b, 0x86, 0xe4, 0x9c,
    0xa0, 0x95, 0xd3, 0xa8, 0xad, 0x08, 0x67, 0xed, 0x9d, 0xe5, 0x16, 0xda, 0x50, 0xba, 0x78, 0xbc, 0x59, 0xf3, 0x76,
    0x6c, 0x63, 0x3b, 0x44, 0xea, 0xef, 0x7e, 0x29, 0xc2, 0x32, 0x8f, 0x03, 0xfa, 0x62, 0x1f, 0xd0, 0xf8, 0x33, 0x26,
    0xb4, 0xf1, 0xaa, 0x9c, 0x44, 0xc9, 0x5f, 0x03, 0x05, 0x78, 0x06, 0xef, 0xb2, 0xa8, 0x05, 0x3d, 0x2d, 0x90, 0xf5,
    0xb9, 0xf3, 0x3a, 0x96, 0xaa, 0xbf, 0x27, 0xea, 0x00, 0x2a, 0xf1, 0x7b, 0x1c, 0x5c, 0x1b, 0x4e, 0x38, 0xbb, 0xf8,
    0xde, 0x12, 0x61, 0x5a, 0x3e, 0x8e, 0xd9, 0x71, 0x08, 0x0e, 0xed, 0x3f, 0x62, 0xd7, 0x2b, 0x1c, 0x41, 0x7b, 0x6c,
    0xc7, 0xb3, 0x70, 0xa9, 0x48, 0x34, 0xcf, 0x9d, 0xb3, 0x99, 0xa9, 0xd2, 0x6e, 0xe4, 0x6b, 0x78, 0x82, 0xf2, 0x31,
    0x97, 0x72, 0xf9, 0xe5, 0x1b, 0xec, 0x98, 0xd5, 0xe7, 0x13, 0x87, 0xcb, 0x2d, 0xf7, 0xad, 0x4b, 0x3c, 0xb9, 0xee,
    0x56, 0xdc, 0x76, 0x02, 0x4f, 0x1b, 0xac, 0x47, 0x16, 0x15, 0xab, 0xbb, 0xf9, 0x8d, 0xf7, 0x4f, 0xf6, 0x3f, 0x00,
    0x00, 0x00, 0xff, 0xff, 0x15, 0x96, 0x3b, 0x73, 0x1b, 0x39, 0x10, 0x84, 0x67, 0x00, 0x0c, 0x26, 0xc2, 0x63, 0x01,
    0x44, 0xfb, 0x22, 0x37, 0xa2, 0x28, 0x72, 0x37, 0x12, 0x45, 0x51, 0xce, 0xce, 0xf2, 0x23, 0x92, 0x5d, 0x2e, 0x87,
    0xae, 0xab, 0xbb, 0xdc, 0xf7, 0x07, 0xee, 0x07, 0xdc, 0xef, 0xbe, 0x26, 0xd3, 0xad, 0xe5, 0x62, 0x30, 0xdd, 0x5f,
    0xb7, 0x01, 0xcb, 0x47, 0xb6, 0xc0, 0x60, 0xbc, 0x29, 0x1f, 0x24, 0x11, 0xee, 0xba, 0xd9, 0x8a, 0xbd, 0x9d, 0x65,
    0x91, 0xb7, 0xbe, 0x79, 0x8a, 0x2f, 0x6d, 0x58, 0x0c, 0x61, 0x47, 0xf2, 0x16, 0xee, 0xd7, 0x61, 0xcc, 0x64, 0x21,
    0xe0, 0x9d, 0xad, 0x01, 0xb2, 0x74, 0x5f, 0x94, 0x68, 0xd4, 0x38, 0xbe, 0x06, 0x2b, 0x8d, 0x6a, 0xca, 0xcf, 0xd9,
    0xbb, 0x5f, 0x2d, 0x3c, 0x75, 0x54, 0xa4, 0x06, 0xf1, 0x97, 0x44, 0x49, 0x9b, 0xa5, 0x47, 0x1f, 0xd7, 0x94, 0xd3,
    0xe4, 0x4b, 0x96, 0xac, 0x5a, 0x2e, 0x1b, 0x50, 0xb1, 0xd3, 0xe2, 0x19, 0x78, 0x1b, 0xb3, 0xb8, 0x80, 0xf9, 0xf7,
    0xc1, 0x54, 0x07, 0xce, 0x71, 0xa4, 0xfa, 0xad, 0xf9, 0x10, 0x33, 0xb9, 0xe8, 0x1d, 0x95, 0xe0, 0xe1, 0x6d, 0xf8,
    0x62, 0x02, 0x91, 0xed, 0x37, 0xae, 0x66, 0x70, 0x31, 0x19, 0xcf, 0x32, 0xa5, 0x0f, 0x97, 0x69, 0x26, 0x1f, 0xcd,
    0xe6, 0x52, 0x6f, 0xc4, 0x07, 0x0a, 0xd3, 0x63, 0xc6, 0x2c, 0x6d, 0x8c, 0x37, 0x04, 0x80, 0x71, 0x9d, 0x54, 0x15,
    0xaf, 0x69, 0xfa, 0x5c, 0xe5, 0x35, 0xf0, 0xe0, 0x3b, 0xfb, 0xac, 0x93, 0xd9, 0xeb, 0x07, 0x33, 0xcb, 0xf3, 0x1a,
    0x07, 0x57, 0x39, 0xce, 0x12, 0xbc, 0x78, 0xd2, 0xab, 0xd5, 0x33, 0x3f, 0x85, 0x6b, 0x43, 0xca, 0x80, 0x91, 0x61,
    0x69, 0x62, 0x41, 0xce, 0x78, 0x5d, 0xd6, 0xd1, 0x9a, 0x23, 0xe4, 0x31, 0xcb, 0x3e, 0xaa, 0xdb, 0xfb, 0x2f, 0x5d,
    0x23, 0x00, 0x4e, 0x06, 0x6b, 0x02, 0x33, 0x8d, 0xce, 0x0d, 0x0f, 0xc8, 0x1a, 0xd8, 0xc5, 0x3b, 0x01, 0x6b, 0xe3,
    0x51, 0x6f, 0x7e, 0x22, 0x39, 0xaf, 0x65, 0xbe, 0x33, 0x8d, 0x8f, 0x25, 0x2f, 0x34, 0x5e, 0xf2, 0x08, 0x5a, 0xc2,
    0xfe, 0x6c, 0x70, 0x46, 0x92, 0x7b, 0x22, 0xc0, 0x6f, 0x31, 0x17, 0x32, 0xfa, 0x12, 0x0b, 0x9c, 0x87, 0x11, 0x0e,
    0x77, 0x52, 0x06, 0x7c, 0x2b, 0xf1, 0x23, 0xc2, 0x02, 0x8a, 0x4a, 0x90, 0xab, 0x42, 0x25, 0xa4, 0xfb, 0xa1, 0xd9,
    0x9f, 0x47, 0x4a, 0xd6, 0x27, 0x5e, 0x88, 0x60, 0x47, 0x70, 0x00, 0xba, 0x72, 0xcc, 0x8f, 0x1d, 0xe4, 0xb4, 0x21,
    0x4b, 0x26, 0x1d, 0x5d, 0x86, 0x3d, 0xc1, 0x69, 0xb2, 0x10, 0xad, 0xfc, 0x0b, 0xad, 0xdd, 0xd4, 0x93, 0xbf, 0xb2,
    0x55, 0x7f, 0x02, 0xa1, 0xfa, 0x84, 0x34, 0xdd, 0x2f, 0x76, 0xb2, 0x47, 0x76, 0x73, 0x36, 0x2b, 0x3b, 0x20, 0x59,
    0x7b, 0xc3, 0x67, 0xd1, 0xba, 0x35, 0x08, 0x29, 0x85, 0x21, 0x00, 0x62, 0x73, 0x29, 0x4e, 0x36, 0xec, 0xc4, 0x8c,
    0x96, 0x33, 0x24, 0x19, 0x31, 0xb3, 0x76, 0x4d, 0x47, 0x9f, 0x5d, 0xe4, 0x78, 0xeb, 0x07, 0xa0, 0x49, 0x8f, 0x46,
    0xed, 0xfc, 0xdf, 0x73, 0x41, 0x0b, 0xd8, 0x23, 0xb1, 0x91, 0x4d, 0x03, 0x49, 0x12, 0x41, 0xbc, 0xc3, 0x9f, 0xc6,
    0xfa, 0x23, 0xc5, 0x9c, 0xc5, 0x50, 0xd8, 0x1f, 0xb6, 0x93, 0x59, 0x00, 0x25, 0x28, 0x5a, 0x38, 0xad, 0x48, 0xfa,
    0x77, 0x73, 0x68, 0x6c, 0xeb, 0x0c, 0xa7, 0x9e, 0xd6, 0xe2, 0xba, 0x53, 0x72, 0x8d, 0xfe, 0xa8, 0xd4, 0xc4, 0x3f,
    0x3c, 0xd3, 0x92, 0xc9, 0xc6, 0x3d, 0xf1, 0xa5, 0x7c, 0x10, 0xd3, 0x05, 0x9c, 0x07, 0xb3, 0xbb, 0x26, 0x4b, 0x5b,
    0xce, 0xb6, 0x7f, 0xd1, 0x5c, 0x7b, 0x28, 0x6c, 0xe0, 0x17, 0xb4, 0x00, 0x6e, 0x37, 0xf3, 0x94, 0x54, 0x91, 0x8a,
    0x11, 0x96, 0xf5, 0xb8, 0x6c, 0x71, 0xa2, 0x7b, 0xf3, 0xcb, 0x14, 0x59, 0xbe, 0x62, 0xc4, 0xb5, 0xae, 0x46, 0xd2,
    0x9d, 0x9c, 0x8d, 0x19, 0x08, 0x15, 0x32, 0x52, 0xf0, 0x9e, 0xa6, 0x21, 0xfe, 0xb5, 0x03, 0x64, 0xe4, 0xdc, 0xdc,
    0xe2, 0x76, 0x3c, 0x76, 0xc6, 0x59, 0xf6, 0x34, 0x0f, 0x8e, 0x85, 0x2f, 0x7e, 0x72, 0x7e, 0x85, 0x79, 0x29, 0x22,
    0xc0, 0xea, 0xc0, 0xee, 0x91, 0x06, 0x39, 0xa6, 0x7b, 0xc6, 0xea, 0xd8, 0x59, 0xb1, 0xc5, 0x1c, 0x8d, 0x7b, 0xcb,
    0x5c, 0x43, 0xc4, 0xb6, 0xae, 0xe6, 0xed, 0xcb, 0x6e, 0x22, 0x97, 0xac, 0x1b, 0x2a, 0xd4, 0x75, 0xe8, 0xfe, 0x64,
    0xde, 0x5c, 0xde, 0xaa, 0x7c, 0x82, 0x6e, 0x9b, 0x21, 0xee, 0xad, 0x57, 0x25, 0x3f, 0xfd, 0x6d, 0xe3, 0x7d, 0xdb,
    0x47, 0x70, 0xdd, 0xe4, 0xe6, 0x3a, 0xf8, 0xf0, 0x25, 0x17, 0xb5, 0xef, 0xbb, 0xc6, 0xe7, 0xe8, 0xd7, 0x08, 0x88,
    0x60, 0xfb, 0x0e, 0xee, 0xa0, 0xe9, 0x27, 0x28, 0xe1, 0xdb, 0xc7, 0x83, 0x73, 0xe1, 0xd9, 0x6f, 0xd5, 0x58, 0xdb,
    0x4d, 0xab, 0x28, 0x7e, 0x54, 0x3a, 0x63, 0x37, 0x46, 0xb0, 0x19, 0x30, 0x54, 0xf4, 0x93, 0xa7, 0x8e, 0xcd, 0x3f,
    0x0f, 0xe4, 0x3d, 0x00, 0x8f, 0x3c, 0x41, 0x1f, 0x70, 0x7e, 0x56, 0xf0, 0xca, 0xe0, 0x89, 0xaf, 0x39, 0x44, 0x63,
    0xdd, 0x96, 0xd0, 0xe7, 0x1a, 0xca, 0x4f, 0xe8, 0x61, 0xaa, 0xaf, 0x38, 0x53, 0x10, 0x45, 0xa0, 0x40, 0x29, 0xe2,
    0x76, 0x7e, 0x89, 0x9d, 0xc4, 0x15, 0xf9, 0xe3, 0x67, 0xe8, 0x5c, 0x1a, 0x5e, 0x7a, 0xea, 0xf4, 0xea, 0x26, 0x24,
    0xbf, 0x50, 0xff, 0xe8, 0x9e, 0xb0, 0x8e, 0x07, 0xf7, 0xbd, 0xcd, 0xc0, 0x00, 0xdc, 0x12, 0x50, 0xf3, 0xdc, 0x9e,
    0xb5, 0xfd, 0x4e, 0xe8, 0x41, 0x00, 0xaf, 0x47, 0x34, 0x74, 0x45, 0x4b, 0xc7, 0xe4, 0x6e, 0xf8, 0xc7, 0x16, 0xb5,
    0x63, 0xfb, 0x31, 0x03, 0x8d, 0xd2, 0xd2, 0xb8, 0xdf, 0xa1, 0x46, 0x20, 0xff, 0x90, 0x40, 0xe1, 0xd0, 0x12, 0x0a,
    0x63, 0x59, 0xce, 0x1e, 0x7b, 0x2e, 0x4f, 0x17, 0xd8, 0xb7, 0xd4, 0x86, 0x49, 0x21, 0x5f, 0x76, 0xef, 0xbe, 0xd1,
    0x47, 0x42, 0xe5, 0x9b, 0x9d, 0x85, 0x3b, 0xab, 0xc1, 0x12, 0x0b, 0xa7, 0xb3, 0x72, 0x40, 0x92, 0xa5, 0x2a, 0xcb,
    0x0e, 0x69, 0x56, 0xae, 0xe8, 0x06, 0x16, 0x1a, 0x86, 0xaa, 0x65, 0x3d, 0x3c, 0xf8, 0x34, 0xe0, 0xee, 0x75, 0xd4,
    0x8a, 0xc1, 0xbc, 0x4c, 0x87, 0x13, 0x92, 0xaf, 0xc5, 0x2a, 0xe7, 0x39, 0x70, 0xed, 0x38, 0xc3, 0x1f, 0xd8, 0x32,
    0xc3, 0x8d, 0x36, 0xf7, 0xf7, 0xb6, 0x83, 0x6b, 0x42, 0x77, 0x6c, 0x04, 0x91, 0xa9, 0xec, 0xd1, 0xd2, 0x6c, 0xca,
    0x07, 0x54, 0x2b, 0x21, 0x77, 0x3c, 0x15, 0xb7, 0x71, 0x91, 0xc3, 0xbd, 0x7b, 0x41, 0xbc, 0x9d, 0x3d, 0xdd, 0xe9,
    0x18, 0xfc, 0x77, 0x5e, 0xba, 0x1f, 0xa3, 0x7e, 0xf6, 0x30, 0x59, 0x30, 0xb1, 0x37, 0x13, 0x69, 0x33, 0x13, 0x20,
    0xcf, 0x1c, 0xdf, 0x30, 0x12, 0x6d, 0x7c, 0xae, 0xd7, 0x82, 0xfa, 0x68, 0xc9, 0x8e, 0xd4, 0x5a, 0x8b, 0xd1, 0x1b,
    0x87, 0xa4, 0x66, 0x42, 0x11, 0x96, 0xa3, 0xda, 0x4d, 0x69, 0x1a, 0xe9, 0x9e, 0x69, 0xf0, 0x22, 0x42, 0x5b, 0xf0,
    0xc4, 0xeb, 0xfb, 0x4b, 0x87, 0xb6, 0x4a, 0xe6, 0x07, 0xfd, 0x2e, 0xe1, 0xca, 0xf7, 0x02, 0x8e, 0x1a, 0x64, 0xe8,
    0xe8, 0x5c, 0x3f, 0x9a, 0x88, 0x8c, 0x33, 0x0d, 0xb7, 0x94, 0xed, 0x0c, 0x8c, 0x95, 0x57, 0x7c, 0xd2, 0x4a, 0xf8,
    0x1f,
};
static const u8 kRunsFixed[] = {
    0x73, 0x74, 0x74, 0x4c, 0x4c, 0x4a, 0x06, 0x22, 0x06, 0x46, 0x26, 0x66, 0x16, 0x56, 0x36, 0x76, 0x0e, 0x27, 0x28,
    0x80, 0x08, 0x03, 0x11, 0x4c, 0x82, 0xd3, 0x19, 0x19, 0xc0, 0xa5, 0x81, 0x08, 0xa6, 0x80, 0xcb, 0x05, 0x03, 0x20,
    0x2b, 0x03, 0x22, 0x98, 0x42, 0x6e, 0x57, 0xec, 0x00, 0x4d, 0x39, 0x10, 0xc1, 0x34, 0xf0, 0xb8, 0xe1, 0x01, 0x98,
    0xda, 0x80, 0x08, 0xa6, 0x91, 0xd7, 0x9d, 0x10, 0xc0, 0xaa, 0x1d, 0x88, 0x60, 0x06, 0xf0, 0x39, 0x12, 0x05, 0x70,
    0x19, 0x03, 0x44, 0x30, 0x83, 0xf8, 0x9d, 0x88, 0x07, 0x78, 0x8c, 0x03, 0x22, 0x98, 0x81, 0x02, 0xce, 0x24, 0x02,
    0xfc, 0xc6, 0x02, 0x11, 0xcc, 0x60, 0x41, 0x17, 0x72, 0x00, 0x41, 0xe3, 0x81, 0x08, 0x66, 0x81, 0x90, 0x2b, 0xd9,
    0x80, 0x18, 0x6b, 0x80, 0x08, 0x66, 0x91, 0xb0, 0x1b, 0x65, 0x80, 0x48, 0xeb, 0x80, 0x08, 0x66, 0xa1, 0x88, 0x3b,
    0x15, 0x00, 0xf1, 0xd6, 0x02, 0x11, 0xcc, 0x62, 0x51, 0x47, 0x6a, 0x01, 0x92, 0xac, 0x07, 0x22, 0x98, 0x03, 0xc4,
    0x9c, 0xa8, 0x0a, 0x48, 0x75, 0x06, 0x10, 0xc1, 0x1c, 0x22, 0xee, 0x4c, 0x7d, 0x40, 0x86, 0x73, 0x80, 0x08, 0xe6,
    0x20, 0x09, 0x17, 0x1a, 0x01, 0xf2, 0x9c, 0x05, 0x44, 0x30, 0x87, 0x49, 0xba, 0xd2, 0x12, 0x90, 0xed, 0x3c, 0x20,
    0x82, 0x39, 0x50, 0xca, 0x8d, 0xe6, 0x80, 0x12, 0x67, 0x02, 0x11, 0xcc, 0xa1, 0xd2, 0xee, 0xf4, 0x01, 0x14, 0x3a,
    0x17, 0x88, 0x60, 0x0e, 0x96, 0x71, 0xa4, 0x23, 0xa0, 0xdc, 0xd9, 0x40, 0x04, 0x73, 0xb8, 0xac, 0x13, 0xbd, 0x01,
    0x55, 0x9c, 0x0f, 0x44, 0x30, 0x0f, 0xc8, 0x39, 0x0f, 0x08, 0xa0, 0x96, 0x37, 0x80, 0x08, 0xe6, 0x11, 0x79, 0x97,
    0x81, 0x03, 0x54, 0xf4, 0x0e, 0x10, 0xc1, 0x3c, 0xa4, 0xe0, 0x3a, 0xc0, 0x80, 0xba, 0xde, 0x02, 0x22, 0x98, 0xc7,
    0x14, 0xdd, 0x06, 0x03, 0xa0, 0xba, 0xf7, 0x80, 0x08, 0xe6, 0x41, 0x25, 0xf7, 0x41, 0x03, 0x68, 0xe1, 0x4d, 0x20,
    0x82, 0x79, 0x54, 0xd9, 0x71, 0x70, 0x01, 0x1a, 0x79, 0x17, 0x88, 0x60, 0x1e, 0x56, 0x71, 0x1a, 0x84, 0x80, 0x76,
    0xde, 0x06, 0x22, 0x98, 0xc7, 0x55, 0x9d, 0x07, 0x2b, 0xa0, 0xa9, 0xf7, 0x81, 0x08, 0x16, 0x00, 0x6a, 0x2e, 0x83,
    0x1a, 0xd0, 0x3a, 0x18, 0x80, 0x08, 0x16, 0x10, 0xea, 0xae, 0x83, 0x1f, 0xd0, 0x21, 0x38, 0x80, 0x08, 0x16, 0x20,
    0x1a, 0x6e, 0x43, 0x04, 0xd0, 0x27, 0x58, 0x80, 0x08, 0x16, 0x30, 0x9a, 0xee, 0x43, 0x09, 0xd0, 0x2d, 0x78, 0x80,
    0x08, 0x16, 0x40, 0x5a, 0x8e, 0x43, 0x0e, 0xd0, 0x33, 0x98, 0x80, 0x08, 0x16, 0x50, 0xda, 0x4e, 0x43, 0x13, 0xd0,
    0x39, 0xb8, 0x80, 0x08, 0x16, 0x60, 0x3a, 0xce, 0x43, 0x18, 0xd0, 0x3f, 0xd8, 0x80, 0x08, 0x16, 0x70, 0xba, 0x2e,
    0x43, 0x1d, 0x0c, 0x48, 0xf0, 0x01, 0x11, 0x2c, 0x00, 0xf5, 0x5c, 0x87, 0x05, 0x18, 0xa8, 0x60, 0x04, 0x22, 0x58,
    0x40, 0xea, 0x03, 0x00,
};
static const u8 kStored[] = {
    0x01, 0x2c, 0x01, 0xd3, 0xfe, 0x07, 0x00, 0x07, 0x02, 0x39, 0x0d, 0x11, 0x07, 0x39, 0x00, 0x0e, 0x12, 0x0f,
    0x0d, 0x05, 0x21, 0x07, 0x05, 0x01, 0x05, 0x13, 0x12, 0x0d, 0x02, 0x04, 0x07, 0x06, 0x1f, 0x07, 0x0e, 0x21,
    0x06, 0x07, 0x12, 0x0d, 0x01, 0x05, 0x00, 0x06, 0x07, 0x01, 0x00, 0x05, 0x05, 0x1b, 0x07, 0x02, 0x07, 0x05,
    0x00, 0x0e, 0x01, 0x13, 0x01, 0x04, 0x06, 0x07, 0x06, 0x0d, 0x1a, 0x05, 0x01, 0x1a, 0x10, 0x21, 0x28, 0x06,
    0x0d, 0x13, 0x0f, 0x1b, 0x0d, 0x00, 0x05, 0x1f, 0x1d, 0x06, 0x02, 0x00, 0x05, 0x06, 0x3b, 0x06, 0x04, 0x13,
    0x00, 0x03, 0x06, 0x04, 0x14, 0x06, 0x10, 0x34, 0x07, 0x13, 0x03, 0x21, 0x1d, 0x12, 0x1f, 0x50, 0x14, 0x11,
    0x06, 0x04, 0x13, 0x05, 0x02, 0x00, 0x37, 0x03, 0x04, 0x0e, 0x05, 0x02, 0x03, 0x21, 0x10, 0x03, 0x05, 0x13,
    0x04, 0x05, 0x0e, 0x01, 0x0f, 0x03, 0x1c, 0x04, 0x06, 0x10, 0x1c, 0x06, 0x06, 0x11, 0x2b, 0x06, 0x04, 0x00,
    0x04, 0x0d, 0x06, 0x0f, 0x20, 0x00, 0x13, 0x01, 0x13, 0x2c, 0x05, 0x07, 0x10, 0x1c, 0x07, 0x04, 0x1b, 0x02,
    0x05, 0x04, 0x05, 0x0e, 0x10, 0x10, 0x1f, 0x07, 0x02, 0x01, 0x13, 0x02, 0x3b, 0x04, 0x14, 0x03, 0x07, 0x1a,
    0x20, 0x10, 0x05, 0x11, 0x00, 0x00, 0x1b, 0x05, 0x02, 0x0f, 0x2b, 0x27, 0x2d, 0x06, 0x0e, 0x1d, 0x1a, 0x01,
    0x45, 0x11, 0x0d, 0x10, 0x0f, 0x00, 0x2e, 0x00, 0x07, 0x1d, 0x0f, 0x03, 0x2a, 0x04, 0x2c, 0x12, 0x1f, 0x1a,
    0x1c, 0x06, 0x29, 0x34, 0x03, 0x46, 0x0f, 0x21, 0x06, 0x46, 0x35, 0x11, 0x0d, 0x06, 0x07, 0x1b, 0x0e, 0x2b,
    0x0d, 0x01, 0x10, 0x04, 0x04, 0x43, 0x03, 0x06, 0x51, 0x1d, 0x14, 0x1f, 0x1c, 0x05, 0x14, 0x07, 0x1b, 0x04,
    0x01, 0x11, 0x06, 0x20, 0x06, 0x1b, 0x04, 0x01, 0x35, 0x2d, 0x1c, 0x00, 0x02, 0x00, 0x03, 0x28, 0x04, 0x06,
    0x0d, 0x37, 0x03, 0x20, 0x2c, 0x02, 0x05, 0x2d, 0x05, 0x01, 0x07, 0x1a, 0x1d, 0x06, 0x1e, 0x13, 0x0e, 0x04,
    0x03, 0x44, 0x07, 0x00, 0x0d, 0x0d, 0x1c, 0x12, 0x01, 0x10, 0x06, 0x3b, 0x28, 0x10, 0x00, 0x2b, 0x14,
};

static ResourceId rid(const char* file, u16 type, u16 id, u16 sub) {
    ResourceId r;
    r.file = file;
    r.path = std::string("ENGLISH\\") + file;
    r.type = type;
    r.id = id;
    r.sub = sub;
    return r;
}
// Applies the merged override to an "original" payload of `origSize` 0xEE bytes; returns the result.
static std::vector<u8> applyTo(const std::shared_ptr<IResourceOverride>& o, const ResourceId& id, size_t origSize,
                               bool* changed = nullptr) {
    std::vector<u8> d(origSize, 0xEE);
    bool c = o && o->wants(id) && o->apply(id, d);
    if (changed)
        *changed = c;
    return d;
}

static std::string toml(const std::string& id, const std::string& ver = "1.0", const std::string& top = "",
                        const std::string& target = "profile = \"pal\"\n") {
    return top + "id = \"" + id + "\"\nname = \"Pack " + id + "\"\nversion = \"" + ver + "\"\n[target]\n" + target;
}

// ---- whole files, patched containers, new entries, levels (against a folder-backed IsoFs)
static std::vector<u8> makeSrsc(const std::vector<std::pair<SrscEntry, std::vector<u8>>>& es) {
    std::vector<u8> v(16, 0);
    memcpy(v.data(), "SRSC", 4);
    v[4] = 1;
    v[5] = 1;
    std::vector<SrscEntry> idx;
    for (auto& [e, d] : es) {
        SrscEntry x = e;
        x.off = (u32)v.size();
        x.size = (u32)d.size();
        v.insert(v.end(), d.begin(), d.end());
        while (v.size() % 8)
            v.push_back(0);
        idx.push_back(x);
    }
    u32 io = (u32)v.size(), n = (u32)idx.size();
    memcpy(&v[6], &io, 4);
    memcpy(&v[10], &n, 4);
    for (auto& x : idx) {
        u8 b[14];
        memcpy(b, &x.type, 2);
        memcpy(b + 2, &x.id, 2);
        memcpy(b + 4, &x.sub, 2);
        memcpy(b + 6, &x.off, 4);
        memcpy(b + 10, &x.size, 4);
        v.insert(v.end(), b, b + 14);
    }
    return v;
}
static bool entryBytes(IsoFs& f, const std::string& path, u16 t, u16 i, u16 s, std::vector<u8>& out) {
    IsoFs::Node n;
    if (!f.lookup(path, n))
        return false;
    std::vector<u8> all = f.readAll(n);
    if (all.size() < 16)
        return false;
    u32 idx, cnt;
    memcpy(&idx, &all[6], 4);
    memcpy(&cnt, &all[10], 4);
    for (u32 k = 0; k < cnt; ++k) {
        SrscEntry e;
        memcpy(&e.type, &all[idx + k * 14], 2);
        memcpy(&e.id, &all[idx + k * 14 + 2], 2);
        memcpy(&e.sub, &all[idx + k * 14 + 4], 2);
        memcpy(&e.off, &all[idx + k * 14 + 6], 4);
        memcpy(&e.size, &all[idx + k * 14 + 10], 4);
        if (e.type == t && e.id == i && e.sub == s) {
            out.assign(all.begin() + e.off, all.begin() + e.off + e.size);
            return true;
        }
    }
    return false;
}

static void filesTests(const fs::path& root, const DiscProfile& pal) {
    const fs::path disc = root / "disc";
    writeFile(disc / "ENGLISH/LEVELS/LEVEL1NA/LEVEL1NA.LVL", makeSrsc({{{0x20, 1, 0, 0, 0}, fill(100, 0x11)},
                                                                       {{0x20, 2, 0, 0, 0}, fill(40, 0x22)},
                                                                       {{0x30, 3, 0, 0, 0}, fill(8, 0x33)}}));
    const std::vector<u8> frLvl = makeSrsc({{{0x20, 2, 0, 0, 0}, fill(40, 0x22)}});
    writeFile(disc / "FRENCH/LEVELS/LEVEL1NA/LEVEL1NA.LVL", frLvl);
    writeFile(disc / "ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(64, 0x44)}}));
    writeText(disc / "ENGLISH/LEVELLST.TXT", "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n");
    IsoFs f;
    std::string err;
    CHECK(f.openFolder(disc.string(), &err));
    f.setLanguageRoot("\\English");
    IsoFs::Node n;
    CHECK(f.lookup("\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", n));

    // memory overlay
    f.addMemoryFile("\\Common\\Weapons\\Extra.txt", {'h', 'i'});
    CHECK(f.lookup("ENGLISH\\COMMON\\WEAPONS\\EXTRA.TXT", n) && n.size == 2 && f.readAll(n).size() == 2);

    // patchSrsc: in place (shorter), appended (longer), new, wildcard, unmatched, bad input
    {
        std::vector<u8> base =
            makeSrsc({{{1, 1, 0, 0, 0}, fill(32, 1)}, {{1, 1, 5, 0, 0}, fill(32, 2)}, {{1, 2, 0, 0, 0}, fill(16, 3)}});
        std::vector<SrscPatch> ps(4);
        ps[0] = {1, 1, 0xFFFF, fill(8, 9), false};
        ps[1] = {1, 2, 0, fill(64, 8), false};
        ps[2] = {9, 9, 1, fill(5, 7), true};
        ps[3] = {5, 5, 5, fill(5, 7), false}; // no such entry: skipped
        std::vector<u8> out;
        CHECK(patchSrsc(base, ps, out, err));
        u32 cnt;
        memcpy(&cnt, &out[10], 4);
        CHECK(cnt == 4 && memcmp(out.data(), "SRSC", 4) == 0 && out.size() > base.size());
        std::vector<u8> bad(base.begin(), base.begin() + 12);
        CHECK(!patchSrsc(bad, ps, out, err) && !err.empty());
        std::vector<SrscPatch> noSub(1);
        noSub[0] = {9, 9, 0xFFFF, fill(1, 1), true};
        CHECK(!patchSrsc(base, noSub, out, err));
    }

    // ODB header record 0x0100: the u16 class and group counts follow the added 0x0102 / 0x0101 records (the guest
    // sizes its class table from them; 3 records past the declared count corrupted the heap), but a pack's own counts
    // stay
    {
        auto hdr = [](u16 c, u16 g) {
            std::vector<u8> h(8, 0);
            memcpy(&h[0], &c, 2);
            memcpy(&h[2], &g, 2);
            return h;
        };
        auto countsOf = [](const std::vector<u8>& b, u16& c, u16& g) {
            u32 idx, n;
            memcpy(&idx, &b[6], 4);
            memcpy(&n, &b[10], 4);
            for (u32 i = 0; i < n; ++i) {
                u16 t;
                u32 off;
                memcpy(&t, &b[idx + i * 14], 2);
                if (t != 0x100)
                    continue;
                memcpy(&off, &b[idx + i * 14 + 6], 4);
                memcpy(&c, &b[off], 2);
                memcpy(&g, &b[off + 2], 2);
            }
        };
        std::vector<u8> odb = makeSrsc({{{0x100, 0, 0, 0, 0}, hdr(2, 1)},
                                        {{0x101, 1, 0, 0, 0}, fill(8, 1)},
                                        {{0x102, 5, 1, 0, 0}, fill(24, 2)},
                                        {{0x102, 9, 1, 0, 0}, fill(24, 3)}});
        std::vector<SrscPatch> add(3);
        for (u16 k = 0; k < 3; ++k)
            add[k] = {0x102, (u16)(20 + k), 1, fill(24, 4), true};
        std::vector<u8> out;
        u16 c = 0, g = 0;
        CHECK(patchSrsc(odb, add, out, err));
        countsOf(out, c, g);
        CHECK(c == 5 && g == 1);
        // a patch that only replaces a record leaves the counts alone
        std::vector<SrscPatch> rep(1);
        rep[0] = {0x102, 5, 1, fill(8, 6), false};
        CHECK(patchSrsc(odb, rep, out, err));
        countsOf(out, c, g);
        CHECK(c == 2 && g == 1);
        // a pack that wrote its own header counts keeps them
        std::vector<SrscPatch> own = add;
        own.push_back({0x100, 0, 0, hdr(40, 7), false});
        CHECK(patchSrsc(odb, own, out, err));
        countsOf(out, c, g);
        CHECK(c == 40 && g == 7);
        // a container without the header record (or with other counts there) is not touched
        std::vector<u8> plain = makeSrsc({{{0x102, 5, 1, 0, 0}, fill(24, 2)}});
        CHECK(patchSrsc(plain, {add[0]}, out, err));
        u32 plainCount;
        memcpy(&plainCount, &out[10], 4);
        CHECK(plainCount == 2);
    }

    // ODB field lists (0x0103): a pack built against the English container (3 records + 1 added) mounted on another
    // language tree whose container has 2 records keeps that tree's lists and appends only the added record's list.
    {
        auto list = [](u32 nFields, u8 tag) {
            std::vector<u8> l;
            p32(l, nFields);
            for (u32 i = 0; i < nFields; ++i) {
                p32(l, 7);
                p16(l, 2);
                l.push_back(tag);
                l.push_back(0);
            }
            return l;
        };
        auto stored = [](const std::vector<u8>& d) { // zlib, one stored block
            std::vector<u8> z = {0x78, 0x01, 1, (u8)d.size(), (u8)(d.size() >> 8), (u8)~d.size(), (u8)(~d.size() >> 8)};
            z.insert(z.end(), d.begin(), d.end());
            u32 a = 1, b = 0;
            for (u8 c : d)
                a = (a + c) % 65521, b = (b + a) % 65521;
            for (int s = 24; s >= 0; s -= 8)
                z.push_back((u8)(((b << 16) | a) >> s));
            return z;
        };
        auto cat = [](std::initializer_list<std::vector<u8>> parts) {
            std::vector<u8> v;
            for (auto& p : parts)
                v.insert(v.end(), p.begin(), p.end());
            return v;
        };
        auto listsOf = [&](const std::vector<u8>& b) {
            u32 idx, n;
            memcpy(&idx, &b[6], 4);
            memcpy(&n, &b[10], 4);
            for (u32 i = 0; i < n; ++i) {
                u16 t;
                u32 off, size;
                memcpy(&t, &b[idx + i * 14], 2);
                memcpy(&off, &b[idx + i * 14 + 6], 4);
                memcpy(&size, &b[idx + i * 14 + 10], 4);
                if (t != 0x103)
                    continue;
                std::vector<u8> raw;
                CHECK(inflateRaw(&b[off + 2], size - 2, raw, 1u << 20));
                return raw;
            }
            return std::vector<u8>();
        };
        const std::vector<u8> own = cat({list(1, 'a'), list(2, 'b')});
        std::vector<u8> tree = makeSrsc({{{0x102, 5, 1, 0, 0}, fill(24, 2)},
                                         {{0x102, 9, 1, 0, 0}, fill(24, 3)},
                                         {{0x103, 0, 0, 0, 0}, stored(own)}});
        const std::vector<u8> english = cat({list(1, 'x'), list(1, 'y'), list(1, 'z'), list(3, 'n')});
        std::vector<SrscPatch> ps(2);
        ps[0] = {0x102, 30, 1, fill(24, 4), true};
        ps[1] = {0x103, 0, 0, stored(english), false};
        std::vector<u8> out;
        CHECK(patchSrsc(tree, ps, out, err));
        CHECK(listsOf(out) == cat({own, list(3, 'n')}));
        CHECK(logHas("field lists of an ODB with 2 own classes kept"));
        // the English tree (3 records) takes the pack's lists as they are
        std::vector<u8> en = makeSrsc({{{0x102, 5, 1, 0, 0}, fill(24, 2)},
                                       {{0x102, 9, 1, 0, 0}, fill(24, 3)},
                                       {{0x102, 11, 1, 0, 0}, fill(24, 3)},
                                       {{0x103, 0, 0, 0, 0}, stored(cat({list(1, 'x'), list(1, 'y'), list(1, 'z')}))}});
        CHECK(patchSrsc(en, ps, out, err));
        CHECK(listsOf(out) == english);
        std::vector<u8> merged = stored(english);
        CHECK(!rebaseFieldLists(stored(cat({list(1, 'x'), list(1, 'y'), list(1, 'z')})), 3, 1, merged));
        CHECK(!rebaseFieldLists({1, 2, 3}, 2, 1, merged) && merged == stored(english));
    }

    // `level_aspect` lists the levels a pack wants shown at one picture shape.
    {
        Manifest m;
        std::string err;
        CHECK(parseManifest("format = 1\nid = \"a\"\nlevel_aspect = [\"Picture Level = 4:3\", \"Other = 16:9\"]\n"
                            "[target]\nprofile = \"pal\"\n",
                            false, m, err));
        CHECK(m.levelAspect.size() == 2 && m.levelAspect[0] == "Picture Level = 4:3" &&
              m.levelAspect[1] == "Other = 16:9");
        CHECK(parseManifest("format = 1\nid = \"a\"\n[target]\nprofile = \"pal\"\n", false, m, err));
        CHECK(m.levelAspect.empty());
    }

    // Imported-bow sidecars are bounded, enabled-pack-only, and preserve mod load order.
    {
        const fs::path mods = root / "mods_bow_metadata";
        for (const auto* id : {"first", "last", "off"}) {
            writeText(mods / id / "mod.toml", toml(id));
            writeText(mods / id / "gameplay/bows.ini", std::string("format=1\n;") + id);
        }
        writeText(mods / "first/gameplay/deeper/bows.ini", "ignored");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"first", true}, {"off", false}, {"last", true}}) == 2);
        CHECK(mm.bowAimFiles().size() == 2);
        CHECK(mm.bowAimFiles()[0].second == "format=1\n;first");
        CHECK(mm.bowAimFiles()[1].second == "format=1\n;last");
        CHECK(mm.load(mods.string(), {{"first", false}, {"off", false}, {"last", false}}) == 0);
        CHECK(mm.bowAimFiles().empty());
    }

    // A pack whose zip or manifest cannot be read keeps the reason, enabled or not (the title screen names it).
    {
        const fs::path mods = root / "mods_unreadable";
        writeText(mods / "broken.zip", "PK");
        writeText(mods / "ok/mod.toml", toml("ok"));
        writeText(mods / "nomanifest/readme.txt", "x");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"broken", false}, {"ok", false}, {"nomanifest", true}, {"gone", true}}) == 0);
        CHECK(mm.status().size() == 4);
        auto of = [&](const char* key) -> const ModStatus& { // status() is in resolved load order
            for (const ModStatus& s : mm.status())
                if (s.key == key)
                    return s;
            return mm.status().front();
        };
        CHECK(of("broken").state == ModState::Disabled && !of("broken").hasManifest &&
              of("broken").readError == "not a zip (too small)");
        CHECK(of("ok").hasManifest && of("ok").readError.empty());
        CHECK(of("nomanifest").state == ModState::Skipped && of("nomanifest").readError == "no mod.toml or mod.json" &&
              of("nomanifest").message == "invalid pack: no mod.toml or mod.json");
        CHECK(of("gone").key == "gone" && of("gone").source.empty() && of("gone").readError.empty()); // missing
    }

    // A `new` line whose pack is gone is dropped from mods.ini (never decided, nothing to keep); `on` / `off` lines of
    // missing packs are decisions and stay, the `on` one reported as missing.
    {
        const fs::path mods = root / "mods_stale";
        const std::string kept = "[mods]\n; keep me\none = new\nkept = off\nwanted = on\n";
        writeText(mods / "one/mod.toml", toml("one"));
        writeText(mods / "mods.ini", "[mods]\n; keep me\none = new\nGone = new\nkept = off\nwanted = on\n");
        ModManager mm(pal);
        CHECK(mm.load(mods.string()) == 0);
        std::vector<std::string> keys;
        for (const ModStatus& s : mm.status())
            keys.push_back(s.key);
        CHECK((keys == std::vector<std::string>{"one", "kept", "wanted"}));
        auto readIni = [&] {
            std::ifstream in(mods / "mods.ini", std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        CHECK(readIni() == kept);
        CHECK(!fs::exists(mods / "mods.ini.tmp"));
        CHECK(mm.status()[0].isNew && !mm.status()[1].isNew && mm.status()[2].source.empty() &&
              mm.status()[2].enabled && mm.status()[2].state == ModState::Skipped);
        ModManager again(pal); // nothing stale any more: the file is left alone
        CHECK(again.load(mods.string()) == 0 && again.status().size() == 3);
        CHECK(readIni() == kept);
    }

    // Portable English pack files also serve the running language. Explicit disc
    // lookups keep their original language, and localized files retain load precedence.
    {
        const fs::path mods = root / "mods_localized";
        writeText(disc / "GERMAN/LEVELLST.TXT", "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n");
        writeFile(disc / "GERMAN/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(12, 0x21)}}));
        writeZip(
            mods / "first.zip",
            {stored("mod.toml", toml("first", "1.0", "levels = [\"Level 9X - Clone Test\"]\n")),
             stored("files/English/Levels/Level9st/Level9st.lvl", makeSrsc({{{0x20, 1, 0, 0, 0}, fill(10, 0x55)}})),
             stored("files/English/Common/Weap/Weap.txd", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x61)}})),
             stored("files/German/Common/Weap/Weap.txd", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x62)}}))});
        writeText(mods / "last/mod.toml", toml("last"));
        writeFile(mods / "last/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x63)}}));
        IsoFs localized;
        CHECK(localized.openFolder(disc.string(), &err));
        localized.setLanguageRoot("German");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"first", true}, {"last", false}}) == 1);
        CHECK(mm.installFiles(localized) >= 4);
        CHECK(mm.extraLevels().size() == 1);
        std::vector<u8> b;
        CHECK(entryBytes(localized, "\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl", 0x20, 1, 0, b) &&
              b[0] == 0x55);
        CHECK(entryBytes(localized, "GERMAN\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x62);
        CHECK(entryBytes(localized, "ENGLISH\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x61);
        CHECK(localized.findKey("FRENCH\\LEVELS\\LEVEL1NA\\LEVEL1NA.LVL", n) && !n.mem);
        CHECK(mm.load(mods.string(), {{"first", true}, {"last", true}}) == 2);
        CHECK(mm.installFiles(localized) >= 4);
        CHECK(entryBytes(localized, "GERMAN\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x63);
    }

    // The members can be read ahead of the install (prefetchFiles): the same files are served when the language
    // matches, and a read-ahead for another language is dropped and read again.
    {
        const fs::path mods = root / "mods_localized";
        for (const char* ahead : {"\\German", "\\ENGLISH"}) {
            IsoFs localized;
            CHECK(localized.openFolder(disc.string(), &err));
            localized.setLanguageRoot("German");
            ModManager mm(pal);
            CHECK(mm.load(mods.string(), {{"first", true}, {"last", true}}) == 2);
            mm.prefetchFiles(ahead);
            g_log.clear();
            CHECK(mm.installFiles(localized) >= 4);
            CHECK(logHas("pack files installed"));
            CHECK(logHas("ahead of the install") == (std::string(ahead) == "\\German"));
            CHECK(mm.extraLevels().size() == 1);
            std::vector<u8> b;
            CHECK(entryBytes(localized, "\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl", 0x20, 1, 0, b) &&
                  b[0] == 0x55);
            CHECK(entryBytes(localized, "GERMAN\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x63);
            CHECK(entryBytes(localized, "ENGLISH\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x63);
        }
    }

    // pack with files/, LVL override, new/ entry, levels
    {
        const fs::path mods = root / "mods_files";
        writeText(mods / "pk/mod.toml", toml("pk", "1.0",
                                             "levels = [\"Level 9X - Clone Test\", \"Bad/Name\"]\n"
                                             "level_copies = [\"Level 9X - Clone Test = Level 1A - Surdana\"]\n"));
        writeFile(mods / "pk/files/ENGLISH/LEVELS/LEVEL9ST/LEVEL9ST.LVL",
                  makeSrsc({{{0x20, 1, 0, 0, 0}, fill(10, 0x55)}}));
        writeFile(mods / "pk/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(70, 0x66)}}));
        writeFile(mods / "pk/files/ENGLISH/COMMON/BAD/BAD.TXD", fill(40, 0)); // not SRSC: rejected
        writeFile(mods / "pk/overrides/LEVEL1NA.LVL/0020_2_x.bin", fill(30, 0x77));
        writeFile(mods / "pk/new/WEAP.TXD/0040_900_0.bin", fill(24, 0x88));
        writeText(mods / "pk/maps/clone_arena.ini",
                  "[arena clone]\nname = Clone Arena\nlevel = Level 9X - Clone Test\n");
        writeFile(mods / "pk/previews/Clone_Arena.png", fill(20, 0x42));
        writeText(mods / "pk/previews/deeper/x.png", "no");
        writeText(mods / "pk/maps/notes.txt", "not a map file");
        writeText(mods / "pk/maps/deeper/ignored.ini", "[arena no]\n");
        writeFile(mods / "late/overrides/LEVEL1NA.LVL/0020_2_0.bin", fill(12, 0x99));
        writeText(mods / "late/mod.toml", toml("late"));
        ModManager mm(pal);
        g_log.clear();
        CHECK(mm.load(mods.string(), {{"pk", true}, {"late", true}}) == 2);
        CHECK(mm.status()[0].files == 3 && mm.status()[0].added == 1 && mm.status()[0].levels == 1 &&
              mm.status()[0].rejected == 1);
        CHECK(logHas("ignoring level 'Bad/Name'"));
        const size_t served = mm.installFiles(f);
        CHECK(served >= 4);
        CHECK(logHas("is not a valid SRSC container"));
        std::vector<u8> b;
        CHECK(entryBytes(f, "\\Common\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b.size() == 70 && b[0] == 0x66);
        CHECK(entryBytes(f, "\\Common\\WEAP\\WEAP.TXD", 0x40, 900, 0, b) && b.size() == 24 && b[0] == 0x88);
        // LVL: the later pack's exact override wins over the earlier x
        CHECK(entryBytes(f, "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", 0x20, 2, 0, b) && b.size() == 12 &&
              b[0] == 0x99);
        CHECK(entryBytes(f, "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", 0x20, 1, 0, b) && b.size() == 100);
        // other language trees are not patched
        CHECK(f.findKey("FRENCH\\LEVELS\\LEVEL1NA\\LEVEL1NA.LVL", n) && n.size == frLvl.size() && !n.mem);
        // new level file reachable by long name and listed
        CHECK(f.lookup("\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl", n) && n.size > 0);
        // multiplayer maps: only maps/*.ini directly in the folder of a loaded pack
        CHECK(mm.mapFiles().size() == 1 && mm.mapFiles()[0].first.find("mod pk") == 0 &&
              mm.mapFiles()[0].second.find("[arena clone]") == 0);
        CHECK(mm.previewFiles().size() == 1 && mm.previewFiles()[0].first == "Clone_Arena" &&
              mm.previewFiles()[0].second.size() == 20);
        CHECK(mm.extraLevels().size() == 1 && mm.extraLevels()[0].name == "Level 9X - Clone Test" &&
              mm.extraLevels()[0].listIndex == 1 && mm.extraLevels()[0].modId == "pk" &&
              mm.extraLevels()[0].copyOf == "Level 1A - Surdana");
        CHECK(f.lookup("LEVELLST.TXT", n));
        std::vector<u8> lst = f.readAll(n);
        std::string txt(lst.begin(), lst.end());
        CHECK(txt == "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n"
                     "\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl\r\n");
    }

    // the Mods page cover: previews/pack.png of a pack is read even while the pack is off; a pack without one has none
    {
        const fs::path mods = root / "mods_cover";
        writeText(mods / "pic/mod.toml", toml("pic"));
        writeText(mods / "pic/previews/pack.png", "PNGDATA");
        writeText(mods / "plain/mod.toml", toml("plain"));
        ModManager mm(pal);
        mm.load(mods.string(), {{"pic", false}, {"plain", false}});
        CHECK(mm.status().size() == 2 && mm.status()[0].cover.size() == 7 && mm.status()[1].cover.empty());
    }

    // guest limits: a replacement whose entry crosses the whole-entry limit where the original did not is rejected
    {
        const fs::path mods = root / "mods_files2";
        writeText(mods / "big/mod.toml", toml("big"));
        writeFile(mods / "big/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(0x41000, 1)}}));
        IsoFs f2;
        CHECK(f2.openFolder(disc.string(), &err));
        f2.setLanguageRoot("\\English");
        ModManager mm(pal);
        mm.load(mods.string(), {{"big", true}});
        g_log.clear();
        mm.installFiles(f2);
        CHECK(logHas("past the 264192 byte whole-entry limit") && logHas("file rejected"));
        IsoFs::Node n2;
        CHECK(f2.lookup("\\Common\\WEAP\\WEAP.TXD", n2) && n2.size < 0x1000);
        // a pack file over the whole-file cap is rejected at load
        ModLimits lim;
        lim.maxWholeFile = 100;
        ModManager small(pal, lim);
        small.load(mods.string(), {{"big", true}});
        CHECK(small.status()[0].rejected == 1 && small.status()[0].files == 0);
    }

    // a later pack replaces an earlier pack's file: conflict reported, later wins
    {
        const fs::path mods = root / "mods_files3";
        writeText(mods / "a/mod.toml", toml("a"));
        writeText(mods / "b/mod.toml", toml("b"));
        writeFile(mods / "a/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 1)}}));
        writeFile(mods / "b/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(11, 2)}}));
        IsoFs f3;
        CHECK(f3.openFolder(disc.string(), &err));
        f3.setLanguageRoot("\\English");
        ModManager mm(pal);
        mm.load(mods.string(), {{"a", true}, {"b", true}});
        CHECK(mm.conflicts().size() == 1 && mm.conflicts()[0].winner == "b");
        mm.installFiles(f3);
        std::vector<u8> b;
        CHECK(entryBytes(f3, "\\Common\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b.size() == 11);
    }

    // content files (story editing): entries of ODB / XDB / SDB / SSD are replaced per request (0x1202 override layer)
    // and added with new/ (container rebuilt); both together, a wildcard sub, and a pack that only adds
    {
        const fs::path d2 = root / "disc_story";
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.ODB",
                  makeSrsc({{{0x102, 4167, 3055, 0, 0}, fill(86, 0x10)}, {{0x103, 0, 0, 0, 0}, fill(40, 0x11)}}));
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.XDB",
                  makeSrsc({{{0x400, 22, 8, 0, 0}, fill(64, 0x20)}, {{0x400, 23, 8, 0, 0}, fill(64, 0x21)}}));
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.SDB",
                  makeSrsc({{{0x302, 5, 7, 0, 0}, fill(48, 0x30)}, {{0x304, 5, 7, 0, 0}, fill(200, 0x31)}}));
        writeFile(d2 / "ENGLISH/LEVELS/LEVEL1ON/LEVEL1ON.SSD", makeSrsc({{{0x311, 9, 0, 0, 0}, fill(120, 0x40)}}));
        const fs::path mods = root / "mods_story";
        writeText(mods / "story/mod.toml", toml("story"));
        writeFile(mods / "story/overrides/SYSTEM.ODB/0102_4167_3055.bin", fill(90, 0xA1));
        writeFile(mods / "story/overrides/SYSTEM.XDB/0400_22_x.bin", fill(80, 0xA2));
        writeFile(mods / "story/overrides/SYSTEM.SDB/0302_5_7.bin", fill(52, 0xA3));
        writeFile(mods / "story/overrides/LEVEL1ON.SSD/0311_9_0.bin", fill(100, 0xA4));
        writeFile(mods / "story/new/SYSTEM.XDB/0400_114_8.bin", fill(40, 0xB1));
        writeFile(mods / "story/new/SYSTEM.ODB/0102_4200_3055.bin", fill(88, 0xB2));
        writeFile(mods / "story/new/SYSTEM.ODB/0103_0_0.bin", fill(60, 0xB3)); // replaces the existing 0x103 (same key)
        writeFile(mods / "story/new/LEVEL1ON.SSD/0311_10_0.bin", fill(30, 0xB4));
        writeText(mods / "adds/mod.toml", toml("adds"));
        writeFile(mods / "adds/new/SYSTEM.XDB/0400_115_8.bin", fill(44, 0xB5));
        IsoFs f4;
        std::string err4;
        CHECK(f4.openFolder(d2.string(), &err4));
        f4.setLanguageRoot("\\English");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"story", true}, {"adds", true}}) == 2);
        CHECK(mm.status()[0].added == 4 && mm.status()[0].entries == 4 && mm.status()[1].added == 1);
        // per-request overrides: sizes may differ from the original (86 -> 90, 64 -> 80, 48 -> 52, 120 -> 100)
        CHECK(applyTo(mm.entries(), rid("SYSTEM.ODB", 0x102, 4167, 3055), 86) == fill(90, 0xA1));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 22, 8), 64) == fill(80, 0xA2));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 22, 3), 64) == fill(80, 0xA2)); // x sub
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 23, 8), 64) == fill(64, 0xEE)); // untouched string
        CHECK(applyTo(mm.entries(), rid("SYSTEM.SDB", 0x302, 5, 7), 48) == fill(52, 0xA3));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.SDB", 0x304, 5, 7), 200) == fill(200, 0xEE)); // audio data untouched
        CHECK(applyTo(mm.entries(), rid("LEVEL1ON.SSD", 0x311, 9, 0), 120) == fill(100, 0xA4));
        g_log.clear();
        const size_t served4 = mm.installFiles(f4);
        CHECK(served4 == 3); // SYSTEM.XDB, SYSTEM.ODB, LEVEL1ON.SSD rebuilt; SYSTEM.SDB untouched
        std::vector<u8> b;
        const std::string xdbPath = "\\Common\\System\\SYSTEM.XDB", odbPath = "\\Common\\System\\SYSTEM.ODB";
        CHECK(entryBytes(f4, xdbPath, 0x400, 114, 8, b) && b == fill(40, 0xB1));
        CHECK(entryBytes(f4, xdbPath, 0x400, 115, 8, b) && b == fill(44, 0xB5));
        CHECK(entryBytes(f4, xdbPath, 0x400, 23, 8, b) && b == fill(64, 0x21));
        CHECK(entryBytes(f4, odbPath, 0x102, 4200, 3055, b) && b == fill(88, 0xB2));
        CHECK(entryBytes(f4, odbPath, 0x103, 0, 0, b) && b == fill(60, 0xB3));
        // ODB is plain-read by the game: an `overrides/` entry of it is patched into the rebuilt file as well
        CHECK(entryBytes(f4, odbPath, 0x102, 4167, 3055, b) && b == fill(90, 0xA1));
        IsoFs::Node sn;
        CHECK(f4.lookup("\\Levels\\LEVEL1ON\\LEVEL1ON.SSD", sn) && sn.mem);
        CHECK(f4.lookup("\\Common\\System\\SYSTEM.SDB", sn) && !sn.mem);
    }

    // A pack level converted on the intro template names the intro's own database without a folder ("Level 0 - Intro"),
    // which the engine looks for next to the converted level. It is served there from the sibling level folder, in the
    // running language tree, with the pack's entry patches; the intro's .LVL and other folders are left alone.
    {
        auto lstr = [](std::vector<u8>& v, const std::string& s) {
            p16(v, (u32)s.size() + 2);
            v.insert(v.end(), s.begin(), s.end());
            v.push_back(0);
            v.push_back(0);
        };
        auto header = [&](const std::vector<std::string>& deps) {
            std::vector<u8> h;
            lstr(h, "OotF 00 - Intro");
            p32(h, 64);
            p32(h, 64);
            p32(h, (u32)deps.size());
            u32 i = 1;
            for (const std::string& d : deps) {
                p16(h, i++);
                p16(h, 0);
                lstr(h, d);
            }
            return h;
        };
        const fs::path d5 = root / "disc_intro";
        for (const char* lang : {"ENGLISH", "GERMAN"}) {
            const fs::path lv = d5 / lang / "LEVELS";
            const u8 tag = lang[0] == 'G' ? 0xD0 : 0xE0;
            writeText(d5 / lang / "LEVELLST.TXT", "\\Levels\\Level 0 - Intro\\Level 0 - Intro.lvl\r\n");
            writeFile(lv / "LEVEL0RO/LEVEL0RO.LVL", makeSrsc({{{0x20, 1, 0, 0, 0}, fill(20, tag)}}));
            writeFile(lv / "LEVEL0RO/LEVEL0RO.ODB", makeSrsc({{{0x102, 1, 0, 0, 0}, fill(20, tag)}}));
            writeFile(lv / "LEVEL0RO/LEVEL0RO.SSD", makeSrsc({{{0x311, 1, 0, 0, 0}, fill(20, tag)}}));
            writeText(lv / "LEVEL0RO/LEVEL0RO.DB", "version 1\r\ndependencies 0\r\n");
            writeFile(lv / "LEVEL0RO/LEVEL0TS.TXD", makeSrsc({{{0x40, 1, 0, 0, 0}, fill(20, tag)}}));
        }
        const fs::path mods = root / "mods_intro";
        writeText(mods / "ootf/mod.toml", toml("ootf", "1.0", "levels = [\"OotF 00 - Intro\", \"OotF 01 - Lost\"]\n"));
        const fs::path lv = mods / "ootf/files/ENGLISH/LEVELS";
        writeFile(lv / "OOTF00RO/OOTF00RO.LVL",
                  makeSrsc({{{0x0, 0, 0, 0, 0},
                             header({"..\\..\\Common\\System\\System", "Level 0 - Intro", "OotF 00 - Intro_splats"})},
                            {{0x20, 1, 0, 0, 0}, fill(10, 0x55)}}));
        writeFile(lv / "OOTF01ST/OOTF01ST.LVL",
                  makeSrsc({{{0x0, 0, 0, 0, 0}, header({"Level 7 - Nowhere"})}, {{0x20, 1, 0, 0, 0}, fill(10, 0x56)}}));
        writeFile(mods / "ootf/new/LEVEL0RO.ODB/0102_2_0.bin", fill(12, 0x77));
        IsoFs f5;
        std::string err5;
        CHECK(f5.openFolder(d5.string(), &err5));
        f5.setLanguageRoot("\\German");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"ootf", true}}) == 1);
        g_log.clear();
        mm.installFiles(f5);
        std::vector<u8> b;
        const std::string odb = "\\Levels\\OotF 00 - Intro\\Level 0 - Intro.odb";
        CHECK(entryBytes(f5, odb, 0x102, 1, 0, b) && b == fill(20, 0xD0)); // the German tree's own record
        CHECK(entryBytes(f5, odb, 0x102, 2, 0, b) && b == fill(12, 0x77)); // with the pack's new entry
        CHECK(entryBytes(f5, "\\Levels\\OotF 00 - Intro\\Level 0 - Intro.ssd", 0x311, 1, 0, b) && b == fill(20, 0xD0));
        IsoFs::Node in;
        CHECK(f5.lookup("\\Levels\\OotF 00 - Intro\\Level 0 - Intro.db", in) && in.size > 0);
        CHECK(!f5.findKey("GERMAN\\LEVELS\\OOTF00RO\\LEVEL0RO.LVL", in));  // not the intro level itself
        CHECK(!f5.findKey("GERMAN\\LEVELS\\OOTF00RO\\LEVEL0TS.TXD", in));  // nor other stems
        CHECK(!f5.findKey("ENGLISH\\LEVELS\\OOTF00RO\\LEVEL0RO.ODB", in)); // only the running tree
        CHECK(f5.findKey("GERMAN\\LEVELS\\OOTF00RO\\OOTF00RO.LVL", in));   // the level itself, mounted in German
        CHECK(logHas("names the database 'Level 0 - Intro' without a folder") &&
              logHas("serving the 3 file(s) of GERMAN\\LEVELS\\LEVEL0RO"));
        CHECK(logHas("level 'OotF 01 - Lost' depends on 'Level 7 - Nowhere' next to it, which is not on the disc"));
    }
}

int main() {
    setLogSink(sink);
    setVerbose(false);
    fs::path root = fs::temp_directory_path() / "drakan_mod_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // ---- manifest parsing
    {
        Manifest m;
        std::string err;
        CHECK(parseManifest("# c\nformat = 1\nid = \"a.b-c_1\"\nname = 'X'  # trailing\nversion = \"1.2.3\"\n"
                            "author = \"me\"\ndescription = \"d \\\"q\\\"\"\n[target]\nprofile = [\"pal\", \"usa\"]\n"
                            "hash = \"00ff\"\n[load]\nafter = [\n  \"a\",\n  \"b\", # c\n]\nbefore = [\"z\"]\n"
                            "dependencies = [\"core>=1.2\"]\n",
                            false, m, err));
        CHECK(m.id == "a.b-c_1" && m.name == "X" && m.version == "1.2.3" && m.author == "me" &&
              m.description == "d \"q\"");
        CHECK(m.profiles.size() == 2 && m.profiles[1] == "usa" && m.profileHashes.size() == 1);
        CHECK(m.loadAfter.size() == 2 && m.loadAfter[1] == "b" && m.loadBefore[0] == "z" &&
              m.dependencies[0] == "core>=1.2");
        CHECK(parseManifest("{\"id\":\"j1\",\"name\":\"J\",\"version\":\"2\",\"target\":{\"profile\":\"pal\","
                            "\"hash\":\"ABCDEF\"},\"load\":{\"after\":[\"x\"]},\"dependencies\":[\"y\",\"z>=2\"],"
                            "\"extra\":[{\"a\":1},[1,2]],\"n\":{\"k\":[]}}",
                            true, m, err));
        CHECK(m.id == "j1" && m.profiles[0] == "pal" && m.profileHashes[0] == "ABCDEF" && m.loadAfter[0] == "x" &&
              m.dependencies.size() == 2);
        // A custom campaign (docs/reference/modding.md "Campaign packs").
        CHECK(
            parseManifest("id = \"c1\"\nlevels = [\"A Copy\", \"B Copy\"]\ncampaign = \"test-valley\"\n"
                          "campaign_name = \"The Test Valley\"\ncampaign_description = \"Two levels.\"\n"
                          "campaign_start = \"A Copy\"\ncampaign_saves = \"testvalley\"\n[target]\nprofile = \"pal\"\n",
                          false, m, err));
        CHECK(m.campaign == "test-valley" && m.campaignName == "The Test Valley" &&
              m.campaignDescription == "Two levels." && m.campaignStart == "A Copy" && m.campaignSaves == "testvalley");
        CHECK(!parseManifest("name = \"x\"\n", false, m, err) && err.find("'id'") != std::string::npos);
        CHECK(!parseManifest("id = \"a b\"\n", false, m, err));
        CHECK(!parseManifest("id = \"a\"\nname = \"unterminated\n", false, m, err));
        CHECK(!parseManifest("{\"id\": ", true, m, err));
        CHECK(!parseManifest("[1]", true, m, err));
        CHECK(compareVersions("1.10", "1.9") > 0 && compareVersions("1.0", "1") == 0 &&
              compareVersions("0.9", "1") < 0);
        CHECK(profileHash("SCES-50006", "BESCES-50006") != profileHash("SCUS-97128", "BASCUS-97128"));
    }

    // ---- inflate / zip
    {
        std::vector<u8> plain = plainBlob(), out;
        CHECK(inflateRaw(kDeflated, sizeof kDeflated, out, plain.size()) && out == plain);
        CHECK(!inflateRaw(kDeflated, sizeof kDeflated / 2, out, plain.size()));
        CHECK(!inflateRaw(kDeflated, sizeof kDeflated, out, plain.size() - 1)); // output limit
        writeZip(root / "z.zip",
                 {stored("a.txt", "hello"),
                  {"b.bin", plain, std::vector<u8>(kDeflated, kDeflated + sizeof kDeflated), 8, crcOf(plain)},
                  stored("../evil.bin", "x")});
        std::string err;
        auto z = openArchive((root / "z.zip").string(), err);
        CHECK(z != nullptr);
        std::vector<u8> r;
        CHECK(z && z->read("a.txt", r) && std::string(r.begin(), r.end()) == "hello");
        CHECK(z && z->read("b.bin", r) && r == plain);
        CHECK(z && !z->read("../evil.bin", r) && z->list().size() == 2); // zip-slip entry dropped
        CHECK(z && !z->read("missing", r));
        auto bad = stored("c.bin", "abc");
        bad.crc ^= 1;
        writeZip(root / "bad.zip", {bad});
        auto zb = openArchive((root / "bad.zip").string(), err);
        CHECK(zb && !zb->read("c.bin", r));
        writeZip(root / "enc.zip", {stored("a", "b")}, true);
        CHECK(!openArchive((root / "enc.zip").string(), err) && err.find("encrypted") != std::string::npos);
        writeText(root / "notzip.zip", "this is not a zip file at all, just text padding.......");
        CHECK(!openArchive((root / "notzip.zip").string(), err));
        CHECK(!openArchive((root / "nope").string(), err));
    }

    // ---- inflate: long codes, a stored block inside the bit stream, fixed codes, overlapping matches, bad input
    {
        std::vector<u8> skew = skewBlob(1500, 12345), more = skewBlob(1500, 777), out;
        skew.insert(skew.end(), more.begin(), more.end());
        CHECK(inflateRaw(kSkewDeflated, sizeof kSkewDeflated, out, skew.size()) && out == skew);
        CHECK(inflateRaw(kSkewDeflated, sizeof kSkewDeflated, out, 64u << 20) && out == skew); // a cap, not the size
        CHECK(!inflateRaw(kSkewDeflated, sizeof kSkewDeflated, out, skew.size() - 1));
        const std::vector<u8> runs = runsBlob();
        CHECK(inflateRaw(kRunsFixed, sizeof kRunsFixed, out, runs.size()) && out == runs);
        CHECK(!inflateRaw(kRunsFixed, sizeof kRunsFixed, out, runs.size() - 1));
        const std::vector<u8> plain300 = skewBlob(300, 99);
        CHECK(inflateRaw(kStored, sizeof kStored, out, plain300.size()) && out == plain300);
        CHECK(!inflateRaw(kStored, sizeof kStored, out, plain300.size() - 1));
        // every truncation fails cleanly
        bool truncatedOk = true;
        for (size_t n = 0; n < sizeof kSkewDeflated; ++n)
            truncatedOk = truncatedOk && !inflateRaw(kSkewDeflated, n, out, skew.size());
        for (size_t n = 0; n < sizeof kRunsFixed; ++n)
            truncatedOk = truncatedOk && !inflateRaw(kRunsFixed, n, out, runs.size());
        for (size_t n = 0; n < sizeof kStored; ++n)
            truncatedOk = truncatedOk && !inflateRaw(kStored, n, out, plain300.size());
        CHECK(truncatedOk);
        // damaged streams end (true or false) within the output cap
        bool boundedOk = true;
        u32 x = 4242;
        for (int round = 0; round < 4000; ++round) {
            std::vector<u8> d(kSkewDeflated, kSkewDeflated + sizeof kSkewDeflated);
            for (int k = 0; k < 3; ++k) {
                x = x * 1103515245u + 12345u;
                d[(x >> 8) % d.size()] ^= (u8)(1u << ((x >> 28) & 7));
            }
            inflateRaw(d.data(), d.size(), out, 5000);
            boundedOk = boundedOk && out.size() <= 5000;
        }
        CHECK(boundedOk);
    }

    // ---- zip: many members read from several threads at once, with and without the file held open
    {
        const std::vector<u8> plain = plainBlob(), runs = runsBlob();
        std::vector<ZEnt> ents;
        for (int i = 0; i < 120; ++i) {
            const std::string name = "new/A.TXD/m" + std::to_string(i) + ".bin";
            if (i % 3 == 0)
                ents.push_back(
                    {name, plain, std::vector<u8>(kDeflated, kDeflated + sizeof kDeflated), 8, crcOf(plain)});
            else if (i % 3 == 1)
                ents.push_back(
                    {name, runs, std::vector<u8>(kRunsFixed, kRunsFixed + sizeof kRunsFixed), 8, crcOf(runs)});
            else
                ents.push_back(stored(name, "member " + std::to_string(i)));
        }
        ents.push_back(stored("new/A.TXD/m5.bin", "a later member of the same name is never served"));
        const fs::path zipPath = root / "many.zip";
        writeZip(zipPath, ents);
        std::string err;
        auto z = openArchive(zipPath.string(), err);
        CHECK(z != nullptr);
        for (int pass = 0; z && pass < 2; ++pass) {
            z->holdOpen(pass == 0);
            std::atomic<int> wrong{0};
            std::vector<std::thread> pool;
            for (int t = 0; t < 8; ++t)
                pool.emplace_back([&, t] {
                    std::vector<u8> r;
                    for (int rep = 0; rep < 4; ++rep)
                        for (int i = t; i < 120; i += 3) {
                            const ZEnt& e = ents[(size_t)i];
                            if (!z->read(e.name, r) || r != e.plain)
                                ++wrong;
                        }
                });
            for (std::thread& t : pool)
                t.join();
            CHECK(wrong == 0);
            z->holdOpen(false);
        }
        // after holdOpen(false) the archive holds no handle on the file: it can be replaced while the game runs
        std::error_code ec;
        fs::rename(zipPath, root / "many-moved.zip", ec);
        CHECK(!ec);
    }

    const DiscProfile pal{"pal", profileHash("SCES-50006", "BESCES-50006")};
    const DiscProfile usa{"usa", profileHash("SCUS-97128", "BASCUS-97128")};

    // ---- merge order, conflicts, wildcard, text, zip + folder
    {
        fs::path mods = root / "mods1";
        writeText(mods / "alpha/mod.toml", toml("alpha"));
        writeFile(mods / "alpha/overrides/interfce.txd/0040_97_0.bin", fill(100, 0xA1));
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/0040_98_x.bin", fill(50, 0xA2));
        writeFile(mods / "alpha/overrides/INTERFCE.XDB/0400_5_2.bin", fill(10, 0xA3));
        writeText(mods / "alpha/overrides/INTERFCE.XDB/text.ini", "156 = Alpha\n157.en = A157\n");
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/bogus.bin", fill(4, 1));
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/0040_1_0.bin", {}); // empty: rejected
        // zip pack B: nested single top-level folder, mixed case
        writeZip(mods / "beta.zip", {stored("Beta/MOD.TOML", toml("beta", "2.0")),
                                     stored("Beta/overrides/INTERFCE.TXD/0040_97_0.bin", fill(120, 0xB1)),
                                     stored("Beta/overrides/INTERFCE.TXD/0040_98_3.bin", fill(60, 0xB2)),
                                     stored("Beta/overrides/INTERFCE.XDB/text.ini", "156 = Beta\n")});
        writeText(mods / "off/mod.toml", toml("off"));
        writeText(mods / "usaonly/mod.toml", toml("usaonly", "1.0", "", "profile = \"usa\"\n"));
        writeFile(mods / "usaonly/overrides/X.TXD/0040_1_0.bin", fill(8, 1));
        writeText(mods / "nomanifest/readme.txt", "hi");
        g_log.clear();
        ModManager mm(pal);
        mm.load(mods.string(), {{"alpha", true},
                                {"beta", true},
                                {"off", false},
                                {"usaonly", true},
                                {"nomanifest", true},
                                {"ghost", true}});
        CHECK(mm.status().size() == 6);
        size_t loaded = 0;
        for (auto& s : mm.status())
            loaded += s.state == ModState::Loaded;
        CHECK(loaded == 2);
        auto st = [&](const char* k) -> const ModStatus* {
            for (auto& s : mm.status())
                if (s.key == k)
                    return &s;
            return nullptr;
        };
        CHECK(st("alpha")->state == ModState::Loaded && st("alpha")->entries == 3 && st("alpha")->texts == 2 &&
              st("alpha")->rejected == 2);
        CHECK(st("beta")->state == ModState::Loaded && st("beta")->entries == 2 && st("beta")->texts == 1);
        CHECK(st("off")->state == ModState::Disabled);
        CHECK(st("usaonly")->state == ModState::Skipped && st("usaonly")->message.find("usa") != std::string::npos);
        CHECK(logHas("targets disc profile usa but this build is pal"));
        CHECK(st("nomanifest")->state == ModState::Skipped &&
              st("nomanifest")->message.find("mod.toml") != std::string::npos);
        CHECK(st("ghost")->state == ModState::Skipped && st("ghost")->message.find("not found") != std::string::npos);
        CHECK(mm.entryCount() == 4); // 97/0, 98/x, XDB 5/2, 98/3
        auto e = mm.entries();
        CHECK(e != nullptr);
        // the later pack (beta) wins the same entry; alpha-only entries survive
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 97, 0), 100) == fill(120, 0xB1));
        CHECK(applyTo(e, rid("INTERFCE.XDB", 0x400, 5, 2), 10) == fill(10, 0xA3));
        // wildcard (alpha, earlier) vs specific (beta, later) for 98/3: beta wins; other subs use the wildcard
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 98, 3), 60) == fill(60, 0xB2));
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 98, 0), 50) == fill(50, 0xA2));
        bool ch = true;
        applyTo(e, rid("INTERFCE.TXD", 0x40, 99, 0), 10, &ch);
        CHECK(!ch);
        CHECK(!e->wants(rid("X.TXD", 0x40, 1, 0))); // skipped pack contributes nothing
        bool c97 = false, c98 = false, c156 = false;
        for (auto& c : mm.conflicts()) {
            c97 |= c.what.find("0040/97/0") != std::string::npos && c.winner == "beta" && c.loser == "alpha";
            c98 |= c.what.find("0040/98/3") != std::string::npos && c.winner == "beta";
            c156 |= c.what.find("156.*") != std::string::npos && c.winner == "beta";
        }
        CHECK(c97 && c98 && c156 && mm.conflicts().size() == 3);
        CHECK(logHas("conflict:"));
        // text: the later pack wins 156, alpha's 157.en survives
        auto t = mm.texts();
        CHECK(t != nullptr && mm.textCount() == 3);
        auto txt = xdb::encode({"orig", "o", "o", "o", "o"});
        ResourceId tid = rid("INTERFCE.XDB", 0x400, 156, 2);
        std::vector<u8> d = txt;
        std::vector<std::string> sl;
        CHECK(t->wants(tid) && t->apply(tid, d) && xdb::decode(d, sl) && sl[0] == "Beta" && sl[4] == "Beta");
        tid.id = 157;
        d = txt;
        CHECK(t->apply(tid, d) && xdb::decode(d, sl) && sl[0] == "A157" && sl[1] == "o");
        CHECK(!mm.report().empty());
    }

    // ---- reversed order flips the winner
    {
        ModManager mm(pal);
        mm.load((root / "mods1").string(), {{"beta", true}, {"alpha", true}});
        CHECK(applyTo(mm.entries(), rid("INTERFCE.TXD", 0x40, 97, 0), 100) == fill(100, 0xA1));
        // alpha's wildcard 98/x is now the later pack: it beats beta's specific 98/3
        CHECK(applyTo(mm.entries(), rid("INTERFCE.TXD", 0x40, 98, 3), 60) == fill(50, 0xA2));
    }

    // ---- mods.ini: unknown packs are appended as "new" (off, ModStatus::isNew); order and enabled flags honoured
    {
        fs::path mods = root / "mods2";
        writeText(mods / "one/mod.toml", toml("one"));
        writeFile(mods / "one/overrides/A.TXD/0040_1_0.bin", fill(8, 1));
        writeText(mods / "two/mod.toml", toml("two"));
        writeFile(mods / "two/overrides/A.TXD/0040_1_0.bin", fill(8, 2));
        ModManager first(pal);
        CHECK(first.load(mods.string()) == 0); // nothing enabled yet
        std::ifstream in(mods / "mods.ini");
        std::string ini((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        CHECK(ini.find("one = new") != std::string::npos && ini.find("two = new") != std::string::npos);
        CHECK(first.status().size() == 2 && first.status()[0].isNew && first.status()[1].isNew);
        CHECK(first.status()[0].state == ModState::Disabled && !first.status()[0].enabled);
        {
            // `new` survives the next start (still undecided) and loads nothing; `off` is a decision.
            ModManager again(pal);
            CHECK(again.load(mods.string()) == 0);
            CHECK(again.status().size() == 2 && again.status()[0].isNew && again.status()[1].isNew);
            writeText(mods / "mods.ini", "[mods]\none = off\ntwo = new\n");
            ModManager decided(pal);
            CHECK(decided.load(mods.string()) == 0);
            CHECK(!decided.status()[0].isNew && decided.status()[1].isNew);
        }
        writeText(mods / "mods.ini", "[mods]\n; comment\ntwo = on\none = on\n");
        ModManager m2(pal);
        CHECK(m2.load(mods.string()) == 2);
        CHECK(applyTo(m2.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1)); // one is later
        writeText(mods / "mods.ini", "one = on\ntwo = off\n");
        ModManager m3(pal);
        CHECK(m3.load(mods.string()) == 1);
        CHECK(applyTo(m3.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1));
        CHECK(fs::file_size(mods / "mods.ini") == 19); // untouched when nothing is new
        CHECK(!m3.status()[0].isNew && !m3.status()[1].isNew);
    }

    // ---- profile hash, dependencies, duplicate ids, format, hints
    {
        fs::path mods = root / "mods3";
        const std::string h = hex64(pal.hash);
        writeText(mods / "hashok/mod.toml", "id = \"hashok\"\n[target]\nhash = \"" + h + "\"\n");
        writeText(mods / "hashbad/mod.toml", "id = \"hashbad\"\n[target]\nprofile = \"pal\"\nhash = \"deadbeef\"\n");
        writeText(mods / "notarget/mod.toml", "id = \"notarget\"\n");
        writeText(mods / "any/mod.toml", "id = \"any\"\nversion = \"1.5\"\n[target]\nprofile = \"*\"\n");
        writeText(mods / "needs/mod.json",
                  "{\"id\":\"needs\",\"target\":{\"profile\":\"pal\"},\"dependencies\":[\"any>=1.2\"],"
                  "\"load\":{\"after\":[\"any\"]}}");
        writeText(mods / "needsnew/mod.toml", "dependencies = [\"any>=2.0\"]\n" + toml("needsnew"));
        writeText(mods / "needsmissing/mod.toml", "dependencies = [\"nothere\"]\n" + toml("needsmissing"));
        writeText(mods / "chain/mod.toml", "dependencies = [\"needsmissing\"]\n" + toml("chain"));
        writeText(mods / "dup1/mod.toml", toml("same"));
        writeText(mods / "dup2/mod.toml", toml("same"));
        writeText(mods / "future/mod.toml", "format = 9\n" + toml("future"));
        g_log.clear();
        ModManager mm(pal);
        mm.load(mods.string(), {{"hashok", true},
                                {"hashbad", true},
                                {"notarget", true},
                                {"needs", true},
                                {"any", true},
                                {"needsnew", true},
                                {"needsmissing", true},
                                {"chain", true},
                                {"dup1", true},
                                {"dup2", true},
                                {"future", true}});
        auto state = [&](const char* k) {
            for (auto& s : mm.status())
                if (s.key == k)
                    return s.state;
            return ModState::Disabled;
        };
        CHECK(state("hashok") == ModState::Loaded && state("hashbad") == ModState::Skipped);
        CHECK(state("notarget") == ModState::Skipped && logHas("no target profile"));
        CHECK(state("any") == ModState::Loaded && state("needs") == ModState::Loaded);
        CHECK(state("needsnew") == ModState::Skipped && logHas("needs 'any' >= 2.0 but 1.5"));
        CHECK(state("needsmissing") == ModState::Skipped && state("chain") == ModState::Skipped); // transitive
        CHECK(state("dup1") == ModState::Loaded && state("dup2") == ModState::Skipped &&
              logHas("already used by 'dup1'"));
        CHECK(state("future") == ModState::Skipped && logHas("needs mod format 9"));
        CHECK(logHas("'needs' asks to load after 'any'")); // hint violated: "any" is listed later
        // the USA build rejects the PAL-pinned pack by hash, and accepts profile = "*"
        ModManager mu(usa);
        mu.load(mods.string(), {{"hashok", true}, {"any", true}});
        CHECK(mu.status()[0].state == ModState::Skipped && mu.status()[1].state == ModState::Loaded);
    }

    // ---- oversize: growth past the whole-entry limit is rejected; already-large entries and plain growth are not
    {
        fs::path mods = root / "mods4";
        writeText(mods / "big/mod.toml", toml("big"));
        writeFile(mods / "big/overrides/A.TXD/0040_1_0.bin", fill(0x40801, 7)); // just over
        writeFile(mods / "big/overrides/A.TXD/0040_2_0.bin", fill(0x40800, 8)); // exactly at the limit
        writeFile(mods / "big/overrides/A.TXD/0040_3_0.bin", fill(0x60000, 9)); // original already larger
        writeFile(mods / "big/overrides/A.TXD/0040_4_0.bin", fill(5000, 6));    // plain growth
        g_log.clear();
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"big", true}}) == 1);
        auto e = mm.entries();
        bool ch = true;
        auto r = applyTo(e, rid("A.TXD", 0x40, 1, 0), 1000, &ch);
        CHECK(!ch && r == fill(1000, 0xEE) && logHas("whole-entry limit"));
        r = applyTo(e, rid("A.TXD", 0x40, 2, 0), 1000, &ch);
        CHECK(ch && r.size() == 0x40800);
        r = applyTo(e, rid("A.TXD", 0x40, 3, 0), 0x50000, &ch);
        CHECK(ch && r.size() == 0x60000);
        r = applyTo(e, rid("A.TXD", 0x40, 3, 0), 0x100, &ch);
        CHECK(!ch);
        g_log.clear();
        r = applyTo(e, rid("A.TXD", 0x40, 4, 0), 100, &ch);
        CHECK(ch && r.size() == 5000 && logHas("grows 100 -> 5000"));
        ModLimits lim;
        lim.maxFileBytes = 4000; // per-file sanity cap at load
        ModManager small(pal, lim);
        small.load(mods.string(), {{"big", true}});
        CHECK(small.entryCount() == 0 && small.status()[0].rejected == 4);
    }

    // ---- a corrupt zip is skipped with a message, other packs still load
    {
        fs::path mods = root / "mods5";
        writeText(mods / "broken.zip", "PK but nothing else here, definitely not a valid archive........");
        writeText(mods / "fine/mod.toml", toml("fine"));
        writeFile(mods / "fine/overrides/A.TXD/0040_1_0.bin", fill(8, 1));
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"broken", true}, {"fine", true}}) == 1);
        CHECK(mm.status()[0].state == ModState::Skipped && mm.status()[0].message.find("invalid pack") == 0);
    }

    filesTests(root, pal);

    setLogSink(nullptr);
    fs::remove_all(root);
    printf("mod_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
