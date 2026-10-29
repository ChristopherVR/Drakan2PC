// Drakan: GsrBridge - see include/runtime/gs/gs_gsr_bridge.h
#include "ps2_perf.h"
#include "runtime/gs/gs_gsr_bridge.h"
#include "runtime/ps2_memory.h"
#include "../drakan_config.h"
#include "../drakan_runtime_profile.h"
#include "../drakan_fpsmeter.h"
#include "../drakan_window.h"
#include "../drakan_pause.h"
#include "../drakan_settings_host.h"
#include "../drakan_widescreen.h"
#include "../drakan_dlss.h"
#include "../drakan_present_clock.h"
#include "gs_vblank_handoff.h"
#include "gs_frame_activity.h"
#define GSR_BATCH_DEFINE 1
#include "gs_gsr_batch.h"

#include <gsr/gsr.h>
#include <gsr/scene_guide_camera.h>
#include <gsr/object_motion_scope.h>
#include <gsr/scene_geometry.h>
#include <gsr/scene_models.h>
#include <unordered_map>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr uint32_t kPrivBase = 0x12000000u;

// DRAKAN_FRAME_AUDIT=2: one "[ft]" line per present (P), slow GS record (W) and slow present (H); with the EE-side
// lines (U, F, V) they give a per-frame timeline on one steady_clock axis (docs/reference/env-flags.md).
bool frameTraceEnabled() {
    static const bool on = [] {
        const char* v = std::getenv("DRAKAN_FRAME_AUDIT");
        return v && std::atoi(v) >= 2;
    }();
    return on;
}

using drakan_cfg::compareNoCase;

// "4:3", "16:9", "1.777" -> ratio; "native" -> 0 (the display rectangle's own ratio); "stretch" -> -1 (fill the window)
float parseAspect(const std::string& str, float def) {
    const char* s = str.c_str();
    if (!*s)
        return def;
    if (!compareNoCase(s, "native") || !compareNoCase(s, "auto"))
        return 0.0f;
    if (!compareNoCase(s, "stretch") || !compareNoCase(s, "fill"))
        return -1.0f;
    float a = 0, b = 0;
    if (std::sscanf(s, "%f:%f", &a, &b) == 2 && a > 0 && b > 0)
        return a / b;
    if (std::sscanf(s, "%f", &a) == 1 && a > 0)
        return a;
    return def;
}

// Pure output-size policy. Never use the scaled render target as an input:
// doing so makes a scale change feed back into the next resize calculation.
struct AutoInternalScale {
    bool enabled = false;
    uint32_t requested = 1;

    void configure(const std::string& value) {
        enabled = value.empty() || compareNoCase(value.c_str(), "auto") == 0;
        requested = enabled ? 1u : static_cast<uint32_t>(std::clamp(std::atoi(value.c_str()), 1, 8));
    }

    static uint32_t choose(uint32_t w, uint32_t h, double aspect, bool stretch, bool native512) {
        if (!w || !h)
            return 0; // no decision for a minimized/invalid client
        double vw = w, vh = h;
        if (!stretch && std::isfinite(aspect) && aspect > 0) {
            if (vw / vh > aspect)
                vw = std::floor(vh * aspect + 0.5);
            else
                vh = std::floor(vw / aspect + 0.5);
        }
        const double pixels = native512 ? std::max(vw, vh) : vh;
        return static_cast<uint32_t>(std::clamp(std::ceil(pixels / 512.0), 1.0, 8.0));
    }

    bool update(uint32_t w, uint32_t h, double aspect, bool stretch, bool native512, bool minimized = false) {
        if (!enabled || minimized)
            return false;
        const auto next = choose(w, h, aspect, stretch, native512);
        if (!next || next == requested)
            return false;
        requested = next;
        return true;
    }
};

bool native512Display(uint64_t pmode, uint64_t display1, uint64_t display2) {
    for (unsigned i = 0; i < 2; ++i) {
        if (!(pmode & (1ull << i)))
            continue;
        const auto d = i ? display2 : display1;
        const auto w = (((d >> 32) & 0xfffu) + 1) / (((d >> 23) & 15u) + 1);
        const auto h = (((d >> 44) & 0x7ffu) + 1) / (((d >> 27) & 3u) + 1);
        if (w && h)
            return w == 512 && h == 512;
    }
    return false;
}

// GIF records produced by a thread inside a gsr_batch scope (the VU1 worker during one VIF1 job) are staged here and
// queued in one locked append, instead of one lock and copy per XGKICK packet. Every other record the thread queues,
// and every wait for the renderer, appends the staged records first, so the order seen by the renderer is unchanged.
struct GifStage {
    std::vector<uint8_t> records;
    uint32_t depth = 0;
    GsrBridge::Impl* owner = nullptr;
};
thread_local GifStage t_stage;
constexpr size_t kStageFlushBytes = 256u << 10;

} // namespace

struct GsrBridge::Impl {
    void* parent = nullptr; // the top-level game window (drakan_window): the swapchain lives directly on it
    GSRegisters* priv = nullptr;
    std::unique_ptr<gsr::Renderer> gs;
    uint64_t lastSig = 0;
    uint32_t curW = 0, curH = 0;
    AutoInternalScale autoScale; // host-owned, initialized from env-over-ini selection
    double initialAspect = 4.0 / 3.0;
    bool initialStretch = false;
    uint64_t frames = 0;
    std::string shotPath;
    uint64_t shotAt = 0;
    bool shotDone = false;
    uint64_t statsEvery = 0;
    uint64_t dispLog = 0, dispLogFrom = 0;

    // requests from the settings overlay (made inside the present callback), executed by hostTick outside of the
    // present
    std::atomic<int> reqFullscreen{-1};
    std::atomic<int> reqW{0}, reqH{0};
    std::atomic<int> reqScale{-1}; // -1 no request, 0 Auto, 1..8 fixed

    // Consume on the host thread, outside the overlay/present callback. Keep
    // the resize policy and renderer setter together under the bridge lock.
    void applyScaleRequest() {
        const int requested = reqScale.exchange(-1);
        if (requested < 0)
            return;
        std::lock_guard<std::recursive_mutex> lock(rmtx);
        autoScale.enabled = requested == 0;
        if (autoScale.enabled) {
            // Force the next valid output-size sample, even if its scale is 1.
            // A minimized window defers that sample without changing resources.
            autoScale.requested = 0;
        } else {
            autoScale.requested = static_cast<uint32_t>(std::clamp(requested, 1, 8));
            gs->setInternalScale(autoScale.requested);
        }
    }

    void applyWindowRequests() {
        applyScaleRequest(); // disable Auto before a simultaneous fullscreen resize
        const int fs = reqFullscreen.exchange(-1);
        if (fs >= 0)
            setFullscreen(fs != 0);
        const int rw = reqW.exchange(0), rh = reqH.exchange(0);
        if (rw > 0 && rh > 0 && !fullscreen && parent)
            drakan_win::setClientSize(rw, rh);
    }

    // window state
    bool fullscreen = false;
    bool vsync = true;
    bool altEnterDown = false, f11Down = false, f2Down = false;
    std::chrono::steady_clock::time_point nextFrame = std::chrono::steady_clock::now();
    double refreshSeconds = 0.0; // monitorRefreshSeconds(parent), re-read every second (window moves, mode changes)
    std::chrono::steady_clock::time_point refreshQueryAt{};
    // pacing statistics (PS2X_GS_PACING=<seconds> prints them)
    double pacingEvery = 0;
    std::chrono::steady_clock::time_point pacingT0 = std::chrono::steady_clock::now(),
                                          lastPresent = std::chrono::steady_clock::now();
    uint64_t pacingPresents = 0;
    std::atomic<uint64_t> pacingVsyncs{0}; // counted on the renderer worker
    double pacingMax = 0, pacingSum = 0, pacingMin = 1e9;
    std::atomic<uint64_t> drainWaitNs{0}, handoffWaitNs{0}, fairWaitNs{0};

    void setFullscreen(bool on) {
        if (on == fullscreen || !parent)
            return;
        drakan_win::setFullscreen(on);
        fullscreen = on;
        std::fprintf(stderr, "[gsr] %s\n", on ? "borderless fullscreen" : "windowed");
        {
            std::lock_guard<std::recursive_mutex> r(rmtx);
            syncSize();
        }
    }

    // follow the window's client size: resize the swapchain (no stretched back buffer). Caller holds rmtx.
    void syncSize() {
        int w = 0, h = 0;
        if (!drakan_win::clientSize(w, h))
            return;
        syncSizeTo(static_cast<uint32_t>(w), static_cast<uint32_t>(h), drakan_win::minimized());
    }

