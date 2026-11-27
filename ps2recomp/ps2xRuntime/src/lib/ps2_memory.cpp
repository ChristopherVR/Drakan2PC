#include "ps2_perf.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1_async.h"
#include "runtime/ps2_chain_buffers.h"
#include "drakan_object_motion.h"
#include "ps2_vu1_batch_capture.h"
#include "vu/ps2_vu1_gpu_live.h"
#include "runtime/ps2_address.h"
#include "runtime/ps2_hw_registers.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_capture.h"
#include "gs/gs_direct_path.h"
#include "ps2_log.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <new>
#include <thread>

#include "runtime/ps2_render_queue.h"
#include "runtime/ps2_readback.h"
#if defined(PS2X_HAVE_GSR)
#include "drakan_dlss.h"
#endif

namespace {
// Keep the general dispatch available for comparison with the scratchpad and peripheral shortcuts.
const bool g_terrainBusFast = [] {
    const char* value = std::getenv("PS2X_TERRAIN_BUS_FAST");
    if (value && std::strcmp(value, "0") == 0)
        return false;
    std::fputs("[terrain-bus] direct dispatch active\n", stderr);
    return true;
}();

// The throw (a string built per call) is kept out of line so the check itself inlines into every access: the EE's
// scratchpad and I/O accesses go through here, and the out-of-line check was about 3 % of the EE thread's samples in
// Wartok Canyons.
#if defined(_MSC_VER)
[[noreturn]] __declspec(noinline)
#else
[[noreturn]] __attribute__((noinline))
#endif
void outOfRange(const char* op, uint32_t address) {
    throw std::runtime_error(std::string(op) + " out-of-bounds at address: 0x" + std::to_string(address));
}
inline void inRange(uint32_t offset, size_t bytes, size_t regionSize, const char* op, uint32_t address) {
    if (static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes) > static_cast<uint64_t>(regionSize)) [[unlikely]]
        outOfRange(op, address);
}

template <typename T>
inline T loadScalar(const uint8_t* base, uint32_t offset, size_t regionSize, const char* op, uint32_t address) {
    inRange(offset, sizeof(T), regionSize, op, address);
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

template <typename T>
inline void storeScalar(uint8_t* base, uint32_t offset, size_t regionSize, T value, const char* op, uint32_t address) {
    inRange(offset, sizeof(T), regionSize, op, address);
    std::memcpy(base + offset, &value, sizeof(T));
}

inline bool isGsPrivReg(uint32_t addr) {
    return Ps2AddressInRange(addr, PS2_GS_PRIV_REG_BASE, PS2_GS_PRIV_REG_SIZE);
}

inline bool isIoRegister(uint32_t addr) {
    return Ps2AddressInRange(addr, PS2_IO_BASE, PS2_IO_SIZE);
}

inline uint64_t* gsRegPtr(GSRegisters& gs, uint32_t addr) {
    // Support both 64-bit base offsets and +4 dword aliases.
    uint32_t off = (addr - PS2_GS_PRIV_REG_BASE) & ~0x7u;
    switch (off) {
        case 0x0000:
            return &gs.pmode;
        case 0x0010:
            return &gs.smode1;
        case 0x0020:
            return &gs.smode2;
        case 0x0030:
            return &gs.srfsh;
        case 0x0040:
            return &gs.synch1;
        case 0x0050:
            return &gs.synch2;
        case 0x0060:
            return &gs.syncv;
        case 0x0070:
            return &gs.dispfb1;
        case 0x0080:
            return &gs.display1;
        case 0x0090:
            return &gs.dispfb2;
        case 0x00A0:
            return &gs.display2;
        case 0x00B0:
            return &gs.extbuf;
        case 0x00C0:
            return &gs.extdata;
        case 0x00D0:
            return &gs.extwrite;
        case 0x00E0:
            return &gs.bgcolor;
        // CSR (offset 0x1000) is intentionally not handled here: it is
        // std::atomic<uint64_t> and no longer converts to uint64_t*. Callers must
        // check for offset 0x1000 themselves and go through writeCsrHalf/
        // writeCsrFull/gs.csr.load() instead of gsRegPtr().
        case 0x1010:
            return &gs.imr;
        case 0x1040:
            return &gs.busdir;
        case 0x1080:
            return &gs.siglblid;
        default:
            return nullptr;
    }
}

constexpr uint32_t kGsCsrRegOffset = 0x1000u;

// Atomically apply a 32-bit write to one half (off=0 low dword, off=4 high
// dword) of the GS CSR register. Bits 0..1 of the low dword (SIGNAL/FINISH) are
// write-one-to-clear; everything else is a plain merge. Uses compare_exchange
// so the whole read-modify-write is a single atomic step -- this register is
// also touched by the vsync worker (FIELD bit) and the GIF (SIGNAL/FINISH) on
// other threads, so a load-then-store here would race with them.
inline void writeCsrHalf(std::atomic<uint64_t>& csr, uint32_t off, uint32_t value) {
    constexpr uint32_t kW1cMask = 0x3u;
    uint64_t expected = csr.load();
    uint64_t desired;
    do {
        if (off == 0u) {
            uint32_t oldLow = static_cast<uint32_t>(expected & 0xFFFFFFFFull);
            uint32_t mergedLow = (oldLow & kW1cMask) | (value & ~kW1cMask);
            desired = (expected & 0xFFFFFFFF00000000ull) | static_cast<uint64_t>(mergedLow);
            desired &= ~static_cast<uint64_t>(value & kW1cMask);
        } else {
            uint64_t mask = 0xFFFFFFFFull << (off * 8u);
            desired = (expected & ~mask) | (static_cast<uint64_t>(value) << (off * 8u));
        }
    } while (!csr.compare_exchange_weak(expected, desired));
}

// Same as writeCsrHalf but for a full 64-bit CSR write (bits 0..1 are still
// write-one-to-clear against the current value).
inline void writeCsrFull(std::atomic<uint64_t>& csr, uint64_t value) {
    constexpr uint64_t kW1cMask = 0x3ull;
    uint64_t expected = csr.load();
    uint64_t desired;
    do {
        desired = (expected & kW1cMask) | (value & ~kW1cMask);
        desired &= ~(value & kW1cMask);
    } while (!csr.compare_exchange_weak(expected, desired));
}

constexpr std::array<uint32_t, 4> kEeTimerBases = {
    0x10000000u,
    0x10000800u,
    0x10001000u,
    0x10001800u,
};
constexpr uint32_t kEeTimerCountOffset = 0x00u;
constexpr uint32_t kEeTimerModeOffset = 0x10u;
constexpr uint32_t kEeTimerCompareOffset = 0x20u;
constexpr uint32_t kEeTimerHoldOffset = 0x30u;
constexpr uint32_t kEeTimerModeClksMask = 0x3u;
constexpr uint32_t kEeTimerModeConfigMask = 0x3FFu;
constexpr uint32_t kEeTimerModeStatusMask = 0xC00u;
constexpr uint32_t kEeTimerModeZret = 1u << 6;
constexpr uint32_t kEeTimerModeCue = 1u << 7;
constexpr uint32_t kEeTimerModeCmpe = 1u << 8;
constexpr uint32_t kEeTimerModeOvfe = 1u << 9;
constexpr uint32_t kEeTimerModeEquf = 1u << 10;
constexpr uint32_t kEeTimerModeOvff = 1u << 11;
constexpr uint64_t kEeClockHz = 294912000ull;
constexpr std::array<uint64_t, 4> kEeTimerClockHz = {
    147456000ull,
    9216000ull,
    576000ull,
    15734ull,
};

inline bool decodeEeTimerRegister(uint32_t address, size_t& timerIndex, uint32_t& offset) {
    for (size_t index = 0; index < kEeTimerBases.size(); ++index) {
        const uint32_t candidateOffset = address - kEeTimerBases[index];
        if (candidateOffset == kEeTimerCountOffset || candidateOffset == kEeTimerModeOffset ||
            candidateOffset == kEeTimerCompareOffset || (index < 2u && candidateOffset == kEeTimerHoldOffset)) {
            timerIndex = index;
            offset = candidateOffset;
            return true;
        }
    }
    return false;
}

constexpr uint64_t ticksUntilMatch(uint32_t count, uint32_t target) {
    const uint32_t distance = (target - count) & 0xFFFFu;
    return distance == 0u ? 0x10000ull : static_cast<uint64_t>(distance);
}

struct DmaTagView {
    uint16_t qwc = 0;
    uint8_t id = 0;
    bool irq = false;
    uint32_t addr = 0;
    uint32_t upper = 0;
};

inline DmaTagView decodeDmaTag(uint64_t tag) {
    DmaTagView out{};
    out.qwc = static_cast<uint16_t>(tag & 0xFFFFu);
    out.id = static_cast<uint8_t>((tag >> 28u) & 0x7u);
    out.irq = ((tag >> 31u) & 0x1ull) != 0ull;
    out.addr = static_cast<uint32_t>((tag >> 32u) & 0x7FFFFFFFu);
    out.upper = static_cast<uint32_t>((tag >> 16u) & 0xFFFFu);
    return out;
}

inline uint32_t gifTagNloop(uint64_t tagLo) {
    return static_cast<uint32_t>(tagLo & 0x7FFFu);
}

inline uint8_t gifTagFlg(uint64_t tagLo) {
    return static_cast<uint8_t>((tagLo >> 58u) & 0x3u);
}

inline uint32_t gifTagNreg(uint64_t tagLo) {
    uint32_t nreg = static_cast<uint32_t>((tagLo >> 60u) & 0xFu);
    return nreg == 0u ? 16u : nreg;
}

} // namespace

