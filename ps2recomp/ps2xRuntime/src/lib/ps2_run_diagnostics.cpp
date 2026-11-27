#include "ps2_run_diagnostics.h"

#include "drakan_present_clock.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
double envSeconds(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::atof(value) : 0.0;
}

constexpr double kStallSeconds = 8.0; // a frame boundary missing this long counts as a freeze
constexpr int kMaxStallDumps = 6;
constexpr size_t kProfileTopCount = 25;
} // namespace

void PcProfiler::startFromEnvironment(const EeScheduler& scheduler) {
    const char* prof = std::getenv("PS2X_PROFILE");
    if (!prof || m_thread.joinable())
        return;
    const double profSeconds = std::atof(prof);
    m_thread = std::thread([this, &scheduler, profSeconds]() {
        std::unordered_map<uint32_t, uint32_t> hist;
        const auto t0 = std::chrono::steady_clock::now();
        while (!m_stop.load() &&
               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < profSeconds) {
            uint32_t pc = 0, ra = 0, sp = 0;
            int tid = 0;
            if (scheduler.sampleLive(pc, ra, sp, tid))
                ++hist[pc];
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        std::vector<std::pair<uint32_t, uint32_t>> v(hist.begin(), hist.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::cerr << "[profile] top pcs:";
        for (size_t i = 0; i < v.size() && i < kProfileTopCount; ++i)
            std::cerr << " " << std::hex << v[i].first << "x" << std::dec << v[i].second;
        std::cerr << std::endl;
    });
}

void PcProfiler::stop() {
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

StallWatchdog::StallWatchdog(const EeScheduler& scheduler, PS2Memory& memory, HistoryFn dispatchHistory)
    : m_scheduler(scheduler), m_memory(memory), m_dispatchHistory(dispatchHistory),
      m_periodSeconds(envSeconds("PS2X_WATCHDOG")), m_maxSeconds(envSeconds("PS2X_MAX_SECONDS")), m_start(Clock::now()),
      m_lastDump(m_start), m_beatSince(m_start) {}

double StallWatchdog::secondsSinceStart() const {
    return std::chrono::duration<double>(Clock::now() - m_start).count();
}

bool StallWatchdog::poll() {
    const auto now = Clock::now();
    const bool stall = heartbeatStalled(now);
    if (stall || (m_periodSeconds > 0.0 && std::chrono::duration<double>(now - m_lastDump).count() >= m_periodSeconds))
        dump(now, stall);
    if (m_maxSeconds > 0.0 && std::chrono::duration<double>(now - m_start).count() >= m_maxSeconds) {
        std::cerr << "[run] PS2X_MAX_SECONDS reached, stopping" << std::endl;
        return false;
    }
    return true;
}

// True once per stall: the heartbeat has not moved for kStallSeconds (0 means the game has not reached its first
// frame yet, which is not a stall).
bool StallWatchdog::heartbeatStalled(Clock::time_point now) {
    const uint64_t beat = drakan_present_clock::g_guestHeartbeat.load(std::memory_order_relaxed);
    if (beat != m_lastBeat || beat == 0) {
        m_lastBeat = beat;
        m_beatSince = now;
        m_stallReported = false;
        return false;
    }
    if (m_stallReported || m_stallDumps >= kMaxStallDumps ||
        std::chrono::duration<double>(now - m_beatSince).count() < kStallSeconds)
        return false;
    m_stallReported = true;
    ++m_stallDumps;
    std::cerr << "[stall] guest input boundary silent for 8 s (frame " << beat << ")" << std::endl;
    return true;
}

void StallWatchdog::dump(Clock::time_point now, bool stall) {
    m_lastDump = now;
    uint32_t spc = 0, sra = 0, ssp = 0;
    int stid = 0;
    const bool ok = m_scheduler.sampleLive(spc, sra, ssp, stid);
    std::cerr << "[watchdog t=" << std::fixed << std::setprecision(1)
              << std::chrono::duration<double>(now - m_start).count() << "s] live thread=" << stid << " pc=0x"
              << std::hex << (ok ? spc : 0) << " ra=0x" << sra << " sp=0x" << ssp << std::dec << " | "
              << m_dispatchHistory() << std::endl;
    // Explicit forensic output only: one best-effort snapshot on an actual silent-input stall, never on an ordinary
    // periodic watchdog tick. No guest pause or memory/register writes.
    if (stall && !m_snapshotAttempted) {
        m_snapshotAttempted = true;
        writeForensicSnapshot(stid);
    }
    printPeekedWords();
    const auto snap = m_scheduler.snapshot();
    for (const auto& sm : snap.semaphores)
        std::cerr << "    sema " << sm.id << " count=" << sm.count << "/" << sm.maxCount << " waiters=" << sm.waiters
                  << std::endl;
    for (const auto& t : snap.threads) {
        std::cerr << "    thread " << t.id << " status=" << static_cast<int>(t.status)
                  << " wait=" << static_cast<int>(t.waitReason) << " waitId=" << t.waitId << " pc=0x" << std::hex
                  << t.pc << " ra=0x" << t.ra << " entry=0x" << t.entry << std::dec << " prio=" << t.currentPriority
                  << std::endl;
    }
}

// PS2X_WATCHDOG_DUMP_DIR: the retail 32 MB of guest RAM (<base>.ram) and the live registers read before and after
// copying it (<base>.txt); the guest keeps running, so the two register reads show how far it moved meanwhile.
void StallWatchdog::writeForensicSnapshot(int liveThread) {
    const char* directory = std::getenv("PS2X_WATCHDOG_DUMP_DIR");
    if (!directory)
        return;
    std::error_code error;
    const std::filesystem::path dir(directory); // getenv is in the ANSI code page, as path(const char*) expects
    if (!dir.is_absolute() || !std::filesystem::is_directory(dir, error) || error) {
        std::cerr << "[watchdog-dump] requires an existing absolute output directory" << std::endl;
        return;
    }
    const auto stamp = Clock::now().time_since_epoch().count();
    const auto base = dir / ("watchdog-stall-" + std::to_string(stamp));
    std::filesystem::path registersPath = base;
    registersPath += ".txt";
    std::filesystem::path memoryPath = base;
    memoryPath += ".ram";
    std::ofstream registers(registersPath, std::ios::out);
    const auto captureRegisters = [&](const char* phase) {
        const R5900Context* live = m_scheduler.liveContextForCrash();
        registers << phase << " best-effort, guest not suspended; pc=" << std::hex << (live ? live->pc : 0)
                  << " thread=" << std::dec << liveThread << '\n';
        if (live)
            for (int reg = 0; reg < 32; ++reg)
                registers << "gpr" << std::dec << reg << "=" << std::hex << getRegU32(live, reg) << '\n';
    };
    captureRegisters("before-ram");
    std::ofstream memory(memoryPath, std::ios::out | std::ios::binary);
    memory.write(reinterpret_cast<const char*>(m_memory.getRDRAM()), PS2_RETAIL_RAM_SIZE);
    memory.close();
    captureRegisters("after-ram");
    registers << "dispatch=" << m_dispatchHistory() << '\n';
    registers.close();
    std::cerr << "[watchdog-dump] " << base.string() << " ram=" << (memory ? "ok" : "failed")
              << " registers=" << (registers ? "ok" : "failed") << std::endl;
}

// PS2X_PEEK: comma separated guest addresses (hex); prints the 32-bit word at each. "*base:off" is the word at
// [word at base] + off (one pointer hop).
void StallWatchdog::printPeekedWords() {
    const char* peek = std::getenv("PS2X_PEEK");
    if (!peek)
        return;
    const std::string list = peek;
    uint8_t* const rdram = m_memory.getRDRAM();
    size_t pos = 0;
    std::cerr << "    peek:";
    while (pos < list.size()) {
        const size_t comma = list.find(',', pos);
        const std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        uint32_t addr = 0;
        if (!item.empty() && item[0] == '*') {
            const size_t colon = item.find(':');
            const uint32_t base = static_cast<uint32_t>(std::strtoul(item.substr(1, colon - 1).c_str(), nullptr, 16));
            const uint32_t off = colon == std::string::npos
                                     ? 0u
                                     : static_cast<uint32_t>(std::strtoul(item.c_str() + colon + 1, nullptr, 16));
            uint32_t ptr = 0;
            if (base < PS2_RAM_SIZE)
                std::memcpy(&ptr, rdram + base, 4);
            addr = (ptr & 0x1FFFFFFFu) + off;
        } else
            addr = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
        uint32_t word = 0;
        if (addr < PS2_RAM_SIZE)
            std::memcpy(&word, rdram + addr, 4);
        else
            word = m_memory.read32(addr);
        std::cerr << " [" << std::hex << addr << "]=" << word << std::dec;
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    std::cerr << std::endl;
}
