// CPU-only integration checks: compile the real bridge and gs_core against a
// recording backend. No D3D implementation, device, swapchain or window is used.
// External host integrations are stubs; production window creation is not called.
#include <memory>
#define private public
#include "runtime/gs/gs_gsr_bridge.h"
#undef private
#include "gs_gsr_bridge.cpp"
#include "gs_internal.h"

#include <cassert>
#include <future>

namespace {
std::atomic<bool> neural{false}, overlay{false};
std::atomic<int> limit{0};
std::atomic<uint64_t> worldCount{0};
std::atomic<uint32_t> viewportW{0}, viewportH{0};
struct Event {
    char kind;
    gsr::DisplayState display{};
    uint32_t field = 0;
};
std::mutex eventMutex;
std::vector<Event> events;
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    assert(condition);
}
void record(char kind) {
    std::lock_guard<std::mutex> lock(eventMutex);
    events.push_back({kind});
}
std::vector<Event> recorded() {
    std::lock_guard<std::mutex> lock(eventMutex);
    return events;
}
void clearEvents() {
    std::lock_guard<std::mutex> lock(eventMutex);
    events.clear();
}
size_t count(char kind) {
    const auto copy = recorded();
    return static_cast<size_t>(std::count_if(copy.begin(), copy.end(), [&](const auto& e) { return e.kind == kind; }));
}
void triangle(gsr::Renderer& r) {
    r.writeRegister(0x00, 3);
    r.writeRegister(0x05, 0);
    r.writeRegister(0x05, 160);
    r.writeRegister(0x05, 160ull << 16);
}
void display(GSRegisters& r, uint64_t page) {
    r.pmode = 3;
    r.smode2 = 3;
    r.dispfb1 = page | (8ull << 9);
    r.dispfb2 = (page + 1) | (8ull << 9);
    r.display1 = 511ull << 32 | 511ull << 44;
    r.display2 = r.display1;
}
std::unique_ptr<GsrBridge> bridge(GSRegisters& regs, bool uncapped = true, bool async = false) {
    std::unique_ptr<GsrBridge> b(new GsrBridge);
    auto& s = *b->m_impl;
    s.priv = &regs;
    s.uncappedRequested = uncapped;
    s.async = async;
    // Sequential boundary checks drive presentation explicitly. Dedicated
    // async/sync pacing checks below enable the real acknowledgement waits.
    s.stopPresentHandoff.store(!async);
    gsr::Config config;
    s.gs = gsr::Renderer::create(config);
    s.presentationFieldMode.update(0, false);
    s.vblankHandoff.configure(true);
    return b;
}
bool awaitLatch(GsrBridge::Impl& s, uint64_t serial) {
    std::unique_lock<std::mutex> lock(s.qmtx);
    return s.presentCv.wait_for(lock, std::chrono::seconds(2), [&] { return s.latchProgress.latest() >= serial; });
}
} // namespace

