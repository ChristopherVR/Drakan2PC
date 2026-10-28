// Drakan: The Ancients' Gates (PAL, SCES-500.06) - game-specific boot shortcuts and RSRCIOP HLE wiring.
//
// The game reboots the IOP and then loads its own IOP modules (incl. RSRCIOP, which serves the 0x1201 / 0x1202 /
// 0x1203 SIF RPC services) out of an encrypted bundle on the disc. We do not emulate any of that: the module load is
// skipped and the three RSRCIOP services are provided natively by the standalone library decomp/runtime/hle
// (namespace hle), reading the game data straight from the .iso.
//
// Environment / options:
//   DRAKAN_ISO           path of the disc image   (default: a Drakan*.iso next to the exe or up to 4 parent folders up)
//   DRAKAN_LANG          language root            (default \ENGLISH)
//   DRAKAN_RES_DELIVERY  "rpc1301" (default) | "direct"   how type-0 resource data reaches the EE
//   DRAKAN_HLE_LOG       max number of logged HLE lines (default 400, 0 = silent)
//
// See docs/reference/iop-and-io.md section 7 and docs/status/recomp-status.md.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "Kernel/Syscalls/RPC.h"
#include "Kernel/Stubs/IPU.h"

#include "hle/RsrcIop.h"
#ifdef PS2X_HAVE_UI
#include "ui/Prompts.h"
#include "ui/FontAtlas.h"
#endif
#ifdef PS2X_HAVE_GSR
#include <gsr/gsr.h>
#include "drakan_dlss.h"
#endif
#include "spu2/AudioOutput.h"
#include "drakan_pause.h"
#include "drakan_config.h"
#include "drakan_settings_host.h"
#include "drakan_subtitles.h"
#include "drakan_menus.h"
#include "drakan_widescreen.h"
#include "drakan_inventory_typography.h"
#include "drakan_frame_rate.h"
#include "drakan_controls_menu.h"
#include "drakan_dual_wield.h"
#include "spu2/Sdr.h"
#include "spu2/Spu2.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#if defined(PS2X_HAVE_DRAKAN_INPUT)
void drakanInstallInput(PS2Runtime& rt); // drakan_input_glue.cpp
#else
void drakanInstallPortableCamera(PS2Runtime& runtime);
#endif

std::atomic<bool> g_drakanMovieSeen{
    false}; // set when the game requests the logo movie (= the memory card warning was dismissed)
uint32_t g_drakanWrite4[8] = {};
std::atomic<uint32_t> g_drakanWrite4Next{0};

namespace {
// EE addresses in SCES_500.06 (see docs/reference/iop-and-io.md section 4).
constexpr uint32_t kLoadIopModules = 0x101710u;      // main(): load LEVELS.RRC bundle + 7 IOP modules
constexpr uint32_t kSifLoadModuleBuffer = 0x4c3798u; // wrapper used by LoadIopModules
constexpr uint32_t kOptionsHandler =
    0x4424e0u; // Options screen event handler (virtual): widget 0x2345 "Adjust Screen" -> "PC Settings"
constexpr uint32_t kOptionsAdjustScreenWidget = 0x2345u;
constexpr uint32_t kXdbAdjustScreen = 156u; // INTERFCE.XDB text entry of that widget

PS2Runtime::RecompiledFunction g_origOptionsHandler = nullptr;

// Movie-player completion callback thunk (screens 0x1d/0x1e, see docs/journal/2026-10-17/crash-listener.md section 1):
// void thunk(Screen *listener, int flag) calls (listener+0x98)->vslot 0x7c. The movie job stores the raw screen pointer
// (job+0xbc) and the game has no unregister call, it relies on the screen outliving the job. When START is pressed in
// the same frame as the New Game confirm the pause menu opens over the loading game and the game's own "close all
// screens" (FUN_00417520) destroys the freshly created intro screen while its job still runs; the later completion then
// jumps through the recycled object (jalr 0 / garbage). Ignore completions for screens that are no longer in the screen
// manager table.
constexpr uint32_t kMovieDoneThunk = 0x440610u;
constexpr uint32_t kScreenManager = 0x4fb600u; // screens by id at +0x10 + 4*id (ids 0..0x20)
PS2Runtime::RecompiledFunction g_origMovieDone = nullptr;

void hookMovieDone(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t listener = getRegU32(c, 4);
    bool alive = false;
    if (listener != 0u) {
        for (uint32_t id = 0; id <= 0x20u && !alive; ++id) {
            uint32_t entry = 0;
            std::memcpy(&entry, rdram + kScreenManager + 0x10u + 4u * id, 4);
            alive = (entry == listener);
        }
    }
    if (!alive) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 8)
            std::fprintf(stderr, "[drakan] movie completion (flag %u) for destroyed GUI screen 0x%x ignored\n",
                         getRegU32(c, 5), listener);
        SET_GPR_S32(c, 2, 0);
        c->pc = getRegU32(c, 31);
        return;
    }
    drakan_subtitles::stop(); // the movie (narration) is over
    g_origMovieDone(rdram, c, rt);
}

// Movie pacing (docs/reference/fmv.md section 6). The decode step 0x1767a0 (once per engine tick) shows the next
// frame once the ms clock passes a deadline it advances by intervalMs[player->fps index] (table 0x51b568: {1, 40, 50,
// 100} for "Full", 25, 20, 10 fps). The logo / intro / outro players use "Full": one movie frame per engine tick,
// 25 frames/s on the PAL console, up to 60 in the port, so the intro ran in ~60 s against 123 s of narration. All
// disc movies are 25 fps PAL material (3059 intro frames = 122.4 s, 2098 outro frames = 83.9 s, their soundtracks).
// The table must stay as it is: the movie screen clears the frame buffer every tick and relies on a frame being
// stamped each time (a 40 ms interval left black frames between movie frames). Instead the engine tick is held to the
// PAL rate while a "Full" movie plays: when the player's frame counter (+0x2c) has advanced since the previous tick,
// the next tick waits until 40 ms after the previous frame (absolute schedule, so vblank-aligned ticks still average
// 25 frames/s). Only fresh calls reach this hook: f_0x1767a0 is resumable and the frame is often stamped in a resumed
// part (IPU sync / DMA waits), so the counter is compared across calls, not around one.
constexpr uint32_t kMovieDecodeStep = 0x1767a0u;
constexpr auto kPalMovieFrame = std::chrono::milliseconds(40);
PS2Runtime::RecompiledFunction g_origMovieDecodeStep = nullptr;

void hookMovieDecodeStep(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    using Clock = std::chrono::steady_clock;
    static uint32_t lastPlayer = 0, lastCount = 0;
    static Clock::time_point deadline{};
    const uint32_t player = getRegU32(c, 4) & 0x1ffffffu;
    uint32_t fpsIndex = ~0u, count = 0;
    if (player != 0u && player + 0xb0u < PS2_RAM_SIZE) {
        std::memcpy(&fpsIndex, rdram + player + 0xa8u, 4);
        std::memcpy(&count, rdram + player + 0x2cu, 4); // frames shown so far
    }
    if (fpsIndex == 0u) {
        const auto now = Clock::now();
        if (player != lastPlayer) {
            std::fprintf(stderr, "[drakan] movie player 0x%x: \"Full\" frame rate, engine tick held to 25 fps\n",
                         player);
            lastPlayer = player;
            lastCount = count;
            deadline = now;
        } else if (count != lastCount) {
            lastCount = count;
            // After a stall (loading, F1 menu) restart from now instead of rushing to catch up.
            deadline = now - deadline < 3 * kPalMovieFrame ? deadline + kPalMovieFrame : now + kPalMovieFrame;
        }
        if (now < deadline)
            std::this_thread::sleep_until(deadline);
    }
    g_origMovieDecodeStep(rdram, c, rt);
}