    // Caller holds rmtx. Explicit dimensions also allow device-free CPU checks.
    void syncSizeTo(uint32_t w, uint32_t h, bool minimized = false) {
        if (!w || !h || minimized)
            return;
        const auto oldW = curW, oldH = curH;
        if (w != curW || h != curH) {
            gs->resizeWindow(w, h);
            curW = w;
            curH = h;
            drakan_fpsmeter::setInfo(gs->adapterName(), w, h);
        }
        if (!autoScale.enabled)
            return;
        uint32_t vw = 0, vh = 0;
        gs->getViewport(vw, vh);
        const bool haveViewport = vw && vh;
        const double aspect = haveViewport ? static_cast<double>(vw) / vh : initialAspect;
        const bool known512 = native512Display(gs->readPriv(0), gs->readPriv(0x80), gs->readPriv(0xa0));
        const auto oldScale = autoScale.requested;
        if (autoScale.update(w, h, aspect, haveViewport ? false : initialStretch, known512)) {
            gs->setInternalScale(autoScale.requested); // queues under setMu; no context work here
            std::fprintf(stderr, "[gsr-auto-scale] output=%ux%u->%ux%u viewportAspect=%.6f native512=%d scale=%u->%u\n",
                         oldW, oldH, w, h, aspect, static_cast<int>(known512), oldScale, autoScale.requested);
        }
    }

    void tickWindow();
    drakan_pacing::LatchProgress latchProgress;
    drakan_pacing::WorldActivity worldActivity;
    uint64_t pacingLastWorlds = 0, pacingLastShownWorlds = 0;
    uint64_t latchedWorldSerial = 0;                    // rmtx, associated with the last completed display snapshot
    uint64_t shownWorlds = 0, lastShownWorldSerial = 0; // host; rmtx protects worldActivity
    uint64_t pacingActualPresents = 0;
    double pacingLockMs = 0, pacingPresentMs = 0, pacingWaitMs = 0;
    drakan_vblank::Handoff vblankHandoff;
    drakan_vblank::FieldMode presentationFieldMode;
    std::array<std::atomic<uint64_t>, drakan_vblank::Handoff::ReasonCount> handoffReasons{};
    uint64_t cpuVramWrites = 0;                // rmtx; transfers are separately counted by gsr
    bool lastVblankAdvanced = true;            // worker-owned; only read after executing RecVsync
    bool uncappedRequested = false;            // immutable after create; config/environment at startup
    bool nativeCommitted = false;              // rmtx: rearmed only by an accepted native commit
    std::atomic<bool> completionDriven{false}; // host wake/duplicate-present policy
    uint64_t duplicateWorldVblanks = 0;
    std::array<std::atomic<uint64_t>, 5> closedWorldClears{};
    std::array<unsigned, 5> sceneClearLogs{}; // rmtx; at most 4 traces per source category
    void noteHandoffScene(const gsr::SceneGuideCamera& camera) {
        const auto& stats = gs->stats();
        drakan_vblank::Handoff::Sample content;
        content.draws = stats.drawCalls;
        content.hostToLocal = stats.transfersH2L;
        content.localToLocal = stats.transfersL2L;
        content.uploads = stats.rtUploads;
        content.readbacks = stats.rtReadbacks;
        content.cpuWrites = cpuVramWrites;
        content.liveApplied = stats.liveApplied;
        vblankHandoff.scene(camera, &content);
        if (pacingEvery <= 0)
            return;
        const auto& c = vblankHandoff.sceneClear();
        if (!c.cleared)
            return;
        const unsigned slot = c.sourcePc == 0x139010u   ? 0
                              : c.gui >= 0              ? 3
                              : c.sourcePc == 0x1e93b8u ? 1
                              : c.sourcePc == 0x1e97a0u ? 2
                                                        : 4;
        closedWorldClears[slot].fetch_add(1, std::memory_order_relaxed);
        if (sceneClearLogs[slot] < 4) {
            ++sceneClearLogs[slot];
            std::fprintf(stderr,
                         "[handoff-scene-clear] pc=%x flags=%x gui=%d closedBy=%x phaseBefore=%d phaseAfter=%d\n",
                         c.sourcePc, c.flags, c.gui, c.closedBy, c.phaseBefore, c.phaseAfter);
        }
    }
    std::condition_variable presentCv;
    std::atomic<bool> stopPresentHandoff{false};
    // recursive_mutex has no fairness guarantee. A stream of tiny GIF records
    // can otherwise repeatedly reacquire it ahead of the window thread.
    std::atomic<bool> windowPresentWaiting{false};

    // ---- Drakan perf: asynchronous renderer ---------------------------------------------------------------------
    // The EE thread only appends records (GIF packets, register writes, vsync snapshots) to `pending`; one worker
    // thread replays them, in order, on the renderer. Everything that needs a result from the renderer (local->host
    // reads, VRAM peeks) drains the queue first. `rmtx` serialises every use of the renderer (worker, hostTick/present,
    // drained synchronous calls). PS2X_GS_ASYNC=0 runs the renderer synchronously on the caller as before.
    enum RecordType : uint32_t {
        RecGif = 1,   // payload: u32 path, u32 size, data
        RecReg = 2,   // payload: u32 reg, u64 value
        RecSig = 3,   // payload: u64 value (EE wrote SIGLBLID)
        RecVsync = 4, // payload: 17 x u64 priv snapshot
        RecCam = 5,   // payload: u32 vuAddr, u32 qwCount, 28 x float (VU1 data rows 0..6) - temporal upscaler camera
        RecScene = 6, // payload: SceneGuideCamera, ordered with scene GIF submissions
        RecObject = 7,
        RecGeometry = 9,      // immutable pre-cull native level snapshot
        RecModels = 10,       // owned immutable model snapshot, referenced by queue ticket
        RecDisplayCommit = 8, // payload: 17 x u64; a complete native display transaction
    };
    bool async = true;
    std::mutex qmtx;
    std::mutex modelsMutex;
    uint64_t modelsTicket = 0;
    std::unordered_map<uint64_t, std::shared_ptr<const gsr::SceneModels>> queuedModels;
    std::condition_variable qcv;       // worker waits for records
    std::condition_variable drainedCv; // producers wait for space / drain
    std::vector<uint8_t> pending;
    bool busy = false;
    bool workerWaiting = false;                     // the worker sleeps on qcv: only then a producer has to wake it
    uint64_t queuedCommits = 0, retiredCommits = 0; // qmtx; at most two submitted frames
    bool stopping = false;
    std::thread worker;
    std::recursive_mutex rmtx;
    std::atomic<uint64_t> eeSig{0}; // last SIGLBLID value known to both sides (see pushSig/pullSig)
    static constexpr size_t kMaxPending = 96u << 20;

