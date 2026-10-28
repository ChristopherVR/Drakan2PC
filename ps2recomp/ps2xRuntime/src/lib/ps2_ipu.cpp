#include "runtime/ps2_ipu.h"

#include "fmv/fmv.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
std::atomic<PS2Ipu::FrameObserver> g_frameObserver{nullptr};
std::atomic<uint64_t> g_decodedFrames{0};

bool traceIpu() {
    static const bool on = std::getenv("PS2X_TRACE_IPU") != nullptr;
    return on;
}
} // namespace

void PS2Ipu::setFrameObserver(FrameObserver observer) {
    g_frameObserver.store(observer, std::memory_order_release);
}

uint64_t PS2Ipu::decodedFrameCount() {
    return g_decodedFrames.load(std::memory_order_relaxed);
}

PS2Ipu::PS2Ipu() : m_idec(std::make_unique<fmv::Idec>()) {}
PS2Ipu::~PS2Ipu() = default;

void PS2Ipu::reset() {
    m_in.clear();
    m_bitPos = 0;
    m_ctrl = 0;
    m_cmdData = 0;
    m_busy = false;
    m_out.clear();
    m_outPos = 0;
}

uint32_t PS2Ipu::peek32() {
    const size_t totalBits = m_in.size() * 8;
    if (m_bitPos >= totalBits)
        return 0;
    fmv::BitReader br(m_in.data(), m_in.size());
    br.seek(m_bitPos);
    // BitReader::peek handles 1..25 bits: compose 32 bits from two reads
    const uint32_t hi = br.peek(16);
    br.skip(16);
    const uint32_t lo = br.peek(16);
    return (hi << 16) | lo;
}

uint32_t PS2Ipu::readReg(uint32_t address) {
    switch (address) {
        case 0x10002000: // IPU_CMD: result of the last FDEC (next 32 bits), bit 31 = busy would clash; data only
            return m_cmdData;
        case 0x10002010: // IPU_CTRL
            return (m_ctrl & ~(1u << 31)) | (m_busy ? (1u << 31) : 0u);
        case 0x10002020: // IPU_BP: bit position, FP, IFC
            return static_cast<uint32_t>(m_bitPos & 0x7Fu);
        case 0x10002030: // IPU_TOP
            return peek32();
        default:
            return 0;
    }
}

void PS2Ipu::writeReg(uint32_t address, uint32_t value) {
    switch (address) {
        case 0x10002000:
            command(value);
            break;
        case 0x10002010:
            if (value & (1u << 30)) {
                reset();
            }
            m_ctrl = value & ~(1u << 30);
            break;
        default:
            break;
    }
}

void PS2Ipu::pushFifo(const uint8_t* data16) {
    if (traceIpu()) {
        static int n = 0;
        if (n++ < 40) {
            std::fprintf(stderr, "[ipu] FIFO push:");
            for (int i = 0; i < 16; ++i)
                std::fprintf(stderr, " %02x", data16[i]);
            std::fprintf(stderr, "%c", 10);
        }
    }
    m_in.insert(m_in.end(), data16, data16 + 16);
}

void PS2Ipu::dmaToIpu(const uint8_t* src, uint32_t bytes) {
    compact();
    m_in.insert(m_in.end(), src, src + bytes);
    if (traceIpu())
        std::fprintf(
            stderr,
            "[ipu] toIPU %u bytes (buffer %zu bytes, bitpos %zu) first: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            bytes, m_in.size(), m_bitPos, bytes > 0 ? src[0] : 0, bytes > 1 ? src[1] : 0, bytes > 2 ? src[2] : 0,
            bytes > 3 ? src[3] : 0, bytes > 4 ? src[4] : 0, bytes > 5 ? src[5] : 0, bytes > 6 ? src[6] : 0,
            bytes > 7 ? src[7] : 0);
}

void PS2Ipu::compact() {
    const size_t consumed = std::min(m_bitPos / 8, m_in.size());
    if (consumed > (1u << 20)) {
        m_in.erase(m_in.begin(), m_in.begin() + static_cast<std::ptrdiff_t>(consumed));
        m_bitPos -= consumed * 8;
    }
}

uint32_t PS2Ipu::dmaFromIpu(uint8_t* dst, uint32_t maxBytes) {
    const uint32_t n = std::min<uint32_t>(maxBytes, outputAvailable());
    if (n) {
        std::memcpy(dst, m_out.data() + m_outPos, n);
        m_outPos += n;
    }
    if (m_outPos >= m_out.size() && m_busy) {
        m_busy = false; // the frame's last macroblock left the output FIFO
        m_out.clear();
        m_outPos = 0;
    }
    if (traceIpu())
        std::fprintf(stderr, "[ipu] fromIPU %u bytes (remaining %u, busy %d)\n", n, outputAvailable(), m_busy ? 1 : 0);
    return n;
}