// Helpers for GS VRAM addressing (PSMCT32 path).
static inline uint32_t gs_vram_offset(uint32_t basePage, uint32_t x, uint32_t y, uint32_t fbw) {
    // basePage is in 2048-byte units; fbw is in blocks of 64 pixels.
    uint32_t strideBytes = fbw * 64 * 4;
    return basePage * 2048 + y * strideBytes + x * 4;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drakan debug: PS2X_WATCH_RDRAM=<lo>,<hi>,<triggerFile> (hex guest addresses). Once <triggerFile> exists, writes to
// that RDRAM range are caught with page protection plus single stepping for 3 seconds and the distinct host
// instruction addresses (RVA, resolve with the linker map) of the writers are printed with the first address and
// value written. Finds the guest function that builds a packet.
// ---------------------------------------------------------------------------------------------------------------------
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
namespace {
struct WatchHit {
    uint64_t count = 0;
    uint32_t firstAddr = 0, firstValue = 0;
};
uint8_t* g_wBase = nullptr;
uintptr_t g_wLo = 0, g_wHi = 0; // host addresses, page aligned
std::atomic<bool> g_wArmed{false};
std::mutex g_wMutex;
std::map<uintptr_t, WatchHit> g_wHits;
thread_local uintptr_t t_wPage = 0;
thread_local uintptr_t t_wRip = 0;
thread_local uintptr_t t_wAddr = 0;

LONG CALLBACK rdramWatchVeh(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION && g_wArmed.load(std::memory_order_relaxed) &&
        ep->ExceptionRecord->ExceptionInformation[0] == 1) {
        const uintptr_t a = static_cast<uintptr_t>(ep->ExceptionRecord->ExceptionInformation[1]);
        if (a >= g_wLo && a < g_wHi) {
            const uintptr_t page = a & ~static_cast<uintptr_t>(0xfff);
            DWORD old;
            VirtualProtect(reinterpret_cast<void*>(page), 0x1000, PAGE_READWRITE, &old);
            t_wPage = page;
            t_wAddr = a;
            t_wRip = static_cast<uintptr_t>(ep->ContextRecord->Rip);
            ep->ContextRecord->EFlags |= 0x100; // trap flag: single step the faulting store
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (code == EXCEPTION_SINGLE_STEP && t_wPage) {
        if (g_wArmed.load(std::memory_order_relaxed)) {
            DWORD old;
            VirtualProtect(reinterpret_cast<void*>(t_wPage), 0x1000, PAGE_READONLY, &old);
            static const uint32_t wantValue = [] {
                const char* v = std::getenv("PS2X_WATCH_VALUE");
                return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 16)) : 0u;
            }();
            if (wantValue) {
                uint32_t w0 = 0, w1 = 0;
                std::memcpy(&w0, reinterpret_cast<void*>(t_wAddr), 4);
                std::memcpy(&w1, reinterpret_cast<void*>(t_wAddr + 4), 4);
                bool inside = false;
                // wanted: a store into a packet whose GIF tag (qword with hi == wantValue) lies up to 17 qwords back
                const uintptr_t q0 = t_wAddr & ~static_cast<uintptr_t>(15);
                for (int k = 0; k <= 17 && !inside; ++k) {
                    const uintptr_t qa = q0 - 16u * k;
                    if (qa < reinterpret_cast<uintptr_t>(g_wBase))
                        break;
                    uint32_t hi = 0, regs = 0;
                    std::memcpy(&hi, reinterpret_cast<void*>(qa + 4), 4);
                    std::memcpy(&regs, reinterpret_cast<void*>(qa + 8), 4);
                    inside = hi == wantValue && (regs & 0xffff) == 0x5a12;
                }
                (void)w0;
                (void)w1;
                if (!inside) {
                    t_wPage = 0;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
            std::lock_guard<std::mutex> lock(g_wMutex);
            WatchHit& h = g_wHits[t_wRip];
            if (h.count++ == 0) {
                h.firstAddr = static_cast<uint32_t>(t_wAddr - reinterpret_cast<uintptr_t>(g_wBase));
                std::memcpy(&h.firstValue, reinterpret_cast<void*>(t_wAddr), 4);
            }
        }
        t_wPage = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void rdramWatchStart(uint8_t* rdram) {
    const char* e = std::getenv("PS2X_WATCH_RDRAM");
    if (!e || !*e)
        return;
    std::fprintf(stderr, "[rdram-watch] spec %s", e);
    std::fputc(10, stderr);
    unsigned lo = 0, hi = 0;
    char trig[512] = {};
    if (std::sscanf(e, "%x,%x,%511[^,]", &lo, &hi, trig) != 3)
        return;
    g_wBase = rdram;
    g_wLo = (reinterpret_cast<uintptr_t>(rdram) + lo + 0xfff) & ~static_cast<uintptr_t>(0xfff);
    g_wHi = (reinterpret_cast<uintptr_t>(rdram) + hi) & ~static_cast<uintptr_t>(0xfff);
    AddVectoredExceptionHandler(1, rdramWatchVeh);
    std::string trigger = trig;
    std::thread([trigger]() {
        for (;;) {
            if (FILE* t = std::fopen(trigger.c_str(), "rb")) {
                std::fclose(t);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(g_wLo), g_wHi - g_wLo, PAGE_READONLY, &old);
        g_wArmed = true;
        std::fprintf(stderr, "[rdram-watch] armed");
        std::fputc(10, stderr);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        g_wArmed = false;
        VirtualProtect(reinterpret_cast<void*>(g_wLo), g_wHi - g_wLo, PAGE_READWRITE, &old);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        std::lock_guard<std::mutex> lock(g_wMutex);
        for (const auto& kv : g_wHits)
            std::fprintf(stderr,
                         "[rdram-watch] rip=%llx (rva %llx, map %llx) count=%llu first guest addr=%08x value=%08x\n",
                         (unsigned long long)kv.first, (unsigned long long)(kv.first - base),
                         (unsigned long long)(kv.first - base + 0x140000000ull), (unsigned long long)kv.second.count,
                         kv.second.firstAddr, kv.second.firstValue);
        std::fprintf(stderr, "[rdram-watch] done, %zu distinct writers\n", g_wHits.size());
    }).detach();
}
} // namespace
#else
namespace {
void rdramWatchStart(uint8_t*) {}
} // namespace
#endif

PS2Memory::PS2Memory()
    : m_rdram(nullptr), m_scratchpad(nullptr), iop_ram(nullptr), m_seenGifCopy(false), m_gsVRAM(nullptr) {
    ps2SetScratchpadHostPtr(nullptr);
}

PS2Memory::PS2Memory(IsolatedVU)
    : m_rdram(nullptr), m_scratchpad(nullptr), iop_ram(nullptr), m_seenGifCopy(false), m_gsVRAM(nullptr) {
    // No initialize(): that binds global EE memory and GS state. These buffers
    // are used only by the existing VIF decoder and a private VU interpreter.
    auto code = std::make_unique<uint8_t[]>(PS2_VU1_CODE_SIZE);
    auto data = std::make_unique<uint8_t[]>(PS2_VU1_DATA_SIZE);
    m_vu1Code = code.release();
    m_vu1Data = data.release();
    vif1_regs = {};
}

PS2Memory::~PS2Memory() {
    if (m_rdram) {
        m_ram.release();
        m_rdram = nullptr;
    }

    if (m_scratchpad) {
        ps2SetScratchpadHostPtr(nullptr);
        delete[] m_scratchpad;
        m_scratchpad = nullptr;
    }

    if (m_gsVRAM) {
        delete[] m_gsVRAM;
        m_gsVRAM = nullptr;
    }

    if (m_vu1Code) {
        delete[] m_vu1Code;
        m_vu1Code = nullptr;
    }
    if (m_vu1Data) {
        delete[] m_vu1Data;
        m_vu1Data = nullptr;
    }
    if (m_vu0Code) {
        delete[] m_vu0Code;
        m_vu0Code = nullptr;
    }
    if (m_vu0Data) {
        delete[] m_vu0Data;
        m_vu0Data = nullptr;
    }

    if (iop_ram) {
        delete[] iop_ram;
        iop_ram = nullptr;
    }
}

bool PS2Memory::initialize(size_t ramSize) {
    auto cleanup = [this]() {
        m_ram.release();
        delete[] m_scratchpad;
        delete[] iop_ram;
        delete[] m_gsVRAM;
        delete[] m_vu0Code;
        delete[] m_vu0Data;
        delete[] m_vu1Code;
        delete[] m_vu1Data;
        m_rdram = nullptr;
        m_scratchpad = nullptr;
        ps2SetScratchpadHostPtr(nullptr);
        iop_ram = nullptr;
        m_gsVRAM = nullptr;
        m_vu0Code = nullptr;
        m_vu0Data = nullptr;
        m_vu1Code = nullptr;
        m_vu1Data = nullptr;
    };

    cleanup();
    m_seenGifCopy = false;
    m_dmaStartCount.store(0, std::memory_order_relaxed);
    m_gifCopyCount.store(0, std::memory_order_relaxed);
    m_gsWriteCount.store(0, std::memory_order_relaxed);
    m_vifWriteCount.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_completedDmacMutex);
        m_completedDmacCauses.clear();
        m_completedDmacCount.store(0, std::memory_order_release);
    }
    m_codeRegions.clear();
    m_path3Masked = false;
    m_path3MaskedFifo.clear();
    m_vif1PendingPath2ImageQwc = 0u;
    m_vif1PendingPath2DirectHl = false;
    resetEeTimers();

    try {
        // Allocate main RAM
        // Reserve all of guest RAM, commit the retail part (zeroed by the host); the rest is committed as used. One
        // granule of slack past the end keeps a host copy of a few bytes from a masked pointer at the very top in
        // bounds.
        if (!m_ram.reserve(ramSize + GuestRamReservation::kGranularity, std::min<size_t>(ramSize, PS2_RETAIL_RAM_SIZE)))
            throw std::bad_alloc();
        m_rdram = m_ram.base();
        rdramWatchStart(m_rdram);

        // Allocate scratchpad
        m_scratchpad = new uint8_t[PS2_SCRATCHPAD_SIZE];
        std::memset(m_scratchpad, 0, PS2_SCRATCHPAD_SIZE);
        ps2SetScratchpadHostPtr(m_scratchpad);

        // Initialize EE TLB entries (R5900 has 48 entries).
        m_tlbEntries.assign(48, TLBEntry{0, 0, 0, false});

        // Allocate IOP RAM
        iop_ram = new uint8_t[2 * 1024 * 1024]; // 2MB

        // Initialize IOP RAM with zeros
        std::memset(iop_ram, 0, 2 * 1024 * 1024);

        // Initialize I/O registers
        m_ioRegisters.clear();

        // Initialize GS registers
        memset(&gs_regs, 0, sizeof(gs_regs));
        // memset zero-fills std::atomic<uint64_t>::csr's bytes, which is not itself
        // a guaranteed-valid atomic store; make the zero-initialization explicit.
        gs_regs.csr.store(0);
        gs_regs.dispfb1 = (0ULL << 0) | (10ULL << 9) | (0ULL << 15) | (0ULL << 32) | (0ULL << 43);
        gs_regs.display1 = (0ULL << 0) | (0ULL << 12) | (0ULL << 23) | (0ULL << 27) | (639ULL << 32) | (447ULL << 44);
        gs_regs.dispfb2 = gs_regs.dispfb1;
        gs_regs.display2 = gs_regs.display1;

        // Allocate GS VRAM (4MB)
        m_gsVRAM = new uint8_t[PS2_GS_VRAM_SIZE];
        std::memset(m_gsVRAM, 0, PS2_GS_VRAM_SIZE);

        m_vu0Code = new uint8_t[PS2_VU0_CODE_SIZE];
        m_vu0Data = new uint8_t[PS2_VU0_DATA_SIZE];
        std::memset(m_vu0Code, 0, PS2_VU0_CODE_SIZE);
        std::memset(m_vu0Data, 0, PS2_VU0_DATA_SIZE);

        m_vu1Code = new uint8_t[PS2_VU1_CODE_SIZE];
        m_vu1Data = new uint8_t[PS2_VU1_DATA_SIZE];
        std::memset(m_vu1Code, 0, PS2_VU1_CODE_SIZE);
        std::memset(m_vu1Data, 0, PS2_VU1_DATA_SIZE);
        markVU0CodeModified();
        markVU1CodeModified();

        // Initialize VIF registers
        memset(&vif0_regs, 0, sizeof(vif0_regs));
        memset(&vif1_regs, 0, sizeof(vif1_regs));

        // Initialize DMA registers
        memset(dma_regs, 0, sizeof(dma_regs));

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing PS2 memory: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

void PS2Memory::resetEeTimers() noexcept {
    m_eeTimers = {};
}

uint32_t PS2Memory::advanceEeTimers(uint64_t eeCycles) noexcept {
    if (eeCycles == 0u) {
        return 0u;
    }

    constexpr uint32_t kGifStat = 0x10003020u;
    constexpr uint32_t kGifFqcMask = 0x1F000000u;
    auto gifStatIt = m_ioRegisters.find(kGifStat);
    if (gifStatIt != m_ioRegisters.end())
        gifStatIt->second &= ~kGifFqcMask;

    uint32_t interruptMask = 0u;
    for (size_t index = 0; index < m_eeTimers.size(); ++index) {
        EeTimer& timer = m_eeTimers[index];
        if ((timer.mode & kEeTimerModeCue) == 0u) {
            continue;
        }

        const uint64_t clockHz = kEeTimerClockHz[timer.mode & kEeTimerModeClksMask];
        const uint64_t wholeSeconds = eeCycles / kEeClockHz;
        const uint64_t remainingCycles = eeCycles % kEeClockHz;
        const uint64_t scaled = remainingCycles * clockHz + timer.clockRemainder;
        const uint64_t ticks = wholeSeconds * clockHz + scaled / kEeClockHz;
        timer.clockRemainder = scaled % kEeClockHz;
        if (ticks == 0u) {
            continue;
        }

        const uint32_t oldCount = timer.count & 0xFFFFu;
        const uint32_t compare = timer.compare & 0xFFFFu;
        const uint64_t compareDistance = ticksUntilMatch(oldCount, compare);
        const uint64_t overflowDistance = 0x10000ull - oldCount;
        const bool zeroReturn = (timer.mode & kEeTimerModeZret) != 0u;
        const bool compareReached = ticks >= compareDistance;
        bool overflowReached = false;

        if (zeroReturn) {
            overflowReached = ticks >= overflowDistance && overflowDistance <= compareDistance;
            if (compareReached) {
                const uint64_t remaining = ticks - compareDistance;
                timer.count = compare == 0u ? static_cast<uint32_t>(remaining & 0xFFFFu)
                                            : static_cast<uint32_t>(remaining % compare);
            } else {
                timer.count = static_cast<uint32_t>((oldCount + ticks) & 0xFFFFu);
            }
        } else {
            overflowReached = ticks >= overflowDistance;
            timer.count = static_cast<uint32_t>((oldCount + ticks) & 0xFFFFu);
        }

        if (compareReached && (timer.mode & kEeTimerModeCmpe) != 0u && (timer.mode & kEeTimerModeEquf) == 0u) {
            timer.mode |= kEeTimerModeEquf;
            interruptMask |= 1u << index;
        }
        if (overflowReached && (timer.mode & kEeTimerModeOvfe) != 0u && (timer.mode & kEeTimerModeOvff) == 0u) {
            timer.mode |= kEeTimerModeOvff;
            interruptMask |= 1u << index;
        }
    }
    return interruptMask;
}

uint64_t PS2Memory::cyclesUntilNextEeTimerInterrupt() const noexcept {
    uint64_t nearest = std::numeric_limits<uint64_t>::max();
    for (const EeTimer& timer : m_eeTimers) {
        if ((timer.mode & kEeTimerModeCue) == 0u) {
            continue;
        }

        const uint32_t count = timer.count & 0xFFFFu;
        const uint32_t compare = timer.compare & 0xFFFFu;
        const uint64_t compareDistance = ticksUntilMatch(count, compare);
        const uint64_t overflowDistance = 0x10000ull - count;
        uint64_t eventTicks = std::numeric_limits<uint64_t>::max();

        if ((timer.mode & kEeTimerModeCmpe) != 0u && (timer.mode & kEeTimerModeEquf) == 0u) {
            eventTicks = compareDistance;
        }
        const bool overflowCanOccur = (timer.mode & kEeTimerModeZret) == 0u || overflowDistance <= compareDistance;
        if (overflowCanOccur && (timer.mode & kEeTimerModeOvfe) != 0u && (timer.mode & kEeTimerModeOvff) == 0u) {
            eventTicks = std::min(eventTicks, overflowDistance);
        }
        if (eventTicks == std::numeric_limits<uint64_t>::max()) {
            continue;
        }

        const uint64_t clockHz = kEeTimerClockHz[timer.mode & kEeTimerModeClksMask];
        const uint64_t numerator = eventTicks * kEeClockHz - timer.clockRemainder;
        const uint64_t cycles = (numerator + clockHz - 1u) / clockHz;
        nearest = std::min(nearest, std::max<uint64_t>(1u, cycles));
    }
    return nearest;
}

bool PS2Memory::isScratchpad(uint32_t address) const {
    return ps2IsScratchpadAddress(address);
}

uint8_t* PS2Memory::mapVuMemory(uint32_t physAddr, uint32_t size, uint32_t& offset, uint32_t& limit) {
    uint8_t* const mapped =
        const_cast<uint8_t*>(static_cast<const PS2Memory*>(this)->mapVuMemory(physAddr, size, offset, limit));
    // The EE reaches into VU1 data memory itself (the VU1 worker is idle here): the vertex path, while it follows
    // that memory, first writes what it deferred (vu/ps2_vu1_gpu_live.h). Off: one flag test.
    if (mapped && mapped == m_vu1Data && vu1gpu::live::following())
        vu1gpu::live::directAccess(mapped);
    return mapped;
}

const uint8_t* PS2Memory::mapVuMemory(uint32_t physAddr, uint32_t size, uint32_t& offset, uint32_t& limit) const {
    if (physAddr >= PS2_VU1_CODE_BASE && physAddr < PS2_VU1_DATA_BASE + PS2_VU1_DATA_SIZE)
        vu1async::sync(); // the VU1 worker owns VU1 memory
    auto mapRange = [&](uint32_t base, uint32_t rangeSize, const uint8_t* ptr) -> const uint8_t* {
        if (!ptr || physAddr < base) {
            return nullptr;
        }
        const uint32_t local = physAddr - base;
        if (local >= rangeSize || size > (rangeSize - local)) {
            return nullptr;
        }
        offset = local;
        limit = rangeSize;
        return ptr;
    };

    if (const uint8_t* ptr = mapRange(PS2_VU0_CODE_BASE, PS2_VU0_CODE_SIZE, m_vu0Code)) {
        return ptr;
    }
    if (const uint8_t* ptr = mapRange(PS2_VU0_DATA_BASE, PS2_VU0_DATA_SIZE, m_vu0Data)) {
        return ptr;
    }
    if (const uint8_t* ptr = mapRange(PS2_VU1_CODE_BASE, PS2_VU1_CODE_SIZE, m_vu1Code)) {
        return ptr;
    }
    return mapRange(PS2_VU1_DATA_BASE, PS2_VU1_DATA_SIZE, m_vu1Data);
}

uint32_t PS2Memory::translateAddress(uint32_t virtualAddress) {
    if (isScratchpad(virtualAddress)) {
        return ps2ScratchpadOffset(virtualAddress);
    }

    // EE uncached aliases of main RAM (per PS2 memory map):
    //   0x20000000-0x3FFFFFFF -> 32MB mirror of RDRAM
    // This includes the accelerated window rooted at 0x30100000.
    if (Ps2IsUncachedRamMirrorAddress(virtualAddress)) {
        return virtualAddress & PS2_RAM_MASK;
    }

    // KSEG0/KSEG1 direct-mapped window.
    if (Ps2IsKseg01Address(virtualAddress)) {
        return Ps2DirectMappedPhysicalAddress(virtualAddress);
    }

    // In this runtime, low segments are treated as physical-style addresses already.
    if (virtualAddress < 0x80000000) {
        return virtualAddress;
    }

    // KSEG2/KSEG3 are TLB mapped.
    if (Ps2IsKseg23Address(virtualAddress)) {
        for (const auto& entry : m_tlbEntries) {
            if (entry.valid) {
                // PageMask uses bits [24:13]. Build an address-level mask (plus 4KB base page bits).
                const uint32_t mask = entry.mask & 0x01FFE000u;
                const uint32_t compareMask = ~(mask | 0xFFFu);
                if ((virtualAddress & compareMask) == (entry.vpn & compareMask)) {
                    // TLB hit
                    const uint32_t pageOffsetMask = mask | 0xFFFu;
                    const uint32_t physBase = entry.pfn << 12;
                    return physBase | (virtualAddress & pageOffsetMask);
                }
            }
        }
        throw std::runtime_error("TLB miss for address: 0x" + std::to_string(virtualAddress));
    }

    return virtualAddress;
}

bool PS2Memory::tlbRead(uint32_t index, uint32_t& vpn, uint32_t& pfn, uint32_t& mask, bool& valid) const {
    if (index >= m_tlbEntries.size()) {
        return false;
    }

    const TLBEntry& entry = m_tlbEntries[index];
    vpn = entry.vpn;
    pfn = entry.pfn;
    mask = entry.mask;
    valid = entry.valid;
    return true;
}

bool PS2Memory::tlbWrite(uint32_t index, uint32_t vpn, uint32_t pfn, uint32_t mask, bool valid) {
    if (index >= m_tlbEntries.size()) {
        return false;
    }

    TLBEntry& entry = m_tlbEntries[index];
    entry.vpn = vpn & 0xFFFFF000u;
    entry.pfn = pfn & 0x000FFFFFu;
    entry.mask = mask & 0x01FFE000u;
    entry.valid = valid;
    return true;
}

int32_t PS2Memory::tlbProbe(uint32_t vpn) const {
    const uint32_t normalizedVpn = vpn & 0xFFFFF000u;
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_tlbEntries.size()); ++i) {
        const TLBEntry& entry = m_tlbEntries[i];
        if (!entry.valid) {
            continue;
        }

        const uint32_t mask = entry.mask & 0x01FFE000u;
        const uint32_t compareMask = ~(mask | 0xFFFu);
        if ((normalizedVpn & compareMask) == (entry.vpn & compareMask)) {
            return static_cast<int32_t>(i);
        }
    }

    return -1;
}

uint8_t PS2Memory::read8(uint32_t address) {
    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        return m_scratchpad[physAddr];
    }
    if (physAddr < PS2_RAM_SIZE) {
        return m_rdram[physAddr];
    }
    uint32_t vuOffset = 0;
    uint32_t vuLimit = 0;
    if (const uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint8_t), vuOffset, vuLimit)) {
        (void)vuLimit;
        return vuMem[vuOffset];
    } else if (isIoRegister(physAddr)) {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t value = readIORegister(regAddr);
        uint32_t shift = (physAddr & 3) * 8;
        return static_cast<uint8_t>((value >> shift) & 0xFF);
    }

    return 0;
}

