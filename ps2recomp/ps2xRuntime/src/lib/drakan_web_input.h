#pragma once

struct WebMouseMotion {
    int x;
    int y;
    bool captured;
};
bool drakanWebStopRequested();
bool drakanWebFocused();
void drakanWebSetMenuActive(bool active);
bool drakanWebTakeKeyPress(int key);
bool drakanWebKeyDown(int key);
bool drakanWebTakeMousePress(int button);
bool drakanWebMouseDown(int button);
WebMouseMotion drakanWebTakeMouseMotion();
bool drakanWebTakeResize(int& width, int& height);
void drakanWebSurfaceSize(int& width, int& height);

extern "C" {
double drakan_web_glfw_time();
void drakan_web_glfw_set_time(double value);
void drakan_web_focus(int focused);
void drakan_web_key_event(int key, int down);
void drakan_web_mouse_button(int button, int down);
void drakan_web_mouse_move(int x, int y);
void drakan_web_pointer_lock(int captured);
void drakan_web_release_input();
void drakan_web_resize(int width, int height);
void drakan_web_stop();
int drakan_web_in_menu();
}
