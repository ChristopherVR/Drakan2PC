// Named EE hardware register addresses and bit fields used by the memory-mapped I/O emulation (ps2_memory.cpp).
// Names follow the Sony EE User's Manual (DMAC, GIF, VIF, IPU chapters). Only what the runtime uses is listed.
#pragma once

#include <cstdint>

namespace ps2hw {

// ---- DMA controller (DMAC) ----------------------------------------------------------------------------------------
// The EE has ten DMA channels. Each one is a block of registers at a fixed base address; the same register sits at
// the same offset in every block. A transfer starts when the EE writes CHCR with the STR bit set.
constexpr uint32_t kDmaVif0 = 0x10008000u;    // channel 0: EE memory -> VIF0 (VU0)
constexpr uint32_t kDmaVif1 = 0x10009000u;    // channel 1: EE memory <-> VIF1 (VU1, and GS read-back)
constexpr uint32_t kDmaGif = 0x1000A000u;     // channel 2: EE memory -> GIF PATH3 (straight to the GS)
constexpr uint32_t kDmaFromIpu = 0x1000B000u; // channel 3: IPU (movie decoder) output -> EE memory
constexpr uint32_t kDmaToIpu = 0x1000B400u;   // channel 4: EE memory -> IPU input
constexpr uint32_t kDmaFromSpr = 0x1000D000u; // channel 8: scratchpad -> EE memory
constexpr uint32_t kDmaToSpr = 0x1000D400u;   // channel 9: EE memory -> scratchpad

// The channel register window [kDmaChannelsBegin, kDmaChannelsEnd); a channel base is the address with the low
// 8 bits cleared.
constexpr uint32_t kDmaChannelsBegin = 0x10008000u;
constexpr uint32_t kDmaChannelsEnd = 0x1000F000u;
constexpr uint32_t kDmaChannelBaseMask = 0xFFFFFF00u;

// Register offsets inside one channel block.
constexpr uint32_t kDmaChcr = 0x00u; // channel control (direction, mode, STR ...)
constexpr uint32_t kDmaMadr = 0x10u; // memory address of the data
constexpr uint32_t kDmaQwc = 0x20u;  // quadword (16-byte) count
constexpr uint32_t kDmaTadr = 0x30u; // next DMA tag address (chain mode)
constexpr uint32_t kDmaAsr0 = 0x40u; // CALL return address stack, slot 0
constexpr uint32_t kDmaAsr1 = 0x50u; // CALL return address stack, slot 1
constexpr uint32_t kDmaSadr = 0x80u; // scratchpad address (SPR channels only)

// CHCR bits.
constexpr uint32_t kChcrDir = 1u << 0; // 1 = from memory, 0 = to memory
constexpr uint32_t kChcrModShift = 2u; // 2-bit mode: kDmaModeNormal / Chain / Interleave
constexpr uint32_t kChcrModMask = 0x3u;
constexpr uint32_t kChcrAspShift = 4u; // 2-bit address stack pointer (CALL depth, 0..2)
constexpr uint32_t kChcrAspMask = 0x3u;
constexpr uint32_t kChcrTte = 1u << 6;  // tag transfer enable: send each tag's upper 64 bits with the data
constexpr uint32_t kChcrTie = 1u << 7;  // tag interrupt enable: a tag with its IRQ bit set stops the chain
constexpr uint32_t kChcrStr = 1u << 8;  // start / busy
constexpr uint32_t kChcrTagShift = 16u; // upper 16 bits: bits 16..31 of the last tag read
constexpr uint32_t kDmaModeNormal = 0u;
constexpr uint32_t kDmaModeChain = 1u;
constexpr uint32_t kDmaModeInterleave = 2u;

constexpr uint32_t chcrMode(uint32_t chcr) {
    return (chcr >> kChcrModShift) & kChcrModMask;
}

// DMA tag ID (bits 28..30 of a source-chain tag): where the data is and where the next tag is.
constexpr uint32_t kTagRefe = 0u; // data at ADDR, then end
constexpr uint32_t kTagCnt = 1u;  // data follows the tag, next tag follows the data
constexpr uint32_t kTagNext = 2u; // data follows the tag, next tag at ADDR
constexpr uint32_t kTagRef = 3u;  // data at ADDR, next tag follows this tag
constexpr uint32_t kTagRefs = 4u; // as REF (with stall control on real hardware)
constexpr uint32_t kTagCall = 5u; // data follows the tag, push the address after it, next tag at ADDR
constexpr uint32_t kTagRet = 6u;  // data follows the tag, pop the next tag address (end when the stack is empty)
constexpr uint32_t kTagEnd = 7u;  // data follows the tag, then end

// DMAC global registers.
constexpr uint32_t kDmacCtrl = 0x1000E000u; // D_CTRL; bit 0 (DMAE) enables all DMA
constexpr uint32_t kDmacCtrlEnable = 1u << 0;
constexpr uint32_t kDmacStat = 0x1000E010u; // D_STAT
// D_STAT: bits 0..9 are per-channel "transfer ended" flags (write 1 to clear); bits 16..25 are the matching
// interrupt masks (write 1 to toggle). Bit 31 is the runtime's own summary bit: some unmasked channel flag is set.
constexpr uint32_t kDStatChannelMask = 0x3FFu;
constexpr uint32_t kDStatMaskShift = 16u;
constexpr uint32_t kDStatIrqPending = 1u << 31;

// D_STAT channel bits (and the DMAC interrupt cause numbers handed to the guest's handlers).
constexpr uint32_t kDmaCauseVif0 = 0u;
constexpr uint32_t kDmaCauseVif1 = 1u;
constexpr uint32_t kDmaCauseGif = 2u;
constexpr uint32_t kDmaCauseFromIpu = 3u;
constexpr uint32_t kDmaCauseToIpu = 4u;
constexpr uint32_t kDmaCauseFromSpr = 8u;
constexpr uint32_t kDmaCauseToSpr = 9u;

// ---- GIF, VIF0, VIF1, IPU ----------------------------------------------------------------------------------------
constexpr uint32_t kGifCtrl = 0x10003000u;
constexpr uint32_t kGifMode = 0x10003010u;
constexpr uint32_t kGifStat = 0x10003020u;
constexpr uint32_t kGifStatFqcMask = 0x1F000000u; // GIF_STAT.FQC: quadwords waiting in the GIF FIFO
constexpr uint32_t kGifStatFqcShift = 24u;

// GIF, VIF0 and VIF1 registers together; the VU1 worker thread owns the state behind them.
constexpr uint32_t kGifVifRegsBegin = 0x10003000u;
constexpr uint32_t kGifVifRegsEnd = 0x10003E00u;

constexpr uint32_t kVif0RegsBegin = 0x10003800u;
constexpr uint32_t kVif0RegsEnd = 0x10003A00u;

constexpr uint32_t kVif1RegsBegin = 0x10003C00u;
constexpr uint32_t kVif1RegsEnd = 0x10003E00u;
constexpr uint32_t kVif1Stat = 0x10003C00u;
constexpr uint32_t kVif1Fbrst = 0x10003C10u;
constexpr uint32_t kVif1Mark = 0x10003C30u;
constexpr uint32_t kVif1Cycle = 0x10003C40u;
constexpr uint32_t kVif1Mode = 0x10003C50u;
constexpr uint32_t kVif1Num = 0x10003C60u;
constexpr uint32_t kVif1Mask = 0x10003C70u;
constexpr uint32_t kVif1Code = 0x10003C80u;
constexpr uint32_t kVif1Itops = 0x10003C90u;
constexpr uint32_t kVif1Base = 0x10003CA0u;
constexpr uint32_t kVif1Ofst = 0x10003CB0u;
constexpr uint32_t kVif1Tops = 0x10003CC0u;
constexpr uint32_t kVif1Itop = 0x10003CD0u;
constexpr uint32_t kVif1Top = 0x10003CE0u;

constexpr uint32_t kVifFbrstRst = 1u << 0;      // FBRST.RST: reset the VIF
constexpr uint32_t kVifFbrstStc = 1u << 3;      // FBRST.STC: clear the stall / error flags in STAT
constexpr uint32_t kVifStatMrk = 1u << 6;       // STAT.MRK: a MARK command was seen
constexpr uint32_t kVifStatStallBits = 0x3F00u; // STAT bits 8..13 (VSS, VFS, VIS, INT, ER0, ER1), cleared by STC

constexpr uint32_t kIpuRegsBegin = 0x10002000u; // IPU_CMD, IPU_CTRL, IPU_BP, IPU_TOP
constexpr uint32_t kIpuRegsLast = 0x10002030u;  // inclusive
constexpr uint32_t kIpuCmd = 0x10002000u;

} // namespace ps2hw
