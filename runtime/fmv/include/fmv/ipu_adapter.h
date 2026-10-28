// IPU register-level adapter sketch (header only).
//
// Integration option B: the recompiled EE movie player (0x1767a0 and friends) keeps running unchanged and
// talks to an emulated IPU. Only the commands the game uses are needed: BCLR, FDEC, IDEC, SETIQ, SETVQ, SETTH.
//
// Data flow on the real machine (see docs/reference/fmv.md section 6):
//   toIPU DMA (ch4, 0x1000b400)  : VDB frame bytes  -> IPU_IN_FIFO (0x10007010)  == IpuEmu::feed()
//   IPU_CMD (0x10002000) writes   : FDEC 32 / FDEC 0 / FDEC 8 / IDEC / SETIQ / ...   == IpuEmu::write_cmd()
//   IPU_CTRL (0x10002010)         : bits 16..23 = frame flags byte ((flags & 0xfb) << 16), bit 31 = BUSY
//   fromIPU DMA (ch3, 0x1000b000) : RGB32 macroblocks, 256 pixels * 4 bytes = 64 qwords per MB, MB raster order,
//                                   pixels inside an MB are row-major 16x16, byte order R,G,B,A (A = 0x80)
//   == IpuEmu::drain_out() hands out those qwords.
//
// IDEC command word (0x10010000 in the game; 0x11010000 if the DTD flag is set):
//   bits 27..0 : command specific; bit 24 = DTD (dct_type bit present), bit 27 (OFM) = 0 -> RGB32 output,
//                bit 26 (DTE) = 0 dither off, bits 20..16 = QSC (initial quantiser_scale_code = 1), bits 5..0 = FB
//                (bits to skip first).
// IDEC runs until the whole picture (w*h/256 macroblocks) is done; the hardware knows the MB count from the
// DMA'd size. We do not: the game never tells the IPU the picture size, it simply DMAs out MB after MB and
// stops after w*h*4 bytes (fromIPU chunks of up to 0xfc00 qwc). So the emulation must be MB-lazy: decode a
// macroblock only when fromIPU data is requested (drain_out(), one MB = 64 qwords), and keep IPU_CTRL.BUSY set until
// the frame's last MB has been drained (the player polls sceIpuSync -> IPU_CTRL bit 31).
//
// Command semantics needed by the game (everything else can be left unimplemented / traps):
//   BCLR  (0x0000_00xx)      : in-FIFO bit pointer := xx (0 or bp&0x7f); discards FIFO content when bp==0
//   FDEC  (0x4000_00nn)      : advance bit pointer by nn (32 -> skips the 00 00 01 b0 end marker of the previous frame)
//   FDEC 0 / FDEC 8          : the EE then reads IPU_CMD / IPU_TOP to peek the next bits; FDEC 8 consumes the flags
//   byte IDEC                     : see above SETIQ (0x50000000/58..)  : consumes 64 bytes (8 qwords) from the FIFO as
//   the new intra / non-intra matrix (zigzag order)
//                              -> Idec::set_intra_matrix (the game loads the default one from EE 0x517000)
//   SETVQ (0x60000000)       : consumes 32 bytes (VQCLUT), unused for IDEC
//   SETTH (0x90000000)       : thresholds TH0=TH1=0 -> every pixel gets alpha 0x80. (TH0 <= Y < TH1 would give 0x40, Y
//   < TH0 0.)
//
// The class below is a documented SKETCH of that surface, with the macroblock engine fully functional.
#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include "fmv/fmv.h"

namespace fmv {

// Produce one macroblock of IPU RGB32 output from the bitstream at `br` (the core of IDEC). 256 words,
// little endian R | G<<8 | B<<16 | A<<24, row-major 16x16, A = 0x80 as with SETTH 0,0. Returns false on bad data.
// `idec` must have had begin_frame(flags) called; `first` = first MB of the frame.
inline bool ipu_idec_macroblock_rgb32(Idec& idec, BitReader& br, bool first, uint32_t out[256]) {
    MacroblockYuv mb;
    if (!idec.decode_macroblock(br, first, mb))
        return false;
    yuv_mb_to_rgba(mb, out, 0x80);
    return true;
}

class IpuEmu {
public:
    // toIPU DMA: append bytes to the input bit FIFO.
    void feed(const uint8_t* data, size_t n) { in_.insert(in_.end(), data, data + n); }

    // IPU_CMD write. Returns false for unsupported commands.
    bool write_cmd(uint32_t cmd) {
        switch (cmd >> 28) {
            case 0x0:
                bitpos_ = (cmd & 0x7f);
                if (!(cmd & 0x7f)) {
                    in_.clear();
                }
                return true; // BCLR (sketch)
            case 0x4:
                bitpos_ += cmd & 0x3f;
                return true; // FDEC
            case 0x5:
                return true; // SETIQ (TODO: parse 64 bytes)
            case 0x6:
            case 0x9:
                return true; // SETVQ / SETTH
            case 0x1:
                begin_idec(cmd);
                return true; // IDEC
        }
        return false;
    }
    // IPU_CTRL.BUSY
    bool busy() const { return idec_active_; }

    // fromIPU DMA: produce up to `qwords` quadwords (multiple of 64 expected = whole MBs).
    // Returns qwords produced.
    size_t drain_out(uint32_t* dst, size_t qwords) {
        size_t done = 0;
        while (idec_active_ && done + 64 <= qwords) {
            BitReader br(in_.data(), in_.size());
            br.seek(bitpos_);
            if (!ipu_idec_macroblock_rgb32(idec_, br, mb_index_ == 0, dst + done * 4)) {
                idec_active_ = false;
                break;
            }
            bitpos_ = br.tell();
            ++mb_index_;
            done += 64;
            if (mb_index_ >= mb_total_)
                idec_active_ = false;
        }
        return done;
    }
    // Picture size is not known to the IPU; the integration layer provides it from the 0x342 header (or the
    // size of the fromIPU DMA chain the game programs: w*h*4 bytes).
    void set_picture_size(int w, int h) { mb_total_ = ((w + 15) / 16) * ((h + 15) / 16); }
    // The frame flags byte is peeked by FDEC 8 and fed back through IPU_CTRL by the game.
    void set_ctrl(uint32_t ctrl) { flags_ = (uint8_t)(ctrl >> 16); }

private:
    void begin_idec(uint32_t cmd) {
        (void)cmd;
        idec_.begin_frame(flags_);
        mb_index_ = 0;
        idec_active_ = true;
    }
    std::vector<uint8_t> in_;
    size_t bitpos_ = 0;
    Idec idec_;
    uint8_t flags_ = 0;
    bool idec_active_ = false;
    uint32_t mb_index_ = 0, mb_total_ = 0;
};

} // namespace fmv