uint16_t PS2Memory::read16(uint32_t address) {
    if (address & 1) {
        throw std::runtime_error("Unaligned 16-bit read at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        return loadScalar<uint16_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read16 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE) {
        return loadScalar<uint16_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read16 rdram", address);
    }
    uint32_t vuOffset = 0;
    uint32_t vuLimit = 0;
    if (const uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint16_t), vuOffset, vuLimit)) {
        return loadScalar<uint16_t>(vuMem, vuOffset, vuLimit, "read16 vu", address);
    } else if (isIoRegister(physAddr)) {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t value = readIORegister(regAddr);
        uint32_t shift = (physAddr & 2) * 8;
        return static_cast<uint16_t>((value >> shift) & 0xFFFF);
    }

    return 0;
}

uint32_t PS2Memory::read32(uint32_t address) {
    if (address & 3) {
        throw std::runtime_error("Unaligned 32-bit read at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address)) {
        uint32_t off = address & 7;
        const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
        if (regOff == kGsCsrRegOffset) {
            uint64_t val = gs_regs.csr.load();
            return (uint32_t)(val >> (off * 8));
        }
        uint64_t* reg = gsRegPtr(gs_regs, address);
        if (!reg)
            return 0;
        uint64_t val = *reg;
        return (uint32_t)(val >> (off * 8));
    }

    if (g_terrainBusFast && isIoRegister(address))
        return readIORegister(address);

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        return loadScalar<uint32_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read32 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE) {
        return loadScalar<uint32_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read32 rdram", address);
    }
    uint32_t vuOffset = 0;
    uint32_t vuLimit = 0;
    if (const uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint32_t), vuOffset, vuLimit)) {
        return loadScalar<uint32_t>(vuMem, vuOffset, vuLimit, "read32 vu", address);
    } else if (isIoRegister(physAddr)) {
        return readIORegister(physAddr);
    }

    return 0;
}

uint64_t PS2Memory::read64(uint32_t address) {
    if (address & 7) {
        throw std::runtime_error("Unaligned 64-bit read at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address)) {
        const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
        if (regOff == kGsCsrRegOffset) {
            const uint64_t csr = gs_regs.csr.load();
            // The game waits for the GS by polling CSR.FINISH in a loop (0x1dc688, before its per-frame read-back):
            // a quarter to two thirds of the EE thread while the VU1 and GS workers finish the frame.
            // PS2X_FINISH_YIELD=1 (experiment, off by default): from the 16th poll in a row that finds it clear, give
            // the rest of the time slice to another thread that is ready on this processor. Measured without a
            // benefit and with collapses when CPU-bound background threads take the slice
            // (docs/journal/2026-11-12/perf-cpu.md, 4.4).
            static const bool yieldOn = [] {
                const char* value = std::getenv("PS2X_FINISH_YIELD");
                return value && value[0] == '1';
            }();
            static thread_local uint32_t clearPolls = 0;
            if (csr & 0x2ull) {
                clearPolls = 0;
            } else {
                ps2x_readback::state().finishPolls.fetch_add(1, std::memory_order_relaxed);
                if (yieldOn && ++clearPolls >= 16u)
                    std::this_thread::yield();
            }
            return csr;
        }
        uint64_t* reg = gsRegPtr(gs_regs, address);
        return reg ? *reg : 0;
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        return loadScalar<uint64_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read64 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE) {
        return loadScalar<uint64_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read64 rdram", address);
    }
    uint32_t vuOffset = 0;
    uint32_t vuLimit = 0;
    if (const uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint64_t), vuOffset, vuLimit)) {
        return loadScalar<uint64_t>(vuMem, vuOffset, vuLimit, "read64 vu", address);
    }

    // 64-bit IO read: compose from the two adjacent 32-bit IO register slots
    // to avoid any side-effects from read32 handlers.
    if (isIoRegister(address)) {
        uint32_t lo = m_ioRegisters.count(address) ? m_ioRegisters[address] : 0u;
        uint32_t hi = m_ioRegisters.count(address + 4) ? m_ioRegisters[address + 4] : 0u;
        return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    }
    return (uint64_t)read32(address) | ((uint64_t)read32(address + 4) << 32);
}

__m128i PS2Memory::read128(uint32_t address) {
    if (address & 15) {
        throw std::runtime_error("Unaligned 128-bit read at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        inRange(physAddr, sizeof(__m128i), PS2_SCRATCHPAD_SIZE, "read128 scratchpad", address);
        return _mm_loadu_si128(reinterpret_cast<__m128i*>(&m_scratchpad[physAddr]));
    }
    if (physAddr < PS2_RAM_SIZE) {
        inRange(physAddr, sizeof(__m128i), PS2_RAM_SIZE, "read128 rdram", address);
        return _mm_loadu_si128(reinterpret_cast<__m128i*>(&m_rdram[physAddr]));
    }
    uint32_t vuOffset = 0;
    uint32_t vuLimit = 0;
    if (const uint8_t* vuMem = mapVuMemory(physAddr, sizeof(__m128i), vuOffset, vuLimit)) {
        inRange(vuOffset, sizeof(__m128i), vuLimit, "read128 vu", address);
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(vuMem + vuOffset));
    }

    // 128-bit reads are primarily for quad-word loads in the EE, which are only valid for RAM areas
    // Return zeroes for unsupported areas
    return _mm_setzero_si128();
}

void PS2Memory::write8(uint32_t address, uint8_t value) {
    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        m_scratchpad[physAddr] = value;
    } else if (physAddr < PS2_RAM_SIZE) {
        m_rdram[physAddr] = value;
    } else {
        uint32_t vuOffset = 0;
        uint32_t vuLimit = 0;
        if (uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint8_t), vuOffset, vuLimit)) {
            (void)vuLimit;
            vuMem[vuOffset] = value;
            if (vuMem == m_vu0Code)
                markVU0CodeModified();
            else if (vuMem == m_vu1Code)
                markVU1CodeModified();
            else if (vuMem == m_vu1Data && vu1gpu::live::following())
                vu1gpu::live::directWrite(vuMem, vuOffset, sizeof(uint8_t));
            return;
        }
    }
    if (isIoRegister(physAddr)) {
        // IO registers - handle byte writes by modifying the appropriate byte in the word
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 3) * 8;
        uint32_t mask = ~(0xFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        writeIORegister(regAddr, newValue);
    }
}

void PS2Memory::write16(uint32_t address, uint16_t value) {
    if (address & 1) {
        throw std::runtime_error("Unaligned 16-bit write at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        storeScalar<uint16_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write16 scratchpad", address);
    } else if (physAddr < PS2_RAM_SIZE) {
        storeScalar<uint16_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write16 rdram", address);
    } else {
        uint32_t vuOffset = 0;
        uint32_t vuLimit = 0;
        if (uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint16_t), vuOffset, vuLimit)) {
            storeScalar<uint16_t>(vuMem, vuOffset, vuLimit, value, "write16 vu", address);
            if (vuMem == m_vu0Code)
                markVU0CodeModified();
            else if (vuMem == m_vu1Code)
                markVU1CodeModified();
            else if (vuMem == m_vu1Data && vu1gpu::live::following())
                vu1gpu::live::directWrite(vuMem, vuOffset, sizeof(uint16_t));
            return;
        }
    }
    if (isIoRegister(physAddr)) {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 2) * 8;
        uint32_t mask = ~(0xFFFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        writeIORegister(regAddr, newValue);
    }
}

void PS2Memory::write32(uint32_t address, uint32_t value) {
    if (address & 3) {
        throw std::runtime_error("Unaligned 32-bit write at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address)) {
        uint32_t off = address & 7;
        const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
        if (regOff == kGsCsrRegOffset) {
            // CSR: bits 0..1 of the low dword are write-one-to-clear status bits.
            // Done as a single atomic RMW -- see writeCsrHalf's comment.
            writeCsrHalf(gs_regs.csr, off, value);
        } else if (uint64_t* reg = gsRegPtr(gs_regs, address)) {
            uint64_t mask = 0xFFFFFFFFULL << (off * 8);
            uint64_t newVal = (*reg & ~mask) | ((uint64_t)value << (off * 8));
            if (reg == &gs_regs.dispfb1)
                ps2perf::onDispfb(*reg, newVal);
            else if (reg == &gs_regs.dispfb2)
                ps2perf::onDispfb2(*reg, newVal);
            *reg = newVal;
        }
        return;
    }

    if (g_terrainBusFast && isIoRegister(address)) {
        writeIORegister(address, value);
        return;
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        storeScalar<uint32_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write32 scratchpad", address);
    } else if (physAddr < PS2_RAM_SIZE) {
        // Check if this might be code modification
        markModified(address, 4);

        storeScalar<uint32_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write32 rdram", address);
    } else {
        uint32_t vuOffset = 0;
        uint32_t vuLimit = 0;
        if (uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint32_t), vuOffset, vuLimit)) {
            storeScalar<uint32_t>(vuMem, vuOffset, vuLimit, value, "write32 vu", address);
            if (vuMem == m_vu0Code)
                markVU0CodeModified();
            else if (vuMem == m_vu1Code)
                markVU1CodeModified();
            else if (vuMem == m_vu1Data && vu1gpu::live::following())
                vu1gpu::live::directWrite(vuMem, vuOffset, sizeof(uint32_t));
            return;
        }
    }
    if (isIoRegister(physAddr)) {
        writeIORegister(physAddr, value);
    }
}