// Options screen handler: int handler(Screen *this, Widget *widget, int message). Message 1 on the "Adjust Screen"
// widget (renamed "PC Settings" through the XDB text override) opens the settings overlay instead; the overlay itself
// is opened by the render thread.
void hookOptionsHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t widget = getRegU32(c, 5), msg = getRegU32(c, 6);
    if (msg == 1u && widget >= 0x1000u && (widget & 0x1FFFFFFFu) + 0x16u < PS2_RAM_SIZE) {
        uint16_t id = 0;
        std::memcpy(&id, rdram + (widget & 0x1FFFFFFFu) + 0x14u, 2);
        if (id == kOptionsAdjustScreenWidget) {
            drakan_settings_host::requestOpen();
            SET_GPR_S32(c, 2, 0);
            c->pc = getRegU32(c, 31);
            return;
        }
        if (id == 0x2343u) // native options persist at every input frame
        {
            SET_GPR_S32(c, 2, 0);
            c->pc = getRegU32(c, 31);
            return;
        }
    }
    g_origOptionsHandler(rdram, c, rt);
}

void hookOptionsClose(uint8_t*, R5900Context* c, PS2Runtime*) {
    // Options are already durable; use the ordinary window close instead
    // of the retail "Save the new settings?" memory-card confirmation.
    c->pc = 0x2bebe8u;
}

// The disc image: DRAKAN_ISO, drakan.ini [general] iso, then a Drakan*.iso next to the exe or in a parent folder.
std::string resolveIsoPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* e = std::getenv("DRAKAN_ISO"))
        if (*e)
            return e;
    const std::string cfg = drakan_cfg::getString("general", "iso", nullptr, "");
    if (!cfg.empty() && fs::exists(cfg, ec))
        return cfg;
    fs::path dir = fs::path(drakan_cfg::exeDir());
    for (int up = 0; up < 5 && !dir.empty(); ++up, dir = dir.parent_path()) {
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            std::string n = de.path().filename().string();
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
            if (de.is_regular_file(ec) && n.size() > 4 && n.compare(n.size() - 4, 4, ".iso") == 0 &&
                n.find("drakan") != std::string::npos)
                return de.path().string();
        }
        if (dir == dir.parent_path())
            break;
    }
    return cfg;
}

// drakan.ini [general] language -> disc language folder (env DRAKAN_LANG wins)
std::string resolveLanguageRoot() {
    if (const char* e = std::getenv("DRAKAN_LANG"))
        return e;
    std::string l = drakan_cfg::getString("general", "language", nullptr, "english");
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char ch) { return (char)std::toupper(ch); });
    return l.empty() ? std::string("\\ENGLISH") : "\\" + l;
}

inline void returnS32(R5900Context* ctx, int32_t value) {
    SET_GPR_S32(ctx, 2, value);
    ctx->pc = getRegU32(ctx, 31);
}

// newlib rand (0x4ce028), as the game's own code: seed = _impure_ptr (*0x518e94) -> _rand_next (+0x58)
// * 1103515245 + 12345, result seed & 0x7fffffff. Keeping the guest state honours the game's srand (0x4ce018, not
// stubbed). Callers scale by 1 / 2^31 (snow spawn positions, particle LOD, wind gusts, AI); the 0..0x7fff host rand
// left them all near 0.
constexpr uint32_t kRand = 0x4ce028u, kImpurePtr = 0x518e94u;

void hookRand(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    uint32_t reent = 0, seed = 0;
    std::memcpy(&reent, rdram + kImpurePtr, 4);
    reent &= 0x1FFFFFFFu;
    if (reent >= 0x100000u && reent + 0x5c <= PS2_RAM_SIZE)
        std::memcpy(&seed, rdram + reent + 0x58, 4);
    seed = seed * 1103515245u + 12345u;
    if (reent >= 0x100000u && reent + 0x5c <= PS2_RAM_SIZE)
        std::memcpy(rdram + reent + 0x58, &seed, 4);
    returnS32(ctx, static_cast<int32_t>(seed & 0x7FFFFFFFu));
}

void hookLoadIopModules(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    std::fprintf(stderr, "[drakan] LoadIopModules (0x101710) skipped: IOP modules are HLE'd, returning success\n");
    returnS32(ctx, 0);
}

std::atomic<int> g_nextFakeModuleId{100};

void hookSifLoadModuleBuffer(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int id = g_nextFakeModuleId.fetch_add(1);
    std::fprintf(stderr, "[drakan] sceSifLoadModuleBuffer(0x%x) stubbed -> module id %d\n", getRegU32(ctx, 4), id);
    returnS32(ctx, id);
}

// ------------------------------------------------------------------------------------------------------------
// Logging (the library's verbose log + our own lines share one budget)
std::atomic<long> g_logBudget{400};
std::atomic<long> g_logSuppressed{0};

void logLine(const std::string& s) {
    if (g_logBudget.fetch_sub(1) > 0) {
        std::fprintf(stderr, "[hle] %s\n", s.c_str());
    } else {
        g_logSuppressed.fetch_add(1);
    }
}

void logfmt(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logLine(buf);
}

// ------------------------------------------------------------------------------------------------------------
class RdramMemory final : public hle::IEeMemory {
public:
    explicit RdramMemory(uint8_t* rdram) : m_rdram(rdram) {}
    bool read(hle::u32 addr, void* dst, size_t n) override {
        const uint32_t off = addr & 0x1FFFFFFFu;
        if (static_cast<uint64_t>(off) + n > PS2_RAM_SIZE)
            return false;
        std::memcpy(dst, m_rdram + off, n);
        return true;
    }
    bool write(hle::u32 addr, const void* src, size_t n) override {
        if (n == 4) {
            g_drakanWrite4[g_drakanWrite4Next++ & 7u] = addr; // debug aid (PS2X_TRACE_IPU dumps these words)
        }
        const uint32_t off = addr & 0x1FFFFFFFu;
        if (static_cast<uint64_t>(off) + n > PS2_RAM_SIZE)
            return false;
        std::memcpy(m_rdram + off, src, n);
        return true;
    }

private:
    uint8_t* m_rdram;
};

// Delivers IOP->EE calls (servers 0x1301/0x1302/0x1303 registered by the game via sceSifRegisterRpc) by running the
// registered handler as a guest invocation on the EE scheduler.
// The game creates its RPC server threads with fixed priorities (see FUN_001efdc0 / the sound init); the posted
// handlers run at the same priority so that the relative preemption order of the servers is the one the game was
// written for.
int serverPriority(uint32_t sid) {
    switch (sid) {
        case hle::SID_EE_ALLOC:
            return 4;
        case hle::SID_EE_DONE:
            return 5;
        case hle::SID_EE_STREAM:
            return 6;
        case 0x1310u:
            return 3;
        default:
            return 3;
    }
}

class EeRpcPoster final : public hle::IEeRpcPoster {
public:
    EeRpcPoster(PS2Runtime& rt, uint8_t* rdram) : m_rt(rt), m_rdram(rdram) {}

