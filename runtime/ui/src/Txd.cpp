#include "ui/Txd.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace drakan_ui {

static uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
}
static int64_t pack(Rgba c) {
    return (int64_t)c.r | ((int64_t)c.g << 8) | ((int64_t)c.b << 16) | ((int64_t)c.a << 24);
}
static Rgba unpack(int64_t v) {
    return Rgba{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
}

bool TxdEdit::load(const std::vector<uint8_t>& blob) {
    b_ = blob;
    if (b_.size() < 0x40)
        return false;
    w_ = (int)rd32(b_, 0);
    h_ = (int)rd32(b_, 4);
    bpp_ = (int)rd32(b_, 0xc);
    if (w_ <= 0 || h_ <= 0 || w_ > 4096 || h_ > 4096)
        return false;
    const size_t n = (size_t)w_ * h_;
    if (bpp_ == 8) {
        if (b_.size() < 0x50 || rd32(b_, 0x40) != 1)
            return false;
        palOff_ = 0x50;
        pixOff_ = 0x50 + 1024;
        if (pixOff_ + n != b_.size())
            return false;
    } else if (bpp_ == 24 || bpp_ == 32) {
        pixOff_ = 0x40;
        if (pixOff_ + n * (bpp_ / 8) != b_.size())
            return false;
    } else
        return false;
    pending_.assign(n, -1);
    overflow_ = allocated_ = 0;
    return true;
}

Rgba TxdEdit::pal(int i) const {
    const uint8_t* p = &b_[palOff_ + (size_t)i * 4];
    return Rgba{p[2], p[1], p[0], p[3]};
}

Rgba TxdEdit::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return Rgba{};
    size_t i = sidx(x, y);
    if (pending_[i] >= 0)
        return unpack(pending_[i]);
    if (bpp_ == 8)
        return pal(b_[pixOff_ + i]);
    if (bpp_ == 24) {
        const uint8_t* p = &b_[pixOff_ + i * 3];
        return Rgba{p[0], p[1], p[2], 0x80};
    }
    const uint8_t* p = &b_[pixOff_ + i * 4];
    return Rgba{p[0], p[1], p[2], p[3]};
}

void TxdEdit::set(int x, int y, Rgba c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return;
    pending_[sidx(x, y)] = pack(c);
}

