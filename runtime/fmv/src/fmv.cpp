// Drakan FMV decoder (PS2 IPU IDEC intra macroblock streams). See include/fmv/fmv.h.
//
// Decoding procedure and VLC tables derived from FFmpeg libavcodec/mpeg12dec.c (ipu_decode_frame,
// mpeg2_decode_block_intra, ff_mpeg1_decode_block_intra) and mpeg12data.c.
// Copyright (c) FFmpeg contributors, LGPL 2.1 or later. This file is therefore also LGPL 2.1+.
#include "fmv/fmv.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(_MSC_VER)
#include <stdlib.h>
#endif

namespace fmv {

#include "fmv_tables.inc"

// ------------------------------------------------------------------------------------------------
// constant tables
// ------------------------------------------------------------------------------------------------
static const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const uint8_t kAltScan[64] = {0,  8,  16, 24, 1, 9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49,
                                     41, 33, 26, 18, 3, 11, 4,  12, 19, 27, 34, 42, 50, 58, 35, 43,
                                     51, 59, 20, 28, 5, 13, 6,  14, 21, 29, 36, 44, 52, 60, 37, 45,
                                     53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};
// MPEG default intra quantiser matrix, zigzag order (== EE 0x517000: 08 10 10 13 10 13 16 16 ...).
static const uint8_t kDefaultIntraZz[64] = {8,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
                                            27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
                                            29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
                                            35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83};
static const uint8_t kQScaleNonLinear[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12, 14, 16, 18, 20,  22,
                                             24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};

// ------------------------------------------------------------------------------------------------
// VLC lookup tables (built once)
// ------------------------------------------------------------------------------------------------
struct AcEntry {
    uint8_t len;
    uint8_t run;
    int16_t level;
}; // len==0 invalid; run==255 EOB; run==254 ESC
struct DcEntry {
    uint8_t len;
    int8_t size;
};

struct Tables {
    AcEntry ac[2][1 << 16]; // [0]=B.14, [1]=B.15, indexed by next 16 bits (sign bit is not part of the code)
    DcEntry dc[2][1 << 10]; // [0]=luma, [1]=chroma, indexed by next 10 bits
    float c[8][8];          // IDCT basis: c[u][x] = 0.5*c(u)*cos((2x+1)u*pi/16)
    Tables() {
        std::memset(ac, 0, sizeof ac);
        std::memset(dc, 0, sizeof dc);
        for (int t = 0; t < 2; ++t) {
            const int* code = t ? kVlc2Code : kVlc1Code;
            const int* bits = t ? kVlc2Bits : kVlc1Bits;
            for (int i = 0; i < 113; ++i) {
                AcEntry e;
                e.len = (uint8_t)bits[i];
                if (i < 111) {
                    e.run = (uint8_t)kRun[i];
                    e.level = (int16_t)kLevel[i];
                } else if (i == 111) {
                    e.run = 254;
                    e.level = 0;
                } else {
                    e.run = 255;
                    e.level = 0;
                }
                int sh = 16 - bits[i], base = code[i] << sh;
                for (int k = 0; k < (1 << sh); ++k)
                    ac[t][base + k] = e;
            }
        }
        for (int t = 0; t < 2; ++t)
            for (int i = 0; i < 12; ++i) {
                int b = t ? kDcChrBits[i] : kDcLumBits[i], cd = t ? kDcChrCode[i] : kDcLumCode[i];
                int sh = 10 - b;
                for (int k = 0; k < (1 << sh); ++k)
                    dc[t][(cd << sh) + k] = DcEntry{(uint8_t)b, (int8_t)i};
            }
        const double pi = 3.14159265358979323846;
        for (int u = 0; u < 8; ++u)
            for (int x = 0; x < 8; ++x)
                c[u][x] = (float)(0.5 * (u ? 1.0 : std::sqrt(0.5)) * std::cos((2 * x + 1) * u * pi / 16.0));
    }
};
static const Tables& T() {
    static const Tables t;
    return t;
}

// ------------------------------------------------------------------------------------------------
// BitReader
// ------------------------------------------------------------------------------------------------
static inline uint64_t load_be64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
#if defined(_MSC_VER)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v); // assumes little endian host
#endif
}

uint32_t BitReader::peek(int n) const {
    size_t byte = pos_ >> 3;
    uint64_t v;
    size_t total = nbits_ >> 3;
    if (byte + 8 <= total) {
        v = load_be64(p_ + byte);
    } else {
        v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | (byte + i < total ? p_[byte + i] : 0);
    }
    v <<= (pos_ & 7);
    return (uint32_t)(v >> (64 - n));
}