void PS2Memory::write64(uint32_t address, uint64_t value) {
    if (address & 7) {
        throw std::runtime_error("Unaligned 64-bit write at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address)) {
        const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
        if (regOff == kGsCsrRegOffset) {
            // CSR: bits 0..1 are write-one-to-clear status bits. Done as a single
            // atomic RMW -- see writeCsrFull's comment.
            writeCsrFull(gs_regs.csr, value);
        } else if (uint64_t* reg = gsRegPtr(gs_regs, address)) {
            if (reg == &gs_regs.dispfb1)
                ps2perf::onDispfb(*reg, value);
            else if (reg == &gs_regs.dispfb2)
                ps2perf::onDispfb2(*reg, value);
            *reg = value;
        }
        return;
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (scratch) {
        storeScalar<uint64_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write64 scratchpad", address);
    } else if (physAddr < PS2_RAM_SIZE) {
        markModified(address, 8);
        storeScalar<uint64_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write64 rdram", address);
    } else {
        uint32_t vuOffset = 0;
        uint32_t vuLimit = 0;
        if (uint8_t* vuMem = mapVuMemory(physAddr, sizeof(uint64_t), vuOffset, vuLimit)) {
            storeScalar<uint64_t>(vuMem, vuOffset, vuLimit, value, "write64 vu", address);
            if (vuMem == m_vu0Code)
                markVU0CodeModified();
            else if (vuMem == m_vu1Code)
                markVU1CodeModified();
            else if (vuMem == m_vu1Data && vu1gpu::live::following())
                vu1gpu::live::directWrite(vuMem, vuOffset, sizeof(uint64_t));
            return;
        }
    }
    if (isIoRegister(physAddr)) {
        write32(address, (uint32_t)value);
        write32(address + 4, (uint32_t)(value >> 32));
    }
}

void PS2Memory::write128(uint32_t address, __m128i value) {
    if (address & 15) {
        throw std::runtime_error("Unaligned 128-bit write at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    const uint32_t physAddr = g_terrainBusFast && scratch ? ps2ScratchpadOffset(address) : translateAddress(address);

    if (!scratch && physAddr == 0x10004000u) // VIF0_FIFO
    {
        alignas(16) uint8_t fifoData[16];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(fifoData), value);
        processVIF0Data(fifoData, sizeof(fifoData));
        return;
    }
    if (!scratch && physAddr == 0x10007010u) // IPU_IN_FIFO
    {
        alignas(16) uint8_t fifoData[16];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(fifoData), value);
        m_ipu.pushFifo(fifoData);
        return;
    }
    if (!scratch && physAddr == 0x10005000u) // VIF1_FIFO
    {
        alignas(16) uint8_t fifoData[16];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(fifoData), value);
        dispatchVif1(std::vector<uint8_t>(fifoData, fifoData + sizeof(fifoData)));
        return;
    }

    if (scratch) {
        inRange(physAddr, sizeof(__m128i), PS2_SCRATCHPAD_SIZE, "write128 scratchpad", address);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&m_scratchpad[physAddr]), value);
    } else if (physAddr < PS2_RAM_SIZE) {
        markModified(address, 16);
        inRange(physAddr, sizeof(__m128i), PS2_RAM_SIZE, "write128 rdram", address);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&m_rdram[physAddr]), value);
    } else {
        uint32_t vuOffset = 0;
        uint32_t vuLimit = 0;
        if (uint8_t* vuMem = mapVuMemory(physAddr, sizeof(__m128i), vuOffset, vuLimit)) {
            inRange(vuOffset, sizeof(__m128i), vuLimit, "write128 vu", address);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(vuMem + vuOffset), value);
            if (vuMem == m_vu0Code)
                markVU0CodeModified();
            else if (vuMem == m_vu1Code)
                markVU1CodeModified();
            else if (vuMem == m_vu1Data && vu1gpu::live::following())
                vu1gpu::live::directWrite(vuMem, vuOffset, sizeof(__m128i));
            return;
        }
    }
    if (isIoRegister(physAddr)) {
        // Non-RAM 128-bit stores are modeled as two 64-bit stores.
        uint64_t lo = _mm_extract_epi64(value, 0);
        uint64_t hi = _mm_extract_epi64(value, 1);

        write64(address, lo);
        write64(address + 8, hi);
    }
}

