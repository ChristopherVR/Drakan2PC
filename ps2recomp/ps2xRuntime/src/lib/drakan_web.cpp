#if defined(PS2X_HAVE_PORTABLE_GPU)
#include "gs/gs_gpu_raster.h"
#endif
#include "drakan_web_input.h"
#include "drakan_web_events.h"
#include "drakan_software_frame_clock.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <emscripten.h>

namespace {
const auto g_webClockStart = std::chrono::steady_clock::now();
std::atomic<double> g_webClockOffset{0.0};
std::atomic<bool> g_webStopRequested{false};
std::atomic<bool> g_webMenuActive{true};
std::array<std::atomic<bool>, 512> g_webKeyPresses{}, g_webKeysDown{};
std::atomic<unsigned> g_webMousePresses{0}, g_webMouseDown{0};
std::atomic<int> g_webMouseX{0}, g_webMouseY{0};
std::atomic<bool> g_webPointerLock{false}, g_webFocused{true};
std::atomic<uint64_t> g_webResize{0};
std::atomic<uint64_t> g_webSurface{(uint64_t(960) << 32) | 720};
WebEventRing g_rings[2];
std::atomic<bool> g_ringOn[2]{};
std::atomic<int> g_cursorX{0}, g_cursorY{0};
std::atomic<uint32_t> g_padButtons{0};
std::atomic<bool> g_padConnected{false};
std::atomic<float> g_padAxes[4]{};
void emit(WebEvent::Type type, int a, int b = 0) {
    for (int i = 0; i < 2; ++i)
        if (g_ringOn[i].load(std::memory_order_acquire))
            g_rings[i].push({type, a, b});
}
} // namespace
WebEventRing& webEvents(WebEventConsumer consumer) {
    return g_rings[static_cast<int>(consumer)];
}
void webEventsEnable(WebEventConsumer consumer) {
    g_ringOn[static_cast<int>(consumer)].store(true, std::memory_order_release);
}
WebGamepad drakanWebGamepad() {
    WebGamepad g;
    g.connected = g_padConnected.load(std::memory_order_acquire);
    g.buttons = g_padButtons.load(std::memory_order_acquire);
    for (int i = 0; i < 4; ++i)
        g.axes[i] = g_padAxes[i].load(std::memory_order_relaxed);
    return g;
}
void drakanWebCursor(int& x, int& y) {
    x = g_cursorX.load(std::memory_order_relaxed);
    y = g_cursorY.load(std::memory_order_relaxed);
}
void drakanWebRequestFullscreen(bool on) {
    MAIN_THREAD_ASYNC_EM_ASM({ globalThis.drakanSetFullscreen ?.($0 != 0); }, on ? 1 : 0);
}
void drakanWebSetCursorHidden(bool hidden) {
    MAIN_THREAD_ASYNC_EM_ASM(
        {
            const c = document.getElementById('canvas');
            if (c)
                c.style.cursor = $0 ? 'none' : '';
        },
        hidden ? 1 : 0);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_cursor(int x, int y) {
    g_cursorX.store(x, std::memory_order_relaxed);
    g_cursorY.store(y, std::memory_order_relaxed);
    emit(WebEvent::Cursor, x, y);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_wheel(int notches) {
    if (notches)
        emit(WebEvent::Wheel, notches);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_char(int codepoint) {
    if (codepoint > 0)
        emit(WebEvent::Char, codepoint);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_gamepad(int connected, int buttons, float lx, float ly, float rx,
                                                        float ry) {
    g_padAxes[0].store(lx, std::memory_order_relaxed);
    g_padAxes[1].store(ly, std::memory_order_relaxed);
    g_padAxes[2].store(rx, std::memory_order_relaxed);
    g_padAxes[3].store(ry, std::memory_order_relaxed);
    g_padButtons.store(static_cast<uint32_t>(buttons), std::memory_order_release);
    g_padConnected.store(connected != 0, std::memory_order_release);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_resize(int width, int height) {
    const auto clamp = [](int value) { return uint32_t(value < 1 ? 1 : value > 8192 ? 8192 : value); };
    const auto size = (uint64_t(clamp(width)) << 32) | clamp(height);
    g_webSurface.store(size, std::memory_order_release);
    g_webResize.store(size, std::memory_order_release);
}
void drakanWebSurfaceSize(int& width, int& height) {
    const auto size = g_webSurface.load(std::memory_order_acquire);
    width = int(size >> 32);
    height = int(size & 0xffffffffu);
}
bool drakanWebTakeResize(int& width, int& height) {
    const auto size = g_webResize.exchange(0, std::memory_order_acq_rel);
    if (!size)
        return false;
    width = int(size >> 32);
    height = int(size & 0xffffffffu);
    return true;
}
extern "C" EMSCRIPTEN_KEEPALIVE double drakan_web_glfw_time() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_webClockStart).count() +
           g_webClockOffset.load(std::memory_order_acquire);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_glfw_set_time(double value) {
    if (!std::isfinite(value) || value < 0 || value > double(std::numeric_limits<uint64_t>::max()) / 1.0e9)
        return;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_webClockStart).count();
    g_webClockOffset.store(value - elapsed, std::memory_order_release);
}
bool drakanWebFocused() {
    return g_webFocused.load(std::memory_order_acquire);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_focus(int focused) {
    g_webFocused.store(focused != 0, std::memory_order_release);
    emit(WebEvent::Focus, focused != 0);
    if (!focused) {
        g_webPointerLock.store(false, std::memory_order_release);
        drakan_web_release_input();
    }
}
bool drakanWebPointerLocked() {
    return g_webPointerLock.load(std::memory_order_acquire);
}
bool drakanWebStopRequested() {
    return g_webStopRequested.load(std::memory_order_acquire);
}
void drakanWebSetMenuActive(bool active) {
    g_webMenuActive.store(active, std::memory_order_release);
}
extern "C" EMSCRIPTEN_KEEPALIVE int drakan_web_in_menu() {
    return g_webMenuActive.load(std::memory_order_acquire) ? 1 : 0;
}
bool drakanWebTakeKeyPress(int key) {
    return static_cast<unsigned>(key) < g_webKeyPresses.size() &&
           g_webKeyPresses[key].exchange(false, std::memory_order_acq_rel);
}
bool drakanWebKeyDown(int key) {
    return static_cast<unsigned>(key) < g_webKeysDown.size() && g_webKeysDown[key].load(std::memory_order_acquire);
}
bool drakanWebMouseDown(int button) {
    return static_cast<unsigned>(button) < 5 && (g_webMouseDown.load(std::memory_order_acquire) & (1u << button));
}
bool drakanWebTakeMousePress(int button) {
    if (static_cast<unsigned>(button) >= 5)
        return false;
    const unsigned bit = 1u << button;
    return (g_webMousePresses.fetch_and(~bit, std::memory_order_acq_rel) & bit) != 0;
}
WebMouseMotion drakanWebTakeMouseMotion() {
    return {g_webMouseX.exchange(0), g_webMouseY.exchange(0), g_webPointerLock.load(std::memory_order_acquire)};
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_key_event(int key, int down) {
    if (static_cast<unsigned>(key) >= g_webKeyPresses.size())
        return;
    if (down && !drakanWebFocused())
        return;
    const bool was = g_webKeysDown[key].exchange(down != 0);
    if (!was && down)
        g_webKeyPresses[key].store(true, std::memory_order_release);
    if (was != (down != 0))
        emit(WebEvent::Key, key, down != 0);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_mouse_button(int button, int down) {
    if (static_cast<unsigned>(button) >= 5)
        return;
    const unsigned bit = 1u << button;
    if (down && !drakanWebFocused())
        return;
    if (down) {
        if ((g_webMouseDown.fetch_or(bit) & bit) == 0) {
            g_webMousePresses.fetch_or(bit, std::memory_order_release);
            emit(WebEvent::Button, button, 1);
        }
    } else if (g_webMouseDown.fetch_and(~bit) & bit) {
        emit(WebEvent::Button, button, 0);
    }
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_release_input() {
    for (auto& key : g_webKeysDown)
        key = false;
    for (auto& key : g_webKeyPresses)
        key = false;
    g_webMouseDown = 0;
    g_webMousePresses = 0;
    g_webMouseX = 0;
    g_webMouseY = 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_mouse_move(int x, int y) {
    if (!g_webPointerLock.load(std::memory_order_acquire))
        return;
    g_webMouseX.fetch_add(x, std::memory_order_relaxed);
    g_webMouseY.fetch_add(y, std::memory_order_relaxed);
    emit(WebEvent::Motion, x, y);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_pointer_lock(int captured) {
    g_webMouseX = 0;
    g_webMouseY = 0;
    g_webPointerLock.store(captured != 0 && drakanWebFocused(), std::memory_order_release);
}
extern "C" EMSCRIPTEN_KEEPALIVE void drakan_web_stop() {
    g_webStopRequested.store(true, std::memory_order_release);
}

// Read-only instrumentation: no logging, guest-memory mutation or pacing change.
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t drakan_web_completed_frames() {
    return static_cast<uint32_t>(drakan_presentation::g_frameClock.completedFrames());
}

extern "C" EMSCRIPTEN_KEEPALIVE double drakan_web_game_seconds() {
    return drakan_presentation::g_frameMetrics.gameSeconds();
}
extern "C" EMSCRIPTEN_KEEPALIVE double drakan_web_upload_metric(unsigned index) {
    return double(drakan_presentation::g_frameMetrics.uploadBucket(index));
}
extern "C" EMSCRIPTEN_KEEPALIVE double drakan_web_frame_metric(unsigned index) {
    const auto& metrics = drakan_presentation::g_frameMetrics;
    if (index < 8)
        return double(metrics.bucket(index));
    if (index == 8)
        return double(metrics.updates());
    if (index == 9)
        return double(metrics.invalid());
    if (index == 10)
        return double(metrics.uploads());
    if (index == 11)
        return double(metrics.uploadErrors());
    return index == 12 ? double(metrics.enabled()) : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t drakan_web_gpu_jobs() {
#if defined(PS2X_HAVE_PORTABLE_GPU)
    return uint32_t(gs_gpu::completedJobs());
#else
    return 0;
#endif
}