// ------------------------------------------------------------------------------------------------
// IDCT (float, separable, round-to-nearest; error vs the ideal real IDCT is < 1 LSB)
// ------------------------------------------------------------------------------------------------
static inline void idct_put(const int16_t* in, uint8_t* dst, int stride) {
    const Tables& t = T();
    int nzrows = 0, acmask = 0;
    for (int v = 0; v < 8; ++v) {
        int any = 0;
        for (int u = 0; u < 8; ++u)
            any |= in[v * 8 + u];
        if (any)
            nzrows |= 1 << v;
        if (v == 0) {
            acmask = in[1] | in[2] | in[3] | in[4] | in[5] | in[6] | in[7];
        }
    }
    if ((nzrows & ~1) == 0 && acmask == 0) { // DC only
        int p = std::clamp((in[0] + 4) >> 3, 0, 255);
        for (int y = 0; y < 8; ++y)
            std::memset(dst + y * stride, p, 8);
        return;
    }
    // pass 1 (rows): tmp[v][x] = sum_u in[v][u] * c[u][x]   (sparse in u)
    alignas(16) float tmp[8][8];
    for (int v = 0; v < 8; ++v) {
        if (!(nzrows >> v & 1))
            continue;
        float r[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int u = 0; u < 8; ++u) {
            int cf = in[v * 8 + u];
            if (!cf)
                continue;
            const float fc = (float)cf;
            for (int x = 0; x < 8; ++x)
                r[x] += fc * t.c[u][x];
        }
        for (int x = 0; x < 8; ++x)
            tmp[v][x] = r[x];
    }
    // pass 2 (columns): out[y][x] = sum_v c[v][y] * tmp[v][x]
    alignas(16) float acc[8][8] = {};
    for (int v = 0; v < 8; ++v) {
        if (!(nzrows >> v & 1))
            continue;
        for (int y = 0; y < 8; ++y) {
            const float cv = t.c[v][y];
            for (int x = 0; x < 8; ++x)
                acc[y][x] += cv * tmp[v][x];
        }
    }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int p = (int)(acc[y][x] + 1024.5f) - 1024; // round half up (values stay far inside +-1024)
            dst[y * stride + x] = (uint8_t)(p < 0 ? 0 : p > 255 ? 255 : p);
        }
}

// ------------------------------------------------------------------------------------------------
// Idec
// ------------------------------------------------------------------------------------------------
Idec::Idec() {
    set_intra_matrix(kDefaultIntraZz);
    begin_frame(0);
}

void Idec::set_intra_matrix(const uint8_t zz[64]) {
    for (int i = 0; i < 64; ++i)
        matrix_[kZigzag[i]] = zz[i]; // SETIQ matrices are always in zigzag order
}

void Idec::begin_frame(uint8_t flags) {
    flags_ = flags;
    prec_ = flags & 3;
    scan_ = (flags & kFlagAltScan) ? kAltScan : kZigzag;
    last_dc_[0] = last_dc_[1] = last_dc_[2] = 128 << prec_;
    qscale_ = (flags & kFlagQScaleType) ? kQScaleNonLinear[1] : 2; // quantiser_scale_code 1
}

bool Idec::decode_block(BitReader& br, int n, int16_t* blk) {
    const Tables& t = T();
    const bool chroma = n >= 4;
    const int comp = chroma ? n - 3 : 0;
    // DC
    DcEntry de = t.dc[chroma][br.peek(10)];
    if (!de.len)
        return false;
    br.skip(de.len);
    int diff = 0;
    if (de.size) {
        int v = (int)br.get(de.size);
        diff = (v < (1 << (de.size - 1))) ? v - (1 << de.size) + 1 : v;
    }
    int dc = last_dc_[comp] + diff;
    last_dc_[comp] = dc;
    const bool mp1 = (flags_ & kFlagMpeg1) != 0;
    int mismatch;
    if (mp1) {
        blk[0] = (int16_t)(dc * matrix_[0]);
        mismatch = 0;
    } else {
        int d = std::clamp(dc * (1 << (3 - prec_)), -2048, 2047); // saturation
        blk[0] = (int16_t)d;
        mismatch = d ^ 1;
    }
    const AcEntry* tab = t.ac[((flags_ & kFlagVlcB15) && !mp1) ? 1 : 0];
    int i = 0;
    const int qs = qscale_;
    for (;;) {
        const AcEntry e = tab[br.peek(16)];
        if (!e.len)
            return false;
        br.skip(e.len);
        if (e.run == 255)
            break; // EOB
        int level;
        if (e.run == 254) { // escape: 6 bit run, then 12 bit (MPEG-2) / 8 or 16 bit (MPEG-1) level
            i += (int)br.get(6) + 1;
            if (mp1) {
                level = (int)br.get(8);
                if (level >= 128)
                    level -= 256;
                if (level == -128)
                    level = (int)br.get(8) - 256;
                else if (level == 0)
                    level = (int)br.get(8);
            } else {
                level = (int)br.get(12);
                if (level >= 2048)
                    level -= 4096;
                if (level == 0 || level == -2048)
                    return false; // forbidden by MPEG-2
            }
            if (i > 63)
                return false;
            int j = scan_[i];
            int a = level < 0 ? -level : level;
            a = (a * qs * matrix_[j]) >> 4;
            if (mp1)
                a = (a - 1) | 1;
            level = std::clamp(level < 0 ? -a : a, -2048, 2047);
            mismatch ^= level;
            blk[j] = (int16_t)level;
        } else {
            i += e.run + 1;
            if (i > 63)
                return false;
            int j = scan_[i];
            int a = (e.level * qs * matrix_[j]) >> 4;
            if (mp1)
                a = (a - 1) | 1;
            if (br.get1())
                a = -a;
            a = std::clamp(a, -2048, 2047);
            mismatch ^= a;
            blk[j] = (int16_t)a;
        }
        if (br.overrun())
            return false;
    }
    if (!mp1)
        blk[63] = (int16_t)(blk[63] ^ (mismatch & 1)); // MPEG-2 mismatch control
    return !br.overrun();
}

