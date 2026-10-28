// Drakan FMV decoder: PS2 IPU "IDEC" intra macroblock streams (.VDB movies) -> YUV420 / RGBA8.
// Dependency-free C++20. See README.md and docs/reference/fmv.md.
//
// VLC tables (src/fmv_tables.inc) and the decoding procedure follow FFmpeg's `ipu` decoder
// (libavcodec/mpeg12dec.c, ipu_decode_frame; mpeg12data.c), (c) FFmpeg contributors, LGPL 2.1+.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fmv {

// ---------------------------------------------------------------------------------------------
// Bit reader (MSB first), bounds safe: reads past the end return zero bits and set overrun().
// ---------------------------------------------------------------------------------------------
class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* d, size_t n) : p_(d), nbits_(n * 8) {}
    uint32_t peek(int n) const; // 1..25 bits
    void skip(int n) { pos_ += (size_t)n; }
    uint32_t get(int n) {
        uint32_t v = peek(n);
        pos_ += (size_t)n;
        return v;
    }
    uint32_t get1() { return get(1); }
    size_t tell() const { return pos_; } // bit position
    void seek(size_t bitpos) { pos_ = bitpos; }
    size_t bits_left() const { return pos_ > nbits_ ? 0 : nbits_ - pos_; }
    bool overrun() const { return pos_ > nbits_; }
    void align() { pos_ = (pos_ + 7) & ~(size_t)7; }
    const uint8_t* data() const { return p_; }
    size_t size_bits() const { return nbits_; }

private:
    const uint8_t* p_ = nullptr;
    size_t nbits_ = 0, pos_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Stream flags (first byte of every frame == IPU_CTRL bits 16..23 as the EE player programs them).
// ---------------------------------------------------------------------------------------------
enum : uint8_t {
    kFlagDcPrecMask = 0x03, // intra_dc_precision (0=8,1=9,2=10 bit; 3 = 11 bit)
    kFlagDtd = 0x04,        // every MB carries a dct_type bit
    kFlagAltScan = 0x10,    // alternate scan
    kFlagVlcB15 = 0x20,     // intra_vlc_format: table B.15
    kFlagQScaleType = 0x40, // non-linear quantiser scale
    kFlagMpeg1 = 0x80,      // MPEG-1 style coefficient coding
};

// One decoded macroblock, 4:2:0. y is 16x16 row-major (already frame/field interleaved), cb/cr 8x8.
struct MacroblockYuv {
    uint8_t y[256];
    uint8_t cb[64];
    uint8_t cr[64];
};

// Macroblock-level IDEC engine (what the IPU IDEC command does). Reusable, not thread safe.
class Idec {
public:
    Idec();
    // Start a frame: resets DC predictors (128<<prec) and quantiser_scale_code (1). `flags` = frame byte 0.
    void begin_frame(uint8_t flags);
    // Decode the next macroblock from `br`. `first` = first MB of the frame (no address increment bit).
    // Returns false on a syntax error / bit overrun.
    bool decode_macroblock(BitReader& br, bool first, MacroblockYuv& out);
    // Replace the intra quantiser matrix (zigzag order, as loaded by SETIQ; default = MPEG default, EE 0x517000).
    void set_intra_matrix(const uint8_t zigzag[64]);
    int last_qscale() const { return qscale_; }

private:
    bool decode_block(BitReader& br, int n, int16_t* blk);
    uint8_t flags_ = 0;
    int prec_ = 0, qscale_ = 2, last_dc_[3] = {128, 128, 128};
    uint16_t matrix_[64]; // natural order
    const uint8_t* scan_ = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------------
struct YuvFrame {                   // planes padded to whole macroblocks
    int width = 0, height = 0;      // visible size
    int stride_y = 0, stride_c = 0; // = mbw*16, mbw*8
    int padded_h = 0;
    std::vector<uint8_t> y, cb, cr;
    void alloc(int w, int h);
};

// YCbCr (BT.601 limited range, chroma replicated as the IPU CSC does) -> RGBA8 (bytes R,G,B,A).
// alpha: 255 for a PC texture, 0x80 reproduces the IPU RGB32 alpha.
void yuv_to_rgba(const YuvFrame& f, uint8_t* dst, size_t dst_stride_bytes, uint8_t alpha = 255);
// Same conversion for 8-bit samples: used by the macroblock level adapter.
void yuv_mb_to_rgba(const MacroblockYuv& mb, uint32_t out[256], uint8_t alpha);

// Decode one frame buffer (flags byte ... trailing 00 00 01 b0). Reusable, not thread safe.
class FrameDecoder {
public:
    // strict=true additionally requires the frame to end with exactly the 32 bit end marker (like FFmpeg).
    bool decode(const uint8_t* data, size_t size, int w, int h, YuvFrame& out, bool strict = true);
    const std::string& error() const { return err_; }

private:
    Idec idec_;
    std::string err_;
};

struct FrameSpan {
    const uint8_t* data;
    size_t size;
};

// A movie: header + frame index over caller-owned memory (the pointers must outlive the Movie).
class Movie {
public:
    // SRSC resources: `hdr342` = body of resource 0x342 (u16 namelen, name, u32 w,h,frames, u32 offs[frames+2]),
    // `blob343` = body of resource 0x343 (concatenated frames).
    bool open_srsc(const uint8_t* hdr342, size_t hdr_size, const uint8_t* blob343, size_t blob_size);
    // Raw frame list.
    bool open_frames(int w, int h, std::vector<FrameSpan> frames);

    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    uint32_t frame_count() const { return (uint32_t)frames_.size(); }
    const std::string& name() const { return name_; }
    FrameSpan frame(uint32_t n) const { return frames_[n]; }

    // Frames are independent intra frames: any order is fine ("sequential" use is just fastest-friendly).
    bool decode_yuv(uint32_t n, YuvFrame& out);
    // dst: w*4 bytes per row at least; stride in bytes.
    bool decode_rgba(uint32_t n, uint8_t* dst, size_t stride_bytes, uint8_t alpha = 255);
    const std::string& error() const { return err_; }

private:
    uint32_t w_ = 0, h_ = 0;
    std::string name_;
    std::vector<FrameSpan> frames_;
    FrameDecoder dec_;
    YuvFrame tmp_;
    std::string err_;
};

} // namespace fmv