// An EE store to a hardware register (I/O space 0x10000000-0x1000FFFF, or a GS privileged register). Returns false
// for an address no handler claims. Each register block has its own handler below; DMA starts are the big one.
bool PS2Memory::writeIORegister(uint32_t address, uint32_t value) {
    using namespace ps2hw;
    // The VU1 worker thread owns the GIF / VIF state, so wait for it before the EE changes those registers. Two
    // exceptions are EE-only: GIF_MODE while the render queue runs, and VIF1_STAT during a late GS read-back (only
    // its FIFO direction bit comes from the EE; runtime/ps2_readback.h sets and clears it around its DMA).
    const bool lateReadbackStat =
        address == kVif1Stat && ps2x_readback::mode() == ps2x_readback::Mode::Late && vu1async::renderQueueEnabled();
    if (address >= kGifVifRegsBegin && address < kGifVifRegsEnd &&
        !(address == kGifMode && vu1async::renderQueueEnabled()) && !lateReadbackStat)
        vu1async::sync();

    size_t timerIndex = 0u;
    uint32_t timerOffset = 0u;
    if (decodeEeTimerRegister(address, timerIndex, timerOffset))
        return writeEeTimerRegister(m_eeTimers[timerIndex], timerOffset, value);

    if (isGsPrivReg(address)) {
        // NB: unreachable from write8/16/32/64 today since those all funnel IO register writes through addresses in
        // PS2_IO_BASE's range, which is disjoint from PS2_GS_PRIV_REG_BASE; kept correct for direct callers.
        writeGsPrivilegedRegister(address, value);
        return true;
    }

    if (address >= kIpuRegsBegin && address <= kIpuRegsLast) {
        // Drakan: the IPU is emulated (see ps2_ipu.cpp); IDEC output may satisfy a waiting fromIPU DMA
        m_ipu.writeReg(address, value);
        if (address == kIpuCmd)
            serviceIpuFromDma();
        return true;
    }

    if (address == kDmacStat) {
        writeDmacStat(value);
        return true;
    }

    m_ioRegisters[address] = value;

    if (address >= kVif1RegsBegin && address < kVif1RegsEnd) {
        m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        writeVif1Register(address, value);
        return true;
    }

    if (address >= kVif0RegsBegin && address < kVif0RegsEnd) {
        m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (address >= kDmaChannelsBegin && address < kDmaChannelsEnd) {
        // Writing CHCR with STR set starts the channel. The transfer runs to completion right here: this runtime has
        // no DMA timing, so a channel is never seen busy afterwards.
        if ((address & ~kDmaChannelBaseMask) == kDmaChcr && (value & kChcrStr) != 0u)
            startDmaChannel(address & kDmaChannelBaseMask, value);
        return true;
    }

    // Timer 0 and the 0x10000200 block: stored above, nothing else to do.
    if ((address >= 0x10000000u && address < 0x10000100u) || (address >= 0x10000200u && address < 0x10000300u))
        return true;

    return false;
}

bool PS2Memory::writeEeTimerRegister(EeTimer& timer, uint32_t offset, uint32_t value) {
    switch (offset) {
        case kEeTimerCountOffset:
            timer.count = value & 0xFFFFu;
            timer.clockRemainder = 0u;
            return true;
        case kEeTimerModeOffset: {
            // The two status flags (EQUF, OVFF) are write-1-to-clear; the rest of MODE is plain configuration.
            const uint32_t previousMode = timer.mode;
            const uint32_t status = (previousMode & kEeTimerModeStatusMask) & ~(value & kEeTimerModeStatusMask);
            timer.mode = (value & kEeTimerModeConfigMask) | status;
            if (((previousMode ^ timer.mode) & (kEeTimerModeClksMask | kEeTimerModeCue)) != 0u) {
                timer.clockRemainder = 0u;
            }
            return true;
        }
        case kEeTimerCompareOffset:
            timer.compare = value & 0xFFFFu;
            return true;
        case kEeTimerHoldOffset:
            timer.hold = value & 0xFFFFu;
            return true;
        default:
            return false;
    }
}

// A 32-bit store into one half of a 64-bit GS privileged register.
void PS2Memory::writeGsPrivilegedRegister(uint32_t address, uint32_t value) {
    m_ioRegisters[address] = value;
    const uint32_t off = address & 7u;
    const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
    if (regOff == kGsCsrRegOffset) {
        writeCsrHalf(gs_regs.csr, off, value);
    } else if (uint64_t* reg = gsRegPtr(gs_regs, address)) {
        const uint64_t mask = 0xFFFFFFFFull << (off * 8u);
        const uint64_t next = (*reg & ~mask) | (static_cast<uint64_t>(value) << (off * 8u));
        if (reg == &gs_regs.dispfb1)
            ps2perf::onDispfb(*reg, next);
        else if (reg == &gs_regs.dispfb2)
            ps2perf::onDispfb2(*reg, next);
        *reg = next;
    }
    m_gsWriteCount.fetch_add(1, std::memory_order_relaxed);
}

void PS2Memory::writeDmacStat(uint32_t value) {
    using namespace ps2hw;
    const auto it = m_ioRegisters.find(kDmacStat);
    const uint32_t current = it != m_ioRegisters.end() ? it->second : 0u;
    // Channel flags (low bits) are write-1-to-clear; the mask bits (16..25) toggle where a 1 is written.
    const uint32_t status = (current & kDStatChannelMask) & ~(value & kDStatChannelMask);
    const uint32_t mask =
        ((current >> kDStatMaskShift) & kDStatChannelMask) ^ ((value >> kDStatMaskShift) & kDStatChannelMask);
    uint32_t next = current & ~(kDStatChannelMask | (kDStatChannelMask << kDStatMaskShift) | kDStatIrqPending);
    next |= status | (mask << kDStatMaskShift);
    if ((status & mask) != 0u)
        next |= kDStatIrqPending;
    m_ioRegisters[kDmacStat] = next;
}

// Sets channel `cause`'s D_STAT flag and recomputes the summary bit.
void PS2Memory::raiseDmacStatus(uint32_t cause) {
    using namespace ps2hw;
    const auto it = m_ioRegisters.find(kDmacStat);
    uint32_t dstat = (it != m_ioRegisters.end() ? it->second : 0u) | (1u << cause);
    const uint32_t status = dstat & kDStatChannelMask;
    const uint32_t mask = (dstat >> kDStatMaskShift) & kDStatChannelMask;
    if ((status & mask) != 0u)
        dstat |= kDStatIrqPending;
    else
        dstat &= ~kDStatIrqPending;
    m_ioRegisters[kDmacStat] = dstat;
}

void PS2Memory::writeVif1Register(uint32_t address, uint32_t value) {
    using namespace ps2hw;
    // Fields narrower than 32 bits keep only their defined bits (EE User's Manual, VIF registers).
    switch (address) {
        case kVif1Fbrst:
            if (value & kVifFbrstRst) {
                const bool wasPath3Masked = m_path3Masked;
                std::memset(&vif1_regs, 0, sizeof(vif1_regs));
                m_vif1PendingPath2ImageQwc = 0u;
                m_vif1PendingPath2DirectHl = false;
                m_path3Masked = false;
                // A reset drops MSKPATH3, so PATH3 packets held back by it go out now.
                if (wasPath3Masked)
                    flushMaskedPath3Packets();
            }
            if (value & kVifFbrstStc)
                vif1_regs.stat &= ~kVifStatStallBits;
            break;
        case kVif1Mark:
            vif1_regs.mark = value & 0xFFFFu;
            vif1_regs.stat &= ~kVifStatMrk; // a CPU write to MARK clears STAT.MRK
            break;
        case kVif1Cycle:
            vif1_regs.cycle = value & 0xFFFFu;
            break;
        case kVif1Mode:
            vif1_regs.mode = value & 0x3u;
            break;
        case kVif1Num:
            vif1_regs.num = value & 0xFFu;
            break;
        case kVif1Mask:
            vif1_regs.mask = value;
            break;
        case kVif1Code:
            vif1_regs.code = value;
            break;
        case kVif1Itops:
            vif1_regs.itops = value & 0x3FFu;
            break;
        case kVif1Base:
            vif1_regs.base = value & 0x3FFu;
            break;
        case kVif1Ofst:
            vif1_regs.ofst = value & 0x3FFu;
            break;
        case kVif1Tops:
            vif1_regs.tops = value & 0x3FFu;
            break;
        case kVif1Itop:
            vif1_regs.itop = value & 0x3FFu;
            break;
        case kVif1Top:
            vif1_regs.top = value & 0x3FFu;
            break;
        default:
            break;
    }
}

std::vector<PS2Memory::PendingTransfer>& PS2Memory::pendingTransfersFor(uint32_t channelBase) {
    if (channelBase == ps2hw::kDmaGif)
        return m_pendingGifTransfers;
    if (channelBase == ps2hw::kDmaVif1)
        return m_pendingVif1Transfers;
    return m_pendingVif0Transfers;
}

// The EE wrote CHCR with STR set on `channelBase`. Scratchpad and IPU channels run here; GIF / VIF0 / VIF1 data is
// gathered into a pending transfer and normally sent on at once (processPendingTransfers). Other channels (SIF) are
// handled by the kernel stubs, so their start is only recorded.
void PS2Memory::startDmaChannel(uint32_t channelBase, uint32_t chcr) {
    using namespace ps2hw;
    const auto dctrlIt = m_ioRegisters.find(kDmacCtrl);
    const bool dmacEnabled = (dctrlIt == m_ioRegisters.end()) || ((dctrlIt->second & kDmacCtrlEnable) != 0u);
    if (!dmacEnabled)
        return;

    const uint32_t madr = m_ioRegisters[channelBase + kDmaMadr];
    // QWC is a 16-bit register; the upper bits of a 32-bit store are not part of it (a stray value there used to
    // make one GIF / VIF transfer walk gigabytes of the RAM ring).
    const uint32_t qwc = m_ioRegisters[channelBase + kDmaQwc] & 0xFFFFu;
    m_dmaStartCount.fetch_add(1, std::memory_order_relaxed);

    if (tryProcessScratchpadDma(channelBase, chcr))
        return;
    if ((channelBase == kDmaFromIpu || channelBase == kDmaToIpu) && tryProcessIpuDma(channelBase))
        return;

    const bool gsChannel = channelBase == kDmaGif || channelBase == kDmaVif1 || channelBase == kDmaVif0;
    // GIF and VIF1 end at the GS: without GS memory (unit tests that build none) their transfers are dropped.
    if (!gsChannel || (!m_gsVRAM && channelBase != kDmaVif0))
        return;

    if (chcrMode(chcr) == kDmaModeChain)
        gatherDmaChain(channelBase, chcr);
    else
        enqueueDmaTransfer(channelBase, chcr, madr, qwc); // normal and interleave mode: one block at MADR

    // A GIF transfer with nobody to receive it (no packet callback, no arbiter) stays queued.
    const bool autoProcessTransfers =
        (channelBase == kDmaGif) ? (m_gifPacketCallback || m_gifArbiter != nullptr) : true;
    if (autoProcessTransfers)
        processPendingTransfers();
}

// Queues one block of `qwCount` quadwords at guest `srcAddr`; the bytes are read when the queue is processed.
void PS2Memory::enqueueDmaTransfer(uint32_t channelBase, uint32_t chcr, uint32_t srcAddr, uint32_t qwCount) {
    if (qwCount == 0)
        return;
    PendingTransfer pt;
    pt.fromScratchpad = isScratchpad(srcAddr);
    pt.srcAddr = srcAddr;
    pt.qwc = qwCount;
    pt.toMemory = channelBase == ps2hw::kDmaVif1 && (chcr & ps2hw::kChcrDir) == 0u;
    pendingTransfersFor(channelBase).push_back(pt);
}

namespace {
// Address stack and cursor of a source-chain DMA (CALL / RET nest up to two levels through ASR0 / ASR1).
struct DmaChainCursor {
    uint32_t tagAddr = 0;
    uint32_t asr0 = 0;
    uint32_t asr1 = 0;
    uint32_t asp = 0; // 0..2 entries on the stack
};

// Applies one tag: sets where its data lives and moves the cursor to the next tag. Returns true when the chain ends
// after this tag's data.
bool stepDmaChain(DmaChainCursor& cursor, const DmaTagView& tag, uint32_t& dataAddr) {
    using namespace ps2hw;
    const uint32_t afterTag = cursor.tagAddr + 16u;
    switch (tag.id) {
        case kTagRefe:
            dataAddr = tag.addr;
            cursor.tagAddr = afterTag;
            return true;
        case kTagCnt:
            dataAddr = afterTag;
            cursor.tagAddr = afterTag + static_cast<uint32_t>(tag.qwc) * 16u;
            return false;
        case kTagNext:
            dataAddr = afterTag;
            cursor.tagAddr = tag.addr;
            return false;
        case kTagRef:
        case kTagRefs:
            dataAddr = tag.addr;
            cursor.tagAddr = afterTag;
            return false;
        case kTagCall: {
            dataAddr = afterTag;
            const uint32_t retAddr = afterTag + static_cast<uint32_t>(tag.qwc) * 16u;
            if (cursor.asp == 0u) {
                cursor.asr0 = retAddr;
                cursor.asp = 1u;
            } else if (cursor.asp == 1u) {
                cursor.asr1 = retAddr;
                cursor.asp = 2u;
            }
            cursor.tagAddr = tag.addr;
            return false;
        }
        case kTagRet:
            dataAddr = afterTag;
            if (cursor.asp == 2u) {
                cursor.tagAddr = cursor.asr1;
                cursor.asp = 1u;
            } else if (cursor.asp == 1u) {
                cursor.tagAddr = cursor.asr0;
                cursor.asp = 0u;
            } else {
                return true;
            }
            return false;
        default: // kTagEnd
            dataAddr = afterTag;
            return true;
    }
}

// PS2X_DBG_VP: log a VIF tag that UNPACKs V4-32 x4 to VU1 address 0 (the view-projection matrix), with the matrix.
void debugTraceViewMatrixTag(const uint8_t* rdram, const uint8_t* tagHost, uint32_t tagAddr, uint32_t dataAddr,
                             uint32_t qwc) {
    static const char* dbgUnp = std::getenv("PS2X_DBG_VP");
    if (!dbgUnp)
        return;
    uint64_t hi = 0;
    std::memcpy(&hi, tagHost + 8, 8);
    if ((hi >> 32) != 0x6c040000u && (hi & 0xffffffffu) != 0x6c040000u)
        return;
    const uint32_t da = dataAddr & 0x1ffffffu;
    float vpf[16];
    if (da + 64 <= PS2_RAM_SIZE)
        std::memcpy(vpf, rdram + da, 64);
    static int n = 0;
    if (n++ < 12)
        std::fprintf(stderr,
                     "[dbg-vp] tag@%08x hi=%016llx src=%08x qwc=%u : %g %g %g %g | %g "
                     "%g %g %g | %g %g %g %g | %g %g %g %g\n",
                     tagAddr, (unsigned long long)hi, dataAddr, (unsigned)qwc, vpf[0], vpf[1], vpf[2], vpf[3], vpf[4],
                     vpf[5], vpf[6], vpf[7], vpf[8], vpf[9], vpf[10], vpf[11], vpf[12], vpf[13], vpf[14], vpf[15]);
}

// PS2X_GIFDMA_LOG=N logs GIF DMA payloads of N or more quadwords. PS2X_GIFDMA_FIND reports GIF DMA payloads that start
// with a 4-vertex PRIM 0x7d ST/RGBAQ/FOG/XYZ2 strip (the Stratos glare rays), with the tag and source addresses.
void debugTraceGifPayload(const uint8_t* rdram, uint32_t tagAddr, bool tagInSpr, uint32_t id, uint32_t dataAddr,
                          uint32_t qwc, uint32_t chcr, const std::function<uint32_t()>& raFn) {
    static const int dbgGifDma = []() {
        const char* e = std::getenv("PS2X_GIFDMA_LOG");
        return e ? std::atoi(e) : 0;
    }();
    if (dbgGifDma > 0 && static_cast<int>(qwc) >= dbgGifDma)
        std::fprintf(stderr, "[gifdma] tag@%08x id=%u data=%08x qwc=%u\n", tagAddr, id, dataAddr,
                     static_cast<unsigned>(qwc));
    static const bool findGif = std::getenv("PS2X_GIFDMA_FIND") != nullptr;
    if (!findGif || tagInSpr)
        return;
    const uint32_t da = dataAddr & 0x1ffffffu;
    if (da + 16 > PS2_RAM_SIZE)
        return;
    uint64_t t0 = 0, t1 = 0;
    std::memcpy(&t0, rdram + da, 8);
    std::memcpy(&t1, rdram + da + 8, 8);
    const bool pre = (t0 >> 46) & 1;
    if (!pre || ((t0 >> 47) & 0x7ff) != 0x7d || (t0 & 0x7fff) != 4 || (t1 & 0xffff) != 0x5a12)
        return;
    static int nf = 0;
    if (nf < 3) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(rdram + da);
        const uint32_t* b = reinterpret_cast<const uint32_t*>(rdram + (tagAddr & 0x1ffffffu));
        std::fprintf(stderr, "  dmatag %08x %08x %08x %08x\n", b[0], b[1], b[2], b[3]);
        for (int q = 0; q < 8; ++q)
            std::fprintf(stderr, "  qw%d %08x %08x %08x %08x\n", q, w[q * 4], w[q * 4 + 1], w[q * 4 + 2], w[q * 4 + 3]);
    }
    if (nf++ < 40)
        std::fprintf(stderr, "[gifdma-find] tag@%08x data=%08x qwc=%u chcr=%08x ra=%08x\n", tagAddr, dataAddr,
                     static_cast<unsigned>(qwc), chcr, raFn ? raFn() : 0u);
}
} // namespace

// Appends `qwCount` quadwords from guest `srcAddr` to `out`. Main RAM and the scratchpad are read as rings: a
// physical address past the end of the block starts again at 0.
void PS2Memory::appendDmaPayload(std::vector<uint8_t>& out, uint32_t srcAddr, uint32_t qwCount) {
    uint32_t bytes = qwCount * 16u; // qwCount comes from a 16-bit tag field
    const bool scratch = isScratchpad(srcAddr);
    uint32_t src = translateAddress(srcAddr);
    const uint8_t* const base = scratch ? m_scratchpad : m_rdram;
    const uint32_t size = scratch ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
    while (bytes > 0) {
        if (src >= size)
            src = 0;
        const uint32_t chunk = std::min(bytes, size - src);
        out.insert(out.end(), base + src, base + src + chunk);
        bytes -= chunk;
        src += chunk;
    }
}

// Source-chain mode: follow the DMA tags from TADR, gathering every tag's data (and, with CHCR.TTE on a VIF channel,
// the tag's upper 64 bits, which carry two VIFcodes) into one buffer, queued as a single transfer. Writes back TADR,
// the address stack and CHCR as the hardware leaves them when the chain ends.
void PS2Memory::gatherDmaChain(uint32_t channelBase, uint32_t chcr) {
    using namespace ps2hw;
    // A guard against a chain that loops forever (a NEXT tag pointing at itself, garbage in RAM).
    constexpr int kMaxChainTags = 4096;

    DmaChainCursor cursor;
    cursor.tagAddr = m_ioRegisters[channelBase + kDmaTadr];
    cursor.asr0 = m_ioRegisters[channelBase + kDmaAsr0];
    cursor.asr1 = m_ioRegisters[channelBase + kDmaAsr1];
    cursor.asp = (chcr >> kChcrAspShift) & kChcrAspMask;
    const bool tieEnabled = (chcr & kChcrTie) != 0u;
    const bool isVif1 = channelBase == kDmaVif1;
    const bool transferTagData = (isVif1 || channelBase == kDmaVif0) && (chcr & kChcrTte) != 0u;

    // Gathered in a scratch vector that keeps its capacity from chain to chain; the block that travels is made in one
    // allocation at the end (runtime/ps2_chain_buffers.h). PS2X_DMA_CHAIN_POOL=0: a new vector grows with every
    // piece, as before.
    static thread_local std::vector<uint8_t> t_chainScratch;
    std::vector<uint8_t> chainOwn;
    const bool chainScratch = ps2x_chain_buffers::enabled();
    std::vector<uint8_t>& chainBuf = chainScratch ? t_chainScratch : chainOwn;
    chainBuf.clear();
    auto objectChain = drakan_object_motion::beginChain(isVif1);

    uint32_t lastTagUpper = (chcr >> kChcrTagShift) & 0xFFFFu;
    for (int tagsProcessed = 0; tagsProcessed < kMaxChainTags; ++tagsProcessed) {
        const uint32_t currentTagAddr = cursor.tagAddr;
        const bool tagInSpr = isScratchpad(currentTagAddr);
        uint32_t physTag = 0;
        try {
            physTag = translateAddress(currentTagAddr);
        } catch (...) {
            break; // TLB miss
        }
        const uint8_t* const tagBase = tagInSpr ? m_scratchpad : m_rdram;
        const uint32_t tagLimit = tagInSpr ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
        if (physTag > tagLimit - 16) // not physTag + 16: a TLB-mapped tag address near 4 GB would wrap the sum
            break;

        const uint8_t* const tagHost = tagBase + physTag;
        uint64_t rawTag = 0;
        std::memcpy(&rawTag, tagHost, sizeof(rawTag));
        const DmaTagView tag = decodeDmaTag(rawTag);
        lastTagUpper = tag.upper;

        uint32_t dataAddr = 0;
        bool endChain = stepDmaChain(cursor, tag, dataAddr);

        if (transferTagData) {
            if (!tagInSpr)
                debugTraceViewMatrixTag(m_rdram, tagHost, currentTagAddr, dataAddr, tag.qwc);
            // Tags whose data lives elsewhere (REFE / REF / REFS) are recorded for object-motion tracking with their
            // data address and second VIFcode.
            if (isVif1 && !tagInSpr) {
                uint32_t ref = 0, vif = 0;
                if (tag.id == kTagRefe || tag.id == kTagRef || tag.id == kTagRefs) {
                    std::memcpy(&ref, tagHost + 4u, 4);
                    std::memcpy(&vif, tagHost + 12u, 4);
                }
                objectChain.tag(physTag, static_cast<uint32_t>(chainBuf.size()), ref & 0x1fffffffu, vif);
            }
            chainBuf.insert(chainBuf.end(), tagHost + 8u, tagHost + 16u);
        }

        if (tag.qwc > 0) {
            if (channelBase == kDmaGif)
                debugTraceGifPayload(m_rdram, currentTagAddr, tagInSpr, tag.id, dataAddr, tag.qwc, chcr, m_debugRaFn);
            appendDmaPayload(chainBuf, dataAddr, tag.qwc);
        }
        if (tag.irq && tieEnabled)
            endChain = true;
        if (endChain)
            break;
    }

    m_ioRegisters[channelBase + kDmaTadr] = cursor.tagAddr;
    m_ioRegisters[channelBase + kDmaAsr0] = cursor.asr0;
    m_ioRegisters[channelBase + kDmaAsr1] = cursor.asr1;
    chcr = (chcr & ~(kChcrAspMask << kChcrAspShift)) | ((cursor.asp & kChcrAspMask) << kChcrAspShift);
    chcr = (chcr & 0x0000FFFFu) | (lastTagUpper << kChcrTagShift);
    m_ioRegisters[channelBase + kDmaChcr] = chcr;

    if (chainBuf.empty())
        return;
    PendingTransfer pt;
    if (chainScratch) {
        pt.chainData = ps2x_chain_buffers::copyOf(chainBuf.data(), chainBuf.size());
    } else {
        ps2x_chain_buffers::noteChain(chainBuf.size());
        pt.chainData = std::move(chainOwn);
    }
    if (isVif1)
        pt.objectMotion = objectChain.finish(static_cast<uint32_t>(pt.chainData.size()));
    pendingTransfersFor(channelBase).push_back(std::move(pt));
}