bool Idec::decode_macroblock(BitReader& br, bool first, MacroblockYuv& out) {
    if (!first && !br.get1())
        return false; // macroblock_address_increment == 1
    bool quant = false;
    if (!br.get1()) { // '1' = intra, '01' = intra + quant
        if (!br.get1())
            return false;
        quant = true;
    }
    int dct_type = 0;
    if (flags_ & kFlagDtd)
        dct_type = (int)br.get1();
    if (quant) {
        int code = (int)br.get(5);
        qscale_ = (flags_ & kFlagQScaleType) ? kQScaleNonLinear[code] : code << 1;
    }
    alignas(16) int16_t blk[6][64];
    std::memset(blk, 0, sizeof blk);
    for (int n = 0; n < 6; ++n)
        if (!decode_block(br, n, blk[n]))
            return false;
    if (dct_type) { // field DCT: blocks 0/1 = even lines, 2/3 = odd lines
        idct_put(blk[0], out.y, 32);
        idct_put(blk[1], out.y + 8, 32);
        idct_put(blk[2], out.y + 16, 32);
        idct_put(blk[3], out.y + 16 + 8, 32);
    } else {
        idct_put(blk[0], out.y, 16);
        idct_put(blk[1], out.y + 8, 16);
        idct_put(blk[2], out.y + 8 * 16, 16);
        idct_put(blk[3], out.y + 8 * 16 + 8, 16);
    }
    idct_put(blk[4], out.cb, 8);
    idct_put(blk[5], out.cr, 8);
    return true;
}

