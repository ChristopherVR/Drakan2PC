#include "portable_gpu.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <cstdio>
#include <mutex>

namespace {
// Process-lifetime control storage. Reading/Writing retain caller allocations;
// Ready/Async may be cancelled before any later worker access to those addresses.
struct SharedRequest {
    std::atomic<int> state{0};
    std::atomic<int> initialized{0};
    uint32_t ranges[10]{};
    uint32_t width = 0, height = 0, textureWords = 0;
};
static_assert(sizeof(SharedRequest) == 15 * sizeof(uint32_t));
static_assert(std::atomic<int>::is_always_lock_free);
SharedRequest g_request;
std::mutex g_requestMutex;
bool g_started = false, g_failed = false;
} // namespace

// clang-format off
int drakanRunGpuJob(const char* shader, const uint32_t* draws, unsigned drawBytes,
    const uint32_t* bins, unsigned binBytes, const uint32_t* textures, unsigned textureBytes,
    uint32_t* pixels, unsigned pixelBytes, unsigned width, unsigned height,
    const uint32_t* textureWrites, unsigned textureWriteBytes, unsigned textureWords) {
    std::lock_guard lock(g_requestMutex);
    if(g_failed)return 0;
    if(!g_started) {
        // Only startup needs the UI. Its worker can start independently of this
        // blocked pthread, then watch the shared queue without UI callbacks.
        const int started=MAIN_THREAD_EM_ASM_INT({
            try {
                if(!Atomics.waitAsync || !navigator.gpu)return 0;
                globalThis.drakanRasterWorker?.terminate();
                const worker=globalThis.drakanRasterWorker=new Worker(new URL('portable_gpu_worker.js',document.baseURI));
                const control=new Int32Array(HEAPU8.buffer,$1,15);
                worker.onerror=event=>{
                    event.preventDefault();console.error('[portable-gpu]',event.message);
                    Atomics.store(control,1,-1);Atomics.notify(control,1);
                    const fail=()=>{
                        const state=Atomics.load(control,0);
                        // terminate() aborts in parallel; it is not an ownership
                        // fence. Let a synchronous copy release its claim first.
                        if(state===2 || state===5){setTimeout(fail,0);return;}
                        if(Atomics.compareExchange(control,0,state,-1)!==state){setTimeout(fail,0);return;}
                        worker.terminate();Atomics.notify(control,0);
                    };
                    fail();
                };
                worker.postMessage({memory:wasmMemory,control:$1,shader:UTF8ToString($0)});
                return 1;
            }catch(error){console.error('[portable-gpu]',error);return 0;}
        }, shader, &g_request);
        g_started=true;
        if(!started){g_failed=true;return 0;}
        const double deadline=emscripten_get_now()+30000.0;
        while(g_request.initialized.load(std::memory_order_acquire)==0) {
            const double remaining=deadline-emscripten_get_now();
            if(remaining<=0)break;
            emscripten_futex_wait(&g_request.initialized,0,remaining);
        }
        if(g_request.initialized.load(std::memory_order_acquire)!=1){
            g_request.state.store(4,std::memory_order_release);
            emscripten_futex_wake(&g_request.state,1);g_failed=true;return 0;
        }
    }
    int previous=g_request.state.load(std::memory_order_acquire);
    if(g_request.initialized.load(std::memory_order_acquire)!=1 || (previous!=0 && previous!=6)) {
        g_failed=true;return 0;
    }
    const uint32_t ranges[]={uint32_t(uintptr_t(draws)),drawBytes,uint32_t(uintptr_t(bins)),binBytes,
        uint32_t(uintptr_t(textures)),textureBytes,uint32_t(uintptr_t(pixels)),pixelBytes,
        uint32_t(uintptr_t(textureWrites)),textureWriteBytes};
    std::copy(std::begin(ranges),std::end(ranges),g_request.ranges);
    g_request.width=width;g_request.height=height;g_request.textureWords=textureWords;
    if(!g_request.state.compare_exchange_strong(previous,1,std::memory_order_acq_rel)) {
        g_failed=true;return 0;
    }
    emscripten_futex_wake(&g_request.state,1);
    const double deadline=emscripten_get_now()+30000.0;
    for(;;) {
        int state=g_request.state.load(std::memory_order_acquire);
        if(state==6)return 1;
        if(state==-1 || state==4){g_failed=true;return 0;}
        const double remaining=deadline-emscripten_get_now();
        if(remaining<=0 && (state==1 || state==3)) {
            if(g_request.state.compare_exchange_strong(state,4,std::memory_order_acq_rel)) {
                emscripten_futex_wake(&g_request.state,1);g_failed=true;return 0;
            }
            continue;
        }
        // Reading (2) and Writing (5) contain no awaits. Keep allocations alive
        // until that critical section completes, even when the deadline passes.
        emscripten_futex_wait(&g_request.state,state,remaining>0?remaining:1000.0);
    }
}
// clang-format on
namespace gsr::gpu {
namespace {
class WebCompute final : public Compute {
    std::string name_ = "Browser WebGPU compute";
    bool failed_ = false;

public:
    const std::string& adapter() const override { return name_; }
    bool run(Job& job) override {
        if (failed_)
            return false;
        if (!drakanRunGpuJob(rasterShader(), job.draws.data(), job.draws.size() * 4, job.bins.data(),
                             job.bins.size() * 4, job.textures.data(), job.textures.size() * 4, job.pixels.data(),
                             job.pixels.size() * 4, job.width, job.height, job.textureWrites.data(),
                             job.textureWrites.size() * 4, job.textureWords))
            failed_ = true;
        return !failed_;
    }
};
} // namespace
std::unique_ptr<Compute> createCompute() {
    return std::make_unique<WebCompute>();
}
} // namespace gsr::gpu
