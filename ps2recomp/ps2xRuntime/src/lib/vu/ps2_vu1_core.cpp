#include "runtime/ps2_vu1.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "ps2_vu1_detail.h"
#include "ps2_vu1_fast.h"
#include "ps2_vu1_ring.h"
#include "ps2_vu1_flag_retire.h"
#include "ps2_vu1_decode.h"
#include "ps2_vu1_step.h"
#include "aot/ps2_vu1_aot.h"
#include "ps2_vu1_func.h"

#include <algorithm>
#include <bit>
#include <smmintrin.h>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <vector>
#include <filesystem>
#include <limits>
#include <ps2_log.h>
#include "ps2_perf.h"
#include "../ps2_vu1_batch_capture.h"
#include "../ps2_vu1_batch_dispatch.h"

namespace {
constexpr uint8_t laneForComponent(uint32_t component) {
    return static_cast<uint8_t>(1u << (3u - component));
}
} // namespace

static void drakanVu1Dump(PS2Memory* mem, const VU1State& st, const uint8_t* data, uint32_t dataSize,
                          const uint8_t* code, uint32_t codeSize, uint32_t xg);
void ps2WatchFlush();
static thread_local uint8_t* g_dbgVuCode = nullptr;
static thread_local uint32_t g_dbgVuCodeSize = 0;
static thread_local std::vector<uint8_t> g_dbgEntryData;
static int g_dbgTrace =
    0; // plain statics: debug aids only touched by the single VU1 thread, no TLS access in the instruction loop
struct DbgRingEnt {
    uint16_t pc;
    int16_t vi[14]; // The corruption report below prints vi1 through vi14.
};
static std::vector<DbgRingEnt> g_dbgRing;
static uint64_t g_dbgRingN = 0;
static bool g_dbgRingOn = false;
static uint64_t g_dbgRunStart = 0;
static thread_local uint32_t g_dbgEntryTop = 0, g_dbgEntryItop = 0;

namespace {
// 128-bit mask with component c all ones when lane bit (1 << (3 - c)) is set
struct LaneMask128Table {
    alignas(16) uint32_t m[16][4];
    constexpr LaneMask128Table() : m() {
        for (int lanes = 0; lanes < 16; ++lanes)
            for (int c = 0; c < 4; ++c)
                m[lanes][c] = ((lanes >> (3 - c)) & 1) ? 0xFFFFFFFFu : 0u;
    }
};
constexpr LaneMask128Table kLaneMask128;
inline __m128i laneMask128(uint32_t lanes) {
    return _mm_load_si128(reinterpret_cast<const __m128i*>(kLaneMask128.m[lanes & 15u]));
}
} // namespace

VU1Interpreter::FlagPipelineEntry* VU1Interpreter::allocFlagEntry() {
    return Vu1Fast::allocFlag(*this);
}

void Vu1Fast::xgkickCatchUp(VU1Interpreter& v) {
    if (v.m_xgkick.active && spare(v).xgLast != v.m_cycle)
        v.progressXgkick();
}

void Vu1Fast::xgkickBeforeStore(VU1Interpreter& v, uint32_t address) {
    // The qwords of the PATH1 transfer that are due by now have to be read before this store changes VU memory.
    const Spare& sp = spare(v);
    if (!v.m_activeVuData || v.m_activeVuDataSize < 16u)
        return;
    const uint64_t due = (static_cast<uint64_t>(v.m_xgkick.cycleCredit) + (v.m_cycle - sp.xgLast)) / 2u;
    if (due == 0u)
        return;
    const uint32_t nq = v.m_activeVuDataSize / 16u;
    const uint32_t start = (v.m_xgkick.sourceAddress + v.m_xgkick.copiedBytes) / 16u;
    const uint32_t at = address / 16u;
    if (Vu1Ring::distance(at, start, nq) < due)
        v.progressXgkick();
}

void Vu1Fast::settleFlags(VU1Interpreter& v) {
    Spare& sp = spare(v);
    Vu1Flags::retire(v.m_flagPipeline.data(), VU1Interpreter::kMaxFlagEntries - 1u, v.m_cycle, sp.flagHead,
                     sp.flagCount, v.m_state.mac, v.m_state.status, v.m_state.clip);
}

VU1Interpreter::VU1Interpreter(Unit unit) : m_unit(unit) {
    reset();
}

void VU1Interpreter::resetScheduler() {
    m_flagMask = m_storeMask = m_vfMask = m_viMask = m_accMask = m_efuMask = 0;
    Vu1Fast::Spare& clean = Vu1Fast::spare(*this);
    if (clean.schedClean != 0u && !vu1batch::captureActive) {
        // Only the functional engine ran since the last full reset: the pipelines, ready tables and the stall ring
        // still hold their reset values (several KB that every MSCAL used to clear again).
        clean.runBase = m_cycle;
        m_minReady = ~0ull;
        m_maxRegReady = 0;
        m_xgkick.active = false;
        m_xgkick.sourceAddress = 0;
        m_xgkick.totalBytes = 0;
        m_xgkick.copiedBytes = 0;
        m_xgkick.currentTagEnd = 0;
        m_xgkick.cycleCredit = 0;
        m_xgkick.issueCycle = 0;
        m_xgkick.currentTagEop = false;
        m_nextWriteSequence = 0;
        m_efuResourceReady = 0;
        m_workingClip = m_state.clip;
        m_viBranchBackupValue = 0;
        m_viBranchBackupReg = 0;
        m_viBranchBackupValid = false;
        m_stopRequested = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
        return;
    }
    {
        Vu1Fast::Spare& sp = Vu1Fast::spare(*this);
        const uint32_t aotValid = sp.aotValid; // property of the microcode, not of the run
        const uint32_t sinkNone = sp.sinkNone;
        const uint64_t blkFast = sp.blkFast, blkSlow = sp.blkSlow,
                       blkPairs = sp.blkPairs; // statistics survive the run reset
        uint64_t why[10];
        std::memcpy(why, sp.why, sizeof(why));
        sp = {};
        std::memcpy(sp.why, why, sizeof(why));
        sp.aotValid = aotValid;
        sp.sinkNone = sinkNone;
        sp.blkFast = blkFast;
        sp.blkSlow = blkSlow;
        sp.blkPairs = blkPairs;
        sp.runBase = m_cycle;
    }
    m_minReady = ~0ull;
    m_maxRegReady = 0;
    m_flagPipeline = {};
    m_fdiv = {};
    m_efu = {};
    m_storePipeline = {};
    m_vfWritePipeline = {};
    m_viWritePipeline = {};
    m_accWritePipeline = {};
    m_xgkick.active = false;
    m_xgkick.sourceAddress = 0;
    m_xgkick.totalBytes = 0;
    m_xgkick.copiedBytes = 0;
    m_xgkick.currentTagEnd = 0;
    m_xgkick.cycleCredit = 0;
    m_xgkick.issueCycle = 0;
    m_xgkick.currentTagEop = false; // the 64 KB packet buffer is not cleared: only [0, copiedBytes) is ever read
    m_vfReady = {};
    m_viReady = {};
    m_accReady = {};
    m_vfLatestWrite = {};
    m_viLatestWrite = {};
    m_accLatestWrite = {};
    m_nextWriteSequence = 0;
    m_efuResourceReady = 0;
    m_workingClip = m_state.clip;
    m_viBranchBackupValue = 0;
    m_viBranchBackupReg = 0;
    m_viBranchBackupValid = false;
    m_stopRequested = false;
    m_pendingHaltD = false;
    m_pendingHaltT = false;
    Vu1Fast::spare(*this).schedClean = 1u;
}

void VU1Interpreter::reset() {
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f;
    m_state.q = 1.0f;
    m_state.r = 0x3F800000u;
    m_cycle = 0;
    resetScheduler();
}

float VU1Interpreter::broadcast(const float* vf, uint8_t bc) {
    return normalizeOperand(vf[bc & 3u]);
}

