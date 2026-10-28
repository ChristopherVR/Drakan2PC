#include "runtime/gs/gs_capture.h"
#include "runtime/ps2_memory.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>

namespace {
enum : uint32_t {
    REC_FRAME_START = 1,
    REC_GIF_PACKET = 2,
    REC_PRIV_REGS = 3,
    REC_VSYNC = 4,
};
}

GsCapture& GsCapture::instance() {
    static GsCapture inst;
    return inst;
}

void GsCapture::configure(const GSRegisters* regs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_regs = regs;
    const char* env = std::getenv("PS2X_GS_CAPTURE");
    if (!env || std::atol(env) <= 0 || m_file) {
        return;
    }
    m_maxFrames = static_cast<uint64_t>(std::atol(env));
    if (const char* from = std::getenv("PS2X_GS_CAPTURE_FROM"))
        if (std::atof(from) > 0.0) {
            m_startAfterSec = std::atof(from);
            return; // armed later from onVsync
        }
    openFile();
}

void GsCapture::openFile() {
    const char* dirEnv = std::getenv("PS2X_GS_CAPTURE_DIR");
    std::filesystem::path dir = dirEnv ? dirEnv : "../../captures";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    m_path = (dir / (std::string("gscap_") + stamp + ".bin")).string();
    m_file = std::fopen(m_path.c_str(), "wb");
    if (!m_file) {
        std::cerr << "[gs-capture] cannot open " << m_path << std::endl;
        return;
    }
    // header: 32 bytes
    char header[32] = {};
    std::memcpy(header, "DRKGSCAP", 8);
    const uint32_t version = 1;
    std::memcpy(header + 8, &version, 4);
    const uint64_t ms =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    std::memcpy(header + 16, &ms, 8);
    std::fwrite(header, 1, sizeof(header), m_file);
    m_enabled = true;
    std::cerr << "[gs-capture] recording " << m_maxFrames << " frame(s) to " << m_path << std::endl;
    const uint64_t zeroFrame[2] = {0, 0};
    writeRecord(REC_FRAME_START, zeroFrame, sizeof(zeroFrame));
}

void GsCapture::writeRecord(uint32_t type, const void* payload, uint32_t size, const void* payload2, uint32_t size2) {
    const uint32_t total = size + size2;
    std::fwrite(&type, 4, 1, m_file);
    std::fwrite(&total, 4, 1, m_file);
    if (size)
        std::fwrite(payload, 1, size, m_file);
    if (size2)
        std::fwrite(payload2, 1, size2, m_file);
}

void GsCapture::writeRegsIfChanged(bool force) {
    if (!m_regs)
        return;
    uint64_t now[20] = {m_regs->pmode,      m_regs->smode1,           m_regs->smode2,
                        m_regs->srfsh,      m_regs->synch1,           m_regs->synch2,
                        m_regs->syncv,      m_regs->dispfb1,          m_regs->display1,
                        m_regs->dispfb2,    m_regs->display2,         m_regs->extbuf,
                        m_regs->extdata,    m_regs->extwrite,         m_regs->bgcolor,
                        m_regs->csr.load(), m_regs->vsyncTick.load(), m_regs->imr,
                        m_regs->busdir,     m_regs->siglblid};
    // csr and vsyncTick change constantly; they do not trigger a record by themselves
    bool changed = !m_haveRegs;
    for (int i = 0; i < 20 && !changed; ++i) {
        if (i == 15 || i == 16)
            continue;
        changed = now[i] != m_lastRegs[i];
    }
    if (changed || force) {
        writeRecord(REC_PRIV_REGS, now, sizeof(now));
        std::memcpy(m_lastRegs, now, sizeof(now));
        m_haveRegs = true;
    }
}

void GsCapture::onPacket(uint8_t path, bool path2DirectHl, bool path3Image, const uint8_t* data, uint32_t size) {
    if (!m_enabled)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file)
        return;
    writeRegsIfChanged(false);
    uint8_t head[8] = {path, static_cast<uint8_t>((path2DirectHl ? 1 : 0) | (path3Image ? 2 : 0)), 0, 0, 0, 0, 0, 0};
    std::memcpy(head + 4, &size, 4);
    writeRecord(REC_GIF_PACKET, head, sizeof(head), data, size);
    ++m_packets;
    m_bytes += size;
}

void GsCapture::onVsync(uint64_t vsyncTick) {
    if (m_startAfterSec >= 0.0) {
        static const auto t0 = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= m_startAfterSec) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_startAfterSec = -1.0;
            openFile();
        }
        return;
    }
    if (!m_enabled)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file)
        return;
    writeRegsIfChanged(true);
    writeRecord(REC_VSYNC, &vsyncTick, sizeof(vsyncTick));
    ++m_frame;
    std::fflush(m_file);
    if (m_frame >= m_maxFrames) {
        std::cerr << "[gs-capture] done: " << m_frame << " frames, " << m_packets << " packets, " << m_bytes
                  << " bytes -> " << m_path << std::endl;
        std::fclose(m_file);
        m_file = nullptr;
        m_enabled = false;
        return;
    }
    const uint64_t rec[2] = {m_frame, vsyncTick};
    writeRecord(REC_FRAME_START, rec, sizeof(rec));
}

void GsCapture::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_enabled = false;
}
