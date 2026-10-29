// drakan::gpu presentation surfaces, see gpu/Gpu.h.
#include "gpu/Gpu.h"

#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace drakan::gpu {

std::unique_ptr<Surface> Context::createSurface(const NativeWindow& window, std::string* error) {
    const auto fail = [&](const char* why) -> std::unique_ptr<Surface> {
        if (error)
            *error = why;
        return {};
    };
    auto desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    desc.label = view("drakan window");
    // Only one of these is chained; all are plain structs, so building them unconditionally needs no platform headers.
#if !defined(__EMSCRIPTEN__) // the browser's webgpu.h only knows canvases
    auto win32 = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    auto xlib = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    auto wayland = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
#else
    auto canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
#endif
    switch (window.kind) {
#if !defined(__EMSCRIPTEN__)
        case NativeWindow::Kind::Win32:
            if (!window.window)
                return fail("no window handle");
            win32.hinstance = window.display;
#if defined(_WIN32)
            if (!win32.hinstance) // Vulkan's Win32 surfaces need the module instance
                win32.hinstance = GetModuleHandleW(nullptr);
#endif
            win32.hwnd = window.window;
            desc.nextInChain = &win32.chain;
            break;
        case NativeWindow::Kind::Xlib:
            if (!window.display || !window.xlibWindow)
                return fail("no X11 display / window");
            xlib.display = window.display;
            xlib.window = window.xlibWindow;
            desc.nextInChain = &xlib.chain;
            break;
        case NativeWindow::Kind::Wayland:
            if (!window.display || !window.window)
                return fail("no Wayland display / surface");
            wayland.display = window.display;
            wayland.surface = window.window;
            desc.nextInChain = &wayland.chain;
            break;
#endif
        case NativeWindow::Kind::Canvas:
#if defined(__EMSCRIPTEN__)
            canvas.selector = {window.canvasSelector.data(), window.canvasSelector.size()};
            desc.nextInChain = &canvas.chain;
            break;
#else
            return fail("a canvas surface needs the browser build");
#endif
        default:
            return fail("no window to present into");
    }
    WGPUSurface surface = wgpuInstanceCreateSurface(instance_, &desc);
    if (!surface)
        return fail("cannot create a surface for the window");
    std::unique_ptr<Surface> result(new Surface(*this, surface));
    if (window.kind == NativeWindow::Kind::Canvas)
        result->canvas_ = window.canvasSelector;
    return result;
}

bool windowClientSize(const NativeWindow& window, uint32_t& width, uint32_t& height) {
#if defined(_WIN32)
    RECT rc{};
    if (window.kind == NativeWindow::Kind::Win32 && window.window &&
        GetClientRect(static_cast<HWND>(window.window), &rc) && rc.right > rc.left && rc.bottom > rc.top) {
        width = uint32_t(rc.right - rc.left);
        height = uint32_t(rc.bottom - rc.top);
        return true;
    }
#else
    (void)window;
    (void)width;
    (void)height;
#endif
    return false;
}

Surface::~Surface() {
    if (current_)
        wgpuTextureRelease(current_);
    if (surface_) {
        if (width_)
            wgpuSurfaceUnconfigure(surface_);
        wgpuSurfaceRelease(surface_);
    }
}

bool Surface::configure(uint32_t width, uint32_t height, bool vsync) {
    if (!surface_ || !width || !height)
        return false;
#if defined(__EMSCRIPTEN__)
    // The canvas's drawing size is the image size (CSS sizes the element); only the thread owning it can set it.
    if (!canvas_.empty())
        emscripten_set_canvas_element_size(canvas_.c_str(), int(width), int(height));
#endif
    auto caps = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(surface_, context_.adapter(), &caps) != WGPUStatus_Success || !caps.formatCount)
        return false;
    // Prefer a plain 8-bit UNORM format: the GS output is already display-referred (no sRGB conversion).
    format_ = caps.formats[0];
    for (size_t i = 0; i < caps.formatCount; ++i)
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
            format_ = caps.formats[i];
            break;
        }
    WGPUPresentMode mode = WGPUPresentMode_Fifo; // always supported
    if (!vsync)
        for (WGPUPresentMode wanted : {WGPUPresentMode_Mailbox, WGPUPresentMode_Immediate})
            for (size_t i = 0; i < caps.presentModeCount; ++i)
                if (caps.presentModes[i] == wanted && mode == WGPUPresentMode_Fifo)
                    mode = wanted;
    WGPUCompositeAlphaMode alpha = caps.alphaModes ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Auto;
    for (size_t i = 0; i < caps.alphaModeCount; ++i)
        if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque)
            alpha = WGPUCompositeAlphaMode_Opaque;
    // Copy-out (window screenshots) where the platform allows it.
    const WGPUTextureUsage copyOut = caps.usages & WGPUTextureUsage_CopySrc;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    auto config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = context_.device();
    config.format = format_;
    config.usage = WGPUTextureUsage_RenderAttachment | copyOut;
#if defined(__EMSCRIPTEN__)
    config.usage |= WGPUTextureUsage_CopyDst; // the frame is copied in at present (rhi_webgpu.cpp)
#endif
    config.width = width;
    config.height = height;
    config.presentMode = mode;
    config.alphaMode = alpha;
    wgpuSurfaceConfigure(surface_, &config);
    width_ = width;
    height_ = height;
    vsync_ = vsync;
    copySource_ = copyOut != 0;
    return true;
}

WGPUTexture Surface::acquire() {
    if (current_) {
        wgpuTextureRelease(current_);
        current_ = nullptr;
    }
    auto frame = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(surface_, &frame);
    switch (frame.status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            current_ = frame.texture;
            return current_;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            if (frame.texture)
                wgpuTextureRelease(frame.texture);
            configure(width_, height_, vsync_);
            return nullptr;
        default:
            if (frame.texture)
                wgpuTextureRelease(frame.texture);
            return nullptr;
    }
}

void Surface::present() {
    if (!current_)
        return;
#if !defined(__EMSCRIPTEN__)
    wgpuSurfacePresent(surface_); // the browser presents when the frame's task returns
#endif
    wgpuTextureRelease(current_);
    current_ = nullptr;
}

} // namespace drakan::gpu