    template <typename T>
    static void put(std::vector<uint8_t>& v, const T& x) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
        v.insert(v.end(), p, p + sizeof(T));
    }

    void enqueueBegin(std::unique_lock<std::mutex>& lock, size_t incoming) {
        // back-pressure: do not let the EE run unboundedly ahead of the renderer
        drainedCv.wait(lock, [&] { return pending.size() + incoming <= kMaxPending || pending.empty(); });
    }

    // qmtx held. A SIGLBLID the EE wrote since the last record goes ahead of the next GIF records.
    void putSig() {
        const uint64_t sig = priv->siglblid;
        if (sig != eeSig.load(std::memory_order_relaxed)) {
            eeSig.store(sig, std::memory_order_relaxed);
            put(pending, static_cast<uint32_t>(RecSig));
            put(pending, static_cast<uint32_t>(8));
            put(pending, sig);
        }
    }

    // qmtx held. Queues this thread's staged GIF records ahead of whatever the caller appends next.
    void appendStaged(std::unique_lock<std::mutex>& lock) {
        GifStage& stage = t_stage;
        if (stage.records.empty() || stage.owner != this)
            return;
        enqueueBegin(lock, stage.records.size() + 24u);
        putSig();
        if (pending.empty()) {
            pending.swap(stage.records); // the worker took the previous queue: hand over the buffer, keep its spare
        } else {
            pending.insert(pending.end(), stage.records.begin(), stage.records.end());
        }
        stage.records.clear();
    }

    // Producer side of qmtx: every record and every wait for the renderer goes through here.
    std::unique_lock<std::mutex> lockProducer() {
        std::unique_lock<std::mutex> lock(qmtx);
        appendStaged(lock);
        return lock;
    }

    void flushStaged() {
        if (t_stage.records.empty())
            return;
        std::unique_lock<std::mutex> lock = lockProducer();
        const bool wake = workerWaiting;
        lock.unlock();
        if (wake)
            qcv.notify_one();
    }

    // Wait until every record queued so far has been executed.
    void drain(drakan_runtime_profile::Id reason) {
        if (!async)
            return;
        drakan_runtime_profile::Scope profile(reason);
        const auto start = pacingEvery > 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        std::unique_lock<std::mutex> lock = lockProducer();
        if (!pending.empty() && workerWaiting)
            qcv.notify_one();
        drainedCv.wait(lock, [&] { return pending.empty() && !busy; });
        if (pacingEvery > 0)
            drainWaitNs.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(),
                std::memory_order_relaxed);
    }

    void execute(uint32_t type, const uint8_t* p, uint32_t size);
    void workerMain();

    void pushSigToGsr() // synchronous mode / worker side
    {
        // EE-side direct writes to SIGLBLID (priv register) win over gsr's copy
        const uint64_t cur = priv->siglblid;
        if (cur != lastSig) {
            lastSig = cur;
            gs->writePriv(kPrivBase + 0x1080, lastSig);
        }
    }
    void pullSigFromGsr() {
        const uint64_t v = gs->readPriv(kPrivBase + 0x1080);
        if (v != lastSig) {
            lastSig = v;
            priv->siglblid = v;
            eeSig.store(v, std::memory_order_relaxed);
        }
    }
    static constexpr size_t kPrivCount = 17;
    static void snapshotPriv(const GSRegisters& r, uint64_t* v) {
        v[0] = r.pmode;
        v[1] = r.smode1;
        v[2] = r.smode2;
        v[3] = r.srfsh;
        v[4] = r.synch1;
        v[5] = r.synch2;
        v[6] = r.syncv;
        v[7] = r.dispfb1;
        v[8] = r.display1;
        v[9] = r.dispfb2;
        v[10] = r.display2;
        v[11] = r.extbuf;
        v[12] = r.extdata;
        v[13] = r.extwrite;
        v[14] = r.bgcolor;
        v[15] = r.imr;
        v[16] = r.busdir;
    }
    void applyPriv(const uint64_t* v) {
        static const uint32_t off[] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
                                       0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0};
        for (size_t i = 0; i < sizeof(off) / sizeof(off[0]); ++i)
            gs->writePriv(kPrivBase + off[i], v[i]);
        gs->writePriv(kPrivBase + 0x1010, v[15]);
        gs->writePriv(kPrivBase + 0x1040, v[16]);
    }
    void doVsync(const uint64_t* snapshot, uint64_t vsyncTick);
    void doDisplayCommit(const uint64_t* snapshot);
    void publishDisplay(); // rmtx held; serial and world association published together
    void completedFrameDiagnostics();
    void waitForHandoff(uint64_t serial, bool nativeCompletion = false); // no renderer/context lock held
};

void GsrBridge::Impl::execute(uint32_t type, const uint8_t* p, uint32_t size) {
    (void)size;
    using namespace drakan_runtime_profile;
    Scope profile(type == RecGif             ? Gif
                  : type == RecScene         ? Scene
                  : type == RecVsync         ? Vblank
                  : type == RecDisplayCommit ? DisplayCommit
                                             : OtherRecord);
    switch (type) {
        case RecGif: {
            uint32_t path, bytes;
            std::memcpy(&path, p, 4);
            std::memcpy(&bytes, p + 4, 4);
            gs->submitGifPacket(path == 1   ? gsr::GifPath::Path1
                                : path == 2 ? gsr::GifPath::Path2
                                            : gsr::GifPath::Path3,
                                p + 8, bytes);
            pullSigFromGsr();
            break;
        }
        case RecReg: {
            uint32_t reg;
            uint64_t value;
            std::memcpy(&reg, p, 4);
            std::memcpy(&value, p + 8, 8);
            gs->writeRegister(static_cast<uint8_t>(reg), value);
            pullSigFromGsr();
            break;
        }
        case RecSig: {
            uint64_t value;
            std::memcpy(&value, p, 8);
            lastSig = value;
            gs->writePriv(kPrivBase + 0x1080, value);
            break;
        }
        case RecCam: {
            uint32_t a, n;
            float rows[28];
            std::memcpy(&a, p, 4);
            std::memcpy(&n, p + 4, 4);
            std::memcpy(rows, p + 8, sizeof(rows));
            gs->noteVuUpload(a, n, rows);
            break;
        }
        case RecObject: {
            gsr::ObjectMotionScope scope;
            std::memcpy(&scope, p, sizeof scope);
            gs->noteObjectMotion(scope);
            break;
        }
        case RecModels: {
            if (size != 8)
                break;
            uint64_t ticket;
            std::memcpy(&ticket, p, 8);
            std::shared_ptr<const gsr::SceneModels> models;
            {
                std::lock_guard<std::mutex> lock(modelsMutex);
                auto it = queuedModels.find(ticket);
                if (it != queuedModels.end()) {
                    models = std::move(it->second);
                    queuedModels.erase(it);
                }
            }
            if (models)
                gs->noteSceneModels(std::move(models));
            break;
        }
        case RecGeometry: {
            if (size < 16 || (size - 16) % sizeof(gsr::SceneTriangle))
                break;
            gsr::SceneGeometry geometry;
            std::memcpy(&geometry.identity, p, 8);
            std::memcpy(&geometry.layers, p + 8, 4);
            std::memcpy(&geometry.waterLayers, p + 12, 4);
            geometry.triangles.resize((size - 16) / sizeof(gsr::SceneTriangle));
            std::memcpy(geometry.triangles.data(), p + 16, size - 16);
            gs->noteSceneGeometry(geometry);
            break;
        }
        case RecScene: {
            gsr::SceneGuideCamera camera;
            std::memcpy(&camera, p, sizeof camera);
            gs->noteSceneCamera(camera); // flush older draws before taking the phase content anchor
            if (camera.sourcePc == 0x161a78u || camera.sourcePc == 0x437170u)
                break; // material / grid-capture marker, never a world/presentation boundary
            noteHandoffScene(camera);
            worldActivity.scene(camera, gs->stats().drawCalls);
            drakan_fpsmeter::setWorldFrames(worldActivity.worlds());
            break;
        }
        case RecVsync: {
            uint64_t snapshot[kPrivCount];
            std::memcpy(snapshot, p, sizeof(snapshot));
            doVsync(snapshot, 0);
            break;
        }
        case RecDisplayCommit: {
            uint64_t snapshot[kPrivCount];
            std::memcpy(snapshot, p, sizeof(snapshot));
            doDisplayCommit(snapshot);
            break;
        }
        default:
            break;
    }
}

void GsrBridge::Impl::workerMain() {
    std::vector<uint8_t> work;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(qmtx);
            workerWaiting = true;
            {
                drakan_runtime_profile::Scope profile(drakan_runtime_profile::WorkerIdle);
                qcv.wait(lock, [&] { return !pending.empty() || stopping; });
            }
            workerWaiting = false;
            if (pending.empty() && stopping)
                return;
            work.swap(pending);
            busy = true;
            drainedCv.notify_all(); // space freed
        }
        size_t at = 0;
        while (at < work.size()) {
            if (windowPresentWaiting.load(std::memory_order_acquire)) {
                const auto start =
                    pacingEvery > 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                std::unique_lock<std::mutex> lock(qmtx);
                drakan_runtime_profile::Scope profile(drakan_runtime_profile::FairWait);
                presentCv.wait_for(lock, std::chrono::milliseconds(20), [&] {
                    return !windowPresentWaiting.load(std::memory_order_acquire) || stopping ||
                           stopPresentHandoff.load();
                });
                if (pacingEvery > 0)
                    fairWaitNs.fetch_add(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                            .count(),
                        std::memory_order_relaxed);
            }
            uint32_t type, size;
            std::memcpy(&type, work.data() + at, 4);
            std::memcpy(&size, work.data() + at + 4, 4);
            uint64_t published = 0;
            {
                std::unique_lock<std::recursive_mutex> r(rmtx, std::defer_lock);
                {
                    drakan_runtime_profile::Scope profile(drakan_runtime_profile::WorkerLock);
                    r.lock();
                }
                const auto before = latchProgress.latest();
                const auto execStart = std::chrono::steady_clock::now();
                execute(type, work.data() + at + 8, size);
                if (const auto took = std::chrono::steady_clock::now() - execStart;
                    frameTraceEnabled() && took > std::chrono::milliseconds(8))
                    std::fprintf(
                        stderr, "[ft] W %lld %u %lld\n",
                        (long long)std::chrono::duration_cast<std::chrono::microseconds>(execStart.time_since_epoch())
                            .count(),
                        type, (long long)std::chrono::duration_cast<std::chrono::microseconds>(took).count());
                const auto after = latchProgress.latest();
                if (after != before)
                    published = after;
            }
            // Keep every uncapped native frame, and legacy neural frames, latched
            // until the window thread presents it. Never wait while holding a
            // renderer/context lock, and keep ImGui/Present on their host thread.
            waitForHandoff(published, type == RecDisplayCommit && uncappedRequested);
            if (type == RecDisplayCommit) {
                {
                    std::lock_guard<std::mutex> lock(qmtx);
                    ++retiredCommits;
                }
                drainedCv.notify_all();
            }
            at += 8u + ((size + 7u) & ~7u);
        }
        work.clear();
        {
            std::lock_guard<std::mutex> lock(qmtx);
            busy = false;
        }
        drainedCv.notify_all();
    }
}

