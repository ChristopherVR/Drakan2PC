// Minimal PNG (stored deflate) / BMP writers for tests and screenshots.
#include "gsr/gsr.h"
#include "gs_texup.h"

#include <cstdio>
#include <cstring>

namespace gsr {
namespace {
uint32_t crcTable[256];
bool crcInit = false;
uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    if (!crcInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
        crcInit = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(0, td.data(), td.size()));
}
} // namespace

bool writePng(const char* path, const Image& img) {
    if (!img)
        return false;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(img.width * 3 + 1) * img.height);
    for (uint32_t y = 0; y < img.height; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < img.width; ++x) {
            const uint8_t* p = &img.rgba[((size_t)y * img.width + x) * 4];
            raw.push_back(p[0]);
            raw.push_back(p[1]);
            raw.push_back(p[2]);
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    while (pos < raw.size() || raw.empty()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF);
        z.push_back(n >> 8);
        z.push_back(~n & 0xFF);
        z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + (ptrdiff_t)pos, raw.begin() + (ptrdiff_t)(pos + n));
        pos += n;
        if (last)
            break;
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, img.width);
    be32(ihdr, img.height);
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

bool readImage(const char* path, Image& img) {
    std::vector<uint32_t> px;
    uint32_t w = 0, h = 0;
    if (!path || !texup::readImageFile(path, px, w, h) || !w || !h || px.size() < size_t(w) * h)
        return false;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const uint32_t v = px[i];
        img.rgba[i * 4 + 0] = uint8_t(v);
        img.rgba[i * 4 + 1] = uint8_t(v >> 8);
        img.rgba[i * 4 + 2] = uint8_t(v >> 16);
        const uint32_t a = v >> 24; // GS units: 0x80 = opaque
        img.rgba[i * 4 + 3] = uint8_t(a >= 0x80 ? 255 : a * 2);
    }
    return true;
}

bool writeBmp(const char* path, const Image& img) {
    if (!img)
        return false;
    const uint32_t rowBytes = (img.width * 3 + 3) & ~3u;
    const uint32_t dataSize = rowBytes * img.height;
    uint8_t hdr[54] = {'B', 'M'};
    auto w32 = [&](int off, uint32_t v) { std::memcpy(hdr + off, &v, 4); };
    w32(2, 54 + dataSize);
    w32(10, 54);
    w32(14, 40);
    w32(18, img.width);
    w32(22, img.height);
    hdr[26] = 1;
    hdr[28] = 24;
    w32(34, dataSize);
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = (int)img.height - 1; y >= 0; --y) {
        for (uint32_t x = 0; x < img.width; ++x) {
            const uint8_t* p = &img.rgba[((size_t)y * img.width + x) * 4];
            row[x * 3 + 0] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
    return true;
}
} // namespace gsr