bool PS2Memory::tryProcessIpuDma(uint32_t channelBase) {
    const uint32_t madr = m_ioRegisters[channelBase + ps2hw::kDmaMadr];
    const uint32_t qwc = m_ioRegisters[channelBase + ps2hw::kDmaQwc] & 0xFFFFu;
    if (channelBase == ps2hw::kDmaToIpu) {
        const uint32_t bytes = qwc * 16u;
        uint32_t phys = 0;
        try {
            phys = translateAddress(madr & ~0xFu);
        } catch (const std::exception&) {
            return false;
        }
        if (phys >= PS2_RAM_SIZE || bytes > PS2_RAM_SIZE - phys)
            return false;
        if (std::getenv("PS2X_TRACE_IPU")) {
            std::fprintf(stderr, "[ipu] toIPU DMA madr=%08x qwc=%u slots:", madr, qwc);
            for (uint32_t a = 0x6ee700; a <= 0x6ee760; a += 16) {
                uint32_t w[4];
                std::memcpy(w, m_rdram + a, 16);
                std::fprintf(stderr, " [%x]=%x,%x,%x,%x", a, w[0], w[1], w[2], w[3]);
            }
            std::fprintf(stderr, "\n");
        }
        m_ipu.dmaToIpu(m_rdram + phys, bytes);
        m_ioRegisters[channelBase + ps2hw::kDmaMadr] = madr + bytes;
        m_ioRegisters[channelBase + ps2hw::kDmaQwc] = 0u;
        completeDmacChannel(channelBase, ps2hw::kDmaCauseToIpu);
        return true;
    }
    // fromIPU: serviced now if data is ready, otherwise when the next IDEC produces some
    m_ipuFromDmaActive = true;
    serviceIpuFromDma();
    return true;
}

void PS2Memory::serviceIpuFromDma() {
    constexpr uint32_t kCh = ps2hw::kDmaFromIpu;
    if (!m_ipuFromDmaActive)
        return;
    uint32_t madr = m_ioRegisters[kCh + ps2hw::kDmaMadr];
    uint32_t qwc = m_ioRegisters[kCh + ps2hw::kDmaQwc] & 0xFFFFu;
    if (qwc != 0u && m_ipu.outputAvailable() != 0u) {
        uint32_t phys = 0;
        try {
            phys = translateAddress(madr & ~0xFu);
        } catch (const std::exception&) {
            return;
        }
        uint32_t want = qwc * 16u;
        if (phys >= PS2_RAM_SIZE)
            return;
        want = std::min<uint32_t>(want, PS2_RAM_SIZE - phys);
        const uint32_t got = m_ipu.dmaFromIpu(m_rdram + phys, want);
        markModified(madr, got);
        madr += got;
        qwc -= got / 16u;
        m_ioRegisters[kCh + ps2hw::kDmaMadr] = madr;
        m_ioRegisters[kCh + ps2hw::kDmaQwc] = qwc;
    }
    if (qwc == 0u) {
        m_ipuFromDmaActive = false;
        completeDmacChannel(kCh, ps2hw::kDmaCauseFromIpu);
    }
}

bool PS2Memory::tryProcessScratchpadDma(uint32_t channelBase, uint32_t chcr) {
    constexpr uint32_t kSprFromChannel = ps2hw::kDmaFromSpr;
    constexpr uint32_t kSprToChannel = ps2hw::kDmaToSpr;
    if (channelBase != kSprFromChannel && channelBase != kSprToChannel)
        return false;

    if (ps2hw::chcrMode(chcr) != ps2hw::kDmaModeNormal)
        return false; // chain / interleave scratchpad DMA falls through to the generic path (ignored)

    const uint32_t qwc = m_ioRegisters[channelBase + ps2hw::kDmaQwc] & 0xFFFFu;
    const uint32_t byteCount = qwc * 16u;
    const uint32_t originalMadr = m_ioRegisters[channelBase + ps2hw::kDmaMadr] & 0x7FFFFFF0u;
    const uint32_t originalSadr = m_ioRegisters[channelBase + ps2hw::kDmaSadr] & 0x3FF0u;

    uint32_t mainOffset = 0u;
    try {
        mainOffset = translateAddress(originalMadr);
    } catch (const std::exception&) {
        return false;
    }

    if (mainOffset > PS2_RAM_SIZE || byteCount > PS2_RAM_SIZE - mainOffset)
        return false;

    const bool fromScratchpad = channelBase == kSprFromChannel;
    uint32_t scratchOffset = originalSadr;
    uint32_t bytesLeft = byteCount;
    uint32_t copied = 0u;
    while (bytesLeft != 0u) {
        const uint32_t scratchChunk = PS2_SCRATCHPAD_SIZE - scratchOffset;
        const uint32_t chunk = std::min(bytesLeft, scratchChunk);
        if (fromScratchpad) {
            std::memcpy(m_rdram + mainOffset + copied, m_scratchpad + scratchOffset, chunk);
            markModified(mainOffset + copied, chunk);
        } else {
            std::memcpy(m_scratchpad + scratchOffset, m_rdram + mainOffset + copied, chunk);
        }

        copied += chunk;
        bytesLeft -= chunk;
        scratchOffset = (scratchOffset + chunk) & (PS2_SCRATCHPAD_SIZE - 1u);
    }

    m_ioRegisters[channelBase + ps2hw::kDmaMadr] = (originalMadr + byteCount) & 0x7FFFFFF0u;
    m_ioRegisters[channelBase + ps2hw::kDmaQwc] = 0u;
    m_ioRegisters[channelBase + ps2hw::kDmaSadr] = (originalSadr + byteCount) & 0x3FF0u;
    completeDmacChannel(channelBase, fromScratchpad ? ps2hw::kDmaCauseFromSpr : ps2hw::kDmaCauseToSpr);
    return true;
}

// A channel finished: clear its CHCR.STR, flag it in D_STAT and queue the DMAC interrupt cause for the guest.
void PS2Memory::completeDmacChannel(uint32_t channelBase, uint32_t cause) {
    m_ioRegisters[channelBase + ps2hw::kDmaChcr] &= ~ps2hw::kChcrStr;
    raiseDmacStatus(cause);
    queueCompletedDmacCause(cause);
}

void PS2Memory::runVif1Job(const uint8_t* data, uint32_t sizeBytes, const drakan_object_motion::Job* motion) {
    drakan_object_motion::VifJob motionScope(motion);
    processVIF1Data(data, sizeBytes);
    if ((!vu1batch::captureActive || !vu1batch::captureDrain(this)) && m_gifArbiter)
        m_gifArbiter->drain();
}
void PS2Memory::runVif1Slice(const uint8_t* data, uint32_t size, uint32_t offset,
                             const drakan_object_motion::Job* motion, bool begin, bool end) {
    drakan_object_motion::VifJob motionScope(motion, begin, end);
    processVIF1Data(data, size, offset);
    if (end && (!vu1batch::captureActive || !vu1batch::captureDrain(this)) && m_gifArbiter)
        m_gifArbiter->drain();
}

void PS2Memory::dispatchVif1(std::vector<uint8_t>&& job, drakan_object_motion::JobPtr motion) {
    if (!vu1async::enabled()) {
        runVif1Job(job.data(), static_cast<uint32_t>(job.size()), motion.get());
        ps2x_chain_buffers::release(job);
        return;
    }
    vu1async::Meta meta;
    meta.objectMotion = std::move(motion);
    if (m_vu1MetaProvider)
        m_vu1MetaProvider(meta.dEn, meta.tEn, meta.ctx);
    vu1async::submit(this, std::move(job), meta);
}

// One GS local-to-host read into guest memory (runtime/ps2_readback.h). The blocking form waits for the VU1 worker
// and, inside the renderer bridge, for the GS worker: the read must see everything queued before it.
void PS2Memory::serveLocalToHost(uint8_t* dst, uint32_t bytes) {
    namespace rb = ps2x_readback;
    rb::State& state = rb::state();
    const uint64_t key = state.callerKey.exchange(0, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();
    bool served = false;
    if (rb::mode() == rb::Mode::Late && key && bytes <= rb::kMaxLateBytes && vu1async::renderQueueEnabled() &&
        !vu1async::onWorker()) {
        std::vector<uint8_t> previous(bytes);
        if (state.lookup(key, bytes, previous.data())) {
            if (rb::verify()) {
                vu1async::sync();
                m_localToHostFn(dst, bytes);
                state.verified.fetch_add(1, std::memory_order_relaxed);
                if (std::memcmp(dst, previous.data(), bytes) != 0)
                    state.verifyDifferent.fetch_add(1, std::memory_order_relaxed);
                state.store(key, dst, bytes);
            } else {
                std::memcpy(dst, previous.data(), bytes);
                // Runs on the VU1 worker behind the transfer set-up queued just before this read, so it takes exactly
                // this read's pixels. It owns its buffer; the guest buffer may be gone by then.
                vu1async::submitRenderTask([this, key, bytes] {
                    const auto keep = [key](std::vector<uint8_t>&& current) {
                        rb::state().store(key, current.data(), static_cast<uint32_t>(current.size()));
                    };
#if defined(PS2X_HAVE_GSR)
                    // The GS worker takes the pixels when it reaches this point of the stream: nothing waits.
                    if (drakan_dlss::readLocalToHostLater(bytes, keep))
                        return;
#endif
                    std::vector<uint8_t> current(bytes);
                    current.resize(m_localToHostFn(current.data(), bytes)); // waits for the GS on this worker
                    keep(std::move(current));
                });
                state.lateServed.fetch_add(1, std::memory_order_relaxed);
            }
            served = true;
        } else {
            state.lateMissed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!served) {
        vu1async::sync();
        m_localToHostFn(dst, bytes);
        if (key && bytes <= rb::kMaxLateBytes)
            state.store(key, dst, bytes);
    }
    state.reads.fetch_add(1, std::memory_order_relaxed);
    state.readBytes.fetch_add(bytes, std::memory_order_relaxed);
    state.waitNs.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()),
        std::memory_order_relaxed);
}

namespace {
// Visits `bytes` bytes starting at physical offset `start` of a memory block of `blockSize` bytes, the way the DMAC
// reads it: a run that reaches the end of the block carries on at offset 0. fn(offset, length) gets each contiguous
// run; the walk stops once fewer than `minRun` bytes are left.
template <typename Fn>
void forEachDmaRun(uint32_t start, uint32_t bytes, uint32_t blockSize, uint32_t minRun, Fn&& fn) {
    uint32_t at = start;
    while (bytes > 0 && bytes >= minRun) {
        if (at >= blockSize)
            at = 0;
        const uint32_t chunk = std::min(bytes, blockSize - at);
        fn(at, chunk);
        bytes -= chunk;
        at += chunk;
    }
}

uint32_t transferBytes(uint32_t qwc) {
    const uint64_t bytes = static_cast<uint64_t>(qwc) * 16ull;
    return bytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(bytes);
}
} // namespace

// Hands the queued GIF / VIF0 / VIF1 DMA transfers on, in that order, then marks those channels done. GIF (PATH3) and
// VIF0 work is done on this thread; VIF1 work goes to the VU1 worker when it runs (dispatchVif1).
void PS2Memory::processPendingTransfers() {
    const bool queueGif = vu1async::renderQueueEnabled() && !vu1async::onWorker() && m_pendingVif0Transfers.empty();
    if ((!m_pendingGifTransfers.empty() && !queueGif) || !m_pendingVif0Transfers.empty())
        vu1async::sync(); // PATH3 / VIF0 work must not overtake queued VIF1 work

    const bool hadGif = !m_pendingGifTransfers.empty();
    sendPendingGifTransfers(queueGif);

    const bool hadVif0 = !m_pendingVif0Transfers.empty();
    sendPendingVif0Transfers();

    // arbitrate what the EE thread itself queued (PATH3 DMA / VIF0 ...) before the VIF1 jobs start
    if (m_gifArbiter && ((hadGif && !queueGif) || hadVif0))
        m_gifArbiter->drain(); // the worker is idle here (synced at the top)

    const bool hadVif1 = !m_pendingVif1Transfers.empty();
    sendPendingVif1Transfers();

    // Every queued transfer has been handed on, so the channels report done (QWC counted down to 0).
    using namespace ps2hw;
    if (hadGif) {
        completeDmacChannel(kDmaGif, kDmaCauseGif);
        m_ioRegisters[kDmaGif + kDmaQwc] = 0;
    }
    if (hadVif0) {
        completeDmacChannel(kDmaVif0, kDmaCauseVif0);
        m_ioRegisters[kDmaVif0 + kDmaQwc] = 0;
    }
    if (hadVif1) {
        completeDmacChannel(kDmaVif1, kDmaCauseVif1);
        m_ioRegisters[kDmaVif1 + kDmaQwc] = 0;
    }
}