void GsrBridge::Impl::waitForHandoff(uint64_t serial, bool nativeCompletion) {
    if (!serial || (!nativeCompletion && !drakan_dlss::neuralRequested()) || stopPresentHandoff.load() ||
        drakan_win::minimized())
        return;
    drakan_runtime_profile::Scope profile(drakan_runtime_profile::Handoff);
    const auto start = pacingEvery > 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    std::unique_lock<std::mutex> lock(qmtx);
    const bool shown = presentCv.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return latchProgress.shown() >= serial || stopping || stopPresentHandoff.load() ||
               (!nativeCompletion && !drakan_dlss::neuralRequested());
    });
    if (pacingEvery > 0)
        handoffWaitNs.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(),
            std::memory_order_relaxed);
    static unsigned timeouts = 0;
    if (!shown && timeouts++ < 4)
        std::fprintf(stderr, "[gsr-presentation] host handoff timed out at latch %llu\n", (unsigned long long)serial);
}

GsrBridge::GsrBridge() : m_impl(new Impl) {}

GsrBridge::~GsrBridge() {
    if (m_impl) {
        {
            std::lock_guard<std::mutex> lock(m_impl->qmtx);
            m_impl->stopPresentHandoff.store(true);
        }
        m_impl->presentCv.notify_all();
    }
    drakan_dlss::detach();
    drakan_ws::attachRenderer(nullptr, nullptr);
    drakan_settings_host::detachRenderer(); // overlay (ImGui, window subclass) before the renderer and the window go
                                            // away
    if (m_impl && m_impl->worker.joinable()) {
        m_impl->drain(drakan_runtime_profile::DrainShutdown);
        {
            std::lock_guard<std::mutex> lock(m_impl->qmtx);
            m_impl->stopping = true;
        }
        m_impl->qcv.notify_all();
        m_impl->worker.join();
    }
    if (m_impl && m_impl->gs) {
        const gsr::Stats& s = m_impl->gs->stats();
        std::fprintf(stderr,
                     "[gsr] exit: frames=%llu gifPackets=%llu tags=%llu vertices=%llu draws=%llu texDecodes=%llu "
                     "texRevalidated=%llu readbacks=%llu targetUploads=%llu aliasedTextures=%llu l2h=%llu\n",
                     (unsigned long long)m_impl->frames, (unsigned long long)s.gifPackets,
                     (unsigned long long)s.gifTags, (unsigned long long)s.vertices, (unsigned long long)s.drawCalls,
                     (unsigned long long)s.textureDecodes, (unsigned long long)s.textureRevalidated,
                     (unsigned long long)s.rtReadbacks, (unsigned long long)s.targetUploads,
                     (unsigned long long)s.aliasedTextures, (unsigned long long)s.transfersL2H);
        m_impl->gs.reset();
    }
    drakan_win::setModalTick(nullptr, nullptr);
}