    int post(hle::u32 sid, hle::u32 fno, const void* payload, hle::u32 size, void* reply, hle::u32 replyCap,
             bool blocking) override {
        uint32_t sd = 0;
        if (!ps2_syscalls::lookupRpcServer(sid, sd)) {
            logfmt("post(sid=0x%x fno=%u) failed: EE server not registered", sid, fno);
            return -1;
        }
        std::lock_guard<std::mutex> serialize(mutexFor(sid)); // the server's receive buffer is shared
        uint32_t func = 0, buf = 0;
        std::memcpy(&func, m_rdram + (sd & 0x1FFFFFFFu) + 4, 4);
        std::memcpy(&buf, m_rdram + (sd & 0x1FFFFFFFu) + 8, 4);
        if (func == 0 || buf == 0) {
            logfmt("post(sid=0x%x): server has no handler/buffer", sid);
            return -1;
        }
        if (size != 0 && payload != nullptr) {
            const uint32_t off = buf & 0x1FFFFFFFu;
            if (static_cast<uint64_t>(off) + size > PS2_RAM_SIZE)
                return -1;
            std::memcpy(m_rdram + off, payload, size);
        }

        struct Wait {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            uint32_t v0 = 0;
        };
        auto wait = std::make_shared<Wait>();
        m_rt.eeScheduler().postGuestCall(
            func, fno, buf, size, 0,
            [wait](uint32_t v0) {
                std::lock_guard<std::mutex> lock(wait->m);
                wait->v0 = v0;
                wait->done = true;
                wait->cv.notify_all();
            },
            serverPriority(sid));
        {
            std::unique_lock<std::mutex> lock(wait->m);
            if (!wait->cv.wait_for(lock, std::chrono::seconds(20), [&] { return wait->done; })) {
                logfmt("post(sid=0x%x fno=%u): guest handler 0x%x did not return within 20 s", sid, fno, func);
                return -2;
            }
        }
        {
            static std::atomic<int> postLog{0};
            if (sid != 0x1310u && postLog.fetch_add(1) < 400) {
                uint32_t w[4] = {0, 0, 0, 0};
                std::memcpy(w, payload, std::min<uint32_t>(size, 16u));
                uint32_t r[4] = {0, 0, 0, 0};
                if (wait->v0 != 0 && (wait->v0 & 0x1FFFFFFFu) + 16 <= PS2_RAM_SIZE)
                    std::memcpy(r, m_rdram + (wait->v0 & 0x1FFFFFFFu), 16);
                logfmt("post sid=0x%x fno=0x%x size=%u payload=%08x %08x %08x %08x -> handler 0x%x v0=0x%x reply=%08x "
                       "%08x %08x %08x",
                       sid, fno, size, w[0], w[1], w[2], w[3], func, wait->v0, r[0], r[1], r[2], r[3]);
            }
        }
        if (sid == hle::SID_EE_ALLOC || sid == hle::SID_EE_STREAM) {
            static std::atomic<int> reqLog{0};
            if (reqLog.fetch_add(1) < 40 && (fno & 0x1FFFFFFFu) + 0x30 <= PS2_RAM_SIZE) {
                uint32_t q[12];
                std::memcpy(q, m_rdram + (fno & 0x1FFFFFFFu), 48);
                logfmt("   req %08x after post 0x%x: %x %x %x %x | %x %x %x %x | %x %x %x %x", fno, sid, q[0], q[1],
                       q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11]);
            }
        }
        if (!blocking || reply == nullptr || replyCap == 0)
            return 0;
        const uint32_t off = wait->v0 & 0x1FFFFFFFu;
        if (wait->v0 == 0 || static_cast<uint64_t>(off) + replyCap > PS2_RAM_SIZE) {
            std::memset(reply, 0, replyCap);
            return 0;
        }
        std::memcpy(reply, m_rdram + off, replyCap);
        return static_cast<int>(replyCap);
    }

private:
    std::mutex& mutexFor(uint32_t sid) { return m_serial[sid & 3u]; }
    PS2Runtime& m_rt;
    uint8_t* m_rdram;
    std::mutex m_serial[4];
};

// Forwards the 0x1203 / resource-upload traffic to the real SPU2; DRAKAN_SND_LOG=2 additionally logs key on/off,
// address and upload operations (diagnostics for the music/streaming path), 3 = every voice parameter write and the
// voice state at key-on, 4 = also every 0x1203 batch (hleHandleRpc). DRAKAN_SND_DBG=1 hooks the game's sound manager
// (soundDebugInstall).
double nowS() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

class SpuTap final : public hle::ISpu2 {
public:
    explicit SpuTap(spu2::Spu2& s) : m_s(s), m_log(envLevel() >= 2), m_log3(envLevel() >= 3) {}
    void ramWrite(hle::u32 a, const void* src, hle::u32 n) override {
        drakan_subtitles::onSpuUpload(
            a, n); // narration subtitles: the first big upload after the soundtrack request is the stream buffer
        if (m_log)
            std::fprintf(stderr, "[spu %.2f] ramWrite 0x%x n=%u\n", nowS(), a, n);
        m_s.ramWrite(a, src, n);
    }
    void ramRead(hle::u32 a, void* dst, hle::u32 n) override {
        if (m_log)
            std::fprintf(stderr, "[spu %.2f] ramRead 0x%x n=%u\n", nowS(), a, n);
        m_s.ramRead(a, dst, n);
    }
    void setParam(hle::u16 e, hle::u16 v) override {
        if (m_log3)
            std::fprintf(stderr, "[spu %.3f] setParam reg=%02x core%u voice%u = 0x%x\n", nowS(), e >> 8, e & 1u,
                         (e >> 1) & 31u, v);
        m_s.setParam(e, v);
    }
    hle::u16 getParam(hle::u16 e) override { return m_s.getParam(e); }
    void setSwitch(hle::u16 e, hle::u32 v) override {
        if (m_log && (e >> 8) >= 0x15 && (e >> 8) <= 0x16)
            std::fprintf(stderr, "[spu %.2f] %s core%u mask=%06x\n", nowS(), (e >> 8) == 0x15 ? "KON " : "KOFF", e & 1u,
                         v);
        if ((e >> 8) == 0x15 &&
            drakan_subtitles::wantsSpuEvents()) // narration subtitles: cue time 0 = key-on of the stream voice
            for (unsigned i = 0; i < 24; ++i)
                if ((v >> i) & 1u)
                    drakan_subtitles::onVoiceKeyOn(
                        m_s.getAddr(static_cast<uint16_t>(spu2::SD_VADDR_SSA | (e & 1u) | (i << 1))),
                        m_s.framesRendered());
        if (m_log3 && (e >> 8) == 0x15) // DRAKAN_SND_LOG=3: voice state at key-on (f = SPU2 frames rendered = position
                                        // in a DRAKAN_AUDIO_DUMP wav)
            for (unsigned i = 0; i < 24; ++i)
                if ((v >> i) & 1u) {
                    const uint16_t ve = static_cast<uint16_t>((e & 1u) | (i << 1));
                    std::fprintf(stderr, "[spu %.3f]   KON c%u.v%u ssa=%x pitch=%x vol=%x/%x adsr=%04x/%04x f=%llu\n",
                                 nowS(), e & 1u, i, m_s.getAddr(static_cast<uint16_t>(spu2::SD_VADDR_SSA | ve)),
                                 m_s.getParam(static_cast<uint16_t>(spu2::SD_VPARAM_PITCH | ve)),
                                 m_s.getParam(static_cast<uint16_t>(spu2::SD_VPARAM_VOLL | ve)),
                                 m_s.getParam(static_cast<uint16_t>(spu2::SD_VPARAM_VOLR | ve)),
                                 m_s.getParam(static_cast<uint16_t>(spu2::SD_VPARAM_ADSR1 | ve)),
                                 m_s.getParam(static_cast<uint16_t>(spu2::SD_VPARAM_ADSR2 | ve)),
                                 static_cast<unsigned long long>(m_s.framesRendered()));
                }
        m_s.setSwitch(e, v);
    }
    hle::u32 getSwitch(hle::u16 e) override { return m_s.getSwitch(e); }
    void setAddr(hle::u16 e, hle::u32 v) override {
        if (m_log && (e >> 8) >= 0x1c)
            std::fprintf(stderr, "[spu %.2f] setAddr reg=%02x core%u voice%u = 0x%x\n", nowS(), e >> 8, e & 1u,
                         (e >> 1) & 31u, v);
        m_s.setAddr(e, v);
    }
    hle::u32 getAddr(hle::u16 e) override { return m_s.getAddr(e); }
    void setCoreAttr(hle::u16 e, hle::u16 v) override { m_s.setCoreAttr(e, v); }
    hle::u16 getCoreAttr(hle::u16 e) override { return m_s.getCoreAttr(e); }
    static int envLevel() {
        const char* e = std::getenv("DRAKAN_SND_LOG");
        return e ? std::atoi(e) : 0;
    }

private:
    spu2::Spu2& m_s;
    bool m_log;
    bool m_log3;
};

struct HleHost {
    HleHost(PS2Runtime& rt, uint8_t* rdram)
        : mem(rdram), ee(rt, rdram), iop(fs, mem, ee, tap),
          sdr(spu,
              [rdram](uint32_t addr, uint32_t n) -> void* {
                  // sdrdrv buffers are EE pointers in the native port (sceSdRemote is hooked, nothing is DMA'd to an
                  // IOP)
                  const uint32_t off = addr & 0x1FFFFFFFu;
                  return (static_cast<uint64_t>(off) + n <= PS2_RAM_SIZE) ? rdram + off : nullptr;
              }),
          audio(spu), runtime(&rt), rdramPtr(rdram) {}