// ------------------------------------------------------------------------------------------------
// YUV -> RGB (BT.601 limited range, 8.8 fixed point, nearest chroma)
// ------------------------------------------------------------------------------------------------
static inline uint32_t clamp8(int v) {
    return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
static inline uint32_t csc(int y, int cb, int cr, uint32_t a) {
    int yy = 298 * (y - 16) + 128;
    cb -= 128;
    cr -= 128;
    uint32_t r = clamp8((yy + 409 * cr) >> 8);
    uint32_t g = clamp8((yy - 100 * cb - 208 * cr) >> 8);
    uint32_t b = clamp8((yy + 516 * cb) >> 8);
    return r | g << 8 | b << 16 | a << 24;
}

void yuv_mb_to_rgba(const MacroblockYuv& mb, uint32_t out[256], uint8_t alpha) {
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            out[y * 16 + x] =
                csc(mb.y[y * 16 + x], mb.cb[(y >> 1) * 8 + (x >> 1)], mb.cr[(y >> 1) * 8 + (x >> 1)], alpha);
}

void yuv_to_rgba(const YuvFrame& f, uint8_t* dst, size_t stride, uint8_t alpha) {
    for (int y = 0; y < f.height; ++y) {
        const uint8_t* py = &f.y[(size_t)y * f.stride_y];
        const uint8_t* pb = &f.cb[(size_t)(y >> 1) * f.stride_c];
        const uint8_t* pr = &f.cr[(size_t)(y >> 1) * f.stride_c];
        uint8_t* o = dst + y * stride;
        for (int x = 0; x < f.width; ++x) {
            uint32_t v = csc(py[x], pb[x >> 1], pr[x >> 1], alpha);
            std::memcpy(o + 4 * (size_t)x, &v, 4); // little endian: bytes R,G,B,A
        }
    }
}

void YuvFrame::alloc(int w, int h) {
    int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    width = w;
    height = h;
    stride_y = mbw * 16;
    stride_c = mbw * 8;
    padded_h = mbh * 16;
    y.assign((size_t)stride_y * padded_h, 0);
    cb.assign((size_t)stride_c * (padded_h / 2), 128);
    cr.assign((size_t)stride_c * (padded_h / 2), 128);
}

// ------------------------------------------------------------------------------------------------
// FrameDecoder
// ------------------------------------------------------------------------------------------------
bool FrameDecoder::decode(const uint8_t* data, size_t size, int w, int h, YuvFrame& out, bool strict) {
    err_.clear();
    if (w <= 0 || h <= 0 || size < 2) {
        err_ = "bad arguments";
        return false;
    }
    if (out.width != w || out.height != h || out.y.empty())
        out.alloc(w, h);
    BitReader br(data, size);
    uint8_t flags = (uint8_t)br.get(8);
    idec_.begin_frame(flags);
    const int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    MacroblockYuv mb;
    for (int my = 0; my < mbh; ++my)
        for (int mx = 0; mx < mbw; ++mx) {
            if (!idec_.decode_macroblock(br, mx == 0 && my == 0, mb)) {
                err_ = "syntax error at macroblock " + std::to_string(my * mbw + mx);
                return false;
            }
            uint8_t* py = &out.y[(size_t)my * 16 * out.stride_y + mx * 16];
            for (int r = 0; r < 16; ++r)
                std::memcpy(py + (size_t)r * out.stride_y, mb.y + r * 16, 16);
            uint8_t* pb = &out.cb[(size_t)my * 8 * out.stride_c + mx * 8];
            uint8_t* pr = &out.cr[(size_t)my * 8 * out.stride_c + mx * 8];
            for (int r = 0; r < 8; ++r) {
                std::memcpy(pb + (size_t)r * out.stride_c, mb.cb + r * 8, 8);
                std::memcpy(pr + (size_t)r * out.stride_c, mb.cr + r * 8, 8);
            }
        }
    if (strict) {
        br.align();
        if (br.bits_left() != 32) {
            err_ = "trailing bits != 32";
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Movie
// ------------------------------------------------------------------------------------------------
static inline uint32_t rd32(const uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

bool Movie::open_srsc(const uint8_t* h, size_t hs, const uint8_t* blob, size_t bs) {
    err_.clear();
    frames_.clear();
    if (hs < 14) {
        err_ = "header too small";
        return false;
    }
    size_t nl = h[0] | h[1] << 8;
    if (2 + nl + 12 > hs) {
        err_ = "header too small";
        return false;
    }
    size_t len = 0;
    while (len < nl && h[2 + len])
        ++len;
    name_.assign((const char*)h + 2, len);
    size_t k = 2 + nl;
    w_ = rd32(h + k);
    h_ = rd32(h + k + 4);
    uint32_t n = rd32(h + k + 8);
    k += 12;
    if (n > (1u << 24) || k + 4 * ((size_t)n + 2) > hs) {
        err_ = "bad frame count";
        return false;
    }
    uint32_t prev = 0;
    for (uint32_t i = 0; i <= n; ++i) {
        uint32_t a = rd32(h + k + 4 * (size_t)i), b = rd32(h + k + 4 * ((size_t)i + 1));
        if (a < prev || b < a || b > bs) {
            err_ = "bad frame offsets";
            return false;
        }
        prev = a;
        if (i < n)
            frames_.push_back({blob + a, (size_t)(b - a)});
    }
    return true;
}

bool Movie::open_frames(int w, int h, std::vector<FrameSpan> fr) {
    w_ = (uint32_t)w;
    h_ = (uint32_t)h;
    frames_ = std::move(fr);
    name_.clear();
    return true;
}

bool Movie::decode_yuv(uint32_t n, YuvFrame& out) {
    if (n >= frames_.size()) {
        err_ = "frame out of range";
        return false;
    }
    bool ok = dec_.decode(frames_[n].data, frames_[n].size, (int)w_, (int)h_, out, true);
    if (!ok)
        err_ = dec_.error();
    return ok;
}

bool Movie::decode_rgba(uint32_t n, uint8_t* dst, size_t stride, uint8_t alpha) {
    if (!decode_yuv(n, tmp_))
        return false;
    yuv_to_rgba(tmp_, dst, stride, alpha);
    return true;
}

} // namespace fmv
