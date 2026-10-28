#include "portable_gpu.h"
#include <bit>
#include <cstdio>

int main() {
    auto device = gsr::gpu::createCompute();
    if (!device) {
        std::fprintf(stderr, "No portable compute device\n");
        return 77;
    }
    gsr::gpu::Job job;
    job.width = job.height = 16;
    job.draws.resize(8 + gsr::gpu::kDrawWords);
    job.draws[0] = job.draws[1] = 16;
    job.draws[4] = 2;
    job.draws[5] = 1;
    auto* d = job.draws.data() + 8;
    d[1] = d[3] = 16;
    d[5] = 1u << 17;
    d[22] = 1;
    d[28] = d[29] = 1;
    for (unsigned i = 0; i < 3; ++i) {
        auto* v = d + 32 + 12 * i;
        v[0] = std::bit_cast<uint32_t>(i == 1 ? 16.f : 0.f);
        v[1] = std::bit_cast<uint32_t>(i == 2 ? 16.f : 0.f);
        v[4] = std::bit_cast<uint32_t>(123.f);
        v[5] = std::bit_cast<uint32_t>(45.f);
        v[6] = std::bit_cast<uint32_t>(67.f);
        v[7] = std::bit_cast<uint32_t>(128.f);
    }
    d[68] = std::bit_cast<uint32_t>(0.f);
    d[69] = std::bit_cast<uint32_t>(16.f);
    d[70] = std::bit_cast<uint32_t>(-16.f);
    d[71] = std::bit_cast<uint32_t>(-16.f);
    d[72] = std::bit_cast<uint32_t>(16.f);
    d[73] = std::bit_cast<uint32_t>(0.f);
    d[74] = std::bit_cast<uint32_t>(1.f);
    d[75] = std::bit_cast<uint32_t>(1.f / 256.f);
    d[76] = 1;
    job.bins.resize(12);
    for (unsigned i = 0; i < 4; ++i) {
        job.bins[2 * i] = 8 + i;
        job.bins[2 * i + 1] = 1;
    }
    job.textures = {0};
    job.pixels.resize(16 * 16 * 2);
    if (!device->run(job))
        return 1;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            const uint32_t expected = x + y <= 15 ? 0x80432d7b : 0;
            if (job.pixels[2 * (y * 16 + x)] != expected) {
                std::fprintf(stderr, "GPU pixel mismatch at %u,%u: %08x != %08x\n", x, y, job.pixels[2 * (y * 16 + x)],
                             expected);
                return 1;
            }
        }
    std::printf("Portable GPU ordered tile shader passed (%s)\n", device->adapter().c_str());
}