    hle::IsoFs fs;
    RdramMemory mem;
    EeRpcPoster ee;
    spu2::Spu2 spu;
    SpuTap tap{spu};
    hle::RsrcIop iop;
    spu2::SdrServer sdr;
    spu2::AudioOutput audio;
    PS2Runtime* runtime;
    uint8_t* rdramPtr;

    // 0x1202 worker: strictly sequential, like the IOP server thread
    struct Job {
        uint32_t fno;
        std::vector<uint8_t> send;
        uint32_t endFunction, endParameter;
        std::chrono::steady_clock::time_point submitted = std::chrono::steady_clock::now();
    };
    // The real IOP needs milliseconds (queueing, CD/DVD read) before the first completion of a batch arrives, so the
    // game never sees a completion inside the few instructions after it submitted the request. Without this latency the
    // completion can preempt the requester before its own bookkeeping (reference counts) is done and the resource is
    // destroyed under it. Keep it short: in the uncapped menus the guest clock advances at only a few percent of real
    // time, so 2 ms of guest time took 30-200 ms of wall time per batch and a screen that loads a dozen resources
    // (Options: texts plus six background tiles) stayed black for 1-4 s on its first visit.
    std::chrono::microseconds resLatency{50};
    std::mutex qm;
    std::condition_variable qcv;
    std::deque<Job> queue;
    std::thread worker;
    bool workerStarted = false;

    // Latency of the IOP in *guest* time (EE cycles executed), not wall time: the completion must not overtake the
    // requester's own bookkeeping, and that depends on how far the guest got, not on how fast or loaded the host is. If
    // the guest is idle the scheduler skips the time at once. (Also the form that works for a single-threaded
    // cooperative scheduler.)
    void waitGuestLatency() {
        const uint64_t cycles = static_cast<uint64_t>(resLatency.count()) * (EeScheduler::kEeClockHz / 1000000ull);
        if (cycles == 0)
            return;
        struct Gate {
            std::mutex m;
            std::condition_variable cv;
            bool open = false;
        };
        auto gate = std::make_shared<Gate>();
        runtime->eeScheduler().postAfterGuestCycles(cycles, [gate]() {
            std::lock_guard<std::mutex> lock(gate->m);
            gate->open = true;
            gate->cv.notify_all();
        });
        std::unique_lock<std::mutex> lock(gate->m);
        gate->cv.wait_for(lock, std::chrono::seconds(5), [&] { return gate->open; });
    }

    void startWorker() {
        if (workerStarted)
            return;
        workerStarted = true;
        worker = std::thread([this] {
            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(qm);
                    qcv.wait(lock, [this] { return !queue.empty(); });
                    job = std::move(queue.front());
                    queue.pop_front();
                }
                waitGuestLatency();
                iop.call(hle::SID_RESOURCE, job.fno, job.send.data(), static_cast<uint32_t>(job.send.size()), nullptr,
                         0);
                if (job.endFunction != 0) {
                    runtime->eeScheduler().postGuestCall(job.endFunction, job.endParameter, 0, 0, 0, nullptr);
                }
            }
        });
        worker.detach();
    }
};

HleHost* g_host = nullptr; // intentionally leaked: its threads outlive static destruction

bool isRsrcSid(uint32_t sid) {
    return hle::RsrcIop::isRsrcSid(sid);
}

bool hleCanBind(uint32_t sid) {
    return g_host && isRsrcSid(sid);
}

bool hleHandleRpc(PS2Runtime& runtime, uint8_t* rdram, R5900Context*, const ps2x::iop::RpcRequest& request,
                  ps2x::iop::RpcResult& out) {
    if (!g_host || !isRsrcSid(request.sid)) {
        return false;
    }
    std::vector<uint8_t> send(request.send.size);
    if (request.send.size != 0 && !g_host->mem.read(request.send.address, send.data(), send.size())) {
        send.clear();
    }
    out = {};
    out.handled = true;
    {
        static std::atomic<int> rpcLog{0};
        if (rpcLog.fetch_add(1) < 1000 && (request.sid != hle::SID_FILE || request.function <= 1)) {
            char txt[49] = {};
            for (size_t i = 0; i < 48 && i < send.size(); ++i)
                txt[i] = (send[i] >= 32 && send[i] < 127) ? static_cast<char>(send[i]) : '.';
            logfmt("rpc client=0x%x sid=0x%x fno=0x%x mode=%u send=0x%x/%u recv=0x%x/%u endfn=0x%x endarg=0x%x text=%s",
                   request.clientAddress, request.sid, request.function, request.mode, request.send.address,
                   request.send.size, request.receive.address, request.receive.size, request.endFunction,
                   request.endParameter, txt);
        }
    }
    if (request.sid == hle::SID_SOUND && SpuTap::envLevel() >= 4) {
        static std::atomic<int> sndRpcLog{0};
        logfmt("[snd %.3f] 0x1203 fno=%u send=%zu recv=%u", nowS(), request.function, send.size(),
               request.receive.size);
        if (request.function == 0)
            for (size_t off = 0; off + 8 <= send.size() && sndRpcLog.fetch_add(1) < 20000; off += 8) {
                uint16_t func = 0, entry = 0;
                uint32_t value = 0;
                std::memcpy(&func, send.data() + off, 2);
                std::memcpy(&entry, send.data() + off + 2, 2);
                std::memcpy(&value, send.data() + off + 4, 4);
                if (func == 1 && (entry >> 8) <= 1 && SpuTap::envLevel() < 5)
                    continue; // voice volume writes every frame: only at level 5
                logfmt("[snd]   batch func=%u entry=%04x (reg %02x c%u v%u) value=%08x", func, entry, entry >> 8,
                       entry & 1u, (entry >> 1) & 31u, value);
            }
    }
    if (request.sid == hle::SID_RESOURCE) {
        static std::atomic<int> hexLog{0};
        if (hexLog.fetch_add(1) < 200)
            for (size_t off = 0; off + 28 <= send.size(); off += 28) {
                char h[28 * 3 + 1] = {};
                for (int i = 0; i < 28; ++i)
                    std::snprintf(h + i * 3, 4, "%02x ", send[off + i]);
                logfmt("  entry[%zu] %s", off / 28, h);
            }
        for (size_t off = 0; off + 28 <= send.size(); off += 28) {
            uint16_t type = 0;
            std::memcpy(&type, send.data() + off + 10, 2);
            if (type == 0x0342u && !g_drakanMovieSeen.exchange(true))
                std::fprintf(stderr, "[drakan] logo movie requested (memory card warning dismissed)\n");
        }
        // asynchronous on the real IOP: queue to the worker; the end function runs when the batch is done.
        g_host->startWorker();
        {
            std::lock_guard<std::mutex> lock(g_host->qm);
            g_host->queue.push_back({request.function, std::move(send), 0u, 0u});
        }
        g_host->qcv.notify_one();
        // The real IOP server function only queues the batch (the loader thread answers later through 0x1302/0x1301),
        // so the call - and the client's end function - completes immediately. Running the end function after the batch
        // deadlocks: a completion handler may submit the next request through the same (still locked) client.
        return true;
    }
    std::vector<uint8_t> reply(std::max<uint32_t>(request.receive.size, 16u), 0);
    const int n = g_host->iop.call(request.sid, request.function, send.data(), static_cast<uint32_t>(send.size()),
                                   reply.data(), static_cast<uint32_t>(reply.size()));
    if (n > 0 && request.receive.address != 0 && request.receive.size != 0) {
        g_host->mem.write(request.receive.address, reply.data(),
                          std::min<size_t>({static_cast<size_t>(n), reply.size(), request.receive.size}));
    }
    (void)rdram;
    (void)runtime;
    return true;
}

