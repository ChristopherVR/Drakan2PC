// D3DCompile through the shader blob cache (shader_cache.h), with start-up / late accounting.
//
// Every runtime HLSL compile of the D3D11 renderer goes through compile(): the start-up prewarm thread compiles all of
// them (and creates the shader objects so the driver translates them too) while the guest boots, so a later lazy user
// gets the blob from memory. Anything compiled or created after markStartupDone() without a hit is "late": it ran on
// the GS worker while the guest was waiting on it, and is logged with its time ([gsr-shader] late ...).
#pragma once

#include <d3d11.h>
#include <d3dcommon.h>

#include <cstddef>
#include <cstdint>

namespace gsr::shader_compile {

// Same contract as D3DCompile without an include handler. On failure *errors (if given) receives the compiler output.
HRESULT compile(const void* source, size_t length, const char* name, const D3D_SHADER_MACRO* macros, const char* entry,
                const char* target, UINT flags, ID3DBlob** code, ID3DBlob** errors);

// Disk level: GSR_SHADER_CACHE_DIR, else "<exe folder>/shadercache"; GSR_SHADER_CACHE=0 keeps the cache in memory only.
void configureDirectory();

// After this every compile that is not a memory hit and every timed creation counts as late, except on a thread
// that declared itself a background prewarm thread (its work never blocks the guest).
void markStartupDone();
bool startupDone();
void markPrewarmThread();

struct Totals {
    uint64_t lateEvents = 0;
    double lateMs = 0;
};
Totals totals();

// Times one driver-side creation (Create*Shader, a pipeline state, a ray tracing device); logged when late or slow.
class Timed {
public:
    explicit Timed(const char* what);
    ~Timed();
    Timed(const Timed&) = delete;
    Timed& operator=(const Timed&) = delete;

private:
    const char* what;
    int64_t begin;
};

} // namespace gsr::shader_compile