std::unique_ptr<GsrBridge> GsrBridge::create(void* parentHwnd, GSRegisters* priv) {
    if (!parentHwnd || !priv)
        return nullptr;
    auto b = std::unique_ptr<GsrBridge>(new GsrBridge());
    Impl& s = *b->m_impl;
    s.parent = parentHwnd;
    s.priv = priv;
    const auto worldRate = drakan_cfg::getString("video", "world_rate", "DRAKAN_WORLD_RATE", nullptr);
    s.uncappedRequested = compareNoCase(worldRate.c_str(), "uncapped") == 0;

    bool wantFullscreen = drakan_cfg::getBool("video", "fullscreen", "DRAKAN_FULLSCREEN", false);
    {
        // window_mode (written by the settings menu) wins over the older fullscreen key; exclusive is treated as
        // borderless
        const std::string wm = drakan_cfg::getString("video", "window_mode", nullptr, "");
        if (!std::getenv("DRAKAN_FULLSCREEN") && !wm.empty())
            wantFullscreen = !compareNoCase(wm.c_str(), "borderless") || !compareNoCase(wm.c_str(), "exclusive");
    }
    int w = 16, h = 16;
    drakan_win::clientSize(w, h);
    w = std::max(w, 16);
    h = std::max(h, 16);

    // ---- settings (drakan.ini [video], environment variables override) ----
    gsr::Config cfg;
    {
        const drakan_win::Surface surf = drakan_win::surface();
        cfg.hwnd = surf.window;
        cfg.windowDisplay = surf.display;
        cfg.xlibWindow = surf.xlibWindow;
        cfg.windowKind = surf.kind == drakan_win::Surface::Kind::Xlib      ? gsr::Config::WindowKind::Xlib
                         : surf.kind == drakan_win::Surface::Kind::Wayland ? gsr::Config::WindowKind::Wayland
                         : surf.kind == drakan_win::Surface::Kind::Canvas  ? gsr::Config::WindowKind::Canvas
                                                                           : gsr::Config::WindowKind::Win32;
        if (surf.canvasSelector)
            cfg.canvasSelector = surf.canvasSelector;
    }
    cfg.windowWidth = w;
    cfg.windowHeight = h;
    cfg.vsync = drakan_cfg::getBool("video", "vsync", "DRAKAN_VSYNC", true);
    s.vsync = cfg.vsync;
    {
        // aspect: drakan_widescreen decides (4:3 pillarbox on the front-end screens, wide while a 3D world is shown);
        // this is the start-up state
        drakan_ws::configure();
        float fa = 4.0f / 3.0f;
        bool st = false;
        drakan_ws::initialPresenter(fa, st, (float)w / (float)h);
        cfg.forcedAspect = fa;
        cfg.stretchToWindow = st || std::getenv("PS2X_GS_STRETCH") != nullptr;
        s.initialAspect = fa;
        s.initialStretch = cfg.stretchToWindow;
        cfg.smoothMenuHighlight = drakan_ws::smoothHighlightInitial();
    }
    {
        uint32_t outW = static_cast<uint32_t>(w), outH = static_cast<uint32_t>(h);
        if (wantFullscreen) {
            int mw = 0, mh = 0;
            if (drakan_win::monitorSize(mw, mh)) {
                outW = static_cast<uint32_t>(mw);
                outH = static_cast<uint32_t>(mh);
            }
        }
        const std::string scs = drakan_cfg::getString("video", "scale", "PS2X_GS_SCALE", "auto");
        s.autoScale.configure(scs);
        s.autoScale.update(outW, outH, s.initialAspect, s.initialStretch,
                           native512Display(priv->pmode, priv->display1, priv->display2));
        cfg.scale = s.autoScale.requested;
        if (s.autoScale.enabled)
            std::fprintf(stderr, "[gsr-auto-scale] startup output=%ux%u viewportAspect=%.6f scale=%u\n", outW, outH,
                         s.initialAspect, cfg.scale);
    }
    {
        const std::string f = drakan_cfg::getString("video", "filter", "DRAKAN_FILTER", "game");
        cfg.textureFilter = !compareNoCase(f.c_str(), "point") || !compareNoCase(f.c_str(), "nearest")       ? 1
                            : (!compareNoCase(f.c_str(), "bilinear") || !compareNoCase(f.c_str(), "linear")) ? 2
                                                                                                             : 0;
        const std::string d = drakan_cfg::getString("video", "deinterlace", "DRAKAN_DEINTERLACE", "weave");
        cfg.deinterlace = !compareNoCase(d.c_str(), "bob") ? 1 : !compareNoCase(d.c_str(), "blend") ? 2 : 0;
        cfg.flickerFilter = drakan_cfg::getBool("video", "flicker_filter", "DRAKAN_FLICKER_FILTER", false);
        s.vblankHandoff.configure(cfg.deinterlace == 0 && !cfg.flickerFilter);
        s.presentationFieldMode.update(cfg.deinterlace, cfg.flickerFilter);
        cfg.sharpPresent = drakan_cfg::getBool("video", "sharp_present", "DRAKAN_SHARP_PRESENT", true);
    }
    {
        // texture quality (drakan.ini [video]); the GSR_* environment variables are applied by gsr itself and win over
        // these
        const int an = drakan_cfg::getInt("video", "anisotropic", nullptr, 16);
        cfg.anisotropic = an > 1 ? (uint32_t)std::min(an, 16) : 0u;
        const std::string tu = drakan_cfg::getString("video", "texture_upscale", nullptr, "auto");
        cfg.textureUpscale = !compareNoCase(tu.c_str(), "auto") || tu.empty()
                                 ? 0u
                                 : (uint32_t)std::max(1, std::min(8, std::atoi(tu.c_str())));
        const std::string tf = drakan_cfg::getString("video", "texture_upscale_filter", nullptr, "default");
        cfg.textureUpscaleFilter = !compareNoCase(tf.c_str(), "xbrz")     ? 1u
                                   : !compareNoCase(tf.c_str(), "smooth") ? 2u
                                                                          : 0u;
        const std::string mb = drakan_cfg::getString("video", "mip_bias", nullptr, "auto");
        cfg.mipBiasAuto = mb.empty() || !compareNoCase(mb.c_str(), "auto");
        if (!cfg.mipBiasAuto)
            cfg.mipBias = (float)std::atof(mb.c_str());
        // hd_textures (settings menu) and texture_replace (gs README) are the same switch; hd_textures wins when both
        // are present
        cfg.textureReplace = drakan_cfg::isSet("video", "hd_textures", nullptr)
                                 ? drakan_cfg::getBool("video", "hd_textures", nullptr, true)
                                 : drakan_cfg::getBool("video", "texture_replace", nullptr, true);
        cfg.textureDump = drakan_cfg::getBool("video", "texture_dump", nullptr, false);
        std::string td = drakan_cfg::getString("video", "texture_dir", nullptr, "textures");
        if (td.empty())
            td = "textures";
        if (td.size() < 2 || (td[1] != ':' && td[0] != '\\' && td[0] != '/'))
            td = drakan_cfg::exeDir() + td;
        cfg.textureDir = td;
        cfg.textureBudgetMB =
            (uint32_t)std::max(0, drakan_cfg::getInt("video", "texture_budget_mb", nullptr, 0)); // 0 = auto
    }
    { // [video] renderer: d3d11 picks the Direct3D 11 backend (Windows builds have both); anything else is WebGPU
        const std::string api = drakan_cfg::getString("video", "renderer", "PS2X_GS", "webgpu");
        cfg.api = compareNoCase(api.c_str(), "d3d11") == 0 ? gsr::Config::Api::D3D11 : gsr::Config::Api::WebGpu;
    }
    drakan_dlss::configure(cfg); // [video] dlss / dlss_preset / dlss_sharpness
    cfg.allowWarp = true;
    cfg.logUnsupported = std::getenv("PS2X_GS_LOG") != nullptr;
    s.gs = gsr::Renderer::create(cfg);
    if (!s.gs || !s.gs->valid()) {
        std::fprintf(stderr, "[gsr] renderer creation failed\n");
        return nullptr;
    }
    // keep presenting while Windows runs its modal move / size loop
    drakan_win::setModalTick([](void* c) { static_cast<GsrBridge::Impl*>(c)->tickWindow(); }, &s);
    s.curW = w;
    s.curH = h;
    {
        drakan_settings_host::WindowOps ops;
        ops.ctx = &s;
        ops.requestFullscreen = [](void* c, bool on) {
            static_cast<GsrBridge::Impl*>(c)->reqFullscreen.store(on ? 1 : 0);
        };
        ops.requestWindowSize = [](void* c, int cw, int ch) {
            static_cast<GsrBridge::Impl*>(c)->reqW.store(cw);
            static_cast<GsrBridge::Impl*>(c)->reqH.store(ch);
        };
        ops.isFullscreen = [](void* c) { return static_cast<GsrBridge::Impl*>(c)->fullscreen; };
        ops.requestInternalScale = [](void* c, uint32_t scale) {
            static_cast<GsrBridge::Impl*>(c)->reqScale.store(static_cast<int>(std::min(scale, 8u)));
        };
        ops.presentationFieldMode = [](void* c, uint32_t mode, bool flicker, uint32_t mask) {
            static_cast<GsrBridge::Impl*>(c)->presentationFieldMode.update(mode, flicker, mask);
        };
        drakan_dlss::attach(b.get(), s.gs.get());
        drakan_settings_host::attachRenderer(s.gs.get(), s.parent, ops);
    }
    drakan_ws::attachRenderer(s.gs.get(), s.parent);
    GSRegisters* p = priv;
    s.gs->onFinish = [p]() { p->csr.fetch_or(0x2ull, std::memory_order_acq_rel); };
    s.gs->onSignal = [p](uint64_t) { p->csr.fetch_or(0x1ull, std::memory_order_acq_rel); };
    s.lastSig = priv->siglblid;
    s.eeSig.store(s.lastSig);
    s.gs->writePriv(kPrivBase + 0x1080, s.lastSig);
    if (const char* pc = std::getenv("PS2X_GS_PACING"))
        s.pacingEvery = std::atof(pc);
    {
        const char* a = std::getenv("PS2X_GS_ASYNC");
        s.async = !(a && a[0] == '0');
        if (s.async)
            s.worker = std::thread([&s]() { s.workerMain(); });
#if defined(_WIN32)
        drakan_fpsmeter::setThreads(nullptr, s.async ? s.worker.native_handle() : nullptr);
#endif
    }
    if (const char* sp = std::getenv("PS2X_GS_SHOT")) {
        s.shotPath = sp;
        const char* at = std::getenv("PS2X_GS_SHOT_AT");
        s.shotAt = at ? std::strtoull(at, nullptr, 10) : 1200;
    }
    if (const char* dl = std::getenv("PS2X_GS_DISPLOG")) {
        s.dispLog = std::strtoull(dl, nullptr, 10);
        if (const char* df = std::getenv("PS2X_GS_DISPLOG_FROM"))
            s.dispLogFrom = std::strtoull(df, nullptr, 10);
    }
    if (const char* st = std::getenv("PS2X_GS_STATS"))
        s.statsEvery = std::strtoull(st, nullptr, 10);
    std::fprintf(stderr,
                 "[gsr] %s renderer on '%s', window %dx%d, internal scale %u (%ux%u), aspect %.3f%s, vsync %d, "
                 "filter %u, deinterlace %u, flicker_filter %d\n",
                 cfg.api == gsr::Config::Api::D3D11 ? "D3D11" : "WebGPU", s.gs->adapterName().c_str(), w, h, cfg.scale,
                 512u * cfg.scale, 512u * cfg.scale, cfg.forcedAspect, cfg.stretchToWindow ? " (stretch)" : "",
                 (int)cfg.vsync, cfg.textureFilter, cfg.deinterlace, (int)cfg.flickerFilter);
    drakan_fpsmeter::setInfo(s.gs->adapterName(), (uint32_t)w, (uint32_t)h);
    if (wantFullscreen)
        s.setFullscreen(true);
    return b;
}