// ------------------------------------------------------------------------------------------------------------
// Sound: sdrdrv (SID 0x80000701) is serviced natively by hooking the EE client stub sceSdRemote (0x4b91d8); the
// sdrdrv callbacks (SID 0x80000704) and RSRCIOP's voice-position server (SID 0x1310) are posted from a pump thread.
constexpr uint32_t kSdRemote = 0x4b91d8u;
constexpr uint32_t kSdRemoteBuf = 0x56df00u;   // the stub's static RPC buffer (also the reply buffer)
constexpr uint32_t kSdRemoteEndFn = 0x516f80u; // end-function slot the stub passes in no-wait mode
constexpr uint32_t kSidSdrCallback = 0x80000704u;
constexpr uint32_t kSidVoicePos = 0x1310u;

std::atomic<int> g_sdLog{0};

void hookSdRemote(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t sync = getRegU32(ctx, 4);
    const uint32_t cmd = getRegU32(ctx, 5);
    if (!g_host) {
        returnS32(ctx, 0);
        return;
    }
    uint32_t w[16] = {};
    for (int i = 0; i < 6; ++i)
        w[1 + i] = getRegU32(ctx, 6 + i); // a2,a3,t0..t3 -> words 1..6 exactly as the stub packs them
    uint32_t fno = cmd;
    if (cmd == 0x8130u || cmd == 0x8140u)
        fno = w[1] | cmd; // effect attr: the core is ORed into the function number
    uint8_t reply[64] = {};
    const int n = g_host->sdr.call(fno, w, 0x40u, reply, sizeof(reply));
    int32_t result = 0;
    if (n >= 4)
        std::memcpy(&result, reply, 4);
    if (g_sdLog.fetch_add(1) < 120)
        logfmt("sdrdrv fno=0x%x sync=%u args=%x %x %x %x %x %x -> %d (%d bytes)", fno, sync, w[1], w[2], w[3], w[4],
               w[5], w[6], result, n);
    if (n > 0)
        std::memcpy(rdram + kSdRemoteBuf, reply, static_cast<size_t>(std::min(n, 64)));
    if (sync == 0u) {
        uint32_t endFn = 0;
        std::memcpy(&endFn, rdram + kSdRemoteEndFn, 4);
        if (endFn != 0)
            rt->eeScheduler().postGuestCall(endFn, kSdRemoteBuf, 0, 0, 0, nullptr);
    }
    returnS32(ctx, result);
}

// IOP sound-thread equivalent: every 500 ms RSRCIOP reads NAX (current address) of all 2x24 voices with a
// ProcBatchEx(GETADDR NAX, voice mask 0xffffff) and sends the 48 words (192 bytes) to the EE server 0x1310 (fno 0).
// sdrdrv's callback thread forwards SPU2 events as calls to 0x80000704.
const bool g_stereoSync = []() {
    const char* e = std::getenv("DRAKAN_STEREO_SYNC");
    return !(e && e[0] == '0');
}();

void soundPump(HleHost* h) {
    using clock = std::chrono::steady_clock;
    auto nextSnapshot = clock::now() + std::chrono::milliseconds(500);
    int snapLogged = 0;
    // Test aid: DRAKAN_TEST_SUB="text" queues the game's own SetSubtitle(0x5102b0, text, speaker 0) (FUN_004149a0, the
    // call the speech controller makes for a spoken line) DRAKAN_TEST_SUB_AT seconds (default 45) after the sound
    // system started, to look at the game's subtitle bar without having to reach an NPC.
    const char* testSub = std::getenv("DRAKAN_TEST_SUB");
    const double testSubAt = std::getenv("DRAKAN_TEST_SUB_AT") ? std::atof(std::getenv("DRAKAN_TEST_SUB_AT")) : 45.0;
    bool testSubDone = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (testSub && !testSubDone && h->rdramPtr && nowS() >= testSubAt) {
            testSubDone = true;
            constexpr uint32_t kScratch = 0x5b5d20u; // the speech controller's own text buffer (0x2800 bytes)
            std::strncpy(reinterpret_cast<char*>(h->rdramPtr + kScratch), testSub, 0x27ffu);
            h->runtime->eeScheduler().postGuestCall(0x4149a0u, 0x5102b0u, kScratch, 0u, 0u, nullptr);
            std::fprintf(stderr, "[drakan] DRAKAN_TEST_SUB: SetSubtitle queued ('%s')\n", testSub);
        }
        std::vector<spu2::SdrCallbackMsg> msgs;
        h->sdr.pollCallbacks(msgs);
        for (const auto& m : msgs) {
            uint32_t pkt[16] = {m.code, m.voiceBit};
            const int r = h->ee.post(kSidSdrCallback, 0, pkt, sizeof(pkt), nullptr, 0, false);
            if (g_sdLog.fetch_add(1) < 120)
                logfmt("sdrdrv callback code=0x%x arg=0x%x -> %d", m.code, m.voiceBit, r);
        }
        if (clock::now() >= nextSnapshot) {
            nextSnapshot += std::chrono::milliseconds(500);
            // "Stereo Sound" (Options > Audio, word 0x4fb744, default 1): the game copies it into the sound manager
            // (+0x440 = 0x4db930, read by SetGain 0x1e5470 to pan voices) only in Options_ApplyAll (0x3f9e20) / when
            // the box is toggled. A fresh start without a settings load never calls it, so everything was played mono
            // (pan ignored). Keep the manager flag equal to the option.
            if (h->rdramPtr) {
                uint32_t vt = 0, opt = 0, cur = 0;
                std::memcpy(&vt, h->rdramPtr + 0x4db4f0, 4);
                std::memcpy(&opt, h->rdramPtr + 0x4fb744, 4);
                std::memcpy(&cur, h->rdramPtr + 0x4db930, 4);
                if (vt != 0 && (opt == 0 || opt == 1) && cur != opt && g_stereoSync) {
                    std::memcpy(h->rdramPtr + 0x4db930, &opt, 4);
                    std::fprintf(
                        stderr,
                        "[drakan] sound manager stereo flag %u -> %u (follows Options > Audio > Stereo Sound)\n", cur,
                        opt);
                }
            }
            uint32_t nax[48];
            for (unsigned c = 0; c < 2; ++c)
                for (unsigned v = 0; v < 24; ++v)
                    nax[c * 24 + v] = h->spu.getAddr(static_cast<uint16_t>(spu2::SD_VADDR_NAX | spu2::SD_VOICE(c, v)));
            uint32_t dummy = 0;
            const int r = h->ee.post(kSidVoicePos, 0, nax, sizeof(nax), &dummy, 0, false);
            if (std::getenv("DRAKAN_SND_LOG") != nullptr) {
                std::string act;
                for (unsigned i = 0; i < 48; ++i)
                    if (nax[i] < 0x5000u || nax[i] > 0x5010u) {
                        char t[32];
                        std::snprintf(t, sizeof t, " c%u.v%u=%x", i / 24, i % 24, nax[i]);
                        act += t;
                    }
                double lvRms = 0, lvPeak = 0;
                h->audio.takeLevels(lvRms, lvPeak);
                std::fprintf(stderr, "[snd %.2f] snapshot post=%d peak=%d rms=%.4f active:%s\n", nowS(), r,
                             h->spu.takePeak(), lvRms, act.c_str());
            }
            (void)snapLogged;
        }
    }
}