// PATH3: each transfer goes to the GIF arbiter now, or (queueGif) to the VU1 worker's render queue behind the VIF1 work
// already queued there.
void PS2Memory::sendPendingGifTransfers(bool queueGif) {
    const auto emitGif = [&](const uint8_t* data, uint32_t size) {
        if (queueGif)
            vu1async::submitPath3(this, std::vector<uint8_t>(data, data + size));
        else
            submitGifPacket(GifPathId::Path3, data, size, false);
    };
    // GIF_STAT.FQC shows how full the 16-quadword GIF FIFO is; games poll it to see PATH3 is busy.
    uint32_t observedGifQwc = 0u;
    for (const auto& transfer : m_pendingGifTransfers) {
        const uint64_t transferQwc = !transfer.chainData.empty() ? (transfer.chainData.size() / 16u) : transfer.qwc;
        observedGifQwc =
            static_cast<uint32_t>(std::min<uint64_t>(16u, static_cast<uint64_t>(observedGifQwc) + transferQwc));
    }
    if (observedGifQwc != 0u) {
        uint32_t& gifStat = m_ioRegisters[ps2hw::kGifStat];
        gifStat = (gifStat & ~ps2hw::kGifStatFqcMask) | (observedGifQwc << ps2hw::kGifStatFqcShift);
    }

    for (auto& p : m_pendingGifTransfers) {
        if (!p.chainData.empty()) {
            m_seenGifCopy = true;
            m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);
            if (queueGif)
                vu1async::submitPath3(this, std::move(p.chainData));
            else {
                emitGif(p.chainData.data(), static_cast<uint32_t>(p.chainData.size()));
                ps2x_chain_buffers::release(p.chainData);
            }
            continue;
        }
        if (p.qwc == 0)
            continue;
        uint32_t srcPhys = 0;
        try {
            srcPhys = translateAddress(p.srcAddr);
        } catch (const std::exception&) {
            continue;
        }
        const uint8_t* const base = p.fromScratchpad ? m_scratchpad : m_rdram;
        const uint32_t blockSize = p.fromScratchpad ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
        forEachDmaRun(srcPhys, transferBytes(p.qwc), blockSize, 16u, [&](uint32_t at, uint32_t chunk) {
            m_seenGifCopy = true;
            m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);
            emitGif(base + at, chunk);
        });
    }
    m_pendingGifTransfers.clear();
}

void PS2Memory::sendPendingVif0Transfers() {
    for (auto& p : m_pendingVif0Transfers) {
        if (!p.chainData.empty()) {
            processVIF0Data(p.chainData.data(), static_cast<uint32_t>(p.chainData.size()));
            ps2x_chain_buffers::release(p.chainData);
            continue;
        }
        if (p.qwc == 0)
            continue;
        uint32_t srcPhys = 0;
        try {
            srcPhys = translateAddress(p.srcAddr);
        } catch (const std::exception&) {
            continue;
        }
        if (p.fromScratchpad)
            forEachDmaRun(srcPhys, transferBytes(p.qwc), PS2_SCRATCHPAD_SIZE, 1u,
                          [&](uint32_t at, uint32_t chunk) { processVIF0Data(m_scratchpad + at, chunk); });
        else
            forEachDmaRun(srcPhys, transferBytes(p.qwc), PS2_RAM_SIZE, 1u,
                          [&](uint32_t at, uint32_t chunk) { processVIF0Data(at, chunk); });
    }
    m_pendingVif0Transfers.clear();
}

void PS2Memory::sendPendingVif1Transfers() {
    for (auto& p : m_pendingVif1Transfers) {
        if (p.toMemory) {
            // CHCR.DIR=0 on the VIF1 channel: data flows VIF1 FIFO -> memory (GS local->host read via BUSDIR). It is
            // NOT a VIF packet.
            if (m_localToHostFn && p.qwc > 0 && !p.fromScratchpad) {
                uint32_t dstPhys = 0;
                try {
                    dstPhys = translateAddress(p.srcAddr);
                } catch (const std::exception&) {
                    continue;
                }
                uint32_t bytes = p.qwc * 16u;
                if (dstPhys < PS2_RAM_SIZE) {
                    if (static_cast<uint64_t>(dstPhys) + bytes > PS2_RAM_SIZE)
                        bytes = PS2_RAM_SIZE - dstPhys;
                    serveLocalToHost(m_rdram + dstPhys, bytes);
                }
            }
            continue;
        }
        std::vector<uint8_t> job;
        if (!p.chainData.empty()) {
            job = std::move(p.chainData);
        } else if (p.qwc > 0) {
            // Copied now: the VU1 worker may run the job after the guest has reused the source memory.
            uint32_t srcPhys = 0;
            try {
                srcPhys = translateAddress(p.srcAddr);
            } catch (const std::exception&) {
                continue;
            }
            const uint8_t* const base = p.fromScratchpad ? m_scratchpad : m_rdram;
            const uint32_t blockSize = p.fromScratchpad ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
            const uint32_t bytes = transferBytes(p.qwc);
            job.reserve(bytes);
            forEachDmaRun(srcPhys, bytes, blockSize, 1u,
                          [&](uint32_t at, uint32_t chunk) { job.insert(job.end(), base + at, base + at + chunk); });
        }
        if (!job.empty())
            dispatchVif1(std::move(job), std::move(p.objectMotion));
    }
    m_pendingVif1Transfers.clear();
}

void PS2Memory::queueCompletedDmacCause(uint32_t cause) {
    std::lock_guard<std::mutex> lock(m_completedDmacMutex);
    m_completedDmacCauses.push_back(cause);
    m_completedDmacCount.store(static_cast<uint32_t>(m_completedDmacCauses.size()), std::memory_order_release);
}

std::vector<uint32_t> PS2Memory::consumeCompletedDmacCauses() {
    // Polled at every checkpoint and DMA kick; almost always empty.
    if (m_completedDmacCount.load(std::memory_order_acquire) == 0)
        return {};
    std::lock_guard<std::mutex> lock(m_completedDmacMutex);
    std::vector<uint32_t> causes;
    causes.swap(m_completedDmacCauses);
    m_completedDmacCount.store(0, std::memory_order_release);
    return causes;
}

void PS2Memory::flushMaskedPath3Packets(bool drainImmediately) {
    if (m_path3Masked || m_path3MaskedFifo.empty())
        return;

    auto emit = [&](const uint8_t* packetData, uint32_t packetSize) {
        if (m_gifArbiter)
            m_gifArbiter->submit(GifPathId::Path3, packetData, packetSize, false);
        else if (m_gifPacketCallback)
            m_gifPacketCallback(packetData, packetSize);
    };

    for (const auto& packet : m_path3MaskedFifo) {
        if (packet.size() >= 16u)
            emit(packet.data(), static_cast<uint32_t>(packet.size()));
    }
    m_path3MaskedFifo.clear();

    if (m_gifArbiter && drainImmediately)
        m_gifArbiter->drain();
}

bool PS2Memory::isPath3Masked() const {
    vu1async::sync();
    return m_path3Masked;
}

void PS2Memory::submitGifPacket(GifPathId pathId, const uint8_t* data, uint32_t sizeBytes, bool drainImmediately,
                                bool path2DirectHl) {
    if (vu1batch::captureActive && vu1batch::captureGif(this, pathId, data, sizeBytes, drainImmediately, path2DirectHl))
        return;
    vu1async::sync(); // no-op on the VU1 worker itself
    if (!data || sizeBytes < 16)
        return;

    if (pathId == GifPathId::Path3) {
        if (m_path3Masked) {
            m_path3MaskedFifo.emplace_back(data, data + sizeBytes);
            return;
        }
        flushMaskedPath3Packets(false);
    }

    if (m_gifArbiter && drainImmediately && pathId != GifPathId::Path3 && m_gifArbiter == gif_direct::g_arbiter &&
        gif_direct::g_gs && m_gifArbiter->empty()) {
        // Equivalent to submit + drain of a single packet, without copying it into the arbiter queue.
        const bool directHl = pathId == GifPathId::Path2 && path2DirectHl;
        GsCapture::instance().onPacket(static_cast<uint8_t>(pathId), directHl, false, data, sizeBytes);
        gif_direct::g_gs->processGIFPacketPath(static_cast<uint8_t>(pathId), data, sizeBytes);
        return;
    }

    if (m_gifArbiter)
        m_gifArbiter->submit(pathId, data, sizeBytes, path2DirectHl);
    else if (m_gifPacketCallback)
        m_gifPacketCallback(data, sizeBytes);

    if (m_gifArbiter && drainImmediately)
        m_gifArbiter->drain();
}

void PS2Memory::processGIFPacket(uint32_t srcPhysAddr, uint32_t qwCount) {
    if (!m_rdram || qwCount == 0)
        return;
    const uint64_t bytes64 = static_cast<uint64_t>(qwCount) * 16ull;
    uint32_t sizeBytes = (bytes64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32_t>(bytes64);
    uint32_t bytesLeft = sizeBytes;
    while (bytesLeft >= 16) {
        if (srcPhysAddr >= PS2_RAM_SIZE)
            srcPhysAddr = 0;
        uint32_t chunk = bytesLeft;
        if (srcPhysAddr + chunk > PS2_RAM_SIZE)
            chunk = PS2_RAM_SIZE - srcPhysAddr;
        if (chunk == 0)
            break;

        m_seenGifCopy = true;
        m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);
        submitGifPacket(GifPathId::Path3, m_rdram + srcPhysAddr, chunk);

        bytesLeft -= chunk;
        srcPhysAddr += chunk;
    }
}

void PS2Memory::processGIFPacket(const uint8_t* data, uint32_t sizeBytes) {
    if (m_gifArbiter)
        submitGifPacket(GifPathId::Path3, data, sizeBytes);
    else if (m_gifPacketCallback && data && sizeBytes >= 16)
        m_gifPacketCallback(data, sizeBytes);
}