void GsrBridge::submitGif(uint8_t path, const uint8_t* data, uint32_t sizeBytes) {
    Impl& s = *m_impl;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> r(s.rmtx);
        s.pushSigToGsr();
        s.gs->submitGifPacket(path == 1   ? gsr::GifPath::Path1
                              : path == 2 ? gsr::GifPath::Path2
                                          : gsr::GifPath::Path3,
                              data, sizeBytes);
        s.pullSigFromGsr();
        return;
    }
    const uint32_t payload = 8u + sizeBytes;
    const uint32_t padded = (payload + 7u) & ~7u;
    const auto record = [&](std::vector<uint8_t>& out) {
        const size_t at = out.size();
        out.resize(at + 8u + padded);
        uint8_t* p = out.data() + at;
        const uint32_t head[4] = {static_cast<uint32_t>(Impl::RecGif), payload, static_cast<uint32_t>(path), sizeBytes};
        std::memcpy(p, head, sizeof head);
        std::memcpy(p + 16, data, sizeBytes);
        if (padded != payload)
            std::memset(p + 16 + sizeBytes, 0, padded - payload);
    };
    GifStage& stage = t_stage;
    if (stage.depth != 0u && (stage.owner == &s || stage.records.empty())) {
        stage.owner = &s;
        record(stage.records);
        if (stage.records.size() >= kStageFlushBytes)
            s.flushStaged();
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.putSig();
    s.enqueueBegin(lock, sizeBytes + 32u);
    record(s.pending);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* rows) {
    Impl& s = *m_impl;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> r(s.rmtx);
        s.gs->noteVuUpload(vuAddr, qwCount, rows);
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.enqueueBegin(lock, 160u);
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecCam));
    Impl::put(s.pending, static_cast<uint32_t>(8u + 112u));
    Impl::put(s.pending, vuAddr);
    Impl::put(s.pending, qwCount);
    s.pending.insert(s.pending.end(), reinterpret_cast<const uint8_t*>(rows),
                     reinterpret_cast<const uint8_t*>(rows) + 112u);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::noteSceneCamera(const gsr::SceneGuideCamera& camera) {
    Impl& s = *m_impl;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> lock(s.rmtx);
        s.gs->noteSceneCamera(camera);
        if (camera.sourcePc == 0x161a78u || camera.sourcePc == 0x437170u)
            return; // material / grid-capture markers, never a world or presentation boundary
        s.noteHandoffScene(camera);
        s.worldActivity.scene(camera, s.gs->stats().drawCalls);
        drakan_fpsmeter::setWorldFrames(s.worldActivity.worlds());
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.enqueueBegin(lock, static_cast<uint32_t>(sizeof camera + 16));
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecScene));
    Impl::put(s.pending, static_cast<uint32_t>(sizeof camera));
    const auto* data = reinterpret_cast<const uint8_t*>(&camera);
    s.pending.insert(s.pending.end(), data, data + sizeof camera);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::noteObjectMotion(const gsr::ObjectMotionScope& scope) {
    Impl& s = *m_impl;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> lock(s.rmtx);
        s.gs->noteObjectMotion(scope);
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.enqueueBegin(lock, static_cast<uint32_t>(sizeof scope + 16));
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecObject));
    Impl::put(s.pending, static_cast<uint32_t>(sizeof scope));
    const auto* data = reinterpret_cast<const uint8_t*>(&scope);
    s.pending.insert(s.pending.end(), data, data + sizeof scope);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::writeRegister(uint8_t reg, uint64_t value) {
    Impl& s = *m_impl;
    switch (reg) {
        // SDK pseudo-registers used by the HLE'd libgs calls: they are the EE-visible privileged registers
        case 0x59:
            ps2perf::onDispfb(s.priv->dispfb1, value);
            s.priv->dispfb1 = value;
            return;
        case 0x5a:
            s.priv->display1 = value;
            return;
        case 0x5b:
            s.priv->dispfb2 = value;
            return;
        case 0x5c:
            s.priv->display2 = value;
            return;
        default:
            break;
    }
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> r(s.rmtx);
        s.pushSigToGsr();
        s.gs->writeRegister(reg, value);
        s.pullSigFromGsr();
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.putSig();
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecReg));
    Impl::put(s.pending, static_cast<uint32_t>(16));
    Impl::put(s.pending, static_cast<uint32_t>(reg));
    Impl::put(s.pending, static_cast<uint32_t>(0));
    Impl::put(s.pending, value);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::uploadImage(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, uint64_t trxdir, const uint8_t* data,
                            uint32_t sizeBytes) {
    // Rebuild the GIF packet the DMA chain would have carried: A+D setup (4 regs), then an IMAGE tag + data.
    const uint32_t qw = (sizeBytes + 15u) / 16u;
    std::vector<uint8_t> pkt((size_t)(1 + 4 + 1 + qw) * 16u, 0);
    auto put = [&](size_t i, uint64_t lo, uint64_t hi) {
        std::memcpy(pkt.data() + i * 16, &lo, 8);
        std::memcpy(pkt.data() + i * 16 + 8, &hi, 8);
    };
    put(0, 4ull | (1ull << 60), 0xEull);
    put(1, bitbltbuf, 0x50);
    put(2, trxpos, 0x51);
    put(3, trxreg, 0x52);
    put(4, trxdir, 0x53);
    put(5, (uint64_t)qw | (1ull << 15) | (2ull << 58), 0);
    std::memcpy(pkt.data() + 6 * 16, data, sizeBytes);
    submitGif(3, pkt.data(), (uint32_t)pkt.size());
}

uint32_t GsrBridge::consumeLocalToHost(uint8_t* dst, uint32_t maxBytes) {
    m_impl->drain(drakan_runtime_profile::DrainTransfer);
    std::lock_guard<std::recursive_mutex> r(m_impl->rmtx);
    return m_impl->gs->consumeLocalToHostBytes(dst, maxBytes);
}

uint32_t GsrBridge::readVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) {
    m_impl->drain(drakan_runtime_profile::DrainRead);
    std::lock_guard<std::recursive_mutex> r(m_impl->rmtx);
    return m_impl->gs->readVram(psm, base, bw, x, y);
}

void GsrBridge::writeVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    m_impl->drain(drakan_runtime_profile::DrainWrite);
    std::lock_guard<std::recursive_mutex> r(m_impl->rmtx);
    m_impl->gs->writeVram(psm, base, bw, x, y, value);
    ++m_impl->cpuVramWrites;
}

void GsrBridge::Impl::doVsync(const uint64_t* snapshot, uint64_t vsyncTick) {
    Impl& s = *this;
    ++s.pacingVsyncs;
    // Once native commits own completion, a guest clock edge must never flush
    // or end the next, possibly unfinished world. Pending unsafe presentation
    // settings restore the field-paced path until another safe native commit.
    if (s.nativeCommitted && s.gs->fieldIndependentPresentation()) {
        s.gs->signalVblank();
        return;
    }
    s.nativeCommitted = false;
    s.completionDriven.store(false, std::memory_order_release);
    if (s.dispLog && s.frames >= s.dispLogFrom && s.dispLog-- > 0) {
        std::fprintf(stderr,
                     "[gsr-disp %llu] pmode=%llx smode1=%llx smode2=%llx dispfb1=%llx display1=%llx dispfb2=%llx "
                     "display2=%llx bg=%llx\n",
                     (unsigned long long)s.frames, (unsigned long long)snapshot[0], (unsigned long long)snapshot[1],
                     (unsigned long long)snapshot[2], (unsigned long long)snapshot[7], (unsigned long long)snapshot[8],
                     (unsigned long long)snapshot[9], (unsigned long long)snapshot[10],
                     (unsigned long long)snapshot[14]);
    }
    s.applyPriv(snapshot);
    s.gs->vsync(false);
    // Sample AFTER vsync flushes pending draws/transfers and applies settings.
    // Native paths always advance. Even a duplicate still executes every GS
    // vsync/field callback and endFrame; only its host handoff is redundant.
    const auto& stats = s.gs->stats();
    drakan_vblank::Handoff::Sample content;
    std::copy(snapshot, snapshot + kPrivCount, content.display.begin());
    content.draws = stats.drawCalls;
    content.hostToLocal = stats.transfersH2L;
    content.localToLocal = stats.transfersL2L;
    content.uploads = stats.rtUploads;
    content.readbacks = stats.rtReadbacks;
    content.cpuWrites = s.cpuVramWrites;
    content.liveApplied = stats.liveApplied;
    s.lastVblankAdvanced =
        s.vblankHandoff.advance(drakan_dlss::neuralRequested(), content, s.presentationFieldMode.safe());
    if (s.pacingEvery > 0) {
        const auto reasons = s.vblankHandoff.reasons();
        for (unsigned i = 0; i < s.handoffReasons.size(); ++i)
            if (reasons & (uint32_t{1} << i))
                s.handoffReasons[i].fetch_add(1, std::memory_order_relaxed);
    }
    if (s.lastVblankAdvanced) {
        s.publishDisplay();
    } else
        ++s.duplicateWorldVblanks;
    s.completedFrameDiagnostics();
    (void)vsyncTick;
}

void GsrBridge::Impl::publishDisplay() {
    latchedWorldSerial = worldActivity.worlds();
    {
        std::lock_guard<std::mutex> lock(qmtx);
        latchProgress.publish();
    }
    presentCv.notify_all();
}

void GsrBridge::Impl::doDisplayCommit(const uint64_t* snapshot) {
    if (!uncappedRequested)
        return;
    applyPriv(snapshot);
    // The renderer checks effective and pending settings itself; direct callers
    // of its setters do not have to notify the bridge's legacy handoff policy.
    if (!gs->completeDisplayFrame(true)) {
        nativeCommitted = false;
        completionDriven.store(false, std::memory_order_release);
        return;
    }
    nativeCommitted = true;
    completionDriven.store(true, std::memory_order_release);
    publishDisplay(); // every explicit completion, even an unchanged DISPFB
    completedFrameDiagnostics();
}