void startSound(HleHost& h) {
    // drakan.ini [audio] enabled / volume / device (env DRAKAN_AUDIO, DRAKAN_VOLUME, DRAKAN_AUDIO_DEVICE);
    // DRAKAN_AUDIO_DUMP=<wav> writes the mixed output (what is sent to the speakers) as a 16-bit 48 kHz stereo WAV.
    h.spu.sdInit(0);
    spu2::AudioOutput::Options o;
    o.useDevice = drakan_cfg::getBool("audio", "enabled", "DRAKAN_AUDIO", true);
    o.device = drakan_cfg::getString("audio", "device", "DRAKAN_AUDIO_DEVICE", "default");
    o.volume = static_cast<float>(
        std::max(0.0, std::min(2.0, drakan_cfg::getFloat("audio", "volume", "DRAKAN_VOLUME", 100.0) / 100.0)));
    if (const char* d = std::getenv("DRAKAN_AUDIO_DUMP"))
        o.dumpPath = d;
    h.audio.setOptions(o);
    std::string err;
    const bool live = h.audio.start(&err);
    // The audio thread renders the SPU2 itself (on a wall clock while no device is open), so the voices always advance.
    if (!o.useDevice)
        std::fprintf(stderr, "[drakan] audio disabled (enabled = 0); running the SPU2 silently\n");
    else if (!live)
        std::fprintf(stderr, "[drakan] audio output unavailable (%s); running the SPU2 silently and retrying\n",
                     err.c_str());
    const char* pump = std::getenv("DRAKAN_SND_PUMP");
    if (!(pump && pump[0] == '0'))
        std::thread(soundPump, &h).detach();
}

// Debug aid: DRAKAN_TRACE_FN="1bf6e0,12b640" logs a0..a3 (+16 bytes behind each a-register that looks like a pointer)
// on entry and v0 on return for the listed guest functions (up to 8, only the first DRAKAN_TRACE_N hits, default 200).
constexpr int kMaxTraceFns = 8;
PS2Runtime::RecompiledFunction g_traceOrig[kMaxTraceFns] = {};
uint32_t g_traceAddr[kMaxTraceFns] = {};
std::atomic<long> g_traceBudget{200};

template <int N>
void traceHook(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    const bool log = g_traceBudget.fetch_sub(1) > 0;
    if (log) {
        char line[400];
        int len = std::snprintf(line, sizeof(line), "[fn %x t%d] ra=%x a0=%x a1=%x a2=%x a3=%x", g_traceAddr[N],
                                rt->eeScheduler().currentThreadId(), getRegU32(ctx, 31), getRegU32(ctx, 4),
                                getRegU32(ctx, 5), getRegU32(ctx, 6), getRegU32(ctx, 7));
        for (int r = 4; r < 8; ++r) {
            const uint32_t v = getRegU32(ctx, r) & 0x1FFFFFFFu;
            if (v >= 0x100000u && v + 16 <= PS2_RAM_SIZE && len < 330) {
                uint32_t q[4];
                std::memcpy(q, rdram + v, 16);
                len +=
                    std::snprintf(line + len, sizeof(line) - len, " [a%d]=%x,%x,%x,%x", r - 4, q[0], q[1], q[2], q[3]);
            }
        }
        if (g_traceAddr[N] == 0x43b1e8u || g_traceAddr[N] == 0x43d028u || g_traceAddr[N] == 0x43b4d0u) {
            const uint32_t screen = getRegU32(ctx, 4) & 0x1fffffffu;
            if (screen >= 0x100000u && screen + 0xa8 <= PS2_RAM_SIZE) {
                uint32_t key = 0, selected = 0;
                uint16_t flags = 0;
                std::memcpy(&key, rdram + screen + 0x50, 4);
                std::memcpy(&flags, rdram + screen + 0x9c, 2);
                std::memcpy(&selected, rdram + screen + 0xa4, 4);
                std::snprintf(line + len, sizeof(line) - len, " slotKey=%x flags=%x selected=%u", key, flags, selected);
            }
        }
        std::fprintf(stderr, "%s\n", line);
    }
    g_traceOrig[N](rdram, ctx, rt);
    if (log)
        std::fprintf(stderr, "[fn %x] -> v0=%x\n", g_traceAddr[N], getRegU32(ctx, 2));
}

template <int... Ns>
void installTraceHooks(PS2Runtime& rt, const std::vector<uint32_t>& addrs, std::integer_sequence<int, Ns...>) {
    PS2Runtime::RecompiledFunction hooks[] = {&traceHook<Ns>...};
    for (size_t i = 0; i < addrs.size() && i < kMaxTraceFns; ++i) {
        g_traceAddr[i] = addrs[i];
        g_traceOrig[i] = rt.lookupFunction(addrs[i]);
        if (g_traceOrig[i])
            rt.replaceFunction(addrs[i], hooks[i]);
    }
}

// DRAKAN_SND_DBG=1: logs the game's sound manager decisions (all addresses are SCES_500.06 EE code, see
// docs/reference/sound-debug.md):
//   [play]    PlaySound (FUN_001b7f30): id, flags, emitter position, range cull verdict, result (0 = no instance
//   created) [gain]    distance gain of a positional sound (FUN_001b1cf8) with the listener-relative distance it used
//   [setgain] the gain the instance finally got (FUN_001e5470); gain=0 for a sound inside its range was the sqrt.s bug
PS2Runtime::RecompiledFunction g_playSoundOrig = nullptr, g_gainOrig = nullptr, g_setGainOrig = nullptr;
std::atomic<int> g_playSoundLog{0}, g_gainLog{0}, g_setGainLog{0};

inline uint32_t sdbgRd32(uint8_t* rdram, uint32_t a) {
    uint32_t v = 0;
    a &= 0x1FFFFFFFu;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, rdram + a, 4);
    return v;
}

