// Based on Blackline Interactive implementation
#include "runtime/ps2_memory.h"
#include "drakan_vu1_hook.h"
#include "drakan_object_motion.h"
#include "ps2_vif_unpack.h"
#include "ps2_vu1_batch_capture.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace drakan_vu1 {
std::atomic<UploadHook> g_uploadHook{nullptr};
}

namespace {
bool vifRegLogOn() {
    static const bool on = std::getenv("PS2X_VIF_REGLOG") != nullptr;
    return on;
}
// Commands parsed so far in the current processVIF1Data call (only with PS2X_VIF_REGLOG): position and code.
thread_local std::vector<std::pair<uint32_t, uint32_t>> t_vifTrace;
inline void vifRegTrace(uint32_t pos, uint32_t cmd) {
    if (!vifRegLogOn())
        return;
    if (pos == 0u)
        t_vifTrace.clear();
    if (t_vifTrace.size() < 4096u)
        t_vifTrace.emplace_back(pos, cmd);
}
// Drakan debug: PS2X_VIF_REGLOG=1 reports the first occurrence of every distinct STCYCL / STMOD value and every MPG
// in the VIF1 stream with the bytes around it (the game sets CYCLE once and never uploads microcode in a level, so a
// new value means the stream is being parsed out of step).
void vifRegLog(const char* what, uint32_t cmd, const uint8_t* data, uint32_t pos, uint32_t size, uint32_t offsetBase,
               const uint8_t* rdram) {
    if (!vifRegLogOn())
        return;
    static uint32_t seen[64];
    static int nSeen = 0, reports = 0;
    for (int i = 0; i < nSeen; ++i)
        if (seen[i] == cmd)
            return;
    if (nSeen < 64)
        seen[nSeen++] = cmd;
    if (reports++ >= 48)
        return;
    std::fprintf(stderr, "[vif-reglog] %s cmd=%08x at %u of %u (slice offset %u)\n", what, cmd, pos, size, offsetBase);
    if (const char* dir = std::getenv("PS2X_VIF_REGLOG"); dir && std::strlen(dir) > 1u) {
        // PS2X_VIF_REGLOG=<dir>: also keep the whole transfer
        const std::string path = std::string(dir) + "/vif-reglog-" + std::to_string(reports) + ".bin";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(data, 1, size, f);
            std::fclose(f);
        }
    }
    const uint32_t from = pos >= 128u ? (pos - 128u) & ~15u : 0u;
    const uint32_t to = std::min<uint32_t>(size, pos + 64u);
    for (uint32_t a = from; a + 16u <= to; a += 16u) {
        uint32_t w[4];
        std::memcpy(w, data + a, 16);
        std::fprintf(stderr, "[vif-reglog]   +%u %08x %08x %08x %08x\n", a, w[0], w[1], w[2], w[3]);
    }
    {
        std::string line;
        char buf[32];
        for (const auto& [p, c] : t_vifTrace) {
            std::snprintf(buf, sizeof buf, " %u:%08x", p, c);
            line += buf;
            if (line.size() > 180u) {
                std::fprintf(stderr, "[vif-reglog]   trace%s\n", line.c_str());
                line.clear();
            }
        }
        if (!line.empty())
            std::fprintf(stderr, "[vif-reglog]   trace%s\n", line.c_str());
    }
    // Where in guest RAM do these bytes live, and which DMA tags (REF/REFE/REFS/CNT/NEXT/CALL) send from just before?
    const uint32_t key = (pos & ~15u) >= 32u ? (pos & ~15u) - 32u : 0u;
    if (!rdram || key + 48u > size)
        return;
    int hits = 0;
    for (uint32_t a = 0; a + 48u <= PS2_RAM_SIZE && hits < 4; a += 16u) {
        if (std::memcmp(rdram + a, data + key, 48) != 0)
            continue;
        ++hits;
        const uint32_t src = a + (pos - key);
        std::fprintf(stderr, "[vif-reglog]   bytes found at guest %08x (command at %08x)\n", a, src);
        int tags = 0;
        for (uint32_t t = 0; t + 16u <= PS2_RAM_SIZE && tags < 8; t += 16u) {
            uint64_t tag;
            std::memcpy(&tag, rdram + t, 8);
            const uint32_t id = static_cast<uint32_t>((tag >> 28) & 7u), qwc = static_cast<uint32_t>(tag & 0xffffu);
            const uint32_t addr = static_cast<uint32_t>(tag >> 32) & 0x1ffffff0u;
            const uint32_t dataAt = (id == 1u || id == 2u || id == 5u || id == 6u || id == 7u) ? t + 16u : addr;
            if (qwc == 0u || ((tag >> 16) & 0xfffu) != 0u || dataAt > src || dataAt + qwc * 16u <= src ||
                src - dataAt > 0x10000u)
                continue;
            if (id != 0u && id != 3u && id != 4u)
                continue; // payload-follows tags would match any data; only reference tags are informative
            ++tags;
            std::fprintf(stderr, "[vif-reglog]     tag at %08x id=%u qwc=%u addr=%08x (command %u bytes in)\n", t, id,
                         qwc, addr, src - dataAt);
        }
    }
}
} // namespace

