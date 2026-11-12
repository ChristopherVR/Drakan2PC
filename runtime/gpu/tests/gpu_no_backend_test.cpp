// Asking for an API whose driver is missing must fail with an error, not abort: with a window to present to,
// wgpu-native panics when a surface is created on an instance none of whose backends started (the renderer's
// `renderer = vulkan` fallback relies on the clean failure). The Vulkan loader is told to find no driver.
// Exit 77 = the loader ignored that (it does for elevated processes), so the case cannot be made here.
#include "gpu/Gpu.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
void setEnvironment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
} // namespace

int main() {
    setEnvironment("VK_ICD_FILENAMES", "nonexistent-driver.json");
    setEnvironment("VK_DRIVER_FILES", "nonexistent-driver.json");
    setEnvironment("VK_LOADER_DRIVERS_DISABLE", "*");
    setEnvironment("DRAKAN_GPU_BACKEND", "");

    drakan::gpu::NativeWindow window;
#if defined(_WIN32)
    const HWND hwnd = CreateWindowExW(0, L"STATIC", L"gpu_no_backend_test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr); // never shown
    if (!hwnd) {
        std::printf("FAIL: no test window\n");
        return 1;
    }
    window.kind = drakan::gpu::NativeWindow::Kind::Win32;
    window.display = GetModuleHandleW(nullptr);
    window.window = hwnd;
#endif

    drakan::gpu::ContextOptions options;
    options.backend = drakan::gpu::Backend::Vulkan;
    options.compatibleWith = &window;
    std::string error;
    const auto context = drakan::gpu::Context::create(options, &error);
    if (context) {
        std::printf("SKIP: a Vulkan adapter is still visible (%s)\n", context->adapterName().c_str());
        return 77;
    }
    if (error.empty()) {
        std::printf("FAIL: no error text\n");
        return 1;
    }
    std::printf("ok: %s\n", error.c_str());
    return 0;
}