inline float sdbgF(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void soundDebugPlayHook(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t mgr = getRegU32(ctx, 4), id = getRegU32(ctx, 5), pos = getRegU32(ctx, 6), flags = getRegU32(ctx, 7);
    float px = 0, py = 0, pz = 0;
    if (pos >= 0x100000u) {
        px = sdbgF(sdbgRd32(rdram, pos));
        py = sdbgF(sdbgRd32(rdram, pos + 4));
        pz = sdbgF(sdbgRd32(rdram, pos + 8));
    }
    g_playSoundOrig(rdram, ctx, rt);
    if (g_playSoundLog.fetch_add(1) < 4000)
        std::fprintf(stderr, "[play %.3f] id=%x flags=%x ra=%x pos=(%.1f %.1f %.1f) cull2=%.3g mgrMute=%x -> v0=%x\n",
                     nowS(), id, flags, getRegU32(ctx, 31), px, py, pz, sdbgF(sdbgRd32(rdram, 0x4de96c)),
                     sdbgRd32(rdram, mgr + 0x458), getRegU32(ctx, 2));
}

void soundDebugGainHook(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t inst = getRegU32(ctx, 4);
    if (g_gainLog.fetch_add(1) < 3000) {
        const uint32_t lm = sdbgRd32(rdram, 0x4e0b1c), h = lm ? (sdbgRd32(rdram, lm + 0x24) & 0xffffu) : 0u,
                       gctx = sdbgRd32(rdram, 0x5565d4);
        const uint32_t lobj = h ? sdbgRd32(rdram, sdbgRd32(rdram, gctx + 0x7c) + (h - 1) * 8) : 0u;
        float lx = sdbgF(sdbgRd32(rdram, 0x4dad58)), ly = sdbgF(sdbgRd32(rdram, 0x4dad5c)),
              lz = sdbgF(sdbgRd32(rdram, 0x4dad60));
        if (lobj) {
            lx = sdbgF(sdbgRd32(rdram, lobj + 0x14));
            ly = sdbgF(sdbgRd32(rdram, lobj + 0x18));
            lz = sdbgF(sdbgRd32(rdram, lobj + 0x1c));
        }
        const float dx = sdbgF(sdbgRd32(rdram, inst + 0x48)) - lx, dy = sdbgF(sdbgRd32(rdram, inst + 0x4c)) - ly,
                    dz = sdbgF(sdbgRd32(rdram, inst + 0x50)) - lz;
        const float r = sdbgF(sdbgRd32(rdram, inst + 0x30));
        std::fprintf(stderr, "[gain %.3f] inst=%x id=%x scale=%g fade=%g radius=%g dist=%g listener=(%.0f %.0f %.0f)\n",
                     nowS(), inst, sdbgRd32(rdram, inst + 0xc), sdbgF(sdbgRd32(rdram, inst + 0x20)),
                     sdbgF(sdbgRd32(rdram, inst + 0x1c)), r, std::sqrt(dx * dx + dy * dy + dz * dz), lx, ly, lz);
    }
    g_gainOrig(rdram, ctx, rt);
}

void soundDebugSetGainHook(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t inst = getRegU32(ctx, 4);
    if (g_setGainLog.fetch_add(1) < 3000)
        std::fprintf(stderr, "[setgain %.3f] inst=%x id=%x gain=%g voice=%d\n", nowS(), inst,
                     sdbgRd32(rdram, inst + 0xc), ctx->f[12], static_cast<int32_t>(sdbgRd32(rdram, inst + 0x78)));
    g_setGainOrig(rdram, ctx, rt);
}

void soundDebugInstall(PS2Runtime& rt) {
    const char* e = std::getenv("DRAKAN_SND_DBG");
    if (!e || e[0] != '1')
        return;
    struct Hook {
        uint32_t addr;
        PS2Runtime::RecompiledFunction* orig;
        PS2Runtime::RecompiledFunction hook;
    } hooks[] = {{0x1b7f30u, &g_playSoundOrig, &soundDebugPlayHook},
                 {0x1b1cf8u, &g_gainOrig, &soundDebugGainHook},
                 {0x1e5470u, &g_setGainOrig, &soundDebugSetGainHook}};
    for (const Hook& h : hooks) {
        *h.orig = rt.lookupFunction(h.addr);
        if (*h.orig)
            rt.replaceFunction(h.addr, h.hook);
    }
    std::fprintf(stderr, "[drakan] DRAKAN_SND_DBG: sound manager hooks installed\n");
}

void applyTraceEnv(PS2Runtime& rt) {
    const char* e = std::getenv("DRAKAN_TRACE_FN");
    if (!e)
        return;
    if (const char* n = std::getenv("DRAKAN_TRACE_N"))
        g_traceBudget = std::atol(n);
    std::vector<uint32_t> addrs;
    std::string list = e;
    size_t pos = 0;
    while (pos < list.size()) {
        const size_t comma = list.find(',', pos);
        addrs.push_back(static_cast<uint32_t>(std::strtoul(
            list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos).c_str(), nullptr, 16)));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    installTraceHooks(rt, addrs, std::make_integer_sequence<int, kMaxTraceFns>{});
}

void applyDrakanOverrides(PS2Runtime& runtime) {
    // libipu constant tables of this game (docs/reference/fmv.md: matrix data 0x517000, CLUT 0x517050); the stub's
    // defaults belong to another game
    ps2_stubs::setIpuInitTables(0x517000u, 0x517050u, 0u);

    runtime.replaceFunction(kLoadIopModules, &hookLoadIopModules);
    runtime.replaceFunction(kSifLoadModuleBuffer, &hookSifLoadModuleBuffer);
    runtime.replaceFunction(kRand, &hookRand);
    drakan_controls_menu::install(runtime);
    applyTraceEnv(runtime);
    drakan_frame_rate::install(runtime);
    drakan_dual_wield::install(runtime);
    soundDebugInstall(runtime);
#if defined(PS2X_HAVE_DRAKAN_INPUT)
    drakanInstallInput(runtime);
#else
    drakanInstallPortableCamera(runtime);
#endif
    drakan_ws::configure();
    drakan_ws::install(runtime, runtime.memory().getRDRAM(), &runtime.memory().gs().vsyncTick);
    drakan_inventory_typography::install(runtime, &runtime.memory().gs().vsyncTick);
#ifdef PS2X_HAVE_GSR
    drakan_dlss::installSceneHooks(runtime);
#endif

    const std::string iso = resolveIsoPath();
    const std::string lang = resolveLanguageRoot();
    const char* logEnv = std::getenv("DRAKAN_HLE_LOG");
    if (logEnv)
        g_logBudget = std::atol(logEnv);

    auto host = std::make_unique<HleHost>(runtime, runtime.memory().getRDRAM());
    std::string err;
    bool ok = false;
    if (std::filesystem::is_directory(iso))
        ok = host->fs.openFolder(iso, &err);
    else
        ok = host->fs.openIso(iso, &err);
    if (!ok) {
        std::fprintf(stderr, "[drakan] cannot open game data '%s': %s -- RSRC services disabled (set DRAKAN_ISO)\n",
                     iso.c_str(), err.c_str());
        return;
    }
    host->fs.setLanguageRoot(lang);
    // libsd's reverb presets come from the disc's own LIBSD.IRX; without them reverb stays silent (Drakan never
    // enables it, so this only matters for completeness).
    {
        hle::IsoFs::Node libsd;
        spu2::EffectPresets presets;
        if (host->fs.findKey("IOP\\MODULES\\LIBSD.IRX", libsd) && !libsd.dir) {
            const std::vector<uint8_t> irx = host->fs.readAll(libsd);
            if (spu2::findLibsdEffectPresets(irx.data(), irx.size(), presets))
                host->spu.setEffectPresets(presets);
            else
                std::fprintf(stderr, "[drakan] LIBSD.IRX: reverb presets not recognised; reverb disabled\n");
        }
    }
    // Test aid: DRAKAN_START_LEVEL="Level 4A - Ravenshold" makes New Game (after the intro) load that level in place
    // of the first one, so harness runs reach any level without a save. DRAKAN_START_LEVEL_FROM names the level that
    // is replaced (default the first playable level).
    if (const char* startLevel = std::getenv("DRAKAN_START_LEVEL"); startLevel && *startLevel) {
        const char* from = std::getenv("DRAKAN_START_LEVEL_FROM");
        host->iop.file.setLevelRedirect(from && *from ? from : "Level 1A - Surdana", startLevel);
        std::fprintf(stderr, "[drakan] start level: '%s' replaces '%s'\n", startLevel,
                     from && *from ? from : "Level 1A - Surdana");
    }
#ifdef PS2X_HAVE_UI
    {
        drakan_ui::Options uiOpt = drakan_ui::Options::fromEnvironment();
#ifdef PS2X_HAVE_GSR
        // The generated prompt textures (INTERFCE.TXD 97 / 124 / 94) depend on drakan_input.ini: keep them out of the
        // HD texture dump / replacement
        uiOpt.onGenerated = [](const drakan_ui::PromptOverride::Generated& g) {
            const uint32_t* px = g.rgba;
            const int w = g.w, h = g.h;
            const uint64_t hash = gsr::textureContentHash(px, (uint32_t)w, (uint32_t)h);
            const bool skip =
                !std::getenv("DRAKAN_NO_PROMPT_EXCLUDE"); // test aid: DRAKAN_NO_PROMPT_EXCLUDE=1 lets them into the
                                                          // dump (proves the hash prediction)
            if (skip)
                gsr::excludeTextureHash(hash);
            std::fprintf(stderr, "[drakan] generated prompt texture %dx%d, content hash %016llx%s\n", w, h,
                         (unsigned long long)hash, skip ? " (excluded from HD dump / replacement)" : "");
            // The inventory icon atlas: its section icons keep the HD pack's image of the disc texture; the
            // page-switch tile becomes an HD overlay with captions fitted to the arrow buttons.
            if (g.id == 94 && g.original && skip) {
                const char* windowsDir = std::getenv("WINDIR");
                const std::string fontPath =
                    std::string(windowsDir && *windowsDir ? windowsDir : "C:/Windows") + "/Fonts/arialbd.ttf";
                drakan_ui::TileOverlay overlay;
                const bool registered = drakan_ui::buildPageTileOverlay(px, (uint32_t)w, (uint32_t)h, 4, g.pageNext,
                                                                        g.pagePrev, fontPath, overlay) &&
                                        gsr::registerRuntimeOverlay(hash, g.original, (uint32_t)w, (uint32_t)h, 4,
                                                                    overlay.rgba.data(), overlay.coverage.data());
                std::fprintf(stderr, "[drakan] page tile overlay '%s' / '%s': %s\n", g.pageNext.c_str(),
                             g.pagePrev.c_str(), registered ? "registered" : "unavailable; native tile retained");
            }
            if (drakan_cfg::getBool("general", "native_font_atlas", "DRAKAN_NATIVE_FONT_ATLAS", true)) {
                std::vector<drakan_ui::FontAtlasCell> cells;
                const bool decorative = drakan_ui::font96AsciiCells(px, w, h, cells);
                if (decorative || drakan_ui::font97AsciiCells(px, w, h, cells)) {
                    const char* fontPathEnv = std::getenv("DRAKAN_NATIVE_FONT_TTF");
                    const char* windowsDir = std::getenv("WINDIR");
                    const std::string fontPath =
                        fontPathEnv && *fontPathEnv
                            ? fontPathEnv
                            : (std::string(windowsDir && *windowsDir ? windowsDir : "C:/Windows") +
                               (decorative ? "/Fonts/pala.ttf" : "/Fonts/arial.ttf"));
                    drakan_ui::FontAtlas atlas;
                    const bool built = decorative ? drakan_ui::buildFont96Atlas(px, w, h, fontPath, atlas)
                                                  : drakan_ui::buildFont97Atlas(px, w, h, fontPath, atlas);
                    const bool registered =
                        built && gsr::registerRuntimeTexture(hash, atlas.width, atlas.height, atlas.rgba.data(), true);
                    std::fprintf(stderr, "[font-atlas] font%u hash=%016llx glyphs=%u %ux%u %s\n",
                                 decorative ? 96u : 97u, (unsigned long long)hash, atlas.glyphsReplaced, atlas.width,
                                 atlas.height,
                                 registered ? "registered (native layout)" : "unavailable; native source retained");
                }
            }
        };
#endif
        if (!std::getenv("DRAKAN_UI_PROMPTS")) // drakan.ini [general] ui_prompts = keyboard | ps2
        {
            std::string up = drakan_cfg::getString("general", "ui_prompts", nullptr, "keyboard");
            std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
            uiOpt.prompts = up != "ps2";
        }
        std::fprintf(stderr, "[drakan] %s\n", drakan_ui::install(host->iop.res, uiOpt).c_str());
    }
#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
    {
        // Options > "Adjust Screen" (INTERFCE.XDB 156) becomes "PC Settings"; the Options handler hook opens the
        // overlay for it.
        auto pcText = std::make_shared<hle::XdbTextOverride>();
        pcText->setText("INTERFCE.XDB", static_cast<uint16_t>(kXdbAdjustScreen), -1, "PC Settings");
        host->iop.res.addOverride(pcText);
    }
    drakan_settings_host::setArtSource(
        &host->fs); // the F1 menu draws in the game's own style (UI textures decoded from the disc)
#if defined(PS2X_HAVE_GSR)
    drakan_dlss::setAssetSource(&host->fs);
#endif
#endif
#endif
    const char* dl = std::getenv("DRAKAN_RES_DELIVERY");
    if (dl && std::string(dl) == "direct")
        host->iop.res.setDelivery(hle::ResourceService::Delivery::Direct);
    if (const char* lat = std::getenv("DRAKAN_RES_LATENCY_US"))
        host->resLatency = std::chrono::microseconds(std::atol(lat));
    if (std::getenv("DRAKAN_RES_NOFAIL"))
        host->iop.res.skipFailureCompletions = true;
    hle::setLogSink(logLine);
    hle::setVerbose(g_logBudget.load() > 0);
    std::fprintf(stderr, "[drakan] HLE: %zu files in '%s', language root %s, delivery %s\n", host->fs.entries().size(),
                 iso.c_str(), lang.c_str(), (dl && std::string(dl) == "direct") ? "direct" : "rpc1301");
    host->iop.res.setRequestObserver(&drakan_subtitles::onResourceRequest); // narration subtitles trigger
    g_host = host.release();
    runtime.replaceFunction(kSdRemote, &hookSdRemote);
    startSound(*g_host);
    drakan_settings_host::setAudio(&g_host->audio);
    {
        std::string lg = drakan_cfg::getString("general", "language", "DRAKAN_LANG", "english");
        lg.erase(0, std::min(lg.find_first_not_of("\\/"), lg.size()));
        drakan_subtitles::init(&g_host->audio, runtime.memory().getRDRAM(), lg);
    }
    // Guest liveness watchdog: the vblank counter is advanced by the EE scheduler thread. If it stops (EE held /
    // blocked / hung) the SPU2 output pauses instead of looping the last buffered chunk of a streamed voice.
    // DRAKAN_AUDIO_STALL_MS=0 disables it.
    {
        const char* st = std::getenv("DRAKAN_AUDIO_STALL_MS");
        const long ms = st ? std::atol(st) : 250;
        if (ms > 0)
            g_host->audio.setHeartbeat(&runtime.memory().gs().vsyncTick, static_cast<unsigned>(ms));
    }
    drakan_pause::setAudio(&g_host->audio);
    if (runtime.hasFunction(kMovieDecodeStep)) {
        g_origMovieDecodeStep = runtime.lookupFunction(kMovieDecodeStep);
        runtime.replaceFunction(kMovieDecodeStep, &hookMovieDecodeStep);
    }
    if (runtime.hasFunction(kMovieDoneThunk)) {
        g_origMovieDone = runtime.lookupFunction(kMovieDoneThunk);
        runtime.replaceFunction(kMovieDoneThunk, &hookMovieDone);
    }
    if (runtime.hasFunction(kOptionsHandler)) {
        g_origOptionsHandler = runtime.lookupFunction(kOptionsHandler);
        runtime.replaceFunction(kOptionsHandler, &hookOptionsHandler);
        runtime.replaceFunction(0x442cb0u, &hookOptionsClose);
    }

    drakan_menus::install(runtime); // title Quit, save pages (drakan_menus.cpp)

    PS2Runtime::HleRpcHooks hooks{};
    hooks.canBind = &hleCanBind;
    hooks.handle = &hleHandleRpc;
    runtime.setHleRpcHooks(hooks);
}
} // namespace

