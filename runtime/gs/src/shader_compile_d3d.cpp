#include "shader_compile_d3d.h"

#include "shader_cache.h"

#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace gsr::shader_compile {
namespace {

std::atomic<bool> g_startupDone{false};
thread_local bool t_prewarm = false;
std::mutex g_totalsMutex;
Totals g_totals;

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool late() {
    return g_startupDone.load(std::memory_order_acquire) && !t_prewarm;
}

void noteLate(const char* what, const char* detail, double ms) {
    double total = 0;
    {
        std::lock_guard lock(g_totalsMutex);
        ++g_totals.lateEvents;
        g_totals.lateMs += ms;
        total = g_totals.lateMs;
    }
    std::fprintf(stderr, "[gsr-shader] late %s%s%s %.1f ms on the renderer thread (late total %.1f ms)\n", what,
                 detail ? " " : "", detail ? detail : "", ms, total);
}

bool cacheOff() {
    const char* v = std::getenv("GSR_SHADER_CACHE");
    return v && v[0] == '0';
}

} // namespace

void configureDirectory() {
    if (cacheOff()) {
        shader_cache::global().setDirectory({});
        return;
    }
    if (const char* d = std::getenv("GSR_SHADER_CACHE_DIR"); d && d[0]) {
        shader_cache::global().setDirectory(d);
        return;
    }
    wchar_t exe[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (!n || n >= std::size(exe))
        return;
    shader_cache::global().setDirectory(shader_cache::utf8(std::filesystem::path(exe).parent_path() / L"shadercache"));
}

void markStartupDone() {
    g_startupDone.store(true, std::memory_order_release);
}
bool startupDone() {
    return g_startupDone.load(std::memory_order_acquire);
}
void markPrewarmThread() {
    t_prewarm = true;
}

Totals totals() {
    std::lock_guard lock(g_totalsMutex);
    return g_totals;
}

HRESULT compile(const void* source, size_t length, const char* name, const D3D_SHADER_MACRO* macros, const char* entry,
                const char* target, UINT flags, ID3DBlob** code, ID3DBlob** errors) {
    if (code)
        *code = nullptr;
    if (errors)
        *errors = nullptr;
    std::vector<shader_cache::Macro> list;
    for (const D3D_SHADER_MACRO* m = macros; m && m->Name; ++m)
        list.push_back({m->Name, m->Definition});
    shader_cache::Request request;
    request.source = static_cast<const char*>(source);
    request.length = length;
    request.name = name;
    request.entry = entry;
    request.target = target;
    request.macros = list.data();
    request.macroCount = list.size();
    request.flags = flags;
    const uint64_t key = shader_cache::key(request);

    HRESULT failure = S_OK;
    Microsoft::WRL::ComPtr<ID3DBlob> message;
    std::vector<uint8_t> bytes;
    const auto t0 = nowNs();
    const auto from = shader_cache::global().get(
        key,
        [&](std::vector<uint8_t>& out) {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            const HRESULT hr =
                D3DCompile(source, length, name, macros, nullptr, entry, target, flags, 0, &blob, &message);
            if (FAILED(hr) || !blob) {
                failure = FAILED(hr) ? hr : E_FAIL;
                return false;
            }
            const auto* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
            out.assign(p, p + blob->GetBufferSize());
            return true;
        },
        bytes);
    const double ms = (nowNs() - t0) / 1e6;
    if (from == shader_cache::Source::Failed) {
        if (errors && message)
            *errors = message.Detach();
        return failure != S_OK ? failure : E_FAIL;
    }
    if (from != shader_cache::Source::Memory || ms > 20.0) {
        char detail[160];
        std::snprintf(detail, sizeof detail, "%s:%s %s (%s)", name ? name : "?", entry ? entry : "?",
                      target ? target : "?",
                      from == shader_cache::Source::Disk       ? "disk"
                      : from == shader_cache::Source::Produced ? "compiled"
                                                               : "memory, waited");
        if (late())
            noteLate("compile", detail, ms);
        else if (ms > 20.0 || std::getenv("GSR_SHADER_LOG"))
            std::fprintf(stderr, "[gsr-shader] compile %s %.1f ms%s\n", detail, ms, t_prewarm ? " (prewarm)" : "");
    }
    if (!code)
        return S_OK;
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    if (FAILED(D3DCreateBlob(bytes.size(), &blob)))
        return E_OUTOFMEMORY;
    std::memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
    *code = blob.Detach();
    return S_OK;
}

Timed::Timed(const char* w) : what(w), begin(nowNs()) {}
Timed::~Timed() {
    const double ms = (nowNs() - begin) / 1e6;
    if (late() && ms > 1.0)
        noteLate("create", what, ms);
    else if (ms > 20.0 || std::getenv("GSR_SHADER_LOG"))
        std::fprintf(stderr, "[gsr-shader] create %s %.1f ms%s\n", what, ms, t_prewarm ? " (prewarm)" : "");
}

} // namespace gsr::shader_compile
