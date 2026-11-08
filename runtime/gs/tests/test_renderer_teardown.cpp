// Regression: a D3D11 renderer destroyed right after init() (no frame drawn, the start-up shader prewarm thread
// still joinable) must shut down cleanly. A renderer Impl destroyed on its own used to leave prewarmThread joinable,
// so std::thread's destructor called std::terminate (exit 0xc0000409 on the clean CI runner). Runs on WARP.
#include "d3d11_backend_impl.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace gsr;

int main() {
    const char* prewarm = std::getenv("GSR_SHADER_PREWARM");
    const bool prewarmOn = !(prewarm && prewarm[0] == '0');
    for (int ray = 0; ray < 2; ++ray) {
        LocalMemory memory;
        Config config;
        config.scale = 1;
        config.allowWarp = true;
        config.textureUpscale = 1;
        Stats stats;
        {
            D3D11Backend::Impl renderer(memory, config, stats);
            if (!renderer.init()) {
                std::puts("SKIP: no D3D11 device");
                return 77;
            }
            assert(renderer.prewarmThread.joinable() == prewarmOn);
            if (ray)
                renderer.startRayPrewarm(); // a ray build in flight (or finished, never adopted) at teardown
        }
        std::printf("renderer %d (ray build %s) destroyed cleanly\n", ray, ray ? "started" : "not started");
    }
    std::puts("Renderer teardown with a live prewarm thread passed");
    std::fflush(stdout);
}