bool PS2Memory::tryProcessNativeGifImageUploadChain(GS& gs, uint32_t tadr, uint32_t chcr) {
    if (vu1async::renderQueueEnabled() && gs.d3d11Active())
        return false;
    vu1async::sync();
    static constexpr uint32_t GIF_CHANNEL = 0x1000A000u;
    static constexpr uint32_t D_STAT = 0x1000E010u;
    static constexpr uint32_t D_CTRL = 0x1000E000u;

    if (!m_rdram || !m_gsVRAM || m_path3Masked || gs.d3d11Active())
        return false;
    if (m_gifArbiter && !m_gifArbiter->empty())
        return false;
    if ((chcr & 0x100u) == 0u || ((chcr >> 2u) & 0x3u) != 1u)
        return false;
    if ((chcr & (1u << 7u)) != 0u || ((chcr >> 4u) & 0x3u) != 0u)
        return false;

    const auto dctrlIt = m_ioRegisters.find(D_CTRL);
    if (dctrlIt != m_ioRegisters.end() && ((dctrlIt->second & 0x1u) == 0u))
        return false;

    auto resolveContiguous = [&](uint32_t guestAddr, uint32_t bytes, const uint8_t*& out) -> bool {
        try {
            const bool scratch = isScratchpad(guestAddr);
            const uint32_t phys = translateAddress(guestAddr);
            const uint8_t* base = scratch ? m_scratchpad : m_rdram;
            const uint32_t limit = scratch ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
            if (!base || phys > limit || bytes > limit - phys)
                return false;
            out = base + phys;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };

    auto loadDmaTagAt = [&](uint32_t guestAddr, DmaTagView& out) -> bool {
        const uint8_t* ptr = nullptr;
        if (!resolveContiguous(guestAddr, 16u, ptr))
            return false;
        out = decodeDmaTag(loadScalar<uint64_t>(ptr, 0u, 16u, "native gif dma tag", guestAddr));
        return true;
    };

    auto decodeSetupPayload = [&](const uint8_t* payload, uint64_t(&regs)[4]) -> bool {
        const uint64_t tagLo = loadScalar<uint64_t>(payload, 0u, 80u, "native gif setup tag", 0u);
        const uint64_t tagHi = loadScalar<uint64_t>(payload, 8u, 80u, "native gif setup regs", 0u);
        if (gifTagNloop(tagLo) != 4u || gifTagFlg(tagLo) != GIF_FMT_PACKED || gifTagNreg(tagLo) != 1u ||
            (tagHi & 0xFull) != 0x0Eull) {
            return false;
        }

        static constexpr uint8_t kExpectedRegs[4] = {
            GS_REG_BITBLTBUF,
            GS_REG_TRXPOS,
            GS_REG_TRXREG,
            GS_REG_TRXDIR,
        };

        uint32_t offset = 16u;
        for (uint32_t i = 0; i < 4u; ++i) {
            regs[i] = loadScalar<uint64_t>(payload, offset, 80u, "native gif setup value", 0u);
            const uint64_t reg = loadScalar<uint64_t>(payload, offset + 8u, 80u, "native gif setup register", 0u);
            if ((reg & 0xFFu) != kExpectedRegs[i])
                return false;
            offset += 16u;
        }

        const uint32_t trxdirMode = static_cast<uint32_t>(regs[3] & 0x3ull);
        const uint32_t rrw = static_cast<uint32_t>(regs[2] & 0xFFFull);
        const uint32_t rrh = static_cast<uint32_t>((regs[2] >> 32u) & 0xFFFull);
        return trxdirMode == 0u && rrw != 0u && rrh != 0u;
    };

    DmaTagView setupTag{};
    if (!loadDmaTagAt(tadr, setupTag) || setupTag.id != 1u || setupTag.qwc != 5u || setupTag.irq) {
        return false;
    }

    const uint8_t* setupPayload = nullptr;
    const uint32_t setupPayloadAddr = tadr + 16u;
    if (!resolveContiguous(setupPayloadAddr, 5u * 16u, setupPayload))
        return false;

    uint64_t setupRegs[4] = {};
    if (!decodeSetupPayload(setupPayload, setupRegs))
        return false;

    uint32_t imageTagDmaAddr = setupPayloadAddr + 5u * 16u;
    DmaTagView imageTagDma{};
    if (!loadDmaTagAt(imageTagDmaAddr, imageTagDma) || imageTagDma.id != 1u || imageTagDma.qwc != 1u ||
        imageTagDma.irq) {
        return false;
    }

    const uint8_t* imageGifTag = nullptr;
    if (!resolveContiguous(imageTagDmaAddr + 16u, 16u, imageGifTag))
        return false;

    const uint64_t imageTagLo =
        loadScalar<uint64_t>(imageGifTag, 0u, 16u, "native gif image tag", imageTagDmaAddr + 16u);
    if (gifTagFlg(imageTagLo) != GIF_FMT_IMAGE)
        return false;

    const uint32_t imageQwc = gifTagNloop(imageTagLo);
    if (imageQwc == 0u)
        return false;

    const uint64_t imageBytes64 = static_cast<uint64_t>(imageQwc) * 16ull;
    if (imageBytes64 > 0xFFFFFFFFull)
        return false;
    const uint32_t imageBytes = static_cast<uint32_t>(imageBytes64);

    const uint32_t payloadTagAddr = imageTagDmaAddr + 32u;
    DmaTagView payloadTag{};
    if (!loadDmaTagAt(payloadTagAddr, payloadTag) || payloadTag.qwc != imageQwc || payloadTag.irq) {
        return false;
    }

    uint32_t imageDataAddr = 0u;
    uint32_t finalTadr = payloadTagAddr;
    uint32_t lastTagUpper = payloadTag.upper;
    if (payloadTag.id == 3u || payloadTag.id == 4u) {
        imageDataAddr = payloadTag.addr;
        const uint32_t terminalTagAddr = payloadTagAddr + 16u;
        DmaTagView terminalTag{};
        if (!loadDmaTagAt(terminalTagAddr, terminalTag) || terminalTag.qwc != 0u || terminalTag.irq ||
            (terminalTag.id != 0u && terminalTag.id != 7u)) {
            return false;
        }
        finalTadr = (terminalTag.id == 0u) ? (terminalTagAddr + 16u) : terminalTagAddr;
        lastTagUpper = terminalTag.upper;
    } else if (payloadTag.id == 7u) {
        imageDataAddr = payloadTagAddr + 16u;
        finalTadr = payloadTagAddr;
    } else {
        return false;
    }

    const uint8_t* imageData = nullptr;
    if (!resolveContiguous(imageDataAddr, imageBytes, imageData))
        return false;

    m_dmaStartCount.fetch_add(1, std::memory_order_relaxed);
    m_seenGifCopy = true;
    m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);
    gs.uploadImageNative(setupRegs[0], setupRegs[1], setupRegs[2], setupRegs[3], imageData, imageBytes);

    m_ioRegisters[GIF_CHANNEL + 0x30u] = finalTadr;
    m_ioRegisters[GIF_CHANNEL + 0x40u] = 0u;
    m_ioRegisters[GIF_CHANNEL + 0x50u] = 0u;
    m_ioRegisters[GIF_CHANNEL + 0x00u] = ((chcr & 0x0000FFFFu) | (lastTagUpper << 16u)) & ~0x100u;
    m_ioRegisters[GIF_CHANNEL + 0x20u] = 0u;

    uint32_t dstat = m_ioRegisters.count(D_STAT) ? m_ioRegisters[D_STAT] : 0u;
    dstat |= (1u << 2u);
    const uint32_t status = dstat & 0x3FFu;
    const uint32_t mask = (dstat >> 16u) & 0x3FFu;
    if ((status & mask) != 0u)
        dstat |= (1u << 31u);
    else
        dstat &= ~(1u << 31u);
    m_ioRegisters[D_STAT] = dstat;
    queueCompletedDmacCause(2u);
    return true;
}

bool PS2Memory::tryProcessNativeGifPackedChain(GS& gs, uint32_t tadr, uint32_t chcr) {
    if (vu1async::renderQueueEnabled() && gs.d3d11Active())
        return false;
    vu1async::sync();
    static constexpr uint32_t GIF_CHANNEL = 0x1000A000u;
    static constexpr uint32_t D_STAT = 0x1000E010u;
    static constexpr uint32_t D_CTRL = 0x1000E000u;

    if (!m_rdram || !m_gsVRAM || m_path3Masked || gs.d3d11Active())
        return false;
    if (m_gifArbiter && !m_gifArbiter->empty())
        return false;
    if ((chcr & 0x100u) == 0u || ((chcr >> 2u) & 0x3u) != 1u)
        return false;
    if ((chcr & (1u << 7u)) != 0u || ((chcr >> 4u) & 0x3u) != 0u)
        return false;

    const auto dctrlIt = m_ioRegisters.find(D_CTRL);
    if (dctrlIt != m_ioRegisters.end() && ((dctrlIt->second & 0x1u) == 0u))
        return false;

    auto resolveContiguous = [&](uint32_t guestAddr, uint32_t bytes, const uint8_t*& out) -> bool {
        try {
            const bool scratch = isScratchpad(guestAddr);
            const uint32_t phys = translateAddress(guestAddr);
            const uint8_t* base = scratch ? m_scratchpad : m_rdram;
            const uint32_t limit = scratch ? PS2_SCRATCHPAD_SIZE : PS2_RAM_SIZE;
            if (!base || phys > limit || bytes > limit - phys)
                return false;
            out = base + phys;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };

    const uint8_t* tagPtr = nullptr;
    if (!resolveContiguous(tadr, 16u, tagPtr))
        return false;

    const DmaTagView tag = decodeDmaTag(loadScalar<uint64_t>(tagPtr, 0u, 16u, "native packed gif dma tag", tadr));
    if (tag.id != 7u || tag.qwc == 0u || tag.irq)
        return false;

    const uint64_t payloadBytes64 = static_cast<uint64_t>(tag.qwc) * 16ull;
    if (payloadBytes64 > 0xFFFFFFFFull)
        return false;
    const uint32_t payloadBytes = static_cast<uint32_t>(payloadBytes64);

    const uint8_t* payload = nullptr;
    if (!resolveContiguous(tadr + 16u, payloadBytes, payload))
        return false;
    if (!gs.processNativePackedGIFPacket(payload, payloadBytes))
        return false;

    m_dmaStartCount.fetch_add(1, std::memory_order_relaxed);
    m_seenGifCopy = true;
    m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);

    m_ioRegisters[GIF_CHANNEL + 0x30u] = tadr;
    m_ioRegisters[GIF_CHANNEL + 0x40u] = 0u;
    m_ioRegisters[GIF_CHANNEL + 0x50u] = 0u;
    m_ioRegisters[GIF_CHANNEL + 0x00u] = ((chcr & 0x0000FFFFu) | (tag.upper << 16u)) & ~0x100u;
    m_ioRegisters[GIF_CHANNEL + 0x20u] = 0u;

    uint32_t dstat = m_ioRegisters.count(D_STAT) ? m_ioRegisters[D_STAT] : 0u;
    dstat |= (1u << 2u);
    const uint32_t status = dstat & 0x3FFu;
    const uint32_t mask = (dstat >> 16u) & 0x3FFu;
    if ((status & mask) != 0u)
        dstat |= (1u << 31u);
    else
        dstat &= ~(1u << 31u);
    m_ioRegisters[D_STAT] = dstat;
    queueCompletedDmacCause(2u);
    return true;
}

int PS2Memory::pollDmaRegisters() {
    return 0;
}

uint32_t PS2Memory::readIORegister(uint32_t address) {
    using namespace ps2hw;
    // GIF_MODE is written only by the EE. Unlike GIF_STAT/VIF registers it
    // neither observes nor changes worker-owned state. The register table is
    // also EE-owned; VIF1 jobs use vif1_regs and the ordered GIF arbiter.
    if (address == kGifMode && vu1async::renderQueueEnabled()) {
        const auto it = m_ioRegisters.find(address);
        return it == m_ioRegisters.end() ? 0u : it->second;
    }
    if (address >= kGifVifRegsBegin && address < kGifVifRegsEnd)
        vu1async::sync(); // the VU1 worker updates GIF / VIF state
    size_t timerIndex = 0u;
    uint32_t timerOffset = 0u;
    if (decodeEeTimerRegister(address, timerIndex, timerOffset)) {
        const EeTimer& timer = m_eeTimers[timerIndex];
        switch (timerOffset) {
            case kEeTimerCountOffset:
                return timer.count & 0xFFFFu;
            case kEeTimerModeOffset:
                return timer.mode & (kEeTimerModeConfigMask | kEeTimerModeStatusMask);
            case kEeTimerCompareOffset:
                return timer.compare & 0xFFFFu;
            case kEeTimerHoldOffset:
                return timer.hold & 0xFFFFu;
            default:
                return 0u;
        }
    }

    if (isGsPrivReg(address)) {
        // NB: unreachable from read8/16/32/64 today, same reasoning as the write
        // path above; kept correct for direct callers.
        const uint32_t off = address & 7u;
        const uint32_t regOff = (address - PS2_GS_PRIV_REG_BASE) & ~0x7u;
        if (regOff == kGsCsrRegOffset) {
            return static_cast<uint32_t>((gs_regs.csr.load() >> (off * 8u)) & 0xFFFFFFFFull);
        }
        if (uint64_t* reg = gsRegPtr(gs_regs, address)) {
            return static_cast<uint32_t>((*reg >> (off * 8u)) & 0xFFFFFFFFull);
        }
        return 0u;
    }

    if (address >= kIpuRegsBegin && address <= kIpuRegsLast) {
        return m_ipu.readReg(address);
    }

    if (address == kGifStat) {
        uint32_t stat = m_ioRegisters.count(address) ? m_ioRegisters[address] : 0u;
        const uint32_t mode = m_ioRegisters.count(kGifMode) ? m_ioRegisters[kGifMode] : 0u;
        const uint32_t ctrl = m_ioRegisters.count(kGifCtrl) ? m_ioRegisters[kGifCtrl] : 0u;

        // M3R and IMT mirror GIF_MODE, PSE mirrors GIF_CTRL, and M3P is the
        // effective PATH3 mask controlled by the VIF1 MSKPATH3 command.
        stat = (stat & ~0xFu) | (mode & 0x1u) | (m_path3Masked ? 0x2u : 0u) | (mode & 0x4u) | (ctrl & 0x8u);
        return stat;
    }

    if (address >= 0x10000000 && address < 0x10010000) {
        if (address >= kDmaChannelsBegin && address < kDmaChannelsEnd) {
            // DMA runs to completion when it starts (writeIORegister), so a CHCR read never sees the channel busy.
            if ((address & ~kDmaChannelBaseMask) == kDmaChcr) {
                uint32_t channelStatus = m_ioRegisters[address] & ~kChcrStr;
                m_ioRegisters[address] = channelStatus;
                return channelStatus;
            }
        }

        if (address >= 0x10000200 && address < 0x10000300) {
            return 0;
        }

        // SIF / SBUS registers (EE-IOP mailbox). Fixed values tell the guest's SIF code the IOP side is up.
        if (address >= 0x1000F200 && address <= 0x1000F260) {
            if (address == 0x1000F230) { // SB_SMFLG
                return 0x60000;
            }
            if (address == 0x1000F240) {
                return 0xF0000002;
            }
            return 0;
        }
    }

    auto it = m_ioRegisters.find(address);
    if (it != m_ioRegisters.end()) {
        return it->second;
    }

    return 0;
}

void PS2Memory::registerCodeRegion(uint32_t start, uint32_t end) {
    if (end <= start) {
        std::cerr << "Ignoring invalid code region: start=0x" << std::hex << start << " end=0x" << end << std::dec
                  << std::endl;
        return;
    }

    if ((end - start) > PS2_RAM_SIZE) {
        std::cerr << "Ignoring oversized code region: start=0x" << std::hex << start << " end=0x" << end << std::dec
                  << std::endl;
        return;
    }

    for (const auto& existing : m_codeRegions) {
        if (existing.start == start && existing.end == end) {
            return;
        }
    }

    CodeRegion region;
    region.start = start;
    region.end = end;

    size_t sizeInWords = (end - start + 3u) / 4u;
    region.modified.resize(sizeInWords, false);

    m_codeRegions.push_back(region);
    RUNTIME_LOG("Registered code region: " << std::hex << start << " - " << end << std::dec);
}

bool PS2Memory::isAddressInRegion(uint32_t address, const CodeRegion& region) {
    return (address >= region.start && address < region.end);
}

bool PS2Memory::isCodeAddress(uint32_t address) const {
    for (const auto& region : m_codeRegions) {
        if (address >= region.start && address < region.end) {
            return true;
        }
    }
    return false;
}

void PS2Memory::markModified(uint32_t address, uint32_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (auto& region : m_codeRegions) {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd) {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4) {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size()) {
                region.modified[bitIndex] = true;
                RUNTIME_LOG("Marked code at " << std::hex << addr << std::dec << " as modified");
            }
        }
    }
}

bool PS2Memory::isCodeModified(uint32_t address, uint32_t size) {
    if (size == 0) {
        return false;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (const auto& region : m_codeRegions) {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd) {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4) {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size() && region.modified[bitIndex]) {
                return true; // Found modified code
            }
        }
    }

    return false; // No modifications found
}

void PS2Memory::clearModifiedFlag(uint32_t address, uint32_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (auto& region : m_codeRegions) {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd) {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4) {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size()) {
                region.modified[bitIndex] = false;
            }
        }
    }
}
