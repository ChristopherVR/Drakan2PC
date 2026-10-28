// Drakan: GS/GIF packet capture (see docs/reference/gs-capture.md for the file format).
// Enabled with PS2X_GS_CAPTURE=<number of vsync frames to record>; PS2X_GS_CAPTURE_DIR overrides the output
// directory (default ../../captures relative to the working directory = decomp/build/captures).
#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

struct GSRegisters;

class GsCapture {
public:
    static GsCapture& instance();

    // Called once by the runtime; reads the environment and opens the file when enabled.
    void configure(const GSRegisters* regs);
    bool enabled() const { return m_enabled; }

    // GIF packet as the GS sees it (after arbitration), path = 1 (VU1 XGKICK), 2 (PATH2 / VIF1 DIRECT), 3 (GIF DMA).
    void onPacket(uint8_t path, bool path2DirectHl, bool path3Image, const uint8_t* data, uint32_t size);
    // Vertical blank: closes the current frame.
    void onVsync(uint64_t vsyncTick);
    void close();

private:
    void writeRecord(uint32_t type, const void* payload, uint32_t size, const void* payload2 = nullptr,
                     uint32_t size2 = 0);
    void writeRegsIfChanged(bool force);
    void openFile(); // creates the file and arms recording (also used for the PS2X_GS_CAPTURE_FROM delayed start)

    std::mutex m_mutex;
    bool m_enabled = false;
    FILE* m_file = nullptr;
    const GSRegisters* m_regs = nullptr;
    uint64_t m_frame = 0;
    uint64_t m_maxFrames = 0;
    uint64_t m_packets = 0;
    uint64_t m_bytes = 0;
    uint64_t m_lastRegs[20] = {};
    bool m_haveRegs = false;
    std::string m_path;
    double m_startAfterSec = -1.0; // PS2X_GS_CAPTURE_FROM=<s>: start recording once this much wall time passed
};
