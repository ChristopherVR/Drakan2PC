#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <thread>
#include <cmath>
#include <limits>
#include "drakan_web_input.h"
#include "drakan_software_frame_clock.h"
extern "C" uint32_t drakan_web_completed_frames();
extern "C" double drakan_web_game_seconds();
extern "C" double drakan_web_frame_metric(unsigned index);
extern "C" double drakan_web_upload_metric(unsigned index);
#include <cassert>
#include <cstdio>

int main() {
    glfwSetTime(5.0);
    double beforeClock = glfwGetTime(), workerClock = 0;
    assert(std::isfinite(beforeClock) && beforeClock >= 5.0);
    std::thread clockReader([&] {
        workerClock = glfwGetTime();
        double previous = workerClock;
        for (unsigned i = 0; i < 20000; ++i) {
            const double current = glfwGetTime();
            assert(std::isfinite(current) && current >= previous);
            previous = current;
        }
    });
    clockReader.join();
    const double afterClock = glfwGetTime();
    assert(workerClock >= beforeClock - 0.001 && workerClock <= afterClock + 0.001);
    glfwSetTime(-1);
    glfwSetTime(std::numeric_limits<double>::quiet_NaN());
    assert(std::isfinite(glfwGetTime()) && glfwGetTime() >= afterClock);
    glfwSetTime(0);

    assert(drakan_web_in_menu() == 1);
    drakanWebSetMenuActive(false);
    auto& clock = drakan_presentation::g_frameClock;
    drakan_presentation::g_frameMetrics.enable(true);
    clock.reset(true);
    assert(drakan_web_completed_frames() == 0);
    clock.commit(1000000000);
    clock.commit(1100000000);
    assert(drakan_web_completed_frames() == 2);
    assert(drakan_web_frame_metric(6) == 1);
    drakan_presentation::g_frameMetrics.gameDelta(0.5f);
    assert(drakan_web_game_seconds() == 0.5 && drakan_web_frame_metric(8) == 1);
    drakan_presentation::g_frameMetrics.uploaded(1, 1000000000);
    drakan_presentation::g_frameMetrics.uploaded(2, 1032000000);
    assert(drakan_web_upload_metric(3) == 1 && drakan_web_upload_metric(8) == 0);
    drakan_presentation::g_frameMetrics.enable(false);
    clock.reset(false);
    assert(drakan_web_completed_frames() == 0);
    int width = 0, height = 0;
    assert(!drakanWebTakeResize(width, height));
    drakan_web_resize(1920, 1080);
    drakan_web_resize(2560, 1440);
    assert(drakanWebTakeResize(width, height) && width == 2560 && height == 1440);
    assert(!drakanWebTakeResize(width, height));
    width = height = 0;
    drakanWebSurfaceSize(width, height);
    assert(width == 2560 && height == 1440); // Consuming the event must retain the render extent.
    drakan_web_resize(-1, 100000);
    assert(drakanWebTakeResize(width, height) && width == 1 && height == 8192);
    drakan_web_mouse_button(4, 1);
    drakan_web_mouse_button(4, 0);
    assert(!drakanWebMouseDown(4) && drakanWebTakeMousePress(4));
    drakan_web_key_event(70, 1);
    drakan_web_key_event(70, 0); // complete tap between frames
    assert(!drakanWebKeyDown(70));
    assert(drakanWebTakeKeyPress(70));
    assert(!drakanWebTakeKeyPress(70));
    drakan_web_key_event(70, 1);
    assert(drakanWebTakeKeyPress(70));
    drakan_web_key_event(70, 1); // repeat is not another tap
    assert(drakanWebKeyDown(70) && !drakanWebTakeKeyPress(70));
    drakan_web_key_event(-1, 1);
    drakan_web_key_event(512, 1);
    assert(!drakanWebKeyDown(-1) && !drakanWebTakeKeyPress(512));
    drakan_web_mouse_button(0, 1);
    drakan_web_mouse_button(0, 0);
    assert(!drakanWebMouseDown(0) && drakanWebTakeMousePress(0));
    assert(!drakanWebTakeMousePress(0));
    drakan_web_pointer_lock(1);
    drakan_web_mouse_move(5, -3);
    drakan_web_mouse_move(7, 1);
    auto motion = drakanWebTakeMouseMotion();
    assert(motion.captured && motion.x == 12 && motion.y == -2);
    motion = drakanWebTakeMouseMotion();
    assert(motion.x == 0 && motion.y == 0);
    drakan_web_pointer_lock(0);
    drakan_web_mouse_move(100, 100);
    motion = drakanWebTakeMouseMotion();
    assert(!motion.captured && motion.x == 0 && motion.y == 0);
    drakan_web_mouse_button(1, 1);
    drakan_web_release_input();
    assert(drakan_web_in_menu() == 0); // Focus release does not change the guest's menu state.
    assert(!drakanWebKeyDown(70) && !drakanWebMouseDown(1) && !drakanWebTakeMousePress(1));
    drakan_web_focus(0);
    assert(!drakanWebFocused());
    drakan_web_focus(1);
    assert(drakanWebFocused());
    drakan_web_key_event(70, 1);
    drakan_web_pointer_lock(1);
    drakan_web_mouse_move(12, -5);
    drakan_web_focus(0);
    assert(!drakanWebFocused() && !drakanWebKeyDown(70) && !drakanWebTakeKeyPress(70));
    motion = drakanWebTakeMouseMotion();
    assert(!motion.captured && motion.x == 0 && motion.y == 0);
    drakan_web_key_event(70, 1);
    drakan_web_mouse_button(0, 1);
    drakan_web_pointer_lock(1);
    drakan_web_mouse_move(12, -5);
    motion = drakanWebTakeMouseMotion();
    assert(!drakanWebKeyDown(70) && !drakanWebMouseDown(0));
    assert(!motion.captured && motion.x == 0 && motion.y == 0);
    drakan_web_focus(1);
    assert(drakanWebFocused());
    assert(!drakanWebStopRequested());
    drakan_web_stop();
    assert(drakanWebStopRequested());
    std::puts("Browser input taps, holds, pointer motion and focus release passed");
}