// Every GPU-facing operation is a recording stub; the real parser, settings,
// field state, FIFO, CV wake, acknowledgement and backpressure code runs above.
// gsr::Backend is a concrete pimpl class (no virtuals), so a method added to
// runtime/gs/include/gsr/gsr.h shows up as an unresolved Backend:: symbol at
// link time. Add a matching stub below when that happens.
namespace gsr {
struct Backend::Impl {
    Stats& stats;
    uint32_t scale;
    Impl(Stats& st, uint32_t sc) : stats(st), scale(sc) {}
};
Backend::Backend(LocalMemory&, const Config& cfg, Stats& st) : m_impl(std::make_unique<Impl>(st, cfg.scale)) {}
Backend::~Backend() = default;
bool Backend::init() {
    return true;
}
bool Backend::valid() const {
    return true;
}
std::string Backend::adapterName() const {
    return "CPU recording backend";
}
void Backend::draw(const GsDrawState&, const ClutBuf&, const DrawVertex*, uint32_t) {
    ++m_impl->stats.drawCalls;
    record('D');
}
void Backend::flushToMemory(uint32_t, uint32_t) {}
bool Backend::beginTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    return false;
}
void Backend::completeTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool) {}
void Backend::drawMovieFrame(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const MovieImage&, uint64_t) {}
void Backend::flushAll() {}
void Backend::present(const DisplayState&) {
    record('P');
}
void Backend::latchDisplay(const DisplayState& d, uint32_t field) {
    std::lock_guard<std::mutex> lock(eventMutex);
    events.push_back({'L', d, field});
}
void Backend::presentLatched() {
    record('P');
}
void* Backend::beginPresentWait() {
    return nullptr;
}
bool Backend::waitPresentHandle(void*, uint32_t) {
    return true;
}
void Backend::endPresentWait(bool) {}
void Backend::setVsync(bool) {}
bool Backend::vsyncEnabled() const {
    return false;
}
void Backend::applyLive(const LiveSettings& ls) {
    if (ls.mask & LiveSettings::F_SCALE)
        m_impl->scale = ls.scale;
    record('S');
}
uint32_t Backend::internalScale() const {
    return m_impl->scale;
}
void Backend::getViewport(uint32_t& w, uint32_t& h) const {
    w = viewportW;
    h = viewportH;
}
void Backend::setOverlayCallback(std::function<void(const OverlayContext&)>) {}
bool Backend::capture(const DisplayState&, Image&) {
    return false;
}
bool Backend::captureFramebuffer(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, Image&) {
    return false;
}
void Backend::resize(uint32_t, uint32_t) {}
void Backend::reset() {}
void Backend::endFrame() {
    record('E');
}
void Backend::noteVuUpload(uint32_t, uint32_t, const float*) {
    record('V');
}
void Backend::noteSceneCamera(const SceneGuideCamera&) {
    record('C');
}
void Backend::noteObjectMotion(const ObjectMotionScope&) {
    record('O');
}
std::string Backend::upscaleStatus() const {
    return {};
}
double Backend::upscaleGpuMs() const {
    return 0;
}
void Backend::upscaleCut() {}
bool Backend::upscaleAvailable(std::string&) const {
    return false;
}
bool Backend::neuralRenderingAvailable(std::string&) const {
    return false;
}
std::string Backend::neuralRenderingStatus() const {
    return {};
}
void Backend::noteSceneGeometry(const SceneGeometry&) {}
void Backend::noteSceneModels(std::shared_ptr<const SceneModels>) {}
bool Backend::waterRenderingAvailable(std::string&) const {
    return false;
}
std::string Backend::waterRenderingStatus() const {
    return {};
}
std::string Backend::rayTracingStatus() const {
    return {};
}
bool Backend::captureLastGameplay(Image&, float&) {
    return false;
}
bool writePng(const char*, const Image&) {
    return false;
}
} // namespace gsr
namespace drakan_dlss {
bool neuralRequested() {
    return neural.load();
}
void detach() {}
void hostTick() {}
void configure(gsr::Config&) {}
void attach(GsrBridge*, gsr::Renderer*) {}
} // namespace drakan_dlss
namespace drakan_ws {
void attachRenderer(gsr::Renderer*, void*) {}
void hostTick() {}
void configure() {}
void initialPresenter(float& aspect, bool& stretch, float) {
    aspect = 4.0f / 3;
    stretch = false;
}
bool smoothHighlightInitial() {
    return false;
}
} // namespace drakan_ws
namespace drakan_win {
void setHideCursor(bool) {}
void setModalTick(void (*)(void*), void*) {}
} // namespace drakan_win
namespace drakan_pause {
void hostTick(void*) {}
} // namespace drakan_pause
namespace drakan_settings_host {
void attachRenderer(gsr::Renderer*, void*, const WindowOps&) {}
void detachRenderer() {}
bool isOpen() {
    return overlay.load();
}
int frameLimit() {
    return limit.load();
}
} // namespace drakan_settings_host
namespace drakan_fpsmeter {
void configure() {}
void setThreads(void*, void*) {}
void setInfo(const std::string&, unsigned, unsigned) {}
void setWorldFrames(uint64_t value) {
    worldCount = value;
}
uint64_t worldFrames() {
    return worldCount.load();
}
int cycleMode() {
    return 0;
}
} // namespace drakan_fpsmeter
namespace ps2perf {
std::atomic<uint64_t> g_flips{0};
}
namespace drakan_cfg {
const std::string& exeDir() {
    static const std::string dir;
    return dir;
}
std::string getString(const char*, const char*, const char* env, const char* fallback) {
    const char* value = env ? std::getenv(env) : nullptr;
    return value ? value : fallback;
}
int getInt(const char*, const char*, const char*, int fallback) {
    return fallback;
}
bool getBool(const char*, const char*, const char*, bool fallback) {
    return fallback;
}
bool isSet(const char*, const char*, const char*) {
    return false;
}
} // namespace drakan_cfg
namespace gsr::texup {
uint64_t hashContent(const uint32_t*, uint32_t, uint32_t) {
    return 0;
}
uint64_t hashContentRgb(const uint32_t*, uint32_t, uint32_t) {
    return 0;
}
} // namespace gsr::texup

