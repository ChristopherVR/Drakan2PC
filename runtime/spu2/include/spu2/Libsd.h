// libsd-level constants and structures (names/values follow ps2sdk common/include/libsd-common.h,
// which mirrors the Sony libsd headers) plus the sdrdrv (SID 0x80000701) command ids.
#pragma once
#include <cstdint>

namespace spu2 {

// ---- entry encoding -------------------------------------------------------------------------
// entry = (register index << 8) | flags.  bit0 = core, bits1..5 = voice (SD_VOICE(core,v) = core | v<<1).
// Entries with bit 0x80 set (SD_PARAM_M/E/A/BVOL*) are per-core registers.
constexpr uint16_t SD_VOICE(unsigned core, unsigned v) {
    return (uint16_t)(core | (v << 1));
}

// SetParam / GetParam
constexpr uint16_t SD_VPARAM_VOLL = 0x00 << 8, SD_VPARAM_VOLR = 0x01 << 8, SD_VPARAM_PITCH = 0x02 << 8,
                   SD_VPARAM_ADSR1 = 0x03 << 8, SD_VPARAM_ADSR2 = 0x04 << 8, SD_VPARAM_ENVX = 0x05 << 8,
                   SD_VPARAM_VOLXL = 0x06 << 8, SD_VPARAM_VOLXR = 0x07 << 8;
constexpr uint16_t SD_PARAM_MMIX = 0x08 << 8;
constexpr uint16_t SD_PARAM_MVOLL = (0x09 << 8) | 0x80, SD_PARAM_MVOLR = (0x0A << 8) | 0x80,
                   SD_PARAM_EVOLL = (0x0B << 8) | 0x80, SD_PARAM_EVOLR = (0x0C << 8) | 0x80,
                   SD_PARAM_AVOLL = (0x0D << 8) | 0x80, SD_PARAM_AVOLR = (0x0E << 8) | 0x80,
                   SD_PARAM_BVOLL = (0x0F << 8) | 0x80, SD_PARAM_BVOLR = (0x10 << 8) | 0x80,
                   SD_PARAM_MVOLXL = (0x11 << 8) | 0x80, SD_PARAM_MVOLXR = (0x12 << 8) | 0x80;
// SetSwitch / GetSwitch (24-bit voice masks)
constexpr uint16_t SD_SWITCH_PMON = 0x13 << 8, SD_SWITCH_NON = 0x14 << 8, SD_SWITCH_KON = 0x15 << 8,
                   SD_SWITCH_KOFF = 0x16 << 8, SD_SWITCH_ENDX = 0x17 << 8, SD_SWITCH_VMIXL = 0x18 << 8,
                   SD_SWITCH_VMIXEL = 0x19 << 8, SD_SWITCH_VMIXR = 0x1A << 8, SD_SWITCH_VMIXER = 0x1B << 8;
// SetAddr / GetAddr (byte addresses in SPU2 RAM)
constexpr uint16_t SD_ADDR_ESA = 0x1C << 8, SD_ADDR_EEA = 0x1D << 8, SD_ADDR_TSA = 0x1E << 8, SD_ADDR_IRQA = 0x1F << 8,
                   SD_VADDR_SSA = 0x20 << 8, SD_VADDR_LSAX = 0x21 << 8, SD_VADDR_NAX = 0x22 << 8;
// SetCoreAttr / GetCoreAttr
constexpr uint16_t SD_CORE_EFFECT_ENABLE = 0x2, SD_CORE_IRQ_ENABLE = 0x4, SD_CORE_MUTE_ENABLE = 0x6,
                   SD_CORE_NOISE_CLK = 0x8, SD_CORE_SPDIF_MODE = 0xA;
// CORE_ATTR register bits
constexpr uint16_t SD_SPU2_ON = 1 << 15, SD_MUTE = 1 << 14, SD_ENABLE_EFFECTS = 1 << 7, SD_ENABLE_IRQ = 1 << 6,
                   SD_DMA_IO = 1 << 4, SD_DMA_WRITE = 2 << 4, SD_DMA_READ = 3 << 4, SD_CORE_DMA = 3 << 4,
                   SD_ENABLE_EX_INPUT = 1 << 0;

constexpr uint16_t SD_ADSR_AR_LINEARi = 0, SD_ADSR_AR_EXPi = 1;
constexpr uint16_t SD_ADSR_SR_LINEARi = 0, SD_ADSR_SR_LINEARd = 2, SD_ADSR_SR_EXPi = 4, SD_ADSR_SR_EXPd = 6;
constexpr uint16_t SD_ADSR_RR_LINEARd = 0, SD_ADSR_RR_EXPd = 1;
constexpr uint16_t SD_SET_ADSR1(unsigned arm, unsigned ar, unsigned dr, unsigned sl) {
    return (uint16_t)(((arm & 1) << 15) | ((ar & 0x7f) << 8) | ((dr & 0xf) << 4) | (sl & 0xf));
}
constexpr uint16_t SD_SET_ADSR2(unsigned srm, unsigned sr, unsigned rrm, unsigned rr) {
    return (uint16_t)(((srm & 7) << 13) | ((sr & 0x7f) << 6) | ((rrm & 1) << 5) | (rr & 0x1f));
}

// Transfers
constexpr uint16_t SD_TRANS_WRITE = 0, SD_TRANS_READ = 1, SD_TRANS_STOP = 2, SD_TRANS_WRITE_FROM = 3,
                   SD_TRANS_LOOP = 0x10, SD_TRANS_MODE_DMA = 0, SD_TRANS_MODE_IO = 8;

// Effect modes
enum : int {
    SD_EFFECT_MODE_OFF = 0,
    SD_EFFECT_MODE_ROOM,
    SD_EFFECT_MODE_STUDIO_1,
    SD_EFFECT_MODE_STUDIO_2,
    SD_EFFECT_MODE_STUDIO_3,
    SD_EFFECT_MODE_HALL,
    SD_EFFECT_MODE_SPACE,
    SD_EFFECT_MODE_ECHO,
    SD_EFFECT_MODE_DELAY,
    SD_EFFECT_MODE_PIPE,
    SD_EFFECT_MODE_CLEAR = 0x100
};

// ProcBatch
constexpr uint16_t SD_BATCH_SETPARAM = 1, SD_BATCH_SETSWITCH = 2, SD_BATCH_SETADDR = 3, SD_BATCH_SETCORE = 4,
                   SD_BATCH_WRITEIOP = 5, SD_BATCH_WRITEEE = 6, SD_BATCH_EERETURN = 7, SD_BATCH_GETPARAM = 0x11,
                   SD_BATCH_GETSWITCH = 0x12, SD_BATCH_GETADDR = 0x13, SD_BATCH_GETCORE = 0x14;

constexpr uint8_t ADPCM_LOOP_START = 4, ADPCM_LOOP = 2, ADPCM_LOOP_END = 1;

struct SdBatch {
    uint16_t func;
    uint16_t entry;
    uint32_t value;
};
static_assert(sizeof(SdBatch) == 8, "SdBatch");

struct SdEffectAttr { // sceSdEffectAttr, 20 bytes
    int32_t core;
    int32_t mode;
    int16_t depth_L;
    int16_t depth_R;
    int32_t delay;
    int32_t feedback;
};
static_assert(sizeof(SdEffectAttr) == 20, "SdEffectAttr");

// ---- sdrdrv (EE <-> IOP remote libsd), server 0x80000701; callbacks to EE 0x80000704 ----------
constexpr uint32_t SDR_SID = 0x80000701, SDR_SID_CB = 0x80000704;
enum : uint32_t {
    rSdInit = 0x8000,
    rSdSetParam = 0x8010,
    rSdGetParam = 0x8020,
    rSdSetSwitch = 0x8030,
    rSdGetSwitch = 0x8040,
    rSdSetAddr = 0x8050,
    rSdGetAddr = 0x8060,
    rSdSetCoreAttr = 0x8070,
    rSdGetCoreAttr = 0x8080,
    rSdNote2Pitch = 0x8090,
    rSdPitch2Note = 0x80A0,
    rSdProcBatch = 0x80B0,
    rSdProcBatchEx = 0x80C0,
    rSdVoiceTrans = 0x80D0,
    rSdBlockTrans = 0x80E0,
    rSdVoiceTransStatus = 0x80F0,
    rSdBlockTransStatus = 0x8100,
    rSdSetTransCallback = 0x8110,
    rSdSetIRQCallback = 0x8120,
    rSdSetEffectAttr = 0x8130,
    rSdGetEffectAttr = 0x8140,
    rSdClearEffectWorkArea = 0x8150,
    rSdSetTransIntrHandler = 0x8160,
    rSdSetSpu2IntrHandler = 0x8170,
    rSdStopTrans = 0x8180,
    rSdCleanEffectWorkArea = 0x8190,
    rSdSetEffectMode = 0x81A0,
    rSdSetEffectModeParams = 0x81B0,
    rSdProcBatch2 = 0x81C0,
    rSdProcBatchEx2 = 0x81D0,
    rSdChangeThreadPriority = 0x8F10,
    SDR_CMD_CB_INIT = 0xE620,
    SDR_CMD_CB_DEINIT = 0xE630
};
// EE callback bit flags (SdrEECBData::mode) and the numeric codes seen in the game's dispatcher (doc 3.5)
enum : uint32_t { SDR_CB_DMA0INT = 1u << 8, SDR_CB_DMA1INT = 1u << 9, SDR_CB_IRQINT = 1u << 10 };
enum : uint32_t { SDR_CODE_TRANS0 = 0xb, SDR_CODE_TRANS1 = 0xc, SDR_CODE_SPU2IRQ = 0xd };

} // namespace spu2