void PS2Ipu::command(uint32_t cmd) {
    const uint32_t op = cmd >> 28;
    if (traceIpu())
        std::fprintf(stderr, "[ipu] cmd %08x (op %u) bitpos %zu top %08x ctrl %08x\n", cmd, op, m_bitPos, peek32(),
                     m_ctrl);
    switch (op) {
        case 0x0: // BCLR
            m_bitPos = cmd & 0x7Fu;
            if ((cmd & 0x7Fu) == 0u) {
                m_in.clear();
                m_bitPos = 0;
            }
            break;
        case 0x1:
            idec(cmd);
            break;
        case 0x4: // FDEC
            m_bitPos += cmd & 0x3Fu;
            m_cmdData = peek32();
            break;
        case 0x5: // SETIQ
        {
            m_bitPos = (m_bitPos + 7) & ~static_cast<size_t>(7);
            const size_t byte = m_bitPos / 8;
            if (byte + 64 <= m_in.size() && traceIpu()) {
                std::fprintf(stderr, "[ipu] SETIQ %s matrix:", (cmd & (1u << 27)) ? "non-intra" : "intra");
                for (int i = 0; i < 64; ++i)
                    std::fprintf(stderr, " %u", m_in[byte + i]);
                std::fprintf(stderr, "%c", 10);
            }
            if (byte + 64 <= m_in.size()) {
                if ((cmd & (1u << 27)) == 0)
                    m_idec->set_intra_matrix(&m_in[byte]);
                m_bitPos += 64 * 8;
            }
            break;
        }
        case 0x6: // SETVQ: 32 bytes of CLUT
            m_bitPos = (m_bitPos + 7) & ~static_cast<size_t>(7);
            m_bitPos += 32 * 8;
            break;
        case 0x9: // SETTH
            break;
        default:
            std::fprintf(stderr, "[ipu] unsupported command %08x\n", cmd);
            break;
    }
}

void PS2Ipu::idec(uint32_t cmd) {
    uint8_t flags = static_cast<uint8_t>((m_ctrl >> 16) & 0xFFu);
    if (cmd & (1u << 24))
        flags |= fmv::kFlagDtd;
    const bool rgb16 = (cmd & (1u << 27)) != 0;
    if (rgb16)
        std::fprintf(stderr, "[ipu] IDEC with RGB16 output is not implemented\n");

    fmv::BitReader br(m_in.data(), m_in.size());
    const size_t startBit = m_bitPos + (cmd & 0x3Fu);
    br.seek(startBit);
    m_idec->begin_frame(flags);
    m_out.clear();
    m_outPos = 0;
    uint32_t mbs = 0;
    uint32_t px[256];
    fmv::MacroblockYuv mb;
    bool ok = true;
    while (mbs < 65536) {
        if (mbs != 0 && (br.bits_left() < 8 || br.peek(1) == 0))
            break;
        if (!m_idec->decode_macroblock(br, mbs == 0, mb)) {
            ok = false;
            break;
        }
        fmv::yuv_mb_to_rgba(mb, px, 0x80);
        const size_t old = m_out.size();
        m_out.resize(old + 1024);
        std::memcpy(&m_out[old], px, 1024);
        ++mbs;
    }
    // skip zero padding up to the next start code (00 00 01) on a byte boundary
    size_t pos = (br.tell() + 7) & ~static_cast<size_t>(7);
    const size_t total = m_in.size() * 8;
    while (pos + 24 <= total) {
        fmv::BitReader sc(m_in.data(), m_in.size());
        sc.seek(pos);
        if (sc.peek(24) == 1u)
            break;
        pos += 8;
    }
    m_bitPos = pos;
    m_busy = !m_out.empty();
    ++m_frames;
    g_decodedFrames.fetch_add(1, std::memory_order_relaxed);
    if (const FrameObserver observer = g_frameObserver.load(std::memory_order_acquire);
        observer && ok && startBit % 8 == 0 && pos / 8 <= m_in.size() && pos / 8 > startBit / 8)
        observer(&m_in[startBit / 8], pos / 8 - startBit / 8, m_out.data(), mbs);
    if (const char* dumpDir = std::getenv("PS2X_IPU_DUMP")) // Drakan debug: write decoded frames 1..N as raw RGBA
                                                            // (macroblock raster order, 16x16 each)
    {
        static const int maxDump = []() {
            const char* e = std::getenv("PS2X_IPU_DUMP_N");
            return e ? std::atoi(e) : 4;
        }();
        if (static_cast<int>(m_frames) <= maxDump || static_cast<int>(m_frames) == 100) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/ipu_frame_%u_mb%u.rgba", dumpDir, m_frames, mbs);
            if (FILE* f = std::fopen(path, "wb")) {
                std::fwrite(m_out.data(), 1, m_out.size(), f);
                std::fclose(f);
            }
        }
    }
    if (traceIpu() || !ok)
        std::fprintf(stderr, "[ipu] IDEC frame %u: %u macroblocks%s, flags %02x, next bitpos %zu\n", m_frames, mbs,
                     ok ? "" : " (DECODE ERROR)", flags, m_bitPos);
}