// The boot ELF straight from the disc image: when no extracted SCES_500.06 is configured, main.cpp calls this. The file
// is cached under %LOCALAPPDATA%\DrakanPC (re-extracted when the size differs) because the loader wants a file path.
// Returns "" on failure.
std::string drakanExtractBootElfFromIso() {
    namespace fs = std::filesystem;
    const std::string iso = resolveIsoPath();
    std::error_code ec;
    if (iso.empty() || !fs::is_regular_file(iso, ec))
        return std::string();
    hle::IsoFs isofs;
    std::string err;
    if (!isofs.openIso(iso, &err)) {
        std::fprintf(stderr, "[drakan] cannot open '%s': %s\n", iso.c_str(), err.c_str());
        return std::string();
    }
    hle::IsoFs::Node node;
    if (!isofs.lookup("SCES_500.06", node) || node.dir) {
        std::fprintf(stderr, "[drakan] SCES_500.06 not found on '%s'\n", iso.c_str());
        return std::string();
    }
    const char* la = std::getenv("LOCALAPPDATA");
    fs::path dir = la && *la ? fs::path(la) / "DrakanPC" : fs::temp_directory_path(ec) / "DrakanPC";
    fs::create_directories(dir, ec);
    const fs::path out = dir / "SCES_500.06";
    if (fs::is_regular_file(out, ec) && fs::file_size(out, ec) == node.size)
        return out.string();
    const std::vector<uint8_t> data = isofs.readAll(node);
    if (data.size() != node.size)
        return std::string();
    if (std::FILE* f = std::fopen(out.string().c_str(), "wb")) {
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        return out.string();
    }
    return std::string();
}

// Clean shutdown: fade the SPU2 output out (10 ms ramp, no click), then stop the WASAPI stream (and finish a
// DRAKAN_AUDIO_DUMP wav).
void drakanShutdownAudio() {
    if (!g_host)
        return;
    g_host->audio.setMuted(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    g_host->audio.stop();
}

// Referenced from game_overrides.cpp so the linker keeps this object file out of the static library.
void ps2DrakanOverridesLinkAnchor() {}

// Match by ELF name + entry point (the CRC is not pinned: other regional builds would need their own addresses).
PS2_REGISTER_GAME_OVERRIDE("Drakan: The Ancients' Gates (SCES-500.06)", "SCES_500.06", 0x100008u, 0u,
                           &applyDrakanOverrides)