void GsrBridge::Impl::completedFrameDiagnostics() {
    Impl& s = *this;
    ++s.frames;
    if (s.statsEvery && (s.frames % s.statsEvery) == 0) {
        const gsr::Stats& st = s.gs->stats();
        std::fprintf(stderr,
                     "[gsr] frame %llu: packets=%llu vertices=%llu draws=%llu texDecodes=%llu latches=%llu "
                     "duplicateWorldVblanks=%llu\n",
                     (unsigned long long)s.frames, (unsigned long long)st.gifPackets, (unsigned long long)st.vertices,
                     (unsigned long long)st.drawCalls, (unsigned long long)st.textureDecodes,
                     (unsigned long long)s.latchProgress.latest(), (unsigned long long)s.duplicateWorldVblanks);
    }
    if (!s.shotPath.empty() && !s.shotDone && s.frames >= s.shotAt) {
        s.shotDone = true;
        gsr::Image img;
        if (s.gs->captureDisplay(img) && gsr::writePng(s.shotPath.c_str(), img))
            std::fprintf(stderr, "[gsr] screenshot %ux%u -> %s (frame %llu)\n", img.width, img.height,
                         s.shotPath.c_str(), (unsigned long long)s.frames);
        else
            std::fprintf(stderr, "[gsr] screenshot failed\n");
    }
}

void GsrBridge::commitDisplay() {
    Impl& s = *m_impl;
    if (!s.uncappedRequested)
        return;
    uint64_t snapshot[Impl::kPrivCount];
    Impl::snapshotPriv(*s.priv, snapshot);
    if (!s.async) {
        uint64_t published = 0;
        {
            std::lock_guard<std::recursive_mutex> r(s.rmtx);
            const auto before = s.latchProgress.latest();
            s.doDisplayCommit(snapshot);
            if (s.latchProgress.latest() != before)
                published = s.latchProgress.latest();
        }
        s.waitForHandoff(published, true);
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    {
        drakan_runtime_profile::Scope profile(drakan_runtime_profile::CommitCapacity);
        s.enqueueBegin(lock, sizeof(snapshot) + 8u);
    }
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecDisplayCommit));
    Impl::put(s.pending, static_cast<uint32_t>(sizeof(snapshot)));
    const auto* data = reinterpret_cast<const uint8_t*>(snapshot);
    s.pending.insert(s.pending.end(), data, data + sizeof(snapshot));
    const uint64_t ticket = ++s.queuedCommits;
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
    // Permit CPU work for the next frame while this frame renders. Retiring
    // the previous commit bounds the pipeline without serializing CPU/GPU work
    // or allowing hundreds of tiny menu frames to accumulate.
    const auto start = s.pacingEvery > 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    drakan_runtime_profile::Scope profile(drakan_runtime_profile::CommitRetirement);
    lock.lock();
    s.drainedCv.wait(lock, [&] { return s.retiredCommits >= ticket - 1 || s.stopping; });
    if (s.pacingEvery > 0)
        s.drainWaitNs.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(),
            std::memory_order_relaxed);
}

