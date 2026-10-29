// drakan::gpu - the WebGPU device layer shared by every target (docs/proposals/webgpu-renderer.md).
// One webgpu.h API: wgpu-native (Vulkan / D3D12 / GL) on Windows and Linux, the browser's WebGPU in Emscripten.
// Only surface creation from a window handle differs per platform; callers pass the handle as plain pointers.
#pragma once
#include <webgpu/webgpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drakan::gpu {

enum class Backend { Auto, Vulkan, D3D12, GL, Browser };

const char* backendName(Backend backend);
// "vulkan" | "dx12" | "d3d12" | "gl" | "auto" (case-insensitive); unknown text = Auto.
Backend parseBackend(const std::string& text);

// A window to present into. Plain pointers so callers need no platform headers.
struct NativeWindow {
    enum class Kind { None, Win32, Xlib, Wayland, Canvas };
    Kind kind = Kind::None;
    void* display = nullptr;    // Win32: HINSTANCE (may be null), Xlib: Display*, Wayland: wl_display*
    void* window = nullptr;     // Win32: HWND, Wayland: wl_surface*
    uint64_t xlibWindow = 0;    // Xlib: Window
    std::string canvasSelector; // Canvas: CSS selector, e.g. "#canvas"
};

struct ContextOptions {
    Backend backend = Backend::Auto;   // DRAKAN_GPU_BACKEND overrides Auto
    bool preferRayQuery = false;       // reserved: hardware ray queries cannot be enabled through wgpu-native v29
    bool allowSoftwareAdapter = false; // accept a CPU adapter (tests, headless CI)
    const NativeWindow* compatibleWith = nullptr; // pick an adapter that can present to this window
};

class Surface;

// Pixels of an asynchronous readback (tightly packed rows); ok = false when the copy failed.
using ReadCallback = std::function<void(bool ok, std::vector<uint8_t>&& pixels)>;

class Context {
public:
    // Null on failure, with the reason in *error. Never throws. Not available in the browser (createAsync).
    static std::unique_ptr<Context> create(const ContextOptions& options, std::string* error = nullptr);
    // The browser requests adapters and devices asynchronously: `done` runs later, from this thread's event loop.
    // Natively it runs before createAsync returns.
    using CreateCallback = std::function<void(std::unique_ptr<Context> context, const std::string& error)>;
    static void createAsync(const ContextOptions& options, CreateCallback done);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    WGPUInstance instance() const { return instance_; }
    WGPUAdapter adapter() const { return adapter_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    const std::string& adapterName() const { return adapterName_; }
    Backend backend() const { return backend_; }
    bool softwareAdapter() const { return software_; }
    bool rayQuery() const { return rayQuery_; }
    bool dualSourceBlending() const { return dualSource_; }
    WGPULimits limits() const { return limits_; }

    // True once a validation / out-of-memory error was reported or the device was lost.
    bool failed() const;
    std::string lastError() const;

    // Process device callbacks; wait = block until submitted work finished.
    void poll(bool wait);
    // Compile WGSL; null (and lastError) on failure.
    WGPUShaderModule shaderModule(const std::string& wgsl, const char* label);
    // Copy a region of a texture into `out` (tightly packed rows) and wait for it. Blocks the calling thread; native
    // only (a browser thread cannot wait for its own event loop: use readTextureAsync).
    bool readTexture(WGPUTexture texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                     uint32_t bytesPerPixel, std::vector<uint8_t>& out, uint32_t mipLevel = 0);
    // The copy is submitted now; `done` runs when the data is mapped (from poll() natively, from the event loop in the
    // browser).
    void readTextureAsync(WGPUTexture texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          uint32_t bytesPerPixel, uint32_t mipLevel, ReadCallback done);
    // Create a surface for a window; null when the window kind is not supported on this platform.
    std::unique_ptr<Surface> createSurface(const NativeWindow& window, std::string* error = nullptr);

    struct State; // shared with callbacks

private:
    Context() = default;
    bool init(const ContextOptions& options, std::string* error);
    // Device request shared by both paths; `done(ok, message)` once the device exists (or failed).
    void requestDevice(std::function<void(bool, const std::string&)> done);

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPULimits limits_ = WGPU_LIMITS_INIT;
    std::string adapterName_;
    Backend backend_ = Backend::Auto;
    bool software_ = false;
    bool rayQuery_ = false;
    bool dualSource_ = false;
    std::unique_ptr<State> state_;
};

// A presentable window surface.
class Surface {
public:
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    // (Re)configure for a size; vsync = FIFO, otherwise the lowest-latency mode the surface supports.
    bool configure(uint32_t width, uint32_t height, bool vsync);
    WGPUTextureFormat format() const { return format_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    bool copySource() const { return copySource_; } // the images can be copied out (screenshots, overlay blur)
    // The texture to draw this frame into; null when the surface is outdated / lost (reconfigure and skip the frame).
    WGPUTexture acquire();
    void present();

private:
    friend class Context;
    Surface(Context& context, WGPUSurface surface) : context_(context), surface_(surface) {}
    Context& context_;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;
    WGPUTexture current_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    bool vsync_ = true;
    bool copySource_ = false;
    std::string canvas_; // browser: the canvas selector (its drawing size follows configure)
};

// The drawable size of a native window when the platform can report it (Win32 client area); false otherwise.
bool windowClientSize(const NativeWindow& window, uint32_t& width, uint32_t& height);

inline WGPUStringView view(const char* text) {
    return {text, text ? WGPU_STRLEN : 0};
}
inline std::string str(WGPUStringView text) {
    if (!text.data)
        return {};
    return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}

} // namespace drakan::gpu