std::vector<uint8_t> TxdEdit::finish() {
    const size_t n = (size_t)w_ * h_;
    if (bpp_ == 24) {
        for (size_t i = 0; i < n; ++i)
            if (pending_[i] >= 0) {
                Rgba c = unpack(pending_[i]);
                uint8_t* p = &b_[pixOff_ + i * 3];
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
            }
    } else if (bpp_ == 32) {
        for (size_t i = 0; i < n; ++i)
            if (pending_[i] >= 0) {
                Rgba c = unpack(pending_[i]);
                uint8_t* p = &b_[pixOff_ + i * 4];
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
                p[3] = c.a;
            }
    } else {
        int usage[256] = {};
        std::map<int64_t, int> want; // new colour -> palette index (-1 = not yet resolved)
        for (size_t i = 0; i < n; ++i) {
            if (pending_[i] < 0)
                ++usage[b_[pixOff_ + i]];
            else
                want.emplace(pending_[i], -1);
        }
        Rgba palc[256];
        for (int i = 0; i < 256; ++i)
            palc[i] = pal(i);
        // 1. colours already present in a palette entry that some untouched pixel still uses
        for (auto& kv : want)
            for (int i = 0; i < 256; ++i)
                if (usage[i] > 0 && palc[i] == unpack(kv.first)) {
                    kv.second = i;
                    break;
                }
        // 2. free slots (no pixel uses them any more); never touch index 0 (the background / marker reference colour)
        std::vector<int> freeSlots;
        for (int i = 255; i >= 1; --i)
            if (usage[i] == 0)
                freeSlots.push_back(i);
        for (auto& kv : want) {
            if (kv.second >= 0)
                continue;
            Rgba c = unpack(kv.first);
            // an earlier assignment of the same colour?
            for (int i = 0; i < 256 && kv.second < 0; ++i)
                if (usage[i] == -1 && palc[i] == c)
                    kv.second = i;
            if (kv.second >= 0)
                continue;
            if (!freeSlots.empty()) {
                int s = freeSlots.back();
                freeSlots.pop_back();
                palc[s] = c;
                usage[s] = -1;
                kv.second = s;
                ++allocated_;
                uint8_t* p = &b_[palOff_ + (size_t)s * 4];
                p[0] = c.b;
                p[1] = c.g;
                p[2] = c.r;
                p[3] = c.a;
            } else {
                int best = 0;
                long bd = 1L << 30;
                for (int i = 0; i < 256; ++i) {
                    if (usage[i] == 0)
                        continue;
                    long d = 0;
                    const Rgba& o = palc[i];
                    d += (long)(o.r - c.r) * (o.r - c.r) + (long)(o.g - c.g) * (o.g - c.g) +
                         (long)(o.b - c.b) * (o.b - c.b) + 4L * (o.a - c.a) * (o.a - c.a);
                    if (d < bd) {
                        bd = d;
                        best = i;
                    }
                }
                kv.second = best;
                ++overflow_;
            }
        }
        for (size_t i = 0; i < n; ++i)
            if (pending_[i] >= 0)
                b_[pixOff_ + i] = (uint8_t)want[pending_[i]];
    }
    std::fill(pending_.begin(), pending_.end(), -1);
    return b_;
}

std::vector<Rgba> toRgba(const TxdEdit& t) {
    std::vector<Rgba> v((size_t)t.width() * t.height());
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x)
            v[(size_t)y * t.width() + x] = t.get(x, y);
    return v;
}

static uint32_t crcTable[256];
static bool crcInit = false;
static uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
    if (!crcInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t x = i;
            for (int k = 0; k < 8; ++k)
                x = (x & 1) ? 0xEDB88320u ^ (x >> 1) : x >> 1;
            crcTable[i] = x;
        }
        crcInit = true;
    }
    c = ~c;
    for (size_t i = 0; i < n; ++i)
        c = crcTable[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return ~c;
}
bool writePng(const std::string& path, int w, int h, const std::vector<Rgba>& px, bool alpha8) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            Rgba c = px[(size_t)y * w + x];
            raw.push_back(c.r);
            raw.push_back(c.g);
            raw.push_back(c.b);
            raw.push_back(alpha8 ? (uint8_t)std::min(255, c.a * 2) : c.a);
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t p = 0; p < raw.size();) {
        size_t n = std::min<size_t>(65535, raw.size() - p);
        z.push_back(p + n == raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 0xff));
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 0xff));
        z.push_back((uint8_t)((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + p, raw.begin() + p + n);
        p += n;
    }
    uint32_t ad = (b << 16) | a;
    for (int i = 3; i >= 0; --i)
        z.push_back((ad >> (i * 8)) & 0xff);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    auto chunk = [&](const char* tag, const std::vector<uint8_t>& d) {
        for (int i = 3; i >= 0; --i)
            out.push_back(((uint32_t)d.size() >> (i * 8)) & 0xff);
        std::vector<uint8_t> td(tag, tag + 4);
        td.insert(td.end(), d.begin(), d.end());
        out.insert(out.end(), td.begin(), td.end());
        uint32_t c = crc32(td.data(), td.size());
        for (int i = 3; i >= 0; --i)
            out.push_back((c >> (i * 8)) & 0xff);
    };
    std::vector<uint8_t> ihdr = {0, 0, (uint8_t)(w >> 8), (uint8_t)w, 0, 0, (uint8_t)(h >> 8), (uint8_t)h, 8, 6, 0,
                                 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

} // namespace drakan_ui
