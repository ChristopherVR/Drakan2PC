#pragma once
#include <emscripten.h>
#include <emscripten/html5_webgl.h>
#define GLFW_INCLUDE_ES2
#include <GLFW/glfw3.h>

// Raylib's Web platform embeds DOM operations directly in EM_ASM blocks.
// Its game loop runs on the application pthread, so dispatch those operations
// to the browser thread just like GLFW. Do not change EM_ASM for guest ISO I/O.
#undef EM_ASM
#undef EM_ASM_INT
#undef EM_ASM_DOUBLE
#undef EM_ASM_PTR
#define EM_ASM(...) MAIN_THREAD_EM_ASM(__VA_ARGS__)
#define EM_ASM_INT(...) MAIN_THREAD_EM_ASM_INT(__VA_ARGS__)
#define EM_ASM_DOUBLE(...) MAIN_THREAD_EM_ASM_DOUBLE(__VA_ARGS__)
#define EM_ASM_PTR(...) MAIN_THREAD_EM_ASM_PTR(__VA_ARGS__)

// GLFW's JS context creation bypasses the SDK's native context/TLS ownership.
// Establish it through the C API first, so asynchronous buffer uploads target
// the browser thread. Browser.createContext reuses Module.ctx afterwards.
static inline GLFWwindow* drakanCreateWebWindow(int width, int height, const char* title, GLFWmonitor* monitor,
                                                GLFWwindow* share) {
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = 0;
    attributes.antialias = 0;
    attributes.majorVersion = 1;
    attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_ALWAYS;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attributes);
    if (context <= 0 || emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS)
        return (GLFWwindow*)0;
    MAIN_THREAD_EM_ASM({
        Browser.useWebGL = true;
        Browser.init();
    });
    return glfwCreateWindow(width, height, title, monitor, share);
}
#define glfwCreateWindow(...) drakanCreateWebWindow(__VA_ARGS__)
