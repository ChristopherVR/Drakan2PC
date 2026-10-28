// Drakan: register / DMA level IPU emulation for the game's own movie player (docs/reference/fmv.md).
// Models IPU_CMD/CTRL/BP/TOP (0x10002000..0x10002030), IN_FIFO writes (0x10007010), toIPU DMA (ch4) and fromIPU DMA
// (ch3). Supported commands: BCLR, IDEC (whole frame decoded at once with the dependency-free decoder in
// decomp/runtime/fmv), FDEC, SETIQ, SETVQ, SETTH. Everything else is logged and ignored.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace fmv {
class Idec;
}

class PS2Ipu {
public:
    PS2Ipu();
    ~PS2Ipu();

    void reset();
    uint32_t readReg(uint32_t address);
    void writeReg(uint32_t address, uint32_t value);
    // CPU stores to IPU_IN_FIFO (0x10007010): 16 bytes appended to the bitstream.
    void pushFifo(const uint8_t* data16);
    // toIPU DMA: append `bytes` bytes to the bitstream.
    void dmaToIpu(const uint8_t* src, uint32_t bytes);
    // fromIPU DMA: copy up to maxBytes of RGB32 macroblock data; returns bytes copied.
    uint32_t dmaFromIpu(uint8_t* dst, uint32_t maxBytes);
    uint32_t outputAvailable() const { return static_cast<uint32_t>(m_out.size() - m_outPos); }
    bool busy() const { return m_busy; }

    // Called after every IDEC with the frame's compressed bytes (from the IDEC start, up to the start code that ends
    // the frame) and its RGB32 output (`count` macroblocks of 16x16 pixels, raster order). Drakan HD movies use it
    // (drakan_fmv_hd.cpp). One observer, set from the host; nullptr removes it.
    using FrameObserver = void (*)(const uint8_t* bits, size_t bytes, const uint8_t* macroblocks, uint32_t count);
    static void setFrameObserver(FrameObserver observer);
    // Frames decoded by IDEC since start (any thread): "a movie is playing" while it advances (drakan_widescreen.cpp).
    static uint64_t decodedFrameCount();

private:
    void command(uint32_t cmd);
    uint32_t peek32();
    void idec(uint32_t cmd);
    void compact();

    std::vector<uint8_t> m_in;
    size_t m_bitPos = 0;
    uint32_t m_ctrl = 0;
    uint32_t m_cmdData = 0;
    bool m_busy = false;
    std::vector<uint8_t> m_out;
    size_t m_outPos = 0;
    std::unique_ptr<fmv::Idec> m_idec;
    uint32_t m_frames = 0;
};
