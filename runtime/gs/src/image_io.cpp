// Minimal PNG (stored deflate) / BMP writers for tests and screenshots.
#include "gsr/gsr.h"
#include "gs_texup.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace gsr {
namespace {
constexpr auto makeCrcTables() {
    std::array<std::array<uint32_t, 256>, 4> tables{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        tables[0][i] = c;
    }
    for (size_t row = 1; row < tables.size(); ++row)
        for (size_t i = 0; i < 256; ++i) {
            const uint32_t c = tables[row - 1][i];
            tables[row][i] = tables[0][c & 255] ^ (c >> 8);
        }
    return tables;
}
uint32_t crc32(const uint8_t* p, size_t n) {
    static constexpr auto tables = makeCrcTables();
    uint32_t crc = ~0u;
    while (n >= 4) {
        crc = tables[3][(crc ^ p[0]) & 255] ^ tables[2][((crc >> 8) ^ p[1]) & 255] ^
              tables[1][((crc >> 16) ^ p[2]) & 255] ^ tables[0][(crc >> 24) ^ p[3]];
        p += 4;
        n -= 4;
    }
    while (n--)
        crc = tables[0][(crc ^ *p++) & 255] ^ (crc >> 8);
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
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(out.data() + start, out.size() - start));
}
} // namespace

bool writePng(const char* path, const Image& img) {
    if (!img)
        return false;
    const size_t stride = size_t(img.width) * 3 + 1;
    std::vector<uint8_t> raw(stride * img.height);
    for (uint32_t y = 0; y < img.height; ++y) {
        auto* dst = raw.data() + y * stride + 1; // filter byte is zero
        const auto* src = img.rgba.data() + size_t(y) * img.width * 4;
        for (uint32_t x = 0; x < img.width; ++x, dst += 3, src += 4)
            std::memcpy(dst, src, 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    z.reserve(6 + raw.size() + 5 * ((raw.size() + 65534) / 65535));
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    // Up to 5552 input bytes fit in the 32-bit Adler accumulators between reductions.
    for (size_t begin = 0; begin < raw.size();) {
        const size_t end = std::min(begin + 5552, raw.size());
        while (begin < end) {
            a += raw[begin++];
            b += a;
        }
        a %= 65521;
        b %= 65521;
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
    out.reserve(z.size() + 57); // signature, IHDR, IDAT and IEND overhead
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