int main() {
    // Shared initial/resize algorithm: viewport fit, width for a known square
    // 512-line display, legacy vertical fallback otherwise, bounds and stability.
    check(AutoInternalScale::choose(3840, 2160, 4.0 / 3, false, true) == 6);
    check(AutoInternalScale::choose(3840, 2160, 16.0 / 9, false, true) == 8);
    check(AutoInternalScale::choose(3840, 2160, 1, false, true) == 5);
    check(AutoInternalScale::choose(3840, 2160, 4.0 / 3, true, true) == 8);
    check(AutoInternalScale::choose(3840, 2160, 4.0 / 3, false, false) == 5);
    check(AutoInternalScale::choose(1920, 1080, 4.0 / 3, false, true) == 3);
    check(AutoInternalScale::choose(1280, 720, 4.0 / 3, false, true) == 2);
    check(AutoInternalScale::choose(512, 512, 4.0 / 3, false, true) == 1);
    check(AutoInternalScale::choose(1, 1, 4.0 / 3, false, true) == 1);
    check(AutoInternalScale::choose(UINT32_MAX, UINT32_MAX, 4.0 / 3, false, true) == 8);
    check(AutoInternalScale::choose(0, 2160, 4.0 / 3, false, true) == 0);
    check(AutoInternalScale::choose(3840, 0, 4.0 / 3, false, true) == 0);
    check(AutoInternalScale::choose(3840, 2160, 0, false, true) == 8);
    check(AutoInternalScale::choose(3840, 2160, NAN, false, true) == 8);
    check(AutoInternalScale::choose(3840, 2160, INFINITY, false, true) == 8);
    AutoInternalScale policy;
    policy.configure("AuTo");
    check(policy.enabled);
    check(policy.update(3840, 2160, 4.0 / 3, false, true));
    check(policy.requested == 6);
    check(!policy.update(3840, 2160, 4.0 / 3, false, true));
    check(!policy.update(0, 0, 4.0 / 3, false, true));
    check(policy.requested == 6);
    check(!policy.update(160, 160, 4.0 / 3, false, true, true));
    check(policy.requested == 6);
    check(policy.update(1280, 720, 4.0 / 3, false, true));
    check(policy.requested == 2);
    check(policy.update(3840, 2160, 4.0 / 3, false, true));
    check(policy.requested == 6);
    policy.configure("4");
    check(!policy.enabled && policy.requested == 4);
    check(!policy.update(7680, 4320, 16.0 / 9, false, true));
    check(policy.requested == 4);
    policy.configure("0");
    check(!policy.enabled && policy.requested == 1);
    policy.configure("99");
    check(!policy.enabled && policy.requested == 8);
    policy.configure("");
    check(policy.enabled);
    const uint64_t squareDisplay = (511ull << 32) | (511ull << 44);
    check(native512Display(1, squareDisplay, 0));
    check(native512Display(2, 0, squareDisplay));
    check(!native512Display(0, squareDisplay, squareDisplay));
    check(!native512Display(1, 0, squareDisplay));
    check(!native512Display(1, squareDisplay | (1ull << 23), 0));

    // Exercise the real bridge resize entry with the recording renderer. The
    // setter queues only; no scale is applied until a renderer frame boundary.
    GSRegisters autoRegs{};
    display(autoRegs, 10);
    auto scaling = bridge(autoRegs);
    auto& a = *scaling->m_impl;
    a.gs->writePriv(0, autoRegs.pmode);
    a.gs->writePriv(0x80, autoRegs.display1);
    a.gs->writePriv(0xa0, autoRegs.display2);
    a.autoScale.configure("auto");
    clearEvents();
    {
        std::lock_guard<std::recursive_mutex> lock(a.rmtx);
        a.syncSizeTo(3840, 2160);
        check(a.autoScale.requested == 6 && a.curW == 3840 && a.curH == 2160);
        check(a.gs->internalScale() == 1 && count('S') == 0);
        a.syncSizeTo(3840, 2160);
        check(a.autoScale.requested == 6);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 6);
        auto applied = a.gs->stats().liveApplied;
        viewportW = 2880;
        viewportH = 2160;
        for (unsigned i = 0; i < 4; ++i) {
            a.syncSizeTo(3840, 2160);
            a.gs->completeDisplayFrame();
        }
        check(a.gs->stats().liveApplied == applied && a.gs->internalScale() == 6);
        for (unsigned scene = 0; scene < 3; ++scene) {
            viewportW = 3840;
            viewportH = 2160; // wide world: one increase, then stable
            a.syncSizeTo(3840, 2160);
            a.gs->completeDisplayFrame();
            check(a.gs->internalScale() == 8);
            applied = a.gs->stats().liveApplied;
            for (unsigned i = 0; i < 4; ++i) {
                a.syncSizeTo(3840, 2160);
                a.gs->completeDisplayFrame();
            }
            check(a.gs->stats().liveApplied == applied);
            viewportW = 2880;
            viewportH = 2160; // menu: one decrease, then stable
            a.syncSizeTo(3840, 2160);
            a.gs->completeDisplayFrame();
            check(a.gs->internalScale() == 6);
            applied = a.gs->stats().liveApplied;
            for (unsigned i = 0; i < 4; ++i) {
                a.syncSizeTo(3840, 2160);
                a.gs->completeDisplayFrame();
            }
            check(a.gs->stats().liveApplied == applied);
        }
        a.syncSizeTo(0, 0);
        a.syncSizeTo(160, 160, true);
        check(a.curW == 3840 && a.curH == 2160 && a.autoScale.requested == 6);
        a.syncSizeTo(1280, 720);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 2);
        a.syncSizeTo(3840, 2160);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 6);
        a.autoScale.configure("4");
        a.gs->setInternalScale(4);
        a.gs->completeDisplayFrame();
        a.syncSizeTo(7680, 4320);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 4);
        // The live settings callback queues policy ownership as well as scale.
        a.reqScale.store(0);
        a.applyScaleRequest();
        check(a.autoScale.enabled && a.autoScale.requested == 0 && a.reqScale.load() == -1);
        a.syncSizeTo(3840, 2160);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 6);
        a.reqScale.store(3);
        a.applyScaleRequest();
        check(!a.autoScale.enabled && a.gs->internalScale() == 6);
        a.syncSizeTo(7680, 4320);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 3);
        a.applyScaleRequest();
        check(!a.autoScale.enabled && a.autoScale.requested == 3);
        a.reqScale.store(0);
        a.applyScaleRequest();
        a.syncSizeTo(0, 0);
        a.syncSizeTo(160, 160, true);
        check(a.autoScale.enabled && a.autoScale.requested == 0 && a.gs->internalScale() == 3);
        a.syncSizeTo(512, 512);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 1);
        a.syncSizeTo(3840, 2160);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 6);
        // Last pending setting wins without an intermediate resource rebuild.
        a.reqScale.store(2);
        a.reqScale.store(7);
        a.applyScaleRequest();
        a.syncSizeTo(1280, 720);
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 7);
        a.reqScale.store(99);
        a.applyScaleRequest();
        a.gs->completeDisplayFrame();
        check(a.gs->internalScale() == 8 && !a.autoScale.enabled);
    }
    viewportW = viewportH = 0;
    scaling.reset();

    // Signal-only vblank cannot flush a partial draw, apply a setting, latch,
    // endFrame or advance renderer frames. Completion cannot signal vblank.
    auto r = gsr::Renderer::create(gsr::Config{});
    unsigned callbacks = 0;
    r->onVsync = [&] {
        ++callbacks;
        record('B');
    };
    clearEvents();
    triangle(*r);
    r->setSharpPresent(false);
    check(r->fieldIndependentPresentation());
    check(recorded().empty());
    r->signalVblank();
    check(callbacks == 1);
    check(r->readPriv(0x1000) & (1ull << 13));
    check(r->readPriv(0x1000) & (1ull << 3));
    check(r->stats().frames == 0);
    check(count('D') == 0 && count('S') == 0 && count('L') == 0 && count('E') == 0);
    check(r->completeDisplayFrame(true));
    check(callbacks == 1 && r->stats().frames == 1);
    check(count('D') == 1 && count('L') == 1 && count('E') == 1 && count('P') == 0);
    check(recorded()[3].kind == 'L' && recorded()[3].field == 1);
    const auto csr = r->readPriv(0x1000);
    check(r->completeDisplayFrame(true));
    check(r->readPriv(0x1000) == csr);
    check(callbacks == 1 && r->stats().frames == 2);
    r->presentLatched();
    check(r->stats().frames == 2 && count('P') == 1);
    clearEvents();
    r->vsync(true);
    auto trace = recorded();
    check(trace.size() == 4);
    check(trace[0].kind == 'L' && trace[1].kind == 'P' && trace[2].kind == 'E' && trace[3].kind == 'B');
    check(callbacks == 2 && r->stats().frames == 3);

    // Both direct queued setters and initial configuration protect field modes.
    for (uint32_t mode : {1u, 2u, 99u}) {
        r->setDeinterlace(mode);
        clearEvents();
        check(!r->fieldIndependentPresentation());
        check(recorded().empty());
        check(!r->completeDisplayFrame(true));
        check(count('L') == 0 && count('E') == 0);
        check(!r->fieldIndependentPresentation());
        r->setDeinterlace(0);
        check(r->fieldIndependentPresentation());
        check(r->completeDisplayFrame(true));
    }
    r->setFlickerFilter(true);
    check(!r->fieldIndependentPresentation());
    clearEvents();
    check(!r->completeDisplayFrame(true));
    check(count('L') == 0 && count('E') == 0);
    r->setFlickerFilter(false);
    check(r->fieldIndependentPresentation());
    check(r->completeDisplayFrame(true));
    for (uint32_t mode : {1u, 2u}) {
        gsr::Config c;
        c.deinterlace = mode;
        auto other = gsr::Renderer::create(c);
        check(!other->fieldIndependentPresentation());
        check(!other->completeDisplayFrame(true));
    }
    gsr::Config flicker;
    flicker.flickerFilter = true;
    auto other = gsr::Renderer::create(flicker);
    check(!other->completeDisplayFrame(true));

    GSRegisters regs{};
    display(regs, 10);
    auto b = bridge(regs);
    auto& s = *b->m_impl;
    clearEvents();
    b->vsync(1);
    check(count('L') == 1 && count('E') == 1 && s.latchProgress.latest() == 1);
    check(!s.completionDriven.load());
    s.tickWindow();
    check(s.latchProgress.shown() == 1);
    b->commitDisplay();
    check(s.nativeCommitted && s.completionDriven.load());
    check(s.latchProgress.latest() == 2 && s.frames == 2 && s.pacingVsyncs.load() == 1);
    const auto committedCsr = s.gs->readPriv(0x1000);
    check(s.gs->completeDisplayFrame(true));
    check(s.gs->readPriv(0x1000) == committedCsr);
    clearEvents();
    triangle(*s.gs);
    b->vsync(2);
    check(recorded().empty());
    check(s.latchProgress.latest() == 2 && s.frames == 2);
    check(s.pacingVsyncs.load() == 2);
    check(s.gs->readPriv(0x1000) != committedCsr);
    b->commitDisplay();
    check(count('D') == 1 && count('L') == 1 && count('E') == 1);
    b->commitDisplay();
    check(count('L') == 2 && count('E') == 2); // same buffer, same field
    check(s.latchProgress.latest() == 4);
    s.tickWindow();
    check(s.latchProgress.shown() == 4);
    clearEvents();
    s.tickWindow();
    check(count('P') == 0); // native duplicate present suppressed
    overlay = true;
    s.tickWindow();
    check(count('P') == 1);
    overlay = false;

    // Unsafe modes return to legacy vblank completion and require a fresh
    // safe commit to rearm, including setters bypassing WindowOps notification.
    for (uint32_t mode : {1u, 2u}) {
        s.gs->setDeinterlace(mode);
        clearEvents();
        b->commitDisplay();
        check(!s.nativeCommitted && !s.completionDriven.load());
        check(count('L') == 0 && count('E') == 0);
        b->vsync(3);
        check(count('L') == 1 && count('E') == 1);
        s.gs->setDeinterlace(0);
        b->vsync(4);
        check(!s.nativeCommitted);
        b->commitDisplay();
        check(s.nativeCommitted);
    }
    s.gs->setFlickerFilter(true);
    clearEvents();
    b->vsync(5);
    check(!s.nativeCommitted && count('L') == 1 && count('E') == 1);
    s.gs->setFlickerFilter(false);
    b->commitDisplay();
    check(s.nativeCommitted);

    // An unchanged captureTick/camera across two engine frames still produces
    // two completed worlds. Native commit and signal-only vblank never clear it.
    gsr::SceneGuideCamera world;
    world.flags = world.Valid | world.World | world.HaveViewport;
    world.frameWidth = world.frameHeight = 512;
    world.captureTick = 7;
    gsr::SceneGuideCamera end;
    end.flags = end.EndWorld;
    end.sourcePc = 0x147690;
    for (uint64_t expected : {1ull, 2ull}) {
        b->noteSceneCamera(world);
        triangle(*s.gs);
        b->noteSceneCamera(end);
        check(s.worldActivity.worlds() == expected);
        b->commitDisplay();
        check(s.worldActivity.worlds() == expected && s.latchedWorldSerial == expected);
        b->vsync(7);
        check(s.worldActivity.worlds() == expected);
        s.tickWindow();
        check(s.lastShownWorldSerial == expected);
    }
    // Conventional modes ignore an accidental commit entirely.
    auto disabled = bridge(regs, false, true);
    disabled->commitDisplay();
    check(disabled->m_impl->pending.empty());
    disabled->m_impl->async = false;
    disabled->vsync(1);
    check(disabled->m_impl->frames == 1);

    // Real FIFO/worker/producer checks with NR off AND on. A producer cannot
    // run more than one frame ahead of host acknowledgement.
    b.reset();
    for (bool nr : {false, true}) {
        clearEvents();
        neural = nr;
        auto queued = bridge(regs, true, true);
        auto& q = *queued->m_impl;
        gsr::SceneGuideCamera camera;
        camera.flags = camera.Valid | camera.World | camera.HaveViewport;
        camera.frameWidth = camera.frameHeight = 512;
        queued->noteSceneCamera(camera);
        gsr::ObjectMotionScope object;
        queued->noteObjectMotion(object);
        uint64_t firstSnapshot[GsrBridge::Impl::kPrivCount];
        display(regs, 20);
        GsrBridge::Impl::snapshotPriv(regs, firstSnapshot);
        const auto prefix = q.pending.size();
        q.workerWaiting = true; // observe the normal enqueue notification before starting the worker
        auto producer = std::async(std::launch::async, [&] { queued->commitDisplay(); });
        {
            std::unique_lock<std::mutex> lock(q.qmtx);
            check(q.qcv.wait_for(lock, std::chrono::seconds(2), [&] { return q.pending.size() > prefix; }));
            uint32_t type, bytes;
            std::memcpy(&type, q.pending.data() + prefix, 4);
            std::memcpy(&bytes, q.pending.data() + prefix + 4, 4);
            check(type == GsrBridge::Impl::RecDisplayCommit && bytes == sizeof(firstSnapshot));
            check(std::memcmp(q.pending.data() + prefix + 8, firstSnapshot, sizeof(firstSnapshot)) == 0);
        }
        check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        producer.get();
        display(regs, 30);
        producer = std::async(std::launch::async, [&] { queued->commitDisplay(); });
        check(producer.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
        // Both transactions are now queued; neither may observe later storage.
        {
            std::lock_guard<std::mutex> lock(q.qmtx);
            check(q.queuedCommits == 2);
        }
        display(regs, 40);
        q.worker = std::thread([&] { q.workerMain(); });
        check(awaitLatch(q, 1));
        check(q.latchProgress.latest() == 1);
        check(q.rmtx.try_lock());
        q.rmtx.unlock();
        check(q.qmtx.try_lock());
        q.qmtx.unlock();
        check(producer.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
        trace = recorded();
        check(trace.size() == 4);
        check(trace[0].kind == 'C' && trace[1].kind == 'O' && trace[2].kind == 'L' && trace[3].kind == 'E');
        check(trace[2].display.c[0].fbp == 20 && trace[2].display.c[1].fbp == 21 && trace[2].field == 0);
        q.tickWindow();
        check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        producer.get();
        check(awaitLatch(q, 2));
        check(q.latchProgress.shown() == 1 && q.latchProgress.pending());
        check(count('L') == 2 && q.pacingVsyncs.load() == 0);
        trace = recorded();
        check(trace[5].kind == 'L' && trace[5].display.c[0].fbp == 30 && trace[5].field == 0);
        producer = std::async(std::launch::async, [&] { queued->commitDisplay(); });
        check(producer.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
        q.tickWindow();
        check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        producer.get();
        check(awaitLatch(q, 3));
        // Cancellation retires outstanding work without holding renderer locks.
        {
            std::lock_guard<std::mutex> lock(q.qmtx);
            q.stopPresentHandoff = true;
        }
        q.presentCv.notify_all();
        q.drain(drakan_runtime_profile::DrainShutdown);
        check(q.retiredCommits == 3 && q.queuedCommits == 3);
        queued.reset();
    }
    // The synchronous producer obeys the same native (non-NR) handoff.
    neural = false;
    auto synchronous = bridge(regs);
    auto& sync = *synchronous->m_impl;
    sync.stopPresentHandoff = false;
    auto producer = std::async(std::launch::async, [&] { synchronous->commitDisplay(); });
    check(awaitLatch(sync, 1));
    check(producer.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
    check(sync.rmtx.try_lock());
    sync.rmtx.unlock();
    sync.tickWindow();
    check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    producer.get();

    // Turning NR off cancels a legacy NR-only handoff, but must not cancel
    // completion pacing for a native commit. Stop still releases either wait.
    auto fallback = bridge(regs);
    auto& f = *fallback->m_impl;
    f.stopPresentHandoff = false;
    {
        std::lock_guard<std::mutex> lock(f.qmtx);
        f.latchProgress.publish();
    }
    neural = true;
    auto waiting = std::async(std::launch::async, [&] { f.waitForHandoff(1); });
    check(waiting.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
    neural = false;
    f.presentCv.notify_all();
    check(waiting.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    waiting.get();
    neural = true;
    waiting = std::async(std::launch::async, [&] { f.waitForHandoff(1, true); });
    check(waiting.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
    neural = false;
    f.presentCv.notify_all();
    check(waiting.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(f.qmtx);
        f.stopPresentHandoff = true;
    }
    f.presentCv.notify_all();
    check(waiting.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    waiting.get();
    // Registration copies host pixels and keeps the first atlas immutable.
    uint32_t atlasPixels[4] = {0x80ffffff, 0, 0x400000ff, 0x8000ffff};
    check(!gsr::registerRuntimeTexture(0, 2, 2, atlasPixels));
    check(!gsr::registerRuntimeTexture(0xfa01, 8193, 1, atlasPixels));
    check(!gsr::registerRuntimeTexture(0xfa01, 2, 2, nullptr));
    check(gsr::registerRuntimeTexture(0xfa01, 2, 2, atlasPixels));
    atlasPixels[0] = 0;
    auto atlas = gsr::runtimeTexture(0xfa01);
    check(atlas && atlas->width == 2 && atlas->height == 2 && atlas->pixels[0] == 0x80ffffff);
    check(gsr::isTextureHashExcluded(0xfa01));
    check(gsr::registerRuntimeTexture(0xfa01, 2, 2, atlasPixels));
    check(gsr::runtimeTexture(0xfa01)->pixels[0] == 0x80ffffff);
    for (unsigned i = 0; i < 9; ++i)
        check(gsr::registerRuntimeTexture(0xfb00 + i, 2, 2, atlasPixels));
    check(!gsr::runtimeTexture(0xfa01));
    check(atlas->pixels[0] == 0x80ffffff); // in-flight consumer survives registry eviction
    std::printf("PASS: %u CPU integration checks; real GS core/bridge, recording backend, no GPU/window\n", checks);
}