enum VIFCmd : uint8_t {
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4A,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
};

namespace {
constexpr uint8_t kGifFmtImage = 2u;

uint32_t pendingGifImageQwc(const uint8_t* data, uint32_t sizeBytes) {
    if (!data || sizeBytes < 16u)
        return 0u;

    uint32_t offset = 0u;
    while (offset + 16u <= sizeBytes) {
        uint64_t tagLo = 0u;
        std::memcpy(&tagLo, data + offset, sizeof(tagLo));
        offset += 16u;

        const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
        const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
        uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
        if (nreg == 0u)
            nreg = 16u;

        uint64_t payloadBytes = 0u;
        if (flg == 0u) // PACKED
        {
            payloadBytes = static_cast<uint64_t>(nloop) * nreg * 16ull;
        } else if (flg == 1u) // REGLIST, padded to a quadword
        {
            payloadBytes = static_cast<uint64_t>(nloop) * nreg * 8ull;
            payloadBytes = (payloadBytes + 15ull) & ~15ull;
        } else if (flg == kGifFmtImage) {
            payloadBytes = static_cast<uint64_t>(nloop) * 16ull;
            const uint64_t availableBytes = sizeBytes - offset;
            if (payloadBytes > availableBytes) {
                return static_cast<uint32_t>((payloadBytes - availableBytes) / 16ull);
            }
        } else {
            return 0u;
        }

        if (payloadBytes > static_cast<uint64_t>(sizeBytes - offset))
            return 0u;
        offset += static_cast<uint32_t>(payloadBytes);
    }

    return 0u;
}
} // namespace

