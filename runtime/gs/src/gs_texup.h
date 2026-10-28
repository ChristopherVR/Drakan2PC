// Texture upscaling / replacement core (portable C++20, no D3D11, no platform headers except <thread>; see
// docs/reference/portability.md).
//   * content hash of a decoded texture (RGBA8 after CLUT / TEXA resolution),
//   * xBRZ (pixel-art / UI / flat palette textures) and bicubic + sharpen ("smooth", photographic textures) upscalers,
//   2x / 4x / 8x,
//   * box-filter mip chain generation,
//   * PNG (read + write, RGBA) and DDS (uncompressed 24/32 bit, BC1/BC2/BC3) image I/O for texture dumps and HD
//   replacement packs,
//   * a tiny worker pool so the renderer never stalls on an upscale.
// Pixel format everywhere: uint32_t, R in the low byte, alpha in GS units (0x80 = opaque), like gs_texdecode.h.
//
// xBRZ: port of the algorithm of "xBRZ" by Zenju (https://sourceforge.net/projects/xbrz/, GPL-3.0-or-later): corner
// pre-processing (blend types none/normal/dominant), the rotation-wise pixel scaler and the 2x / 4x blend kernels
// (reimplemented from the published algorithm; the 8x factor runs 4x then 2x). This file is GPL-3.0-or-later
// derived like the rest of gsr.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gsr::texup {

// Runtime-generated keyboard prompts must not be exported or overwritten by a
// disk pack. They still benefit from automatic scaling of their current pixels.
struct ContentPolicy {
    bool automaticScale, externalFiles;
};
constexpr ContentPolicy contentPolicy(bool generated, bool userExcluded) {
    return {!userExcluded, !generated && !userExcluded};
}

enum class Algo : uint8_t {
    Auto = 0,
    XBrz = 1,
    Smooth = 2,
};

// 64-bit content hash of a decoded RGBA texture (level 0) including its dimensions.
uint64_t hashContent(const uint32_t* px, uint32_t w, uint32_t h);
// Identical to hashContent after clearing every alpha byte, without a temporary image.
uint64_t hashContentRgb(const uint32_t* px, uint32_t w, uint32_t h);
std::string hashName(uint64_t h); // 16 lower-case hex digits

struct Stats {
    uint32_t uniqueColors = 0;    // capped
    float equalNeighbourFrac = 0; // fraction of horizontally / vertically adjacent pairs with identical colour
    float alphaPartialFrac = 0;   // fraction of texels with alpha strictly between 0 and opaque
    uint32_t maxAlpha = 0;
};
Stats analyse(const uint32_t* px, uint32_t w, uint32_t h);
// Choose xBRZ for pixel-art like content (large flat regions / few colours), otherwise Smooth.
Algo classify(const Stats& s);
// Uninitialised memory sampled as a texture: the game can point-sample a few written rows of a texture whose other
// rows hold random bytes (the 64x64 CT24 sky ramp of the intro). Any upscale mixes those rows into the sampled ones,
// so such textures stay native. True when at least a quarter of the rows (and 4) average more than 60 levels between
// horizontal neighbours, which painted art and photographic noise do not reach.
bool looksUninitialized(const uint32_t* px, uint32_t w, uint32_t h);

// Upscale `src` (w x h) by factor 2 / 4 / 8 (anything else is rounded down to a power of two, 1 = copy). wrapX / wrapY:
// the texture tiles (GS REPEAT), so the kernels read the opposite edge instead of clamping. `out` receives (w*factor) x
// (h*factor) pixels.
void upscale(const uint32_t* src, uint32_t w, uint32_t h, uint32_t factor, Algo algo, bool wrapX, bool wrapY,
             std::vector<uint32_t>& out);

// 2x2 box filter (alpha-weighted colour) down to the next mip level; w,h are the source size, the result is max(1,w/2)
// x max(1,h/2).
void downsample2x(const uint32_t* src, uint32_t w, uint32_t h, std::vector<uint32_t>& out);

// Image I/O. Alpha is converted between the file (0..255) and GS units (0..0x80) so that 0x80 <-> 255.
bool writePngRGBA(const std::string& path, const uint32_t* px, uint32_t w, uint32_t h);
bool readImageBytes(const std::vector<uint8_t>& bytes, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h);
bool readImageFile(const std::string& path, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h); // .png or .dds
bool fileExists(const std::string& path);
bool makeDirs(const std::string& path);                   // mkdir -p
std::vector<std::string> listDir(const std::string& dir); // file names (no path) of regular files

// Worker pool (single-threaded fallback: threads == 0 runs jobs inline in submit()). Urgent jobs (images of the
// GUI page on screen now) start before every queued background job; each lane runs in submission order.
class Pool {
public:
    explicit Pool(unsigned threads);
    ~Pool();
    void submit(std::function<void()> job, bool urgent = false);
    unsigned threads() const { return (unsigned)m_threads.size(); }

private:
    std::vector<std::thread> m_threads;
    std::deque<std::function<void()>> m_q, m_urgent;
    std::mutex m_mu;
    std::condition_variable m_cv;
    bool m_stop = false;
};

} // namespace gsr::texup