float VU1Interpreter::normalizeResult(float value, uint32_t& laneFlags) const {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = bits & 0x80000000u;
    const uint32_t magnitude = bits & 0x7FFFFFFFu;
    const uint32_t exponent = (bits >> 23) & 0xFFu;

    laneFlags = sign != 0u ? 0x2u : 0u;
    if (magnitude == 0u) {
        laneFlags |= 0x1u;
    } else if (exponent == 0u) {
        laneFlags |= 0x5u;
        bits = sign;
    } else if (exponent == 0xFFu) {
        laneFlags |= 0x8u;
        bits = sign | 0x7F7FFFFFu;
    }

    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t VU1Interpreter::microAddressMask() const {
    return m_unit == Unit::VU1 ? 0x3FFFu : 0x0FFFu;
}

int32_t VU1Interpreter::readBranchVi(uint8_t reg) const {
    if (reg == 0u)
        return 0;
    if (m_viBranchBackupValid && m_viBranchBackupReg == reg) {
        return m_viBranchBackupValue;
    }
    return m_state.vi[reg];
}

void VU1Interpreter::recordViWriteForBranch(uint8_t reg, int32_t oldValue) {
    if (reg == 0u)
        return;
    m_viBranchBackupValue = oldValue;
    m_viBranchBackupReg = reg;
    m_viBranchBackupValid = true;
}

void VU1Interpreter::applyDest(float* dst, const float* result, uint8_t dest) {
    if ((dest & 0xFu) == 0xFu) {
        _mm_storeu_ps(dst, _mm_loadu_ps(result));
        return;
    }
    if (dest & 0x8u)
        dst[0] = result[0];
    if (dest & 0x4u)
        dst[1] = result[1];
    if (dest & 0x2u)
        dst[2] = result[2];
    if (dest & 0x1u)
        dst[3] = result[3];
}

void VU1Interpreter::applyDestAcc(const float* result, uint8_t dest) {
    applyDest(m_upperAccDst, result, dest);
}

void VU1Interpreter::normalizeFmacResult(float* result, uint8_t dest, uint8_t laneFlags[4]) {
    for (uint32_t component = 0; component < 4u; ++component) {
        laneFlags[component] = 0u;
        if ((dest & laneForComponent(component)) == 0u)
            continue;

        {
            // Ordinary result (exponent well inside the range): the exact value cannot be zero, denormal or
            // overflowing, so the only flag is the sign. Everything else takes the long double classification below.
            uint32_t resultBits;
            std::memcpy(&resultBits, &result[component], sizeof(resultBits));
            if (((resultBits >> 23) & 0xFFu) - 8u < 239u) {
                laneFlags[component] = (resultBits >> 31) != 0u ? 0x2u : 0u;
                continue;
            }
        }

        long double exactResult = 0.0L;
        if (calculateFmacExactResult(component, exactResult)) {
            laneFlags[component] = normalizeFmacExactResult(result[component], exactResult);
            continue;
        }

        uint32_t flags = 0u;
        result[component] = normalizeResult(result[component], flags);
        laneFlags[component] = static_cast<uint8_t>(flags);
    }
}

bool VU1Interpreter::calculateFmacExactResult(uint32_t component, long double& result) const {
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);

    const auto operand = [this](float value) { return static_cast<long double>(normalizeOperand(value)); };
    const auto vs = [&](uint32_t lane) { return operand(m_state.vf[fs][lane]); };
    const auto vt = [&](uint32_t lane) { return operand(m_state.vf[ft][lane]); };
    const auto acc = [&](uint32_t lane) { return operand(m_state.acc[lane]); };

    const long double q = operand(m_state.q);
    const long double i = operand(m_state.i);

    if (op < 0x3Cu) {
        if (op <= 0x03u)
            result = vs(component) + vt(op & 3u);
        else if (op <= 0x07u)
            result = vs(component) - vt(op & 3u);
        else if (op <= 0x0Bu)
            result = acc(component) + vs(component) * vt(op & 3u);
        else if (op <= 0x0Fu)
            result = acc(component) - vs(component) * vt(op & 3u);
        else if (op >= 0x18u && op <= 0x1Bu)
            result = vs(component) * vt(op & 3u);
        else {
            switch (op) {
                case 0x1Cu:
                    result = vs(component) * q;
                    break;
                case 0x1Eu:
                    result = vs(component) * i;
                    break;
                case 0x20u:
                    result = vs(component) + q;
                    break;
                case 0x21u:
                    result = acc(component) + vs(component) * q;
                    break;
                case 0x22u:
                    result = vs(component) + i;
                    break;
                case 0x23u:
                    result = acc(component) + vs(component) * i;
                    break;
                case 0x24u:
                    result = vs(component) - q;
                    break;
                case 0x25u:
                    result = acc(component) - vs(component) * q;
                    break;
                case 0x26u:
                    result = vs(component) - i;
                    break;
                case 0x27u:
                    result = acc(component) - vs(component) * i;
                    break;
                case 0x28u:
                    result = vs(component) + vt(component);
                    break;
                case 0x29u:
                    result = acc(component) + vs(component) * vt(component);
                    break;
                case 0x2Au:
                    result = vs(component) * vt(component);
                    break;
                case 0x2Cu:
                    result = vs(component) - vt(component);
                    break;
                case 0x2Du:
                    result = acc(component) - vs(component) * vt(component);
                    break;
                case 0x2Eu: {
                    static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                    static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                    result = component == 3u ? 0.0L : acc(component) - vs(left[component]) * vt(right[component]);
                    break;
                }
                default:
                    return false;
            }
        }
        return true;
    }

    if (special <= 0x03u)
        result = vs(component) + vt(special & 3u);
    else if (special <= 0x07u)
        result = vs(component) - vt(special & 3u);
    else if (special <= 0x0Bu)
        result = acc(component) + vs(component) * vt(special & 3u);
    else if (special <= 0x0Fu)
        result = acc(component) - vs(component) * vt(special & 3u);
    else if (special >= 0x18u && special <= 0x1Bu)
        result = vs(component) * vt(special & 3u);
    else {
        switch (special) {
            case 0x1Cu:
                result = vs(component) * q;
                break;
            case 0x1Eu:
                result = vs(component) * i;
                break;
            case 0x20u:
                result = vs(component) + q;
                break;
            case 0x21u:
                result = acc(component) + vs(component) * q;
                break;
            case 0x22u:
                result = vs(component) + i;
                break;
            case 0x23u:
                result = acc(component) + vs(component) * i;
                break;
            case 0x24u:
                result = vs(component) - q;
                break;
            case 0x25u:
                result = acc(component) - vs(component) * q;
                break;
            case 0x26u:
                result = vs(component) - i;
                break;
            case 0x27u:
                result = acc(component) - vs(component) * i;
                break;
            case 0x28u:
                result = vs(component) + vt(component);
                break;
            case 0x29u:
                result = acc(component) + vs(component) * vt(component);
                break;
            case 0x2Au:
                result = vs(component) * vt(component);
                break;
            case 0x2Cu:
                result = vs(component) - vt(component);
                break;
            case 0x2Du:
                result = acc(component) - vs(component) * vt(component);
                break;
            case 0x2Eu: {
                static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                result = component == 3u ? 0.0L : vs(left[component]) * vt(right[component]);
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

uint8_t VU1Interpreter::normalizeFmacExactResult(float& value, long double exactResult) const {
    const bool negative = std::signbit(exactResult);
    const long double magnitude = std::fabs(exactResult);
    const long double maximum = static_cast<long double>(std::numeric_limits<float>::max());
    const long double minimum = static_cast<long double>(std::numeric_limits<float>::min());
    uint8_t flags = negative ? 0x2u : 0u;

    uint32_t bits = negative ? 0x80000000u : 0u;
    if (magnitude == 0.0L) {
        flags |= 0x1u;
        std::memcpy(&value, &bits, sizeof(value));
    } else if (magnitude > maximum) {
        flags |= 0x8u;
        bits |= 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
    } else if (magnitude < minimum) {
        flags |= 0x5u;
        std::memcpy(&value, &bits, sizeof(value));
    }

    return flags;
}

uint32_t VU1Interpreter::calculateFmacProductSticky(uint8_t dest) const {
    uint32_t extraSticky = 0u;
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const bool productSum = (op >= 0x08u && op <= 0x0Fu) || op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
                            op == 0x29u || op == 0x2Du || op == 0x2Eu || (special >= 0x08u && special <= 0x0Fu) ||
                            special == 0x21u || special == 0x23u || special == 0x25u || special == 0x27u ||
                            special == 0x29u || special == 0x2Du;
    if (!productSum)
        return 0u;

    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);
    for (uint32_t component = 0; component < 4u; ++component) {
        if ((dest & laneForComponent(component)) == 0u)
            continue;
        static constexpr uint8_t crossLeft[4] = {1u, 2u, 0u, 3u};
        static constexpr uint8_t crossRight[4] = {2u, 0u, 1u, 3u};
        const uint8_t leftComponent = op == 0x2Eu ? crossLeft[component] : static_cast<uint8_t>(component);
        const float left = normalizeOperand(m_state.vf[fs][leftComponent]);
        float right = 0.0f;
        if ((op >= 0x08u && op <= 0x0Fu) || (special >= 0x08u && special <= 0x0Fu)) {
            right = normalizeOperand(m_state.vf[ft][(op >= 0x08u && op <= 0x0Fu ? op : special) & 3u]);
        } else if (op == 0x21u || op == 0x25u || special == 0x21u || special == 0x25u) {
            right = normalizeOperand(m_state.q);
        } else if (op == 0x23u || op == 0x27u || special == 0x23u || special == 0x27u) {
            right = normalizeOperand(m_state.i);
        } else if (op == 0x2Eu) {
            right = normalizeOperand(m_state.vf[ft][crossRight[component]]);
        } else {
            right = normalizeOperand(m_state.vf[ft][component]);
        }

        float product = left * right;
        uint32_t productBits, leftBits, rightBits;
        std::memcpy(&productBits, &product, sizeof(productBits));
        std::memcpy(&leftBits, &left, sizeof(leftBits));
        std::memcpy(&rightBits, &right, sizeof(rightBits));
        uint8_t productFlags;
        if (((productBits >> 23) & 0xFFu) - 8u < 239u) {
            productFlags = (productBits >> 31) != 0u ? 0x2u : 0u; // ordinary product: sign only
        } else if ((leftBits & 0x7FFFFFFFu) == 0u || (rightBits & 0x7FFFFFFFu) == 0u) {
            productFlags =
                static_cast<uint8_t>(0x1u | (((leftBits ^ rightBits) >> 31) != 0u ? 0x2u : 0u)); // exact zero
        } else {
            const long double exactProduct = static_cast<long double>(left) * static_cast<long double>(right);
            productFlags = normalizeFmacExactResult(product, exactProduct);
        }
        // Product-sum instructions report Z/S/U/O from the add/subtract result
        // as current flags, while every product condition accumulates into the
        // corresponding sticky flag.
        extraSticky |= productFlags & 0xFu;
    }
    return extraSticky;
}

void VU1Interpreter::updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest, uint32_t extraSticky) {
    if (dest == 0u)
        return;

    uint32_t mac = 0u;
    uint32_t status = 0u;
    for (uint32_t component = 0; component < 4u; ++component) {
        const uint8_t lane = laneForComponent(component);
        if ((dest & lane) == 0u)
            continue;

        const uint32_t flags = laneFlags[component];
        if ((flags & 0x1u) != 0u)
            mac |= lane;
        if ((flags & 0x2u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 4;
        if ((flags & 0x4u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 8;
        if ((flags & 0x8u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 12;
        status |= flags;
    }

    FlagPipelineEntry* entry = allocFlagEntry();
    if (!entry) {
        reportReservedInstruction(true, 0xFFFFFFFFu);
        return;
    }

    *entry = {};
    entry->valid = true;
    entry->issueCycle = m_cycle;
    entry->readyCycle = m_cycle + kFmacLatency;
    entry->mac = mac;
    entry->status = status;
    entry->extraSticky = extraSticky;
    entry->writesMac = true;
    entry->writesStatus = true;
}

void VU1Interpreter::applyFmacDest(float* dst, float* result, uint8_t dest) {
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, calculateFmacProductSticky(dest));
    applyDest(dst, result, dest);
}

void VU1Interpreter::applyFmacDestAcc(float* result, uint8_t dest) {
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, calculateFmacProductSticky(dest));
    applyDestAcc(result, dest);
}

void VU1Interpreter::queueFsset(uint16_t immediate) {
    {
        const Vu1Fast::Spare& sp = Vu1Fast::spare(*this);
        for (uint32_t i = 0; i < sp.flagCount; ++i) {
            FlagPipelineEntry& entry = m_flagPipeline[(sp.flagHead + i) & (kMaxFlagEntries - 1u)];
            if (entry.issueCycle == m_cycle)
                entry.writesStatus = false;
        }
    }

    if (FlagPipelineEntry* slot = allocFlagEntry()) {
        FlagPipelineEntry& entry = *slot;
        entry = {};
        entry.valid = true;
        entry.issueCycle = m_cycle;
        entry.readyCycle = m_cycle + kFmacLatency;
        entry.status = static_cast<uint32_t>(immediate) & 0xFC0u;
        entry.writesSticky = true;
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFFEu);
}

void VU1Interpreter::queueClip(uint32_t clip) {
    m_workingClip = ((m_workingClip << 6) | (clip & 0x3Fu)) & 0xFFFFFFu;
    if (FlagPipelineEntry* slot = allocFlagEntry()) {
        FlagPipelineEntry& entry = *slot;
        entry = {};
        entry.valid = true;
        entry.issueCycle = m_cycle;
        entry.readyCycle = m_cycle + kFmacLatency;
        entry.clip = m_workingClip;
        entry.writesClip = true;
        return;
    }
    reportReservedInstruction(true, 0xFFFFFFFDu);
}

void VU1Interpreter::queueFcset(uint32_t clip) {
    m_workingClip = clip & 0xFFFFFFu;
    {
        const Vu1Fast::Spare& sp = Vu1Fast::spare(*this);
        for (uint32_t i = 0; i < sp.flagCount; ++i) {
            FlagPipelineEntry& entry = m_flagPipeline[(sp.flagHead + i) & (kMaxFlagEntries - 1u)];
            if (entry.issueCycle == m_cycle)
                entry.writesClip = false;
        }
    }
    if (FlagPipelineEntry* slot = allocFlagEntry()) {
        FlagPipelineEntry& entry = *slot;
        entry = {};
        entry.valid = true;
        entry.issueCycle = m_cycle;
        entry.readyCycle = m_cycle + kFmacLatency;
        entry.clip = m_workingClip;
        entry.writesClip = true;
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFFAu);
}

void VU1Interpreter::queueQ(float value, uint32_t latency, uint32_t statusDi) {
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    m_fdiv.valid = true;
    m_fdiv.readyCycle = m_cycle + latency;
    m_fdiv.value = value;
    m_fdiv.statusDi = statusDi & 0x30u;
    noteReady(m_fdiv.readyCycle);
}

void VU1Interpreter::queueP(float value, uint32_t latency) {
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    for (uint32_t index = 0; index < m_efu.size(); ++index) {
        ScalarPipelineEntry& entry = m_efu[index];
        if (!entry.valid) {
            entry.valid = true;
            entry.readyCycle = m_cycle + latency;
            entry.value = value;
            m_efuMask |= 1u << index;
            noteReady(entry.readyCycle);
            // EFU throughput is one cycle shorter than result visibility.
            m_efuResourceReady = m_cycle + (latency > 0u ? latency - 1u : 0u);
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF9u);
}

void VU1Interpreter::queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask) {
    Vu1Step::store(*this, address, words, laneMask);
}

void VU1Interpreter::queueVfWrite(uint8_t reg, uint8_t laneMask, const float value[4], uint32_t latency) {
    if (reg == 0u || laneMask == 0u)
        return;
    const uint32_t freeBits = ~m_vfMask & ((1u << kMaxPendingVfWrites) - 1u);
    if (freeBits != 0u) {
        const uint32_t index = std::countr_zero(freeBits);
        PendingVfWrite& write = m_vfWritePipeline[index];
        write.valid = true;
        write.readyCycle = m_cycle + latency;
        write.sequence = ++m_nextWriteSequence;
        write.reg = reg;
        write.laneMask = laneMask;
        std::copy(value, value + 4, write.value.begin());
        for (uint32_t component = 0; component < 4u; ++component) {
            if ((laneMask & laneForComponent(component)) != 0u)
                m_vfLatestWrite[reg][component] = write.sequence;
        }
        m_vfMask |= 1u << index;
        noteReady(write.readyCycle);
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFF7u);
}

void VU1Interpreter::queueViWrite(uint8_t reg, int32_t value, uint32_t latency) {
    if (reg == 0u)
        return;
    const uint32_t freeBits = ~m_viMask & ((1u << kMaxPendingViWrites) - 1u);
    if (freeBits != 0u) {
        const uint32_t index = std::countr_zero(freeBits);
        PendingViWrite& write = m_viWritePipeline[index];
        write.valid = true;
        write.readyCycle = m_cycle + latency;
        write.sequence = ++m_nextWriteSequence;
        write.reg = reg;
        write.value = value;
        m_viLatestWrite[reg] = write.sequence;
        m_viMask |= 1u << index;
        noteReady(write.readyCycle);
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFF6u);
}

void VU1Interpreter::queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency) {
    if (laneMask == 0u)
        return;
    const uint32_t freeBits = ~m_accMask & ((1u << kMaxPendingAccWrites) - 1u);
    if (freeBits != 0u) {
        const uint32_t index = std::countr_zero(freeBits);
        PendingAccWrite& write = m_accWritePipeline[index];
        write.valid = true;
        write.readyCycle = m_cycle + latency;
        write.sequence = ++m_nextWriteSequence;
        write.laneMask = laneMask;
        std::copy(value, value + 4, write.value.begin());
        for (uint32_t component = 0; component < 4u; ++component) {
            if ((laneMask & laneForComponent(component)) != 0u)
                m_accLatestWrite[component] = write.sequence;
        }
        m_accMask |= 1u << index;
        noteReady(write.readyCycle);
        return;
    }
    reportReservedInstruction(true, 0xFFFFFFF5u);
}

void VU1Interpreter::commitReadyPipelines() {
    if (m_cycle < m_minReady)
        return;

    uint64_t nextReady = ~0ull;

    if (m_fdiv.valid) {
        if (m_fdiv.readyCycle <= m_cycle) {
            Vu1Fast::settleFlags(*this); // flag results ready by now precede the Q/DI status update
            m_state.q = m_fdiv.value;
            if (vu1batch::captureActive)
                vu1batch::scalarCommitted(true);
            const uint32_t currentDi = m_fdiv.statusDi & 0x30u;
            m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
            m_fdiv.valid = false;
        } else
            nextReady = std::min(nextReady, m_fdiv.readyCycle);
    }

    for (uint32_t mask = m_efuMask; mask != 0u; mask &= mask - 1u) {
        const uint32_t index = std::countr_zero(mask);
        ScalarPipelineEntry& entry = m_efu[index];
        if (entry.readyCycle <= m_cycle) {
            m_state.p = entry.value;
            if (vu1batch::captureActive)
                vu1batch::scalarCommitted(false);
            entry.valid = false;
            m_efuMask &= ~(1u << index);
        } else
            nextReady = std::min(nextReady, entry.readyCycle);
    }

    for (uint32_t mask = m_storeMask; mask != 0u; mask &= mask - 1u) {
        const uint32_t index = std::countr_zero(mask);
        PendingStore& store = m_storePipeline[index];
        if (store.readyCycle > m_cycle) {
            nextReady = std::min(nextReady, store.readyCycle);
            continue;
        }
        if (m_activeVuData && store.address + 16u <= m_activeVuDataSize) {
            uint32_t oldWords[4]{};
            std::memcpy(oldWords, m_activeVuData + store.address, sizeof(oldWords));
            for (uint32_t component = 0; component < 4u; ++component) {
                if ((store.laneMask & laneForComponent(component)) != 0u)
                    oldWords[component] = store.words[component];
            }
            std::memcpy(m_activeVuData + store.address, oldWords, sizeof(oldWords));
        }
        m_storeMask &= ~(1u << index);
    }

    for (uint32_t mask = m_vfMask; mask != 0u; mask &= mask - 1u) {
        const uint32_t index = std::countr_zero(mask);
        PendingVfWrite& write = m_vfWritePipeline[index];
        if (write.readyCycle > m_cycle) {
            nextReady = std::min(nextReady, write.readyCycle);
            continue;
        }
        for (uint32_t component = 0; component < 4u; ++component) {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_vfLatestWrite[write.reg][component] == write.sequence) {
                m_state.vf[write.reg][component] = write.value[component];
            }
        }
        m_vfMask &= ~(1u << index);
    }

    for (uint32_t mask = m_viMask; mask != 0u; mask &= mask - 1u) {
        const uint32_t index = std::countr_zero(mask);
        PendingViWrite& write = m_viWritePipeline[index];
        if (write.readyCycle > m_cycle) {
            nextReady = std::min(nextReady, write.readyCycle);
            continue;
        }
        if (m_viLatestWrite[write.reg] == write.sequence)
            m_state.vi[write.reg] = static_cast<int16_t>(write.value);
        m_viMask &= ~(1u << index);
    }

    for (uint32_t mask = m_accMask; mask != 0u; mask &= mask - 1u) {
        const uint32_t index = std::countr_zero(mask);
        PendingAccWrite& write = m_accWritePipeline[index];
        if (write.readyCycle > m_cycle) {
            nextReady = std::min(nextReady, write.readyCycle);
            continue;
        }
        for (uint32_t component = 0; component < 4u; ++component) {
            if ((write.laneMask & laneForComponent(component)) != 0u && m_accLatestWrite[component] == write.sequence) {
                m_state.acc[component] = write.value[component];
            }
        }
        m_accMask &= ~(1u << index);
    }

    m_minReady = nextReady;
}

void VU1Interpreter::progressXgkick() {
    if (!m_xgkick.active || !m_activeVuData || m_activeVuDataSize == 0u)
        return;

    // one cycle of credit per cycle that passed since the progress was last accounted (1 when called every cycle)
    Vu1Fast::Spare& xsp = Vu1Fast::spare(*this);
    m_xgkick.cycleCredit += static_cast<uint32_t>(m_cycle - xsp.xgLast);
    xsp.xgLast = m_cycle;
    while (m_xgkick.active && m_xgkick.cycleCredit >= 2u) {
        uint32_t copyBytes = 16u;
        if (m_xgkick.currentTagEnd != 0u && m_activeVuDataSize >= 16u &&
            (m_activeVuDataSize & (m_activeVuDataSize - 1u)) == 0u) {
            const uint32_t dueQwords = m_xgkick.cycleCredit / 2u;
            const uint32_t tagQwords = (m_xgkick.currentTagEnd - m_xgkick.copiedBytes) / 16u;
            copyBytes = std::min(dueQwords, tagQwords) * 16u;
        }
        m_xgkick.cycleCredit -= (copyBytes / 16u) * 2u;
        if (m_xgkick.copiedBytes > XgkickPipeline::kBufferSize - 16u) {
            reportReservedInstruction(false, 0xFFFFFFFBu);
            m_xgkick.active = false;
            return;
        }

        const uint32_t qwordOffset = m_xgkick.copiedBytes;
        const uint32_t firstSource = Vu1Ring::wrap(m_xgkick.sourceAddress + m_xgkick.copiedBytes, m_activeVuDataSize);
        if (copyBytes > 16u) {
            Vu1Ring::copyPayload(m_xgkick.packet.data() + m_xgkick.copiedBytes, m_activeVuData, m_activeVuDataSize,
                                 firstSource, copyBytes);
        } else if (firstSource + 16u <= m_activeVuDataSize) {
            std::memcpy(m_xgkick.packet.data() + m_xgkick.copiedBytes, m_activeVuData + firstSource, 16u);
        } else {
            for (uint32_t i = 0; i < 16u; ++i) {
                const uint32_t source = (m_xgkick.sourceAddress + m_xgkick.copiedBytes + i) % m_activeVuDataSize;
                m_xgkick.packet[m_xgkick.copiedBytes + i] = m_activeVuData[source];
            }
        }
        if (vu1batch::captureActive)
            vu1batch::packetRead(m_activeMemory, firstSource, copyBytes, m_xgkick.copiedBytes,
                                 m_xgkick.currentTagEnd == 0u);
        m_xgkick.copiedBytes += copyBytes;

        if (m_xgkick.currentTagEnd == 0u) {
            uint64_t tagLo = 0;
            std::memcpy(&tagLo, m_xgkick.packet.data() + qwordOffset, sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint32_t format = static_cast<uint32_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t tagBytes = 16u;
            if (format == 0u)
                tagBytes += static_cast<uint64_t>(nloop) * nreg * 16u;
            else if (format == 1u)
                tagBytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
            else if (format == 2u)
                tagBytes += static_cast<uint64_t>(nloop) * 16u;
            else {
                if (std::getenv("PS2X_VU1_DEBUG_TAG"))
                    std::fprintf(
                        stderr,
                        "[vu1] XGKICK src qw=%u byte=%u offset=%u tag lo=%016llx vi=%d %d %d %d %d %d %d %d %d\n",
                        m_xgkick.sourceAddress / 16u, m_xgkick.sourceAddress, qwordOffset, (unsigned long long)tagLo,
                        m_state.vi[1], m_state.vi[2], m_state.vi[3], m_state.vi[4], m_state.vi[5], m_state.vi[6],
                        m_state.vi[7], m_state.vi[8], m_state.vi[9]);
                if (std::getenv("PS2X_VU1_DEBUG_TAG") && g_dbgVuCode)
                    drakanVu1Dump(m_activeMemory, m_state, m_activeVuData, m_activeVuDataSize, g_dbgVuCode,
                                  g_dbgVuCodeSize, m_xgkick.sourceAddress / 16u);
                reportReservedInstruction(false, 0xFFFFFFF8u);
                m_xgkick.active = false;
                return;
            }

            if (tagBytes > XgkickPipeline::kBufferSize - qwordOffset) {
                reportReservedInstruction(false, 0xFFFFFFFBu);
                m_xgkick.active = false;
                return;
            }
            m_xgkick.currentTagEnd = qwordOffset + static_cast<uint32_t>(tagBytes);
            m_xgkick.currentTagEop = ((tagLo >> 15) & 1u) != 0u;
            if (m_xgkick.currentTagEop)
                m_xgkick.totalBytes = m_xgkick.currentTagEnd;
        }

        if (m_xgkick.copiedBytes >= m_xgkick.currentTagEnd) {
            if (m_xgkick.currentTagEop)
                finishXgkick();
            else {
                // The next transferred qword is another GIFtag.
                m_xgkick.currentTagEnd = 0u;
                m_xgkick.currentTagEop = false;
            }
        }
    }
}

void VU1Interpreter::finishXgkick() {
    if (!m_xgkick.active)
        return;

    if (m_packetTap)
        m_packetTap->emplace_back(m_xgkick.packet.data(), m_xgkick.packet.data() + m_xgkick.totalBytes);
    if (Vu1Fast::spare(*this).sinkNone) {
    } else if (m_activeMemory)
        m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
    else if (m_activeGs)
        m_activeGs->processGIFPacket(m_xgkick.packet.data(), m_xgkick.totalBytes);
    m_xgkick.active = false;
}

// Drakan debug: PS2X_VU1_DUMP="<after seconds>,<count>[,<dir>]" writes vu1dump_<n>.bin at the XGKICK of the first
// <count> programs started after <after> s: u32 magic, u32 pc, u32 dataSize, u32 codeSize, vf[32][4], vi[16], data,
// code.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
static void drakanVu1Dump(PS2Memory* mem, const VU1State& st, const uint8_t* data, uint32_t dataSize,
                          const uint8_t* code, uint32_t codeSize, uint32_t xg) {
    static const char* env = std::getenv("PS2X_VU1_DUMP");
    if (!env)
        return;
    static double after = 0;
    static int count = 0;
    static std::string dir = ".";
    static bool init = false;
    static int done = 0;
    static const auto t0 = std::chrono::steady_clock::now();
    if (!init) {
        init = true;
        std::string e(env);
        after = std::atof(e.c_str());
        size_t c = e.find(',');
        if (c != std::string::npos) {
            count = std::atoi(e.c_str() + c + 1);
            c = e.find(',', c + 1);
            if (c != std::string::npos)
                dir = e.substr(c + 1);
        }
    }
    if (std::getenv("PS2X_VU1_DUMP_BAD") &&
        (uint64_t(xg) + 5) * 16 <=
            dataSize) { // only packets whose first XYZ2 is the degenerate (x=0x8000, y=0/0xffff) vertex
        uint64_t q[2];
        std::memcpy(q, data + (xg + 4) * 16, 16);
        const uint32_t x = uint32_t(q[0] & 0xffff), y = uint32_t((q[0] >> 32) & 0xffff);
        if (!(x == 0x8000 && (y == 0 || y == 0xffff)))
            return;
    }
    if (done >= count || std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < after)
        return;
    std::string path = dir + "/vu1dump_" + std::to_string(done++) + ".bin";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        uint32_t hdr[5] = {0x31555644u, st.pc, dataSize, codeSize, xg};
        std::fwrite(hdr, 4, 5, f);
        std::fwrite(st.vf, 4, 128, f);
        std::fwrite(st.vi, 4, 16, f);
        std::fwrite(data, 1, dataSize, f);
        std::fwrite(code, 1, codeSize, f);
        uint32_t ex[2] = {g_dbgEntryTop, g_dbgEntryItop};
        std::fwrite(ex, 4, 2, f);
        std::fwrite(g_dbgEntryData.data(), 1, g_dbgEntryData.size(), f);
        std::fclose(f);
    }
    if (std::getenv("PS2X_WATCH_RING") && done == 1)
        ps2WatchFlush();
    if (mem && std::getenv("PS2X_VU1_DUMP_SCAN") &&
        dataSize >= 48) { // where in guest RAM do the VU1 qw0..2 (view-projection rows) come from?
        const uint8_t* r = mem->getRDRAM();
        int hits = 0;
        for (uint32_t a = 0; a + 48 <= 32u * 1024 * 1024 && hits < 6; a += 4)
            if (std::memcmp(r + a, data, 48) == 0) {
                std::fprintf(stderr, "[vu1-dump %d] qw0-2 found in RDRAM at 0x%08x\n", done - 1, a);
                ++hits;
                if (hits == 1 &&
                    done == 1) { // other places holding the same row0.x value (the transposed source matrix)
                    uint32_t w0;
                    std::memcpy(&w0, data, 4);
                    int n2 = 0;
                    for (uint32_t b = 0; b + 4 <= 32u * 1024 * 1024 && n2 < 12; b += 4)
                        if (b != a && std::memcmp(r + b, &w0, 4) == 0) {
                            std::fprintf(stderr, "[vu1-dump]   value %08x also at %08x\n", w0, b);
                            ++n2;
                            if (b < 0x700000u)
                                for (int q = -10; q < 14; ++q) {
                                    float f4[4];
                                    uint32_t u4[4];
                                    const uint32_t qa = (b & ~15u) + q * 16;
                                    std::memcpy(f4, r + qa, 16);
                                    std::memcpy(u4, r + qa, 16);
                                    std::fprintf(stderr, "[vu1-dump]      %08x: %g %g %g %g  (%08x %08x %08x %08x)\n",
                                                 qa, f4[0], f4[1], f4[2], f4[3], u4[0], u4[1], u4[2], u4[3]);
                                }
                        }
                }
                if (hits == 1 && done == 1)
                    for (int q = -12; q < 20; ++q) {
                        float f4[4];
                        uint32_t u4[4];
                        std::memcpy(f4, r + a + q * 16, 16);
                        std::memcpy(u4, r + a + q * 16, 16);
                        std::fprintf(stderr, "[vu1-dump]   %08x: %g %g %g %g  (%08x %08x %08x %08x)\n", a + q * 16,
                                     f4[0], f4[1], f4[2], f4[3], u4[0], u4[1], u4[2], u4[3]);
                    }
            }
    }
}

void VU1Interpreter::startXgkick(uint32_t qwordAddress) {
    if (m_unit != Unit::VU1 || !m_activeVuData || m_activeVuDataSize < 16u)
        return;

    const uint32_t sourceAddress = (qwordAddress * 16u) % m_activeVuDataSize;
    static const bool dumpOnlyErr = std::getenv("PS2X_VU1_DUMP_ONLYERR") != nullptr;
    if (g_dbgVuCode && !dumpOnlyErr)
        drakanVu1Dump(m_activeMemory, m_state, m_activeVuData, m_activeVuDataSize, g_dbgVuCode, g_dbgVuCodeSize,
                      qwordAddress);
    m_xgkick.active = false;
    m_xgkick.sourceAddress = 0;
    m_xgkick.totalBytes = 0;
    m_xgkick.copiedBytes = 0;
    m_xgkick.currentTagEnd = 0;
    m_xgkick.cycleCredit = 0;
    m_xgkick.issueCycle = 0;
    m_xgkick.currentTagEop = false; // the 64 KB packet buffer is not cleared: only [0, copiedBytes) is ever read
    m_xgkick.active = true;
    m_xgkick.sourceAddress = sourceAddress;
    m_xgkick.cycleCredit = 1u; // XGKICK's issue cycle counts toward PATH1.
    m_xgkick.issueCycle = m_cycle;
    Vu1Fast::spare(*this).xgLast = m_cycle;
}

void VU1Interpreter::advanceOneCycle() {
    ++m_cycle;
    m_state.cycles = m_cycle;
    Vu1Fast::advanceBusy(Vu1Fast::spare(*this), m_cycle);
    // LSU commits become visible at the cycle boundary before PATH1 consumes
    // its next qword from VU memory.
    if (m_cycle >= m_minReady)
        commitReadyPipelines();
    if (m_xgkick.active)
        progressXgkick();
}

void VU1Interpreter::advanceTo(uint64_t targetCycle) {
    while (m_cycle < targetCycle)
        advanceOneCycle();
}

bool VU1Interpreter::pipelinesPending() const {
    return m_fdiv.valid || m_xgkick.active || Vu1Fast::spare(*this).flagCount != 0u ||
           Vu1Fast::spare(*this).storeReady > m_cycle ||
           (m_efuMask | m_storeMask | m_vfMask | m_viMask | m_accMask) != 0u;
}

void VU1Interpreter::flushPipelines() {
    for (;;) {
        Vu1Fast::xgkickCatchUp(*this);
        Vu1Fast::settleFlags(*this);
        if (!pipelinesPending())
            break;
        advanceOneCycle();
    }
}

uint64_t VU1Interpreter::calculatePairReadyCycle(const DecodedInstructionPair& decoded) const {
    uint64_t ready = m_cycle;
    if (decoded.lowerUsage.pipeline == PipelineFdiv && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.pipeline == PipelineEfu)
        ready = std::max(ready, m_efuResourceReady);
    if (decoded.lowerUsage.waitQ && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.waitP) {
        for (const ScalarPipelineEntry& entry : m_efu)
            if (entry.valid)
                ready = std::max(ready, entry.readyCycle);
    }
    if (decoded.lowerUsage.pipeline == PipelineXgkick && m_xgkick.active)
        ready = std::max(ready, m_cycle + 1u);
    return ready;
}

void VU1Interpreter::markPairWrites(const DecodedInstructionPair& decoded) {
    Vu1Fast::scheduleWrites(Vu1Fast::spare(*this), decoded, m_cycle);
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeUpperUsage(uint32_t upper) const {
    return Vu1Decode::decodeUpperUsage(upper);
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeLowerUsage(uint32_t lower) const {
    return Vu1Decode::decodeLowerUsage(lower, m_unit == Unit::VU1);
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::decodeInstructionPair(const uint8_t* vuCode, uint32_t pc) const {
    uint32_t lower, upper;
    std::memcpy(&lower, vuCode + pc, sizeof(lower));
    std::memcpy(&upper, vuCode + pc + sizeof(lower), sizeof(upper));
    return Vu1Decode::decodePair(lower, upper, m_unit == Unit::VU1);
}

void VU1Interpreter::rebuildDecodedCodeCache(const uint8_t* vuCode, uint32_t codeSize, const PS2Memory* memory,
                                             uint64_t generation) {
    const uint32_t pairCount = std::min<uint32_t>(codeSize / 8u, kMaxDecodedPairs);
    for (uint32_t i = 0; i < pairCount; ++i)
        m_decodedCodeCache[i] = decodeInstructionPair(vuCode, i * 8u);

    m_cachedVuCode = vuCode;
    m_cachedMemory = memory;
    m_cachedCodeSize = codeSize;
    m_cachedCodeGeneration = generation;
    m_decodedCodeCacheValid = true;
}

const VU1Interpreter::DecodedInstructionPair*
VU1Interpreter::prepareDecodedCache(const uint8_t* vuCode, uint32_t codeSize, PS2Memory* memory) {
    const bool trackedVu1Code = memory != nullptr && ((m_unit == Unit::VU1 && vuCode == memory->getVU1Code()) ||
                                                      (m_unit == Unit::VU0 && vuCode == memory->getVU0Code()));
    if (!trackedVu1Code)
        return nullptr;

    const uint64_t generation = m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
    if (!m_decodedCodeCacheValid || m_cachedVuCode != vuCode || m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize || m_cachedCodeGeneration != generation) {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
        Vu1Fast::spare(*this).aotValid =
            (m_unit == Unit::VU1 && Vu1Aot::enabled() && Vu1Aot::imageMatches(vuCode, codeSize)) ? 1u : 0u;
    }
    return m_decodedCodeCache.data();
}

const VU1Interpreter::DecodedInstructionPair& VU1Interpreter::getDecodedInstructionPairForPc(const uint8_t* vuCode,
                                                                                             uint32_t codeSize,
                                                                                             PS2Memory* memory,
                                                                                             uint32_t pc) {
    if ((pc & 7u) != 0u) {
        m_adhocDecoded = decodeInstructionPair(vuCode, pc);
        return m_adhocDecoded;
    }

    const bool trackedVu1Code = memory != nullptr && ((m_unit == Unit::VU1 && vuCode == memory->getVU1Code()) ||
                                                      (m_unit == Unit::VU0 && vuCode == memory->getVU0Code()));
    if (!trackedVu1Code) {
        m_adhocDecoded = decodeInstructionPair(vuCode, pc);
        return m_adhocDecoded;
    }

    const uint64_t generation = m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
    if (!m_decodedCodeCacheValid || m_cachedVuCode != vuCode || m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize || m_cachedCodeGeneration != generation) {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
    }
    const uint32_t pairIndex = pc / 8u;
    if (pairIndex >= kMaxDecodedPairs) {
        m_adhocDecoded = decodeInstructionPair(vuCode, pc);
        return m_adhocDecoded;
    }
    return m_decodedCodeCache[pairIndex];
}

void VU1Interpreter::reportReservedInstruction(bool upper, uint32_t instruction) {
    RUNTIME_ERROR("[VU" << (m_unit == Unit::VU1 ? "1" : "0") << " reserved " << (upper ? "upper" : "lower")
                        << "] cycle=" << m_cycle << " pc=0x" << std::hex << m_state.pc << " instruction=0x"
                        << instruction << std::dec << '\n');
    m_stopRequested = true;
}

namespace {
// Drakan diagnostics: PS2X_VU1_STATS=1 prints the wall time spent inside the VU1 interpreter at exit.
struct Vu1Stats {
    std::atomic<uint64_t> ns{0}, calls{0}, cycles{0};
    bool enabled = std::getenv("PS2X_VU1_STATS") != nullptr;
    ~Vu1Stats() {
        if (enabled)
            std::fprintf(stderr, "[vu1-stats] %llu runs, %.2f s inside the interpreter, %llu cycles\n",
                         (unsigned long long)calls.load(), ns.load() * 1e-9, (unsigned long long)cycles.load());
    }
};
Vu1Stats g_vu1Stats;
} // namespace

void VU1Interpreter::execute(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs,
                             PS2Memory* memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t maxCycles) {
    struct StatScope {
        std::chrono::steady_clock::time_point t0{};
        VU1Interpreter* self;
        uint64_t c0;
        StatScope(VU1Interpreter* s) : self(s), c0(s->m_state.cycles) {
            if (g_vu1Stats.enabled) // no clock reads per run unless PS2X_VU1_STATS is set
                t0 = std::chrono::steady_clock::now();
        }
        ~StatScope() {
            if (!g_vu1Stats.enabled)
                return;
            g_vu1Stats.ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                    .count();
            if ((++g_vu1Stats.calls % 20000) == 0)
                std::fprintf(stderr, "[vu1-stats] %llu runs, %.2f s inside the interpreter, %llu cycles\n",
                             (unsigned long long)g_vu1Stats.calls.load(), g_vu1Stats.ns.load() * 1e-9,
                             (unsigned long long)g_vu1Stats.cycles.load());
            g_vu1Stats.cycles += self->m_state.cycles - c0;
        }
    } statScope(this);
    { // Drakan debug: PS2X_VU1_TRACE_FLAG=<hex> traces the first run (up to 6000 instructions) whose VU data [7].x
      // equals that value
        static const char* tf = std::getenv("PS2X_VU1_TRACE_FLAG");
        static bool done = false;
        if (tf && !done && m_unit == Unit::VU1 && dataSize >= 128) {
            uint32_t fl;
            std::memcpy(&fl, vuData + 7 * 16, 4);
            if (fl == (uint32_t)std::strtoul(tf, nullptr, 16)) {
                done = true;
                g_dbgTrace = 6000;
                std::fprintf(stderr, "[vu1-trace] start pc=%u flags=%08x header9=%u,%u,%u,%u q43=%u,%u,%u\n", startPC,
                             fl, ((uint32_t*)vuData)[36], ((uint32_t*)vuData)[37], ((uint32_t*)vuData)[38],
                             ((uint32_t*)vuData)[39], ((uint32_t*)vuData)[172], ((uint32_t*)vuData)[173],
                             ((uint32_t*)vuData)[174]);
            }
        }
    }
    static const bool dbgDump = std::getenv("PS2X_VU1_DUMP") != nullptr;
    if (m_unit == Unit::VU1 && dbgDump) {
        g_dbgEntryData.assign(vuData, vuData + dataSize);
        g_dbgEntryTop = top;
        g_dbgEntryItop = itop;
    }
    { // Drakan debug: PS2X_VU1_CHECK=<dir>: detect the first run after which the GIF tag templates in VU data qw 11/12
      // are corrupted
        static const char* chkDir = std::getenv("PS2X_VU1_CHECK");
        static uint64_t runNo = 0;
        static bool reported = false;
        static std::vector<uint8_t> prevEntry;
        static uint32_t prevPc = 0, prevTop = 0, prevItop = 0;
        if (chkDir && m_unit == Unit::VU1 && dataSize >= 16 * 16) {
            if (!g_dbgRingOn) {
                g_dbgRing.resize(1u << 22);
                g_dbgRingOn = true;
            }
            static bool ringDone = false;
            (void)ringDone;
            ++runNo;
            uint32_t y11, y12;
            std::memcpy(&y11, vuData + 11 * 16 + 4, 4);
            std::memcpy(&y12, vuData + 12 * 16 + 4, 4);
            const bool ok = (y11 >> 28) == 4u && (y12 >> 28) == 4u;
            if (!ok && !reported && runNo > 1 && !prevEntry.empty()) {
                reported = true;
                std::fprintf(stderr,
                             "[vu1-check] templates corrupted at run #%llu; previous run started at pc=%u top=%u "
                             "itop=%u (y11=%08x y12=%08x)\n",
                             (unsigned long long)runNo, prevPc, prevTop, prevItop, y11, y12);
                { // dump the instruction trace of the previous run (jumps only)
                    const uint64_t n =
                        g_dbgRingN - g_dbgRunStart > g_dbgRing.size() ? g_dbgRing.size() : g_dbgRingN - g_dbgRunStart;
                    std::fprintf(stderr, "[vu1-ring] previous run executed %llu instructions\n",
                                 (unsigned long long)(g_dbgRingN - g_dbgRunStart));
                    uint64_t shown = 0;
                    uint16_t last = 0xffff;
                    for (uint64_t k = 0; k < n && shown < 4000; ++k) {
                        const DbgRingEnt& e = g_dbgRing[(g_dbgRunStart + k) % g_dbgRing.size()];
                        if (k == 0 || e.pc != static_cast<uint16_t>(last + 8)) {
                            std::fprintf(stderr,
                                         "[vu1-ring] #%llu pc=%04x vi1=%d vi2=%d vi6=%d vi7=%d vi9=%d vi10=%d vi11=%d "
                                         "vi12=%d vi13=%d vi14=%d\n",
                                         (unsigned long long)k, e.pc, e.vi[0], e.vi[1], e.vi[5], e.vi[6], e.vi[8],
                                         e.vi[9], e.vi[10], e.vi[11], e.vi[12], e.vi[13]);
                            ++shown;
                        }
                        last = e.pc;
                    }
                }
                std::string path = std::string(chkDir) + "/vu1corrupt.bin";
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    uint32_t hdr[4] = {prevPc, dataSize, codeSize, static_cast<uint32_t>(runNo)};
                    std::fwrite(hdr, 4, 4, f);
                    std::fwrite(prevEntry.data(), 1, prevEntry.size(), f);
                    std::fwrite(vuData, 1, dataSize, f);
                    std::fwrite(vuCode, 1, codeSize, f);
                    std::fclose(f);
                }
            }
            if (ok) {
                prevEntry.assign(vuData, vuData + dataSize);
                prevPc = startPC;
                prevTop = top;
                prevItop = itop;
                g_dbgRunStart = g_dbgRingN;
            }
        }
    }
    resetScheduler();
    m_state.pc = startPC & microAddressMask();
    m_state.ebit = false;
    m_state.haltAfterDelaySlot = false;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    m_state.top = top;
    m_state.itop = itop;
    m_state.branchPending = false;
    m_state.branchTarget = 0;
    m_state.branchDelay = 0;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::resume(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs,
                            PS2Memory* memory, uint32_t top, uint32_t itop, uint32_t maxCycles) {
    struct StatScope {
        std::chrono::steady_clock::time_point t0{};
        VU1Interpreter* self;
        uint64_t c0;
        StatScope(VU1Interpreter* s) : self(s), c0(s->m_state.cycles) {
            if (g_vu1Stats.enabled)
                t0 = std::chrono::steady_clock::now();
        }
        ~StatScope() {
            if (!g_vu1Stats.enabled)
                return;
            g_vu1Stats.ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                    .count();
            ++g_vu1Stats.calls;
            g_vu1Stats.cycles += self->m_state.cycles - c0;
        }
    } statScope(this);
    m_state.top = top;
    m_state.itop = itop;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::run(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs,
                         PS2Memory* memory, uint32_t maxCycles) {
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    g_dbgVuCode = vuCode;
    g_dbgVuCodeSize = codeSize;
    m_activeGs = &gs;
    m_activeMemory = memory;

    // VU arithmetic rounds toward zero. Native SSE switches MXCSR; WebAssembly
    // uses the explicit arithmetic helpers because its rounding mode is fixed.
    const Vu1Simd::ScopedRounding rounding;
    const uint64_t budgetEnd = m_cycle + maxCycles;
    bool programEnded = false;
    // Microcode is only modified between runs, so the decoded-pair cache is validated once per run.
    const DecodedInstructionPair* const decodedCache = prepareDecodedCache(vuCode, codeSize, memory);
    commitReadyPipelines(); // results of a previous (resumed) run that are due at the current cycle
    Vu1Step::Ctx stepCtx{budgetEnd, vuData, dataSize, &gs, memory, codeSize, false, 0};
    bool stopped = false;
    const bool track = vu1batch::captureActive && vu1batch::tracking();
    // Functional engine for the compiled image (ps2_vu1_func.h); it leaves no pipeline pending, so consecutive runs
    // stay on it. Code it does not cover continues in the loops below.
    if (decodedCache != nullptr && Vu1Fast::spare(*this).aotValid != 0u && !vu1batch::captureActive &&
        Vu1Func::enabled() && Vu1Func::canEnter(*this))
        stopped = Vu1Func::run(*this, stepCtx) != Vu1Func::Result::Fallback;
    if (!stopped)
        Vu1Fast::spare(*this).schedClean = 0u; // the exact engine runs: the next reset clears everything
    if (!stopped && decodedCache != nullptr && Vu1Fast::spare(*this).aotValid != 0u) {
        // Microcode that equals the compiled image: one specialised function per pair
        while (m_cycle < budgetEnd && !m_stopRequested) {
            const uint32_t pc = m_state.pc;
            if ((pc & 7u) != 0u || (pc >> 3) >= Vu1Aot::kPairs)
                break; // outside the compiled image: generic loop below
            if (!(track ? vu1batch::dispatch(*this, stepCtx) : Vu1Aot::g_dispatch[pc >> 3](*this, stepCtx))) {
                stopped = true;
                break;
            }
        }
    }
    while (!stopped && m_cycle < budgetEnd && !m_stopRequested) {
        if (track)
            vu1batch::unsupportedTrace();
        if (g_dbgRingOn) {
            DbgRingEnt& e = g_dbgRing[g_dbgRingN++ % g_dbgRing.size()];
            e.pc = static_cast<uint16_t>(m_state.pc);
            for (size_t k = 0; k < std::size(e.vi); ++k)
                e.vi[k] = static_cast<int16_t>(m_state.vi[k + 1]);
        }
        if (g_dbgTrace > 0) {
            --g_dbgTrace;
            std::fprintf(stderr,
                         "[vu1-trace] pc=%04x cyc=%llu vi1=%d vi2=%d vi6=%d vi7=%d vi9=%d vi10=%d vi11=%d vi12=%d "
                         "vi13=%d vi14=%d\n",
                         m_state.pc, (unsigned long long)m_cycle, m_state.vi[1], m_state.vi[2], m_state.vi[6],
                         m_state.vi[7], m_state.vi[9], m_state.vi[10], m_state.vi[11], m_state.vi[12], m_state.vi[13],
                         m_state.vi[14]);
        }
        if (m_state.pc + 8u > codeSize)
            break;

        const DecodedInstructionPair& decoded =
            (decodedCache != nullptr && (m_state.pc & 7u) == 0u && (m_state.pc >> 3) < kMaxDecodedPairs)
                ? decodedCache[m_state.pc >> 3]
                : getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);
        if (!Vu1Step::step(*this, decoded, stepCtx))
            break;
    }
    programEnded = stepCtx.programEnded;

    if (!vu1batch::captureActive || !vu1batch::capturePairs(stepCtx.pairs))
        ps2perf::g_vu1Pairs.fetch_add(stepCtx.pairs, std::memory_order_relaxed);
    Vu1Fast::settleFlags(*this);
    if (programEnded) {
        flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
    m_state.cycles = m_cycle;
    const auto& pending = Vu1Fast::spare(*this).pending;
    if (vu1batch::captureActive)
        vu1batch::runFinished(programEnded, !pipelinesPending() && !(pending.lo | pending.hi | pending.va));
}

// Native equivalent of fixed-image pairs 55..59. No observer can sample the
// intermediate stores: pending scalar/register work and active PATH1 retain the
// existing scheduled loop. Guest cycle and branch state are committed exactly.
bool Vu1Fast::nativeFill(VU1Interpreter& v, uint8_t* data, uint32_t dataSize, uint64_t budgetEnd,
                         uint32_t maxIterations, uint32_t& pairs) {
    pairs = 0;
    auto& st = v.m_state;
    auto& sp = spare(v);
    if (v.m_unit != VU1Interpreter::Unit::VU1 || !data || dataSize != 16384u || data != v.m_activeVuData ||
        dataSize != v.m_activeVuDataSize || v.m_minReady != ~0ull || v.m_xgkick.active ||
        (sp.pending.lo | sp.pending.hi | sp.pending.va) != 0 || st.pc != 55u * 8u || st.ebit || st.haltAfterDelaySlot ||
        st.branchPending || st.branchDelay != 0 || st.vi[0] != 0 || budgetEnd <= v.m_cycle)
        return false;
    constexpr uint32_t zero[4] = {0, 0, 0, 0x3f800000u};
    if (std::memcmp(st.vf[0], zero, sizeof zero))
        return false;
    const uintptr_t x = reinterpret_cast<uintptr_t>(data), y = reinterpret_cast<uintptr_t>(&v);
    if (x <= y ? y - x < dataSize : x - y < sizeof v)
        return false;
    const uint64_t base = v.m_cycle;
    const int16_t firstAfter = static_cast<int16_t>(uint16_t(st.vi[2]) - 1u);
    const uint32_t untilExit = firstAfter > 0 ? uint32_t(firstAfter) + 1u : 1u;
    const uint64_t allowed = (budgetEnd - base - 1u) / 5u;
    const uint32_t limit = uint32_t(std::min<uint64_t>(std::min(maxIterations, untilExit), allowed));
    if (!limit)
        return false;
    const __m128i payload = _mm_loadu_si128(reinterpret_cast<const __m128i*>(st.vf[11]));
    uint32_t address = (uint32_t(uint16_t(st.vi[7])) * 16u) & 16383u;
    uint32_t done = 0;
    for (; done < limit; ++done) {
        if (v.m_stopRequested)
            break;
        _mm_storeu_si128(reinterpret_cast<__m128i*>(data + address), payload);
        address = (address + 16u) & 16383u;
    }
    if (!done)
        return true; // main run observes the requested stop
    st.vi[7] = static_cast<int16_t>(st.vi[7] + done);
    st.vi[2] = static_cast<int16_t>(st.vi[2] - done);
    if (done > 1u || st.vi[2] > 0)
        st.branchTarget = 55u * 8u;
    st.branchPending = false;
    st.branchDelay = 0;
    st.pc = (st.vi[2] > 0 ? 55u : 60u) * 8u;
    v.m_cycle = base + uint64_t(done) * 5u;
    st.cycles = v.m_cycle;
    v.m_viBranchBackupValid = false;
    sp.storeReady = base + uint64_t(done - 1u) * 5u + 1u;
    retireBlockEntry(sp, base, done * 5u);
    pairs = done * 5u;
    if (Vu1Aot::g_blockStats) {
        sp.blkFast += done;
        sp.blkPairs += pairs;
    }
    return true;
}