void GsrBridge::vsync(uint64_t vsyncTick) {
    Impl& s = *m_impl;
    uint64_t snapshot[Impl::kPrivCount];
    Impl::snapshotPriv(*s.priv, snapshot);
    if (!s.async) {
        uint64_t published = 0;
        {
            std::lock_guard<std::recursive_mutex> r(s.rmtx);
            const auto before = s.latchProgress.latest();
            s.doVsync(snapshot, vsyncTick);
            if (s.latchProgress.latest() != before)
                published = s.latchProgress.latest();
        }
        s.waitForHandoff(published);
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    Impl::put(s.pending, static_cast<uint32_t>(Impl::RecVsync));
    Impl::put(s.pending, static_cast<uint32_t>(sizeof(snapshot)));
    s.pending.insert(s.pending.end(), reinterpret_cast<const uint8_t*>(snapshot),
                     reinterpret_cast<const uint8_t*>(snapshot) + sizeof(snapshot));
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::Impl::tickWindow() {
    // NR's DXGI presentation can block on the display queue. Re-presenting an
    // unchanged latch consumes the context lock while the worker is trying to
    // finish the next frame. The settings overlay still needs independent ticks.
    if ((completionDriven.load(std::memory_order_acquire) || drakan_dlss::neuralRequested()) &&
        !drakan_settings_host::isOpen()) {
        bool hasLatch;
        {
            std::lock_guard<std::mutex> lock(qmtx);
            hasLatch = latchProgress.pending();
        }
        if (!hasLatch) {
            // Resizes still apply while guest rendering is paused. Release
            // qmtx before taking rmtx: the renderer publishes in reverse order.
            std::lock_guard<std::recursive_mutex> r(rmtx);
            syncSize();
            return;
        }
    }
    windowPresentWaiting.store(true, std::memory_order_release);
    const auto windowLockStart =
        frameTraceEnabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto lockStart = pacingEvery > 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    uint64_t capturedLatch = 0;
    {
        std::lock_guard<std::recursive_mutex> r(rmtx); // lock order: rmtx, then gsr
        const auto acquired = pacingEvery > 0 ? std::chrono::steady_clock::now() : lockStart;
        windowPresentWaiting.store(false, std::memory_order_release);
        syncSize();
        capturedLatch = latchProgress.latest();
        const auto worlds = latchedWorldSerial;
        const auto presentStart =
            frameTraceEnabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        gs->presentLatched();
        if (frameTraceEnabled()) {
            const auto us = [](auto d) {
                return (long long)std::chrono::duration_cast<std::chrono::microseconds>(d).count();
            };
            const auto now = std::chrono::steady_clock::now();
            if (now - windowLockStart > std::chrono::milliseconds(8))
                std::fprintf(stderr, "[ft] H %lld lock=%lld present=%lld\n", us(windowLockStart.time_since_epoch()),
                             us(presentStart - windowLockStart), us(now - presentStart));
        }
        if (worlds != lastShownWorldSerial) {
            ++shownWorlds;
            lastShownWorldSerial = worlds;
        }
        ++pacingActualPresents;
        if (pacingEvery > 0) {
            pacingLockMs += std::chrono::duration<double, std::milli>(acquired - lockStart).count();
            pacingPresentMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - acquired).count();
        }
    }
    {
        std::lock_guard<std::mutex> lock(qmtx);
        latchProgress.acknowledge(capturedLatch);
    }
    presentCv.notify_all();
}

void GsrBridge::setFullscreen(bool on) {
    m_impl->setFullscreen(on);
}
bool GsrBridge::isFullscreen() const {
    return m_impl->fullscreen;
}

void GsrBridge::hostTick() {
    drakan_runtime_profile::profile().report();
    Impl& s = *m_impl;
    drakan_ws::hostTick(); // wide / 4:3 decision -> presenter aspect + HUD de-stretch
    drakan_dlss::hostTick();
    drakan_pause::hostTick(s.parent); // window focus / overlay -> pause gate for the guest and the SPU2 output
    // Hotkeys (edge detected, only while our window is in the foreground): Alt+Enter and F11 toggle borderless
    // fullscreen.
    if (drakan_win::focused()) {
        namespace vk = drakan_win::vk;
        const bool ae = drakan_win::keyDown(vk::Alt) && drakan_win::keyDown(vk::Enter);
        const bool f11 = drakan_win::keyDown(vk::F11);
        if ((ae && !s.altEnterDown) || (f11 && !s.f11Down))
            s.setFullscreen(!s.fullscreen);
        s.altEnterDown = ae;
        s.f11Down = f11;
        const bool f2 = drakan_win::keyDown(vk::F2) && !drakan_win::keyDown(vk::Control);
        if (f2 && !s.f2Down)
            std::fprintf(stderr, "[gsr] fps counter mode %d\n", drakan_fpsmeter::cycleMode());
        s.f2Down = f2;
    }
    s.applyWindowRequests(); // settings overlay: window mode / size (outside of the present)
    if (drakan_win::minimized()) {
        // Minimised: nothing is visible, so do not present (a flip-model Present into a minimised window can block, and
        // the host loop must keep pumping messages). The guest is independent of the host loop and keeps its own 60 Hz
        // pace.
        drakan_present_clock::g_lockedRefresh.store(0.0, std::memory_order_relaxed);
        s.nextFrame = std::max(s.nextFrame, std::chrono::steady_clock::now()) + std::chrono::microseconds(16667);
        std::this_thread::sleep_until(s.nextFrame);
        return;
    }
    // Pace outside renderer locks: a wait under them would starve GIF processing for most of each refresh interval
    // (Renderer::waitForPresent releases the renderer lock while it waits). Present honors the requested vsync setting.
    const int limit = drakan_settings_host::frameLimit();
    s.vsync = s.gs->vsyncEnabled();
    const auto dbgT0 = std::chrono::steady_clock::now();
    if (drakan_pacing::wakeOnNeuralLatch(s.completionDriven.load(std::memory_order_acquire) ||
                                             drakan_dlss::neuralRequested(),
                                         drakan_settings_host::isOpen(), limit)) {
        // Native completions and neural handoffs wake the host. Do not add an
        // unconditional timer interval after a completed frame: wake on its
        // publication, outside renderer locks. Timeout keeps messages pumping.
        std::unique_lock<std::mutex> lock = s.lockProducer();
        s.presentCv.wait_for(lock, std::chrono::microseconds(16667),
                             [&] { return s.latchProgress.pending() || s.stopPresentHandoff.load(); });
        s.nextFrame = std::chrono::steady_clock::now();
    } else {
        s.nextFrame += std::chrono::microseconds(limit > 0 ? 1000000 / std::max(limit, 1) : 16667);
        const auto now = std::chrono::steady_clock::now();
        if (s.nextFrame < now)
            s.nextFrame = now;
        else
            std::this_thread::sleep_until(s.nextFrame);
    }
    // SetMaximumFrameLatency(1) only throttles callers of the frame-latency waitable (also NR's D3D12 swapchain).
    // Without this wait two or three vsynced flips queued up: every GPU readback then waited 2-3 refreshes (30-48 ms
    // hitches) and input lagged as much.
    const bool vsyncLocked = s.vsync;
    if (vsyncLocked)
        s.gs->waitForPresent(50);
    if (const auto now = std::chrono::steady_clock::now(); now >= s.refreshQueryAt) {
        s.refreshQueryAt = now + std::chrono::seconds(1);
        const double refresh = drakan_win::refreshSeconds();
        if (refresh != s.refreshSeconds)
            std::fprintf(stderr, "[gsr] display refresh %.4f Hz\n", refresh > 0 ? 1.0 / refresh : 0.0);
        s.refreshSeconds = refresh;
    }
    // Shown frames last whole refreshes: the engine tick hook (drakan_frame_rate.h) advances the world to match.
    drakan_present_clock::g_lockedRefresh.store(vsyncLocked ? s.refreshSeconds : 0.0, std::memory_order_relaxed);
    const auto dbgT1 = std::chrono::steady_clock::now();
    s.tickWindow();
    if (frameTraceEnabled())
        std::fprintf(stderr, "[ft] P %lld %llu\n",
                     (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count(),
                     (unsigned long long)s.lastShownWorldSerial);
    if (s.pacingEvery > 0) {
        const auto now = std::chrono::steady_clock::now();
        s.pacingWaitMs += std::chrono::duration<double, std::milli>(dbgT1 - dbgT0).count();
        if (now - dbgT0 > std::chrono::milliseconds(250))
            std::fprintf(stderr, "[gsr-pacing] slow host tick: wait %.0f ms, present %.0f ms (iconic=%d fg=%d)\n",
                         std::chrono::duration<double, std::milli>(dbgT1 - dbgT0).count(),
                         std::chrono::duration<double, std::milli>(now - dbgT1).count(),
                         drakan_win::minimized() ? 1 : 0, drakan_win::focused() ? 1 : 0);
        const double dt = std::chrono::duration<double>(now - s.lastPresent).count();
        s.lastPresent = now;
        s.pacingMax = std::max(s.pacingMax, dt);
        s.pacingMin = std::min(s.pacingMin, dt);
        s.pacingSum += dt;
        ++s.pacingPresents;
        const double el = std::chrono::duration<double>(now - s.pacingT0).count();
        if (el >= s.pacingEvery) {
            const auto worlds = drakan_fpsmeter::worldFrames();
            const auto vsyncs = s.pacingVsyncs.exchange(0);
            std::fprintf(stderr,
                         "[gsr-pacing] %.3fs: hostTicks=%.2f/s presentCalls=%.2f/s worldUpdates=%.2f/s "
                         "worldPresents=%.2f/s guestVsyncs=%.2f/s tickMs=%.2f/%.2f/%.2f waitMs=%.2f lockMs=%.2f "
                         "presentMs=%.2f drainMs=%.2f handoffMs=%.2f fairMs=%.2f\n",
                         el, s.pacingPresents / el, s.pacingActualPresents / el, (worlds - s.pacingLastWorlds) / el,
                         (s.shownWorlds - s.pacingLastShownWorlds) / el, vsyncs / el,
                         1000 * s.pacingSum / std::max<uint64_t>(1, s.pacingPresents), 1000 * s.pacingMin,
                         1000 * s.pacingMax, s.pacingWaitMs, s.pacingLockMs, s.pacingPresentMs,
                         s.drainWaitNs.exchange(0) * 1e-6, s.handoffWaitNs.exchange(0) * 1e-6,
                         s.fairWaitNs.exchange(0) * 1e-6);
            std::fprintf(stderr, "[handoff-pacing] %.3fs:", el);
            for (unsigned i = 0; i < s.handoffReasons.size(); ++i)
                std::fprintf(stderr, " %s=%llu", drakan_vblank::Handoff::reasonNames[i],
                             (unsigned long long)s.handoffReasons[i].exchange(0));
            static constexpr const char* clearNames[] = {"phase", "viewport", "preview", "gui", "unknown"};
            for (unsigned i = 0; i < s.closedWorldClears.size(); ++i)
                std::fprintf(stderr, " clear_%s=%llu", clearNames[i],
                             (unsigned long long)s.closedWorldClears[i].exchange(0));
            std::fputc('\n', stderr);
            s.pacingLastWorlds = worlds;
            s.pacingLastShownWorlds = s.shownWorlds;
            s.pacingActualPresents = 0;
            s.pacingWaitMs = s.pacingLockMs = s.pacingPresentMs = 0;
            s.pacingT0 = now;
            s.pacingPresents = 0;
            s.pacingMax = 0;
            s.pacingMin = 1e9;
            s.pacingSum = 0;
        }
    }
}

void GsrBridge::noteSceneGeometry(const gsr::SceneGeometry& geometry) {
    Impl& s = *m_impl;
    if (geometry.triangles.size() > 450000)
        return;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> lock(s.rmtx);
        s.gs->noteSceneGeometry(geometry);
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    const uint32_t size = 16 + uint32_t(geometry.triangles.size() * sizeof(gsr::SceneTriangle));
    s.enqueueBegin(lock, size + 8);
    Impl::put(s.pending, uint32_t(Impl::RecGeometry));
    Impl::put(s.pending, size);
    const auto* identity = reinterpret_cast<const uint8_t*>(&geometry.identity);
    s.pending.insert(s.pending.end(), identity, identity + 8);
    Impl::put(s.pending, geometry.layers);
    Impl::put(s.pending, geometry.waterLayers);
    const auto* data = reinterpret_cast<const uint8_t*>(geometry.triangles.data());
    if (size > 16)
        s.pending.insert(s.pending.end(), data, data + size - 16);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

void GsrBridge::noteSceneModels(std::shared_ptr<const gsr::SceneModels> models) {
    Impl& s = *m_impl;
    if (!models)
        return;
    if (!s.async) {
        std::lock_guard<std::recursive_mutex> lock(s.rmtx);
        s.gs->noteSceneModels(std::move(models));
        return;
    }
    std::unique_lock<std::mutex> lock = s.lockProducer();
    s.enqueueBegin(lock, 16);
    const uint64_t ticket = ++s.modelsTicket;
    {
        std::lock_guard<std::mutex> owned(s.modelsMutex);
        s.queuedModels.emplace(ticket, std::move(models));
    }
    Impl::put(s.pending, uint32_t(Impl::RecModels));
    Impl::put(s.pending, uint32_t(8));
    const auto* data = reinterpret_cast<const uint8_t*>(&ticket);
    s.pending.insert(s.pending.end(), data, data + 8);
    const bool wake = s.workerWaiting;
    lock.unlock();
    if (wake)
        s.qcv.notify_one();
}

namespace gsr_batch {
void begin() {
    ++t_stage.depth;
}
void end() {
    GifStage& stage = t_stage;
    if (stage.depth == 0u || --stage.depth != 0u || stage.records.empty() || !stage.owner)
        return;
    stage.owner->flushStaged();
}
} // namespace gsr_batch
