#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gsr::gpu {
// Private wire format, shared by the native and browser compute backends. All
// offsets/counts are in 32-bit words; no host pointers or C++ layouts reach WGSL.
// 32 state words, three 8-word vertices, eight edge words. RGBA and Z
// retain their exact integer representations rather than expanded floats.
inline constexpr unsigned kDrawWords = 64;
struct RasterTextureCache;
struct Job {
    std::vector<uint32_t> draws;
    std::vector<uint32_t> bins;
    std::vector<uint32_t> textures;
    // Resident atlas: (destination word offset, word count) pairs describe
    // consecutive slices of textures. Zero textureWords selects legacy upload.
    std::vector<uint32_t> textureWrites;
    unsigned textureWords = 0;
    std::vector<uint32_t> pixels; // interleaved native RGBA/depth
    unsigned width = 0, height = 0;
};
class Compute {
public:
    std::shared_ptr<RasterTextureCache> rasterTextures;
    virtual ~Compute() = default;
    virtual bool run(Job& job) = 0;
    virtual const std::string& adapter() const = 0;
};
std::unique_ptr<Compute> createCompute();
const char* rasterShader();
} // namespace gsr::gpu