void PS2Memory::processVIF0Data(uint32_t srcPhys, uint32_t sizeBytes) {
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF0Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF0Data(const uint8_t* data, uint32_t sizeBytes) {
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;
    while (pos + 4 <= sizeBytes) {
        uint32_t cmd = 0u;
        std::memcpy(&cmd, data + pos, sizeof(cmd));
        pos += 4u;

        const uint8_t opcode = static_cast<uint8_t>((cmd >> 24) & 0x7Fu);
        const uint16_t imm = static_cast<uint16_t>(cmd & 0xFFFFu);
        const uint8_t num = static_cast<uint8_t>((cmd >> 16) & 0xFFu);
        const bool irq = (cmd & 0x80000000u) != 0u;

        vif0_regs.code = cmd;
        vif0_regs.num = num;
        if (irq)
            vif0_regs.stat |= (1u << 11);

        if (opcode == VIF_NOP) {
            continue;
        } else if (opcode == VIF_STCYCL) {
            vif0_regs.cycle = imm;
            continue;
        } else if (opcode == VIF_ITOP) {
            vif0_regs.itops = imm & 0x3FFu;
            continue;
        } else if (opcode == VIF_STMOD) {
            vif0_regs.mode = imm & 3u;
            continue;
        } else if (opcode == VIF_MARK) {
            vif0_regs.mark = imm;
            vif0_regs.stat |= (1u << 6);
            continue;
        } else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA) {
            continue;
        } else if (opcode == VIF_STMASK) {
            if (pos + 4u > sizeBytes)
                break;
            std::memcpy(&vif0_regs.mask, data + pos, sizeof(vif0_regs.mask));
            pos += 4u;
            continue;
        } else if (opcode == VIF_STROW) {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.row, data + pos, 16u);
            pos += 16u;
            continue;
        } else if (opcode == VIF_STCOL) {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.col, data + pos, 16u);
            pos += 16u;
            continue;
        } else if (opcode == VIF_MPG) {
            const uint32_t destAddr = static_cast<uint32_t>(imm & 0x1FFu) * 8u;
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            uint32_t copyBytes = 0u;
            if (m_vu0Code && destAddr < PS2_VU0_CODE_SIZE && mpgBytes > 0u) {
                copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU0_CODE_SIZE)
                    copyBytes = PS2_VU0_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes) {
                    std::memcpy(m_vu0Code + destAddr, data + pos, copyBytes);
                    markVU0CodeModified();
                }
            }

            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        } else if ((opcode & 0x60u) == 0x60u) {
            const uint8_t vn = static_cast<uint8_t>((opcode >> 2) & 0x3u);
            const uint8_t vl = static_cast<uint8_t>(opcode & 0x3u);
            const int components = static_cast<int>(vn) + 1;
            int bitsPerComponent = 32;
            switch (vl) {
                case 0:
                    bitsPerComponent = 32;
                    break;
                case 1:
                    bitsPerComponent = 16;
                    break;
                case 2:
                    bitsPerComponent = 8;
                    break;
                case 3:
                    bitsPerComponent = (vn == 3u) ? 4 : 16;
                    break;
                default:
                    break;
            }
            const int bitsPerVector = (vl == 3u && vn == 3u) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = static_cast<uint32_t>((bitsPerVector + 7) / 8);
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            uint32_t cl = vif0_regs.cycle & 0xFFu;
            uint32_t wl = (vif0_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;
            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl) {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }
            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3u) & ~3u;

            if (m_vu0Data && pos + totalBytes <= sizeBytes && vl == 0u) {
                uint32_t vuAddr = static_cast<uint32_t>(imm & 0x3FFu);
                if ((imm & 0x8000u) != 0u)
                    vuAddr = (vuAddr + (vif0_regs.tops & 0x3FFu)) & 0x3FFu;
                const uint8_t* srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex) {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);
                    uint32_t destVec = (cl >= wl) ? ((vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu)
                                                  : ((vuAddr + writeIndex) & 0x3FFu);
                    const uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU0_DATA_SIZE) {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }
                    if (!sourceAvailable || srcIndex >= sourceVectorCount)
                        continue;
                    const uint8_t* srcVec = srcBase + srcIndex * bytesPerVector;
                    ++srcIndex;
                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu0Data + destOff, sizeof(lanes));
                    const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                    for (uint32_t c = 0; c < limit; ++c) {
                        uint32_t scalar = 0u;
                        std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                        lanes[c] = scalar;
                    }
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(m_vu0Data + destOff),
                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(lanes)));
                }
            }
            pos += totalBytes;
            if (pos > sizeBytes)
                break;
            continue;
        } else {
            break;
        }
    }
}

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes) {
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF1Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF1Data(const uint8_t* data, uint32_t sizeBytes) {
    processVIF1Data(data, sizeBytes, 0);
}

void PS2Memory::processVIF1Data(const uint8_t* data, uint32_t sizeBytes, uint32_t offsetBase) {
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;

    while (pos + 4 <= sizeBytes) {
        if (m_vif1PendingPath2ImageQwc != 0u) {
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            if (availableQw == 0u) {
                break;
            }

            const uint32_t chunkQw = std::min<uint32_t>(m_vif1PendingPath2ImageQwc, availableQw);
            std::vector<uint8_t> imagePacket(16u + static_cast<size_t>(chunkQw) * 16u, 0u);
            const uint64_t imageTag = static_cast<uint64_t>(chunkQw & 0x7FFFu) |
                                      ((m_vif1PendingPath2ImageQwc == chunkQw) ? (1ull << 15) : 0ull) |
                                      (static_cast<uint64_t>(kGifFmtImage) << 58);
            std::memcpy(imagePacket.data(), &imageTag, sizeof(imageTag));
            std::memcpy(imagePacket.data() + 16u, data + pos, static_cast<size_t>(chunkQw) * 16u);
            submitGifPacket(GifPathId::Path2, imagePacket.data(), static_cast<uint32_t>(imagePacket.size()), true,
                            m_vif1PendingPath2DirectHl);

            pos += chunkQw * 16u;
            m_vif1PendingPath2ImageQwc -= chunkQw;
            if (m_vif1PendingPath2ImageQwc == 0u) {
                m_vif1PendingPath2DirectHl = false;
            }
            continue;
        }

        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        if (vu1batch::captureActive)
            vu1batch::command(this, cmd, pos + offsetBase);
        if (drakan_object_motion::boundary(pos + offsetBase) &&
            (!vu1batch::captureActive || !vu1batch::captureDrain(this)) && m_gifArbiter && !m_gifArbiter->empty())
            m_gifArbiter->drain();
        drakan_object_motion::command(pos + offsetBase);
        vifRegTrace(pos, cmd);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F;
        uint16_t imm = cmd & 0xFFFF;
        uint8_t num = (cmd >> 16) & 0xFF;
        const bool irq = (cmd & 0x80000000u) != 0u;

        // Track most-recent command for VIFn_CODE emulation.
        vif1_regs.code = cmd;
        vif1_regs.num = num;
        if (irq)
            vif1_regs.stat |= (1u << 11); // INT

        if (opcode == VIF_NOP) {
            continue;
        } else if (opcode == VIF_STCYCL) {
            vifRegLog("STCYCL", cmd, data, pos - 4u, sizeBytes, offsetBase, m_rdram);
            vif1_regs.cycle = imm;
            continue;
        } else if (opcode == VIF_OFFSET) {
            // VIF double-buffer setup. OFFSET clears DBF and resets TOPS to BASE.
            // Do not rewrite BASE from the previous TOPS value.
            vif1_regs.ofst = imm & 0x3FFu;
            vif1_regs.tops = vif1_regs.base & 0x3FFu;
            vif1_regs.stat &= ~(1u << 7); // clear DBF
            continue;
        } else if (opcode == VIF_BASE) {
            // BASE only updates the base register. TOPS changes on OFFSET/MSCAL.
            vif1_regs.base = imm & 0x3FFu;
            continue;
        } else if (opcode == VIF_ITOP) {
            // ITOP VIFcode writes pending ITOPS; VU XITOP observes it after MSCAL/MSCNT.
            vif1_regs.itops = imm & 0x3FFu;
            continue;
        } else if (opcode == VIF_STMOD) {
            vifRegLog("STMOD", cmd, data, pos - 4u, sizeBytes, offsetBase, m_rdram);
            vif1_regs.mode = imm & 3u;
            continue;
        } else if (opcode == VIF_MSKPATH3) {
            // VIF command docs: MSKPATH3 uses IMMEDIATE bit 15.
            const bool wasMasked = m_path3Masked;
            m_path3Masked = (imm & 0x8000u) != 0u;
            const bool captured = vu1batch::captureActive && vu1batch::captureMask(this, m_path3Masked);
            if (wasMasked && !m_path3Masked && !captured)
                flushMaskedPath3Packets();
            continue;
        } else if (opcode == VIF_MARK) {
            vif1_regs.mark = imm;
            vif1_regs.stat |= (1u << 6); // MRK
            continue;
        } else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA) {
            continue;
        } else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF) {
            if (drakan_object_motion::tracking()) {
                uint32_t motionFlags = 0;
                std::memcpy(&motionFlags, getVU1Data() + 7u * 16u, 4);
                if (vu1batch::captureActive)
                    vu1batch::memoryRead(this, 7u * 16u, 4);
                if ((motionFlags & 1u) && (!vu1batch::captureActive || !vu1batch::captureDrain(this)) && m_gifArbiter &&
                    !m_gifArbiter->empty())
                    m_gifArbiter->drain();
                drakan_object_motion::clipping((motionFlags & 1u) != 0);
            }
            uint32_t startPC = (uint32_t)imm * 8u;

            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscalCallback)
                m_vu1MscalCallback(startPC, runTop, runItop);
            continue;
        } else if (opcode == VIF_MSCNT) {
            if (drakan_object_motion::tracking()) {
                uint32_t motionFlags = 0;
                std::memcpy(&motionFlags, getVU1Data() + 7u * 16u, 4);
                if (vu1batch::captureActive)
                    vu1batch::memoryRead(this, 7u * 16u, 4);
                if ((motionFlags & 1u) && (!vu1batch::captureActive || !vu1batch::captureDrain(this)) && m_gifArbiter &&
                    !m_gifArbiter->empty())
                    m_gifArbiter->drain();
                drakan_object_motion::clipping((motionFlags & 1u) != 0);
            }
            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscntCallback)
                m_vu1MscntCallback(runTop, runItop);
            continue;
        } else if (opcode == VIF_STMASK) {
            if (pos + 4 > sizeBytes)
                break;
            uint32_t maskValue = 0;
            std::memcpy(&maskValue, data + pos, sizeof(maskValue));
            vif1_regs.mask = maskValue;
            pos += 4;
            continue;
        } else if (opcode == VIF_STROW) {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.row, data + pos, 16);
            pos += 16;
            continue;
        } else if (opcode == VIF_STCOL) {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.col, data + pos, 16);
            pos += 16;
            continue;
        } else if (opcode == VIF_MPG) {
            vifRegLog("MPG", cmd, data, pos - 4u, sizeBytes, offsetBase, m_rdram);
            uint32_t destAddr = (uint32_t)imm * 8u;
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            if (m_vu1Code && destAddr < PS2_VU1_CODE_SIZE && mpgBytes > 0) {
                uint32_t copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU1_CODE_SIZE)
                    copyBytes = PS2_VU1_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes) {
                    std::memcpy(m_vu1Code + destAddr, data + pos, copyBytes);
                    markVU1CodeModified();
                }
            }
            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        } else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL) {
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536;
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            const bool truncated = qwCount > availableQw;
            if (qwCount > availableQw)
                qwCount = availableQw;

            if (qwCount > 0) {
                const bool directHl = (opcode == VIF_DIRECTHL);
                submitGifPacket(GifPathId::Path2, data + pos, qwCount * 16, true, directHl);

                const uint32_t pendingImageQw = pendingGifImageQwc(data + pos, qwCount * 16u);
                if (pendingImageQw != 0u) {
                    m_vif1PendingPath2ImageQwc = pendingImageQw;
                    m_vif1PendingPath2DirectHl = directHl;
                }
            }

            pos += qwCount * 16;
            if (truncated) {
                pos = sizeBytes;
                break;
            }
            continue;
        } else if ((opcode & 0x60) == 0x60) {
            uint8_t vn = (opcode >> 2) & 0x3;
            uint8_t vl = opcode & 0x3;
            const bool maskEnable = (opcode & 0x10u) != 0u;
            int components = vn + 1;
            int bitsPerComponent = 32;
            switch (vl) {
                case 0:
                    bitsPerComponent = 32;
                    break;
                case 1:
                    bitsPerComponent = 16;
                    break;
                case 2:
                    bitsPerComponent = 8;
                    break;
                case 3:
                    bitsPerComponent = (vn == 3) ? 4 : 16;
                    break;
                default:
                    break;
            }
            int bitsPerVector = (vl == 3 && vn == 3) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = (bitsPerVector + 7) / 8;
            // UNPACK semantics: NUM is 8-bit and NUM==0 means 256 vectors (writes).
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);

            // STCYCL controls write cycles for UNPACK.
            uint32_t cl = vif1_regs.cycle & 0xFFu;
            uint32_t wl = (vif1_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;

            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl) {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }

            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3) & ~3u;

            uint32_t vuAddr = (uint32_t)imm & 0x3FFu;
            if ((imm & 0x8000u) != 0u)
                vuAddr = (vuAddr + (vif1_regs.tops & 0x3FFu)) & 0x3FFu;

            const bool zeroExtend = (imm & 0x4000u) != 0u;

            { // Drakan debug: PS2X_VIF_LOG=<max lines>: log UNPACKs that write into the VU1 constant block (qw 0..21)
                static const int maxLog = []() {
                    const char* e = std::getenv("PS2X_VIF_LOG");
                    return e ? std::atoi(e) : 0;
                }();
                static int logged = 0;
                if (maxLog > 0 && logged < maxLog && (vuAddr + (unsigned)(num ? num : 256) > 40u && vuAddr < 48u)) {
                    ++logged;
                    std::fprintf(stderr,
                                 "[vif-unpack] addr=%u imm=%04x num=%u fmt=%02x cl=%u wl=%u tops=%u pos=%u/%u "
                                 "first=%08x %08x %08x %08x\n",
                                 vuAddr, (unsigned)imm, (unsigned)num, (unsigned)opcode, cl, wl,
                                 (unsigned)vif1_regs.tops, pos, sizeBytes,
                                 pos + 16 <= sizeBytes ? *(const uint32_t*)(data + pos) : 0u,
                                 pos + 16 <= sizeBytes ? *(const uint32_t*)(data + pos + 4) : 0u,
                                 pos + 16 <= sizeBytes ? *(const uint32_t*)(data + pos + 8) : 0u,
                                 pos + 16 <= sizeBytes ? *(const uint32_t*)(data + pos + 12) : 0u);
                }
            }

            uint32_t cameraFirst = 7, cameraEnd = 0;
            static const bool fastUnpack = [] {
                const char* e = std::getenv("PS2X_VIF_FAST_UNPACK");
                return !e || std::atoi(e) != 0;
            }();
            if (m_vu1Data && totalBytes > 0 && pos + totalBytes <= sizeBytes &&
                !(fastUnpack && (!vu1batch::captureActive || !vu1batch::trackVif(this)) &&
                  vif_unpack::fast(m_vu1Data, data + pos, vuAddr, writeVectorCount, cl, wl, vn, vl, zeroExtend,
                                   maskEnable, vif1_regs.mode, cameraFirst, cameraEnd, vif1_regs.mask, vif1_regs.row,
                                   vif1_regs.col))) {
                const uint8_t* srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex) {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);

                    uint32_t destVec = 0;
                    if (cl >= wl) {
                        destVec = (vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu;
                    } else {
                        destVec = (vuAddr + writeIndex) & 0x3FFu;
                    }

                    uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU1_DATA_SIZE) {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }

                    if (destVec < 7u) {
                        if (destVec < cameraFirst)
                            cameraFirst = destVec;
                        if (destVec + 1 > cameraEnd)
                            cameraEnd = destVec + 1;
                    }
                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu1Data + destOff, sizeof(lanes));
                    uint32_t decompressed[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
                    bool decoded = false;

                    const uint8_t* srcVec = nullptr;
                    if (sourceAvailable && srcIndex < sourceVectorCount) {
                        srcVec = srcBase + srcIndex * bytesPerVector;
                        ++srcIndex;
                        decoded = true;
                    }

                    auto extend16 = [&](uint16_t raw) -> uint32_t {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                    };

                    auto extend8 = [&](uint8_t raw) -> uint32_t {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
                    };

                    bool handledFormat = true;
                    if (!decoded) {
                        handledFormat = false;
                    } else if (vl == 0u) {
                        if (components == 1) {
                            uint32_t scalar = 0;
                            std::memcpy(&scalar, srcVec, sizeof(scalar));
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        } else {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c) {
                                uint32_t scalar = 0;
                                std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                                decompressed[c] = scalar;
                            }
                        }
                    } else if (vl == 1u) {
                        if (components == 1) {
                            uint16_t raw = 0;
                            std::memcpy(&raw, srcVec, sizeof(raw));
                            const uint32_t scalar = extend16(raw);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        } else {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c) {
                                uint16_t raw = 0;
                                std::memcpy(&raw, srcVec + c * 2u, sizeof(raw));
                                decompressed[c] = extend16(raw);
                            }
                        }
                    } else if (vl == 2u) {
                        if (components == 1) {
                            const uint32_t scalar = extend8(srcVec[0]);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        } else {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c) {
                                decompressed[c] = extend8(srcVec[c]);
                            }
                        }
                    } else if (vl == 3u && vn == 3u) {
                        // V4-5: packed color-like format in a single 16-bit value.
                        uint16_t packed = 0;
                        std::memcpy(&packed, srcVec, sizeof(packed));
                        decompressed[0] = packed & 0x1Fu;
                        decompressed[1] = (packed >> 5) & 0x1Fu;
                        decompressed[2] = (packed >> 10) & 0x1Fu;
                        decompressed[3] = (packed >> 15) & 0x01u;
                    } else {
                        handledFormat = false;
                    }

                    // Unknown compressed format fallback: preserve legacy raw-copy behavior.
                    if (!handledFormat && decoded && !maskEnable && (vif1_regs.mode == 0u || vif1_regs.mode == 3u)) {
                        uint32_t copyBytes = (bytesPerVector < 16u) ? bytesPerVector : 16u;
                        std::memcpy(m_vu1Data + destOff, srcVec, copyBytes);
                        if (vu1batch::captureActive) {
                            if (copyBytes % 4u)
                                vu1batch::unsupportedTrace();
                            else
                                vu1batch::memoryWrite(this, destOff, uint8_t((1u << (copyBytes / 4u)) - 1u));
                        }
                        continue;
                    }

                    const bool canAdd = (vl != 3u || vn != 3u);
                    const uint32_t mode = vif1_regs.mode & 3u;
                    const uint32_t colIdx = (cyclePos > 3u) ? 3u : cyclePos;
                    const uint32_t maskCycle = (cyclePos > 3u) ? 3u : cyclePos;

                    uint8_t writtenLanes = 0;
                    for (uint32_t field = 0u; field < 4u; ++field) {
                        uint32_t maskSpec = 0u;
                        if (maskEnable) {
                            const uint32_t shift = ((maskCycle * 4u) + field) * 2u;
                            maskSpec = (vif1_regs.mask >> shift) & 0x3u;
                        }

                        // In fill-write cycles with suspended source reads, treat raw-data selections as row-fill.
                        if (!decoded && maskSpec == 0u)
                            maskSpec = 1u;

                        uint32_t writeVal = lanes[field];
                        if (maskSpec == 0u) {
                            if (handledFormat) {
                                writtenLanes |= uint8_t(1u << field);
                                writeVal = decompressed[field];
                                if (canAdd && (mode == 1u || mode == 2u)) {
                                    writeVal = writeVal + vif1_regs.row[field];
                                    if (mode == 2u)
                                        vif1_regs.row[field] = writeVal;
                                }
                            }
                        } else if (maskSpec == 1u) {
                            writtenLanes |= uint8_t(1u << field);
                            writeVal = vif1_regs.row[field];
                        } else if (maskSpec == 2u) {
                            writtenLanes |= uint8_t(1u << field);
                            writeVal = vif1_regs.col[colIdx];
                        } else {
                            continue; // write-protect
                        }

                        lanes[field] = writeVal;
                    }

                    std::memcpy(m_vu1Data + destOff, lanes, sizeof(lanes));
                    if (vu1batch::captureActive)
                        vu1batch::memoryWrite(this, destOff, writtenLanes);
                }
            }
            if (auto hook = drakan_vu1::g_uploadHook.load(std::memory_order_acquire); hook && cameraEnd > cameraFirst)
                if (!vu1batch::captureActive ||
                    !vu1batch::captureUpload(this, hook, cameraFirst, cameraEnd - cameraFirst, m_vu1Data))
                    hook(cameraFirst, cameraEnd - cameraFirst, m_vu1Data);
            pos += totalBytes;

            if (pos > sizeBytes)
                break;
            continue;
        } else {
            continue;
        }
    }
}
