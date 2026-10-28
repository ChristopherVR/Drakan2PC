#include "runtime/gs/gs_cpu_backend.h"
#include "gs_cpu_raster_math.h"
#include "gs_hd_source_cache.h"
#include "gsr/texture_pack.h"
#include "gs_cpu_tile_workers.h"
#if defined(PS2X_HAVE_PORTABLE_GPU)
#include "gs_gpu_raster.h"
#endif
#include <cstdlib>
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "ps2_log.h"
#include <atomic>
#include <chrono>
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

#if defined(_MSC_VER)
#define GS_CPU_INLINE __forceinline
#define GS_CPU_LAMBDA_INLINE
#elif defined(__GNUC__) || defined(__clang__)
#define GS_CPU_INLINE inline __attribute__((always_inline))
#define GS_CPU_LAMBDA_INLINE __attribute__((always_inline))
#else
#define GS_CPU_INLINE inline
#define GS_CPU_LAMBDA_INLINE
#endif
using namespace GSInternal;

namespace {
thread_local const gsr::PackImage* g_replacement = nullptr;
thread_local bool g_preparedNative = false;
unsigned rasterRounding() {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
    return _mm_getcsr();
#else
    return 0;
#endif
}
class FloatControlScope {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
    unsigned saved_;
#endif
public:
    explicit FloatControlScope(unsigned rounding) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
        saved_ = _mm_getcsr();
        _mm_setcsr(rounding);
#else
        (void)rounding;
#endif
    }
    ~FloatControlScope() {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
        _mm_setcsr(saved_);
#endif
    }
};
void finishDeferredSample(gs_cpu::TileContext& tile);
class TileBinding {
    FloatControlScope rounding_;
    gs_cpu::TileContext* previous_;
    const uint8_t* vram_;

public:
    TileBinding(gs_cpu::TileContext& tile, unsigned rounding, const uint8_t* vram)
        : rounding_(rounding), previous_(gs_cpu::currentTile), vram_(vram) {
        thread_local gs_cpu::PaletteMemo palette;
        palette.reset();
        static const bool useMemo = [] {
            const char* value = std::getenv("PS2X_CPU_PALETTE_MEMO");
            return !value || std::strcmp(value, "0") != 0;
        }();
        tile.palette = useMemo ? &palette : nullptr;
        tile.clut = nullptr;
        tile.clut32 = nullptr;
        tile.deferredSample = nullptr;
        tile.lastTextureState = nullptr;
        tile.textureCache.BeginImmutableRead();
        gs_cpu::currentTile = &tile;
    }
    ~TileBinding() {
        auto& tile = *gs_cpu::currentTile;
        finishDeferredSample(tile);
        // Memo hits skip swizzled reads. Replay the last sample to retain the
        // same final page/tag that the serial hardware cache would expose.
        if (tile.lastTextureState) {
            const auto& tex = tile.lastTextureState->context.tex0;
            GSMem::ReadTexture(tile.textureCache, vram_, tex.psm, tex.tbp0, tex.tbw, tile.lastU, tile.lastV);
        }
        tile.textureCache.EndImmutableRead(vram_);
        tile.palette = nullptr;
        gs_cpu::currentTile = previous_;
    }
};
float fabsQ(float q) {
    return (std::fabs(q) > 1.0e-8f) ? q : 1.0f;
}

u16 Rgba8888ToRgba5551(u32 c) {
    uint32_t r = ((c >> 0) & 0xFF) >> 3;
    uint32_t g = ((c >> 8) & 0xFF) >> 3;
    uint32_t b = ((c >> 16) & 0xFF) >> 3;
    uint32_t a = ((c >> 24) & 0xFF) >> 7;

    return (r | (g << 5) | (b << 10) | (a << 15));
}

u32 Rgba5551ToRgba8888(u16 c) {
    u32 r = ((c >> 0) & 0x1F) << 3;
    u32 g = ((c >> 5) & 0x1F) << 3;
    u32 b = ((c >> 10) & 0x1F) << 3;
    u32 a = ((c >> 15) & 0x01) << 7;

    return (r | (g << 8) | (b << 16) | (a << 24));
}

u32 pack32(u8 r, u8 g, u8 b, u8 a) {
    return static_cast<u32>(r) | (g << 8) | (b << 16) | (a << 24);
}

uint32_t applyTexa(const GSTexaReg& texa, uint8_t psm, uint32_t texel) {
    if (psm == GS_PSM_CT32)
        return texel;

    const uint8_t r = static_cast<uint8_t>(texel & 0xFFu);
    const uint8_t g = static_cast<uint8_t>((texel >> 8) & 0xFFu);
    const uint8_t b = static_cast<uint8_t>((texel >> 16) & 0xFFu);
    const bool rgbZero = r == 0u && g == 0u && b == 0u;
    uint8_t a = static_cast<uint8_t>((texel >> 24) & 0xFFu);

    switch (psm) {
        case GS_PSM_CT24:
            a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            if ((a & 0x80u) != 0u)
                a = texa.ta1;
            else
                a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        default:
            break;
    }

    return (texel & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
}

uint32_t addrPSMCT16Family(uint32_t basePtr, uint32_t width, uint8_t psm, uint32_t x, uint32_t y) {
    switch (psm) {
        case GS_PSM_CT16:
            return GSPSMCT16::addrPSMCT16(basePtr, width, x, y);
        case GS_PSM_CT16S:
            return GSPSMCT16::addrPSMCT16S(basePtr, width, x, y);
        case GS_PSM_Z16:
            return GSPSMCT16::addrPSMZ16(basePtr, width, x, y);
        case GS_PSM_Z16S:
            return GSPSMCT16::addrPSMZ16S(basePtr, width, x, y);
        default:
            return 0u;
    }
}

std::atomic<uint32_t> s_debugPrimitiveCount{0};
std::atomic<uint32_t> s_debugPixelCount{0};
std::atomic<uint32_t> s_debugContext1PrimitiveCount{0};
std::atomic<uint32_t> s_debugFbp150PixelCount{0};

int wrapTextureCoordinate(int coordinate, int textureSize, uint8_t mode, uint16_t regionMin, uint16_t regionMax) {
    switch (mode & 0x3u) {
        case 0: // REPEAT
            return static_cast<int>(static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(textureSize - 1));
        case 1: // CLAMP
            return clampInt(coordinate, 0, textureSize - 1);
        case 2: // REGION_CLAMP
            return std::min(std::max(coordinate, static_cast<int>(regionMin)), static_cast<int>(regionMax));
        case 3: // REGION_REPEAT
            return static_cast<int>((static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(regionMin)) |
                                    static_cast<uint32_t>(regionMax));
        default:
            return coordinate;
    }
}

void finishDeferredSample(gs_cpu::TileContext& tile) {
    if (!tile.deferredSample)
        return;
    const auto& batch = *tile.deferredSample;
    const auto& state = batch.state;
    const auto& a = batch.vertices[0];
    const auto& b = batch.vertices[1];
    const auto& c = batch.vertices[2];
    const float w0 = tile.sampleW0, w1 = tile.sampleW1, w2 = tile.sampleW2;
    float u, v;
    if (state.prim.fst) {
        const auto iu = static_cast<uint16_t>(a.u * w0 + b.u * w1 + c.u * w2);
        const auto iv = static_cast<uint16_t>(a.v * w0 + b.v * w1 + c.v * w2);
        u = static_cast<float>(iu) / 16.0f;
        v = static_cast<float>(iv) / 16.0f;
    } else {
        const float s = a.s * w0 + b.s * w1 + c.s * w2;
        const float t = a.t * w0 + b.t * w1 + c.t * w2;
        const float q = a.q * w0 + b.q * w1 + c.q * w2;
        const float inverse = 1.0f / fabsQ(q);
        u = s * inverse * static_cast<float>(state.textureWidth);
        v = t * inverse * static_cast<float>(state.textureHeight);
    }
    // The fourth bilinear tap is the final read in serial order.
    const int lastU = state.linearFilter ? int(std::floor(u - 0.5f)) + 1 : int(u);
    const int lastV = state.linearFilter ? int(std::floor(v - 0.5f)) + 1 : int(v);
    const auto clamp = state.context.clamp;
    tile.lastU =
        wrapTextureCoordinate(lastU, state.textureWidth, clamp & 3u, (clamp >> 4) & 1023u, (clamp >> 14) & 1023u);
    tile.lastV = wrapTextureCoordinate(lastV, state.textureHeight, (clamp >> 2) & 3u, (clamp >> 24) & 1023u,
                                       (clamp >> 34) & 1023u);
    tile.lastTextureState = &state;
    tile.deferredSample = nullptr;
}

bool passesAlphaTest(uint64_t testReg, uint8_t alpha) {
    if ((testReg & 0x1u) == 0u)
        return true;

    const uint8_t atst = static_cast<uint8_t>((testReg >> 1) & 0x7u);
    const uint8_t aref = static_cast<uint8_t>((testReg >> 4) & 0xFFu);

    switch (atst) {
        case 0:
            return false;
        case 1:
            return true;
        case 2:
            return alpha < aref;
        case 3:
            return alpha <= aref;
        case 4:
            return alpha == aref;
        case 5:
            return alpha >= aref;
        case 6:
            return alpha > aref;
        case 7:
            return alpha != aref;
        default:
            return true;
    }
}

struct PixelWriteMask {
    bool writeRgb = true;
    bool writeAlpha = true;
    bool writeDepth = true;

    bool writesFramebuffer() const { return writeRgb || writeAlpha; }

    bool writesAnything() const { return writesFramebuffer() || writeDepth; }
};

PixelWriteMask classifyAlphaTest(uint64_t testReg, uint8_t alpha, uint8_t framePsm) {
    const bool pass = passesAlphaTest(testReg, alpha);
    if (pass)
        return {};

    // TEST.AFAIL controls what happens when the alpha comparison fails.
    switch (static_cast<uint8_t>((testReg >> 12) & 0x3u)) {
        case 1: // FB_ONLY
            return {true, true, false};
        case 2: // ZB_ONLY
            return {false, false, true};
        case 3: // RGB_ONLY
            // RGB_ONLY is only distinct for RGBA32. The GS treats it as
            // FB_ONLY for RGB24 and RGBA16 framebuffers.
            if (framePsm == GS_PSM_CT32)
                return {true, false, false};
            return {true, true, false};
        case 0: // KEEP
        default:
            return {false, false, false};
    }
}

bool passesDestinationAlphaTest(uint64_t testReg, uint8_t framePsm, uint32_t rawFramebufferPixel) {
    const bool date = ((testReg >> 14) & 0x1u) != 0u;
    if (!date)
        return true;

    const bool datm = ((testReg >> 15) & 0x1u) != 0u;
    switch (framePsm) {
        case GS_PSM_CT32:
            return (((rawFramebufferPixel >> 31) & 0x1u) != 0u) == datm;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            return (((rawFramebufferPixel >> 15) & 0x1u) != 0u) == datm;
        case GS_PSM_CT24:
            // RGB24 has no destination alpha, so DATE always passes.
            return true;
        default:
            return true;
    }
}

struct TextureCombineResult {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

TextureCombineResult combineTexture(const GSTex0Reg& tex, uint8_t vr, uint8_t vg, uint8_t vb, uint8_t va, uint8_t tr,
                                    uint8_t tg, uint8_t tb, uint8_t ta) {
    const bool textureHasAlpha = tex.tcc != 0u;
    TextureCombineResult out{tr, tg, tb, textureHasAlpha ? ta : va};

    switch (tex.tfx) {
        case 0: // MODULATE
            out.r = clampU8((tr * vr) >> 7);
            out.g = clampU8((tg * vg) >> 7);
            out.b = clampU8((tb * vb) >> 7);
            out.a = textureHasAlpha ? clampU8((ta * va) >> 7) : va;
            break;
        case 1: // DECAL
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        case 2: // HIGHLIGHT
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? clampU8(ta + va) : va;
            break;
        case 3: // HIGHLIGHT2
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? ta : va;
            break;
        default:
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
    }

    return out;
}

uint32_t swizzleClutIndexCSM1(uint32_t index) {
    // CSM1 swaps address bits 3 and 4. Preserve the remaining bits:
    // 16-bit CLUTs expose a ninth address bit through CSA[4].
    return (index & ~0x18u) | ((index & 0x08u) << 1u) | ((index & 0x10u) >> 1u);
}

bool isFourBitIndexedPsm(uint8_t psm) {
    return psm == GS_PSM_T4 || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH;
}

bool isEightBitIndexedPsm(uint8_t psm) {
    return psm == GS_PSM_T8 || psm == GS_PSM_T8H;
}

} // namespace

namespace {
static constexpr uint32_t kDefaultDisplayWidth = 640u;
static constexpr uint32_t kDefaultDisplayHeight = 448u;
static constexpr uint32_t kHostFrameWidth = 640u;
static constexpr uint32_t kHostFrameHeight = 512u;

uint16_t encodeFramePixelPSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint16_t>(((r >> 3) & 0x1Fu) | (((g >> 3) & 0x1Fu) << 5) | (((b >> 3) & 0x1Fu) << 10) |
                                 ((a >= 0x40u) ? 0x8000u : 0u));
}

void decodeDisplaySize(uint64_t display64, uint32_t& outWidth, uint32_t& outHeight) {
    const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
    const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
    const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);

    outWidth = (dw + 1u) / (magh + 1u);
    outHeight = dh + 1u;
    if (outWidth < 64u || outHeight < 64u) {
        outWidth = kDefaultDisplayWidth;
        outHeight = kDefaultDisplayHeight;
    }
    outWidth = std::min<uint32_t>(outWidth, kHostFrameWidth);
    outHeight = std::min<uint32_t>(outHeight, kHostFrameHeight);
}

GSFrameReg decodeDisplayFrame(uint64_t dispfb64) {
    GSFrameReg frame{};
    frame.fbp = static_cast<uint32_t>(dispfb64 & 0x1FFu);
    frame.fbw = static_cast<uint32_t>((dispfb64 >> 9) & 0x3Fu);
    frame.psm = static_cast<uint8_t>((dispfb64 >> 15) & 0x1Fu);
    return frame;
}

struct GSDisplayReadOrigin {
    uint32_t x = 0u;
    uint32_t y = 0u;
};

GSDisplayReadOrigin decodeDisplayReadOrigin(uint64_t dispfb64) {
    return {static_cast<uint32_t>((dispfb64 >> 32) & 0x7FFu), static_cast<uint32_t>((dispfb64 >> 43) & 0x7FFu)};
}

bool hasDisplaySetup(uint64_t display64, const GSFrameReg& frame) {
    const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
    const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
    const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);
    return frame.fbw != 0u || dw != 0u || dh != 0u || magh != 0u;
}

struct GSPmodeState {
    bool enableCrt1 = false;
    bool enableCrt2 = false;
    bool mmod = false;
    bool amod = false;
    bool slbg = false;
    uint8_t alp = 0u;
};

GSPmodeState decodePmode(uint64_t pmode64) {
    return {(pmode64 & 0x1ull) != 0ull,        (pmode64 & 0x2ull) != 0ull,
            ((pmode64 >> 5) & 0x1ull) != 0ull, ((pmode64 >> 6) & 0x1ull) != 0ull,
            ((pmode64 >> 7) & 0x1ull) != 0ull, static_cast<uint8_t>((pmode64 >> 8) & 0xFFu)};
}

struct GSSmode2State {
    bool interlaced = false;
    bool frameMode = true;
};

GSSmode2State decodeSMode2(uint64_t smode2) {
    return {(smode2 & 0x1ull) != 0ull, ((smode2 >> 1) & 0x1ull) != 0ull};
}

void applyFieldPresentation(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, bool oddField) {
    if (pixels.empty() || width == 0u || height < 2u)
        return;
    const std::vector<uint8_t> source = pixels;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t sourceY = ((y >> 1u) << 1u) + (oddField ? 1u : 0u);
        if (sourceY >= height)
            sourceY = height - 1u;
        std::memcpy(pixels.data() + y * kHostFrameWidth * 4u, source.data() + sourceY * kHostFrameWidth * 4u,
                    width * 4u);
    }
}

uint32_t countNonBlackPixels(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
    uint32_t count = 0u;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels.data() + y * kHostFrameWidth * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            if (row[x * 4u] != 0u || row[x * 4u + 1u] != 0u || row[x * 4u + 2u] != 0u)
                ++count;
        }
    }
    return count;
}
} // namespace

GSCpuBackend::GSCpuBackend() {
    using namespace GSMem;
    static std::once_flag lookupTablesOnce;
    std::call_once(lookupTablesOnce, []() { InitLookupTables(); });
    for (size_t i = 0; i < kPsmHandlerCount; ++i) {
        switch (i) {
            case GS_PSM_CT32:
                m_readVramFuncs[i] = ReadCT32;
                m_writeVramFuncs[i] = WriteCT32;
                break;
            case GS_PSM_CT24:
                m_readVramFuncs[i] = ReadCT24;
                m_writeVramFuncs[i] = WriteCT24;
                break;
            case GS_PSM_CT16:
                m_readVramFuncs[i] = ReadCT16;
                m_writeVramFuncs[i] = WriteCT16;
                break;
            case GS_PSM_CT16S:
                m_readVramFuncs[i] = ReadCT16S;
                m_writeVramFuncs[i] = WriteCT16S;
                break;
            case GS_PSM_T8:
                m_readVramFuncs[i] = ReadP8;
                m_writeVramFuncs[i] = WriteP8;
                break;
            case GS_PSM_T8H:
                m_readVramFuncs[i] = ReadP8H;
                m_writeVramFuncs[i] = WriteP8H;
                break;
            case GS_PSM_T4:
                m_readVramFuncs[i] = ReadP4;
                m_writeVramFuncs[i] = WriteP4;
                break;
            case GS_PSM_T4HH:
                m_readVramFuncs[i] = ReadP4HH;
                m_writeVramFuncs[i] = WriteP4HH;
                break;
            case GS_PSM_T4HL:
                m_readVramFuncs[i] = ReadP4HL;
                m_writeVramFuncs[i] = WriteP4HL;
                break;
            case GS_PSM_Z32:
                m_readVramFuncs[i] = ReadZ32;
                m_writeVramFuncs[i] = WriteZ32;
                break;
            case GS_PSM_Z24:
                m_readVramFuncs[i] = ReadZ24;
                m_writeVramFuncs[i] = WriteZ24;
                break;
            case GS_PSM_Z16:
                m_readVramFuncs[i] = ReadZ16;
                m_writeVramFuncs[i] = WriteZ16;
                break;
            case GS_PSM_Z16S:
                m_readVramFuncs[i] = ReadZ16S;
                m_writeVramFuncs[i] = WriteZ16S;
                break;
            default:
                m_readVramFuncs[i] = ReadNull;
                m_writeVramFuncs[i] = WriteNull;
                break;
        }
    }
    Reset();
}

GSCpuBackend::~GSCpuBackend() {
    Flush();
}

void GSCpuBackend::Initialize(uint8_t* vram, uint32_t vramSize) {
    const unsigned rasterThreads = gs_cpu::rasterThreadCount();
    if (vram && vramSize < GSMem::MEMORY_SIZE)
        throw std::invalid_argument("GS CPU backend requires at least 4 MiB of VRAM");

    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
    FlushGpuUnlocked();
    m_rasterThreads = rasterThreads;
    m_vram = vram;
    m_vramSize = vramSize;
    const char* enabled = std::getenv("DRAKAN_HD_TEXTURES");
    if (!enabled || std::strcmp(enabled, "0") != 0) {
        const char* directory = std::getenv("DRAKAN_TEXTURE_DIR");
        m_texturePack = std::make_unique<gsr::TexturePack>(directory ? directory : "textures");
        if (m_texturePack->empty())
            m_texturePack.reset();
    } else {
        m_texturePack.reset();
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    // HD sampling benefits from extra parallelism, while EE/audio and host
    // presentation still need two CPUs on the eight-CPU native configuration.
    if (m_texturePack && !std::getenv("PS2X_CPU_RASTER_THREADS")) {
        const unsigned available = std::thread::hardware_concurrency();
        if (available >= 8)
            m_rasterThreads = std::min(available - 2, 8u);
    }
#endif
    m_hdSources = std::make_unique<gs_cpu::HdSourceCache>();
    ResetUnlocked();
}

void GSCpuBackend::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
    FlushGpuUnlocked();
    ResetUnlocked();
}

void GSCpuBackend::ResetUnlocked() {
    m_highResolution.reset();
    m_clut.fill(0u);
    m_clutCbp.fill(0u);
    m_texturePageCache.Invalidate();
    m_transfer = {};
    m_transfer.direction = 3u;
    m_transferState = {};
    m_transferState.direction = 3u;
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
}

void GSCpuBackend::Submit(const GSPrimitiveBatch& batch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SubmitUnlocked(batch);
}

void GSCpuBackend::SubmitUnlocked(const GSPrimitiveBatch& batch) {
    // The GS is independent of the EE FPU. Guest arithmetic may leave the
    // submitting thread in round-towards-zero, but raster interpolation must
    // agree with the GPU / Wasm pipeline. Restore the EE control state on exit.
    FloatControlScope gsRounding(rasterRounding() & ~0x6000u);
    if (!m_vram || batch.vertexCount == 0u)
        return;
    if (batch.state.prim.type == GS_PRIM_SPRITE && batch.vertexCount >= 2 &&
        (batch.vertices[0].x == batch.vertices[1].x || batch.vertices[0].y == batch.vertices[1].y)) {
        // Empty rectangles have no coverage. Expanding them to one pixel draws
        // spurious HUD borders; genuine subpixel rectangles remain eligible.
        if (std::getenv("DRAKAN_FRAME_METRICS")) {
            static thread_local unsigned reports = 0;
            if (reports++ < 32) {
                const auto& c = batch.state.context;
                std::fprintf(stderr, "[portable-empty-sprite] rect=(%.3f,%.3f)-(%.3f,%.3f) texture=%d\n",
                             batch.vertices[0].x - float(c.xyoffset.ofx >> 4),
                             batch.vertices[0].y - float(c.xyoffset.ofy >> 4),
                             batch.vertices[1].x - float(c.xyoffset.ofx >> 4),
                             batch.vertices[1].y - float(c.xyoffset.ofy >> 4), batch.state.prim.tme);
            }
        }
        return;
    }
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred && m_gpuDeferred->touchesSource(batch.state)) {
        FlushDrawsUnlocked();
        FlushGpuUnlocked(GpuFenceReason::Source);
    }
#endif
    if (!QueueDraw(batch)) {
#if defined(PS2X_HAVE_PORTABLE_GPU)
        if (m_gpuDeferred && !m_gpuDeferred->empty() && std::getenv("DRAKAN_FRAME_METRICS")) {
            static thread_local unsigned reports = 0;
            if (reports++ < 16 || (reports & 255u) == 0) {
                const auto& s = batch.state;
                std::fprintf(stderr,
                             "[portable-gpu-cpu] immediate type=%u vertices=%u frame=%u depth=%u "
                             "zmask=%d test=%llx texture=%u fst=%d linear=%d clamp=%llx\n",
                             unsigned(s.prim.type), batch.vertexCount, unsigned(s.context.frame.psm),
                             unsigned(s.context.zbuf.psm), s.context.zbuf.zmask,
                             static_cast<unsigned long long>(s.context.test), unsigned(s.context.tex0.psm), s.prim.fst,
                             s.linearFilter, static_cast<unsigned long long>(s.context.clamp));
            }
        }
#endif
        FlushDrawsUnlocked();
        FlushGpuUnlocked(GpuFenceReason::Cpu);
        DrawPrimitive(batch);
    }
    static const bool profile = std::getenv("PS2X_CPU_RASTER_STATS") != nullptr;
    if (profile) {
        static thread_local uint64_t batches = 0;
        if ((++batches & 4095u) == 0)
            std::fprintf(stderr,
                         "[cpu-raster] batches=%llu tiled=%llu grouped=%llu frame_psm=%u depth_psm=%u tex_psm=%u\n",
                         static_cast<unsigned long long>(batches),
                         static_cast<unsigned long long>(gs_cpu::tileDispatches.load()),
                         static_cast<unsigned long long>(gs_cpu::tilePrimitives.load()), batch.state.context.frame.psm,
                         batch.state.context.zbuf.psm, batch.state.context.tex0.psm);
    }
}

void GSCpuBackend::LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& texclut) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || (!isFourBitIndexedPsm(tex0.psm) && !isEightBitIndexedPsm(tex0.psm)))
        return;

    switch (tex0.cld) {
        case 0u:
        case 6u:
        case 7u:
            return;
        case 1u:
            break;
        case 2u:
            m_clutCbp[0] = tex0.cbp;
            break;
        case 3u:
            m_clutCbp[1] = tex0.cbp;
            break;
        case 4u:
            if (m_clutCbp[0] == tex0.cbp)
                return;
            m_clutCbp[0] = tex0.cbp;
            break;
        case 5u:
            if (m_clutCbp[1] == tex0.cbp)
                return;
            m_clutCbp[1] = tex0.cbp;
            break;
        default:
            return;
    }

#if defined(PS2X_HAVE_PORTABLE_GPU)
    const uint64_t paletteStart = uint64_t(tex0.cbp / 32) * 8192;
    if (m_gpuDeferred && (tex0.csm != 0 || paletteStart + 16384 > GSMem::MEMORY_SIZE ||
                          m_gpuDeferred->touches({paletteStart, paletteStart + 16384}))) {
        FlushDrawsUnlocked();
        FlushGpuUnlocked(GpuFenceReason::Palette);
    }
#endif
    bool independent = false;
    if (!m_pendingDraws.empty() && tex0.csm == 0u) {
        auto source = m_pendingDraws.front().batch.state;
        source.prim.tme = true;
        source.context.tex0.psm = tex0.cpsm;
        source.context.tex0.tbp0 = tex0.cbp;
        source.context.tex0.tbw = 1;
        source.context.tex0.tw = source.context.tex0.th = 4;
        source.context.clamp = 5; // clamp to the 16x16 CSM1 palette footprint
        source.textureWidth = source.textureHeight = 16;
        independent = CanTile(source, m_pendingBounds[0], m_pendingBounds[1], m_pendingBounds[2], m_pendingBounds[3]);
    }
    if (!independent)
        FlushDrawsUnlocked();
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred && (tex0.csm != 0 || paletteStart + 16384 > GSMem::MEMORY_SIZE ||
                          m_gpuDeferred->touches({paletteStart, paletteStart + 16384})))
        FlushGpuUnlocked(GpuFenceReason::Palette);
#endif
    LoadClutUnlocked(tex0, texclut);
    ++m_clutGeneration;
    if (independent)
        m_pendingCacheSequence = ++m_pendingSequence;
}

void GSCpuBackend::LoadClutUnlocked(const GSTex0Reg& tex0, const GSTexClutReg& texclut) {
    const bool fourBit = isFourBitIndexedPsm(tex0.psm);
    const bool sixteenBit = tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S;
    const bool thirtyTwoBit = tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24;
    if (!sixteenBit && !thirtyTwoBit)
        return;

    const uint32_t entryCount = fourBit ? 16u : 256u;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t destinationBase = (static_cast<uint32_t>(tex0.csa) & csaMask) << 4u;

    const bool loadCsm1Suffix = tex0.csm == 0u && thirtyTwoBit && !fourBit;
    const uint32_t firstEntry = loadCsm1Suffix ? destinationBase : 0u;

    for (uint32_t entry = firstEntry; entry < entryCount; ++entry) {
        uint32_t sourceX = 0u;
        uint32_t sourceY = 0u;
        uint32_t sourceWidth = 1u;

        if (tex0.csm == 0u) {
            const uint32_t sourceIndex = swizzleClutIndexCSM1(entry);
            sourceX = sourceIndex & 0x0Fu;
            sourceY = sourceIndex >> 4u;
        } else {
            sourceWidth = texclut.cbw != 0u ? static_cast<uint32_t>(texclut.cbw) : 1u;
            sourceX = (static_cast<uint32_t>(texclut.cou) << 4u) + entry;
            sourceY = static_cast<uint32_t>(texclut.cov);
        }

        const uint32_t raw = ReadTextureVramUnlocked(tex0.cpsm, tex0.cbp, sourceWidth, sourceX, sourceY);
        const uint32_t destination =
            (loadCsm1Suffix ? entry : destinationBase + entry) & (sixteenBit ? 0x1FFu : 0x0FFu);
        if (sixteenBit) {
            m_clut[destination] = static_cast<uint16_t>(raw);
        } else {
            m_clut[destination] = static_cast<uint16_t>(raw & 0xFFFFu);
            m_clut[destination + 256u] = static_cast<uint16_t>(raw >> 16u);
        }
    }
}

void GSCpuBackend::FlushGpuUnlocked(GpuFenceReason reason) {
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred && !m_gpuDeferred->empty()) {
        FloatControlScope rounding(m_gpuDeferred->rounding());
        if (!m_gpuDeferred->flush(*this))
            m_gpuFailed = true;
        if (std::getenv("DRAKAN_FRAME_METRICS")) {
            static thread_local std::array<uint64_t, size_t(GpuFenceReason::Count)> counts{};
            static thread_local auto last = std::chrono::steady_clock::now();
            ++counts[size_t(reason)];
            const auto now = std::chrono::steady_clock::now();
            if (now - last >= std::chrono::seconds(2)) {
                std::fprintf(stderr,
                             "[portable-gpu-fences] boundary=%llu source=%llu palette=%llu target=%llu "
                             "read=%llu write=%llu cpu=%llu transfer=%llu\n",
                             (unsigned long long)counts[0], (unsigned long long)counts[1],
                             (unsigned long long)counts[2], (unsigned long long)counts[3],
                             (unsigned long long)counts[4], (unsigned long long)counts[5],
                             (unsigned long long)counts[6], (unsigned long long)counts[7]);
                counts.fill(0);
                last = now;
            }
        }
    }
#else
    (void)reason;
#endif
}
void GSCpuBackend::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
    FlushGpuUnlocked();
}

void GSCpuBackend::TextureFlush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Eligible draws read immutable, coherent texture pages. Record the
    // invalidation in command order and retain their already captured palettes.
    m_texturePageCache.Invalidate();
    if (!m_pendingDraws.empty())
        m_pendingCacheSequence = ++m_pendingSequence;
}

void GSCpuBackend::Sync(GSSyncReason) {
    Flush();
}

uint32_t GSCpuBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend*>(this)->FlushDrawsUnlocked();
    const_cast<GSCpuBackend*>(this)->FlushGpuUnlocked(GpuFenceReason::Read);
    return ReadVramUnlocked(psm, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const {
    if (!m_vram)
        return 0u;
    return m_readVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) {
    if (!m_vram)
        return 0u;

    if (gs_cpu::currentTile) {
        return GSMem::ReadTexture(gs_cpu::currentTile->textureCache, m_vram, psm, base, bw, x, y);
    }
    return GSMem::ReadTexture(m_texturePageCache, m_vram, psm, base, bw, x, y);
}

void GSCpuBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred && m_gpuDeferred->touches(gs_gpu::memoryRange(psm, base, bw, x, y, 1, 1)))
        FlushGpuUnlocked(GpuFenceReason::Write);
    if (m_highResolution)
        m_highResolution->invalidateMemory(gs_gpu::memoryRange(psm, base, bw, x, y, 1, 1));
#endif
    WriteVramUnlocked(psm, base, bw, x, y, value);
}

void GSCpuBackend::WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    if (!m_vram)
        return;
    m_writeVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y, value);
}

void GSCpuBackend::SnapshotVram(std::vector<uint8_t>& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend*>(this)->FlushDrawsUnlocked();
    const_cast<GSCpuBackend*>(this)->FlushGpuUnlocked(GpuFenceReason::Read);
    if (!m_vram || m_vramSize == 0u) {
        out.clear();
        return;
    }
    out.resize(m_vramSize);
    std::memcpy(out.data(), m_vram, m_vramSize);
}

GSTransferSnapshot GSCpuBackend::GetTransferSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend*>(this)->FlushDrawsUnlocked();
    const_cast<GSCpuBackend*>(this)->FlushGpuUnlocked(GpuFenceReason::Read);
    GSTransferSnapshot result = m_transferState;
    result.localToHostPendingBytes =
        m_localToHostReadPos < m_localToHostBuffer.size() ? m_localToHostBuffer.size() - m_localToHostReadPos : 0u;
    return result;
}

std::shared_ptr<const gs_cpu::SourceImage>
GSCpuBackend::DecodeTextureSource(const GSDrawState& state, const GSMem::TexturePageCache& sourceCache) {
    if (!state.prim.tme || !state.textureWidth || !state.textureHeight || state.textureWidth > 1024 ||
        state.textureHeight > 1024 || !sourceCache.MatchesVram(m_vram))
        return {};
    const auto& tex = state.context.tex0;
    // Decode using a private copy: pack lookup must never mutate the guest's
    // retained hardware texture cache or palette state.
    unsigned pageW = 64, pageH = 32;
    if (tex.psm == GS_PSM_T8) {
        pageW = 128;
        pageH = 64;
    } else if (tex.psm == GS_PSM_T4) {
        pageW = 128;
        pageH = 128;
    } else if (tex.psm == GS_PSM_CT16 || tex.psm == GS_PSM_CT16S || tex.psm == GS_PSM_Z16 || tex.psm == GS_PSM_Z16S)
        pageH = 64;
    else if (tex.psm != GS_PSM_CT32 && tex.psm != GS_PSM_CT24 && tex.psm != GS_PSM_T8H && tex.psm != GS_PSM_T4HL &&
             tex.psm != GS_PSM_T4HH && tex.psm != GS_PSM_Z32 && tex.psm != GS_PSM_Z24)
        return {};
    const size_t start = (size_t(tex.tbp0) / 32) * 8192;
    const size_t columns = (state.textureWidth + pageW - 1) / pageW;
    const size_t rows = (state.textureHeight + pageH - 1) / pageH;
    const size_t length = ((rows - 1) * (size_t(tex.tbw) * 64 / pageW) + columns + 1) * 8192;
    if (start + length > GSMem::MEMORY_SIZE)
        return {};
    const gs_cpu::HdSourceCache::Key key{tex.psm,        tex.tbp0, tex.tbw, state.textureWidth, state.textureHeight,
                                         tex.cpsm,       tex.csm,  tex.csa, state.texa.ta0,     state.texa.ta1,
                                         state.texa.aem, 0};
    static const std::array<uint16_t, 512> noPalette{};
    const bool indexed = tex.psm == GS_PSM_T8 || tex.psm == GS_PSM_T4 || tex.psm == GS_PSM_T8H ||
                         tex.psm == GS_PSM_T4HL || tex.psm == GS_PSM_T4HH;
    return m_hdSources->getShared(key, m_vram + start, length, indexed ? m_clut : noPalette, [&](auto& pixels) {
        auto cache = sourceCache;
        pixels.resize(size_t(state.textureWidth) * state.textureHeight);
        for (uint32_t y = 0; y < state.textureHeight; ++y)
            for (uint32_t x = 0; x < state.textureWidth; ++x) {
                uint32_t pixel = GSMem::ReadTexture(cache, m_vram, tex.psm, tex.tbp0, tex.tbw, x, y);
                switch (tex.psm) {
                    case GS_PSM_T8:
                    case GS_PSM_T8H:
                    case GS_PSM_T4:
                    case GS_PSM_T4HL:
                    case GS_PSM_T4HH:
                        pixel = LookupCLUT(state, static_cast<uint8_t>(pixel), tex.cpsm, tex.csm, tex.csa, tex.psm);
                        break;
                    case GS_PSM_CT16:
                    case GS_PSM_CT16S:
                    case GS_PSM_Z16:
                    case GS_PSM_Z16S:
                        pixel = applyTexa(state.texa, tex.psm, Rgba5551ToRgba8888(pixel));
                        break;
                    case GS_PSM_CT32:
                    case GS_PSM_CT24:
                    case GS_PSM_Z32:
                    case GS_PSM_Z24:
                        pixel = applyTexa(state.texa, tex.psm, pixel);
                        break;
                    default:
                        pixel = 0xffff00ffu;
                        break;
                }
                pixels[size_t(y) * state.textureWidth + x] = pixel;
            }
        return gsr::TexturePack::hashes(pixels, state.textureWidth, state.textureHeight);
    });
}

std::shared_ptr<const gsr::PackImage> GSCpuBackend::PrepareReplacement(const GSDrawState& state) {
    if (!m_texturePack || !state.prim.tme || !state.textureWidth || !state.textureHeight || state.textureWidth > 1024 ||
        state.textureHeight > 1024 || !m_texturePageCache.MatchesVram(m_vram))
        return {};
    static const bool profile = std::getenv("DRAKAN_HD_PROFILE") != nullptr;
    const auto begin = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto decoded = DecodeTextureSource(state, m_texturePageCache);
    if (!decoded)
        return {};
    auto result = m_texturePack->findHashed(decoded->pixels, state.textureWidth, state.textureHeight,
                                            decoded->hashes[0], decoded->hashes[1]);
    if (profile) {
        const auto now = std::chrono::steady_clock::now();
        m_hdSources->recordLookup(std::chrono::duration_cast<std::chrono::nanoseconds>(now - begin).count(),
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    }
    return result;
}

void GSCpuBackend::DrawPrimitive(const GSPrimitiveBatch& batch, const std::shared_ptr<const gsr::PackImage>& prepared,
                                 bool preparedNative) {
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_highResolution)
        m_highResolution->invalidateDraw(batch);
#endif
    const auto& scissor = batch.state.context.scissor;
    auto replacement = prepared ? prepared
                       : m_texturePack && CanTile(batch.state, scissor.x0, scissor.x1, scissor.y0, scissor.y1)
                           ? PrepareReplacement(batch.state)
                           : std::shared_ptr<const gsr::PackImage>{};
    struct RestoreReplacement {
        const gsr::PackImage* saved = g_replacement;
        bool savedNative = g_preparedNative;
        ~RestoreReplacement() {
            g_replacement = saved;
            g_preparedNative = savedNative;
        }
    } restore;
    g_replacement = replacement.get();
    g_preparedNative = prepared && preparedNative;
    const GSDrawState& state = batch.state;
    const auto& ctx = state.context;
    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t primitiveIndex = s_debugPrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
        if (primitiveIndex < 64u) {
            std::cout
                << "[gs:prim] idx=" << primitiveIndex << " type=" << static_cast<uint32_t>(state.prim.type)
                << " tme=" << static_cast<uint32_t>(state.prim.tme) << " abe=" << static_cast<uint32_t>(state.prim.abe)
                << " fst=" << static_cast<uint32_t>(state.prim.fst)
                << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt) << " fbp=" << ctx.frame.fbp
                << " fbw=" << ctx.frame.fbw << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                << " tex0=("
                << "tbp0=" << ctx.tex0.tbp0 << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw) << " psm=0x" << std::hex
                << static_cast<uint32_t>(ctx.tex0.psm) << std::dec << " tw=" << static_cast<uint32_t>(ctx.tex0.tw)
                << " th=" << static_cast<uint32_t>(ctx.tex0.th) << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx) << " cbp=" << ctx.tex0.cbp << " cpsm=0x" << std::hex
                << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                << " csa=" << static_cast<uint32_t>(ctx.tex0.csa) << ")"
                << " texclut=("
                << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                << " cou=" << static_cast<uint32_t>(state.texclut.cou) << " cov=" << state.texclut.cov << ")"
                << " ofx=" << (ctx.xyoffset.ofx >> 4) << " ofy=" << (ctx.xyoffset.ofy >> 4) << " scissor=("
                << ctx.scissor.x0 << "," << ctx.scissor.y0 << ")-(" << ctx.scissor.x1 << "," << ctx.scissor.y1 << ")"
                << " test=0x" << std::hex << ctx.test << " alpha=0x" << ctx.alpha << std::dec << " v0=("
                << batch.vertices[0].x << "," << batch.vertices[0].y << ")"
                << " uv0=(" << (batch.vertices[0].u >> 4) << "," << (batch.vertices[0].v >> 4) << ")"
                << " stq0=(" << batch.vertices[0].s << "," << batch.vertices[0].t << "," << batch.vertices[0].q << ")"
                << " v1=(" << batch.vertices[1].x << "," << batch.vertices[1].y << ")"
                << " uv1=(" << (batch.vertices[1].u >> 4) << "," << (batch.vertices[1].v >> 4) << ")"
                << " stq1=(" << batch.vertices[1].s << "," << batch.vertices[1].t << "," << batch.vertices[1].q << ")"
                << " v2=(" << batch.vertices[2].x << "," << batch.vertices[2].y << ")"
                << " uv2=(" << (batch.vertices[2].u >> 4) << "," << (batch.vertices[2].v >> 4) << ")"
                << " stq2=(" << batch.vertices[2].s << "," << batch.vertices[2].t << "," << batch.vertices[2].q << ")"
                << " rgba0=(" << static_cast<uint32_t>(batch.vertices[0].r) << ","
                << static_cast<uint32_t>(batch.vertices[0].g) << "," << static_cast<uint32_t>(batch.vertices[0].b)
                << "," << static_cast<uint32_t>(batch.vertices[0].a) << ")"
                << " rgba1=(" << static_cast<uint32_t>(batch.vertices[1].r) << ","
                << static_cast<uint32_t>(batch.vertices[1].g) << "," << static_cast<uint32_t>(batch.vertices[1].b)
                << "," << static_cast<uint32_t>(batch.vertices[1].a) << ")"
                << " rgba2=(" << static_cast<uint32_t>(batch.vertices[2].r) << ","
                << static_cast<uint32_t>(batch.vertices[2].g) << "," << static_cast<uint32_t>(batch.vertices[2].b)
                << "," << static_cast<uint32_t>(batch.vertices[2].a) << ")" << std::endl;
        }
    });

    PS2_IF_AGRESSIVE_LOGS({
        if ((state.prim.ctxt != 0u || ctx.frame.fbp == 150u) &&
            s_debugContext1PrimitiveCount.fetch_add(1u, std::memory_order_relaxed) < 32u) {
            std::cout << "[gs:copy-prim]"
                      << " type=" << static_cast<uint32_t>(state.prim.type)
                      << " tme=" << static_cast<uint32_t>(state.prim.tme)
                      << " abe=" << static_cast<uint32_t>(state.prim.abe)
                      << " fst=" << static_cast<uint32_t>(state.prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt) << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm)
                      << std::dec << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0 << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw) << " psm=0x"
                      << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx) << " cbp=" << ctx.tex0.cbp << " cpsm=0x"
                      << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa) << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(state.texclut.cou) << " cov=" << state.texclut.cov << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4) << " ofy=" << (ctx.xyoffset.ofy >> 4) << " scissor=("
                      << ctx.scissor.x0 << "," << ctx.scissor.y0 << ")-(" << ctx.scissor.x1 << "," << ctx.scissor.y1
                      << ")"
                      << " test=0x" << std::hex << ctx.test << " alpha=0x" << ctx.alpha << std::dec << std::endl;
        }
    });

    switch (state.prim.type) {
        case GS_PRIM_SPRITE:
            DrawSprite(batch);
            break;
        case GS_PRIM_TRIANGLE:
        case GS_PRIM_TRISTRIP:
        case GS_PRIM_TRIFAN:
            DrawTriangle(batch);
            break;
        case GS_PRIM_LINE:
        case GS_PRIM_LINESTRIP:
            DrawLine(batch);
            break;
        case GS_PRIM_POINT: {
            const GSVertex& v = batch.vertices[0];
            const auto& ctx = state.context;
            int px = static_cast<int>(v.x) - (ctx.xyoffset.ofx >> 4);
            int py = static_cast<int>(v.y) - (ctx.xyoffset.ofy >> 4);
            auto color = TextureCombineResult{v.r, v.g, v.b, v.a};
            if (state.prim.tme) {
                const uint32_t texel = SampleTexture(state, v.s, v.t, v.q, v.u, v.v, px, py);
                color = combineTexture(ctx.tex0, v.r, v.g, v.b, v.a, uint8_t(texel), uint8_t(texel >> 8),
                                       uint8_t(texel >> 16), uint8_t(texel >> 24));
            }
            WritePixel(state, px, py, static_cast<u32>(v.z), color.r, color.g, color.b, color.a, v.fog);
            break;
        }
        default:
            break;
    }
}

GS_CPU_INLINE void GSCpuBackend::WritePixel(const GSDrawState& __restrict state, int x, int y, int z, uint8_t r,
                                            uint8_t g, uint8_t b, uint8_t a, uint8_t fog, bool depthPassed) {
    const auto& ctx = state.context;
    if (x < ctx.scissor.x0 || x > ctx.scissor.x1 || y < ctx.scissor.y0 || y > ctx.scissor.y1)
        return;

    if (state.prim.fge) {
        const uint32_t inverseFog = 255u - fog;
        auto applyFog = [&](uint8_t input, uint8_t fogColor) -> uint8_t {
            return static_cast<uint8_t>(((static_cast<uint32_t>(fog) * input) >> 8) + ((inverseFog * fogColor) >> 8));
        };

        r = applyFog(r, state.fogR);
        g = applyFog(g, state.fogG);
        b = applyFog(b, state.fogB);
    }

    const u32 fbp = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const u32 fbw = std::max<u32>(ctx.frame.fbw, 1u);
    const u32 fpsm = ctx.frame.psm;
    const u32 zbp = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    const u32 zpsm = ctx.zbuf.psm;

    const PixelWriteMask writeMask = classifyAlphaTest(ctx.test, a, static_cast<uint8_t>(fpsm));
    if (!writeMask.writesAnything()) {
        return;
    }

    const uint32_t ztestMethod = static_cast<uint32_t>((ctx.test >> 17) & 3u);
    const bool alphaBlendEnabled = state.prim.abe && !(state.pabe && (a & 0x80u) == 0u) &&
                                   !gs_cpu::blendIsSource(ctx.alpha, a, fpsm == GS_PSM_CT24);
    const bool preserveDestinationAlpha = writeMask.writeRgb && !writeMask.writeAlpha && fpsm == GS_PSM_CT32;
    const bool destinationAlphaTestNeedsRead =
        ((ctx.test >> 14) & 0x1u) != 0u && (fpsm == GS_PSM_CT32 || fpsm == GS_PSM_CT16 || fpsm == GS_PSM_CT16S);

    // small optimization, avoid reading the framebuffer for simple draws
    // TODO: only one address lookup for rmw
    const bool frmw =
        destinationAlphaTestNeedsRead ||
        (writeMask.writesFramebuffer() && ((ctx.frame.fbmsk != 0) || alphaBlendEnabled || preserveDestinationAlpha));

    u32 rawFramebufferPixel = 0;
    u32 fbrgba = 0;
    if (frmw) {
        rawFramebufferPixel = ReadVramUnlocked(fpsm, fbp, fbw, x, y);
        fbrgba = rawFramebufferPixel;

        if (bitsPerPixel(fpsm) == 16) {
            fbrgba = Rgba5551ToRgba8888(fbrgba);
        } else if (fpsm == GS_PSM_CT24) {
            // The GS supplies 0x80 as destination alpha for RGB24 blending.
            fbrgba |= 0x80000000u;
        }
    }

    if (!passesDestinationAlphaTest(ctx.test, static_cast<uint8_t>(fpsm), rawFramebufferPixel)) {
        return;
    }

    bool zpass = depthPassed;
    uint32_t storedZ = 0u;
    if (!depthPassed)
        switch (ztestMethod) {
            case 0:
                zpass = false;
                break;
            case 1:
                zpass = true;
                break;
            case 2:
                storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
                zpass = static_cast<uint32_t>(z) >= storedZ;
                break;
            case 3:
                storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
                zpass = static_cast<uint32_t>(z) > storedZ;
                break;
        }

    if (!zpass) {
        return;
    }

    if (writeMask.writesFramebuffer()) {
        if (alphaBlendEnabled) {
            uint8_t dr = fbrgba & 0xFF;
            uint8_t dg = (fbrgba >> 8) & 0xFF;
            uint8_t db = (fbrgba >> 16) & 0xFF;
            uint8_t da = (fbrgba >> 24) & 0xFF;

            {
                uint64_t alphaReg = ctx.alpha;
                uint8_t asel = alphaReg & 3;
                uint8_t bsel = (alphaReg >> 2) & 3;
                uint8_t csel = (alphaReg >> 4) & 3;
                uint8_t dsel = (alphaReg >> 6) & 3;
                uint8_t fix = static_cast<uint8_t>((alphaReg >> 32) & 0xFF);

                auto pickRGB = [&](uint8_t sel, int cs, int cd) -> int {
                    if (sel == 0)
                        return cs;
                    if (sel == 1)
                        return cd;
                    return 0;
                };
                int cAlpha = (csel == 0) ? a : (csel == 1) ? da : fix;

                r = clampU8(((pickRGB(asel, r, dr) - pickRGB(bsel, r, dr)) * cAlpha >> 7) + pickRGB(dsel, r, dr));
                g = clampU8(((pickRGB(asel, g, dg) - pickRGB(bsel, g, dg)) * cAlpha >> 7) + pickRGB(dsel, g, dg));
                b = clampU8(((pickRGB(asel, b, db) - pickRGB(bsel, b, db)) * cAlpha >> 7) + pickRGB(dsel, b, db));
            }
        }

        if (writeMask.writeAlpha && (ctx.fba & 0x1ull) != 0ull && ctx.frame.psm != GS_PSM_CT24) {
            a = static_cast<uint8_t>(a | 0x80u);
        }

        u32 pixel = pack32(r, g, b, a);

        if (ctx.frame.fbmsk != 0) {
            pixel = (pixel & ~ctx.frame.fbmsk) | (fbrgba & ctx.frame.fbmsk);
        }

        if (preserveDestinationAlpha) {
            pixel = (pixel & 0x00FFFFFFu) | (fbrgba & 0xFF000000u);
        }

        // format conversion
        if (bitsPerPixel(fpsm) == 16) {
            pixel = Rgba8888ToRgba5551(pixel);
        }

        WriteVramUnlocked(fpsm, fbp, fbw, x, y, pixel);
    }

    if (writeMask.writeDepth && !ctx.zbuf.zmask) {
        WriteVramUnlocked(zpsm, zbp, fbw, x, y, z);
    }
}

GS_CPU_INLINE uint32_t GSCpuBackend::LookupCLUT(const GSDrawState& __restrict state, uint8_t index, uint8_t cpsm,
                                                uint8_t csm, uint8_t csa, uint8_t sourcePsm) {
    const auto* tile = gs_cpu::currentTile;
    const auto& clut = tile && tile->clut ? *tile->clut : m_clut;
    const bool sixteenBit = cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t clutBase = (static_cast<uint32_t>(csa) & csaMask) << 4u;
    const uint32_t sourceIndex =
        isFourBitIndexedPsm(sourcePsm) ? (static_cast<uint32_t>(index) & 0x0Fu) : static_cast<uint32_t>(index);

    uint32_t clutIndex = (clutBase + sourceIndex) & (sixteenBit ? 0x1FFu : 0x0FFu);
    if (!sixteenBit && csm == 0u && isEightBitIndexedPsm(sourcePsm)) {
        const uint32_t block = std::min((sourceIndex & 0xF0u) + clutBase, 240u);
        clutIndex = block + (sourceIndex & 0x0Fu);
    }

    switch (cpsm) {
        case GS_PSM_CT32: {
            const uint32_t raw =
                static_cast<uint32_t>(clut[clutIndex]) | (static_cast<uint32_t>(clut[clutIndex + 256u]) << 16u);
            return applyTexa(state.texa, cpsm, raw);
        }
        case GS_PSM_CT24: {
            const uint32_t raw =
                static_cast<uint32_t>(clut[clutIndex]) | (static_cast<uint32_t>(clut[clutIndex + 256u]) << 16u);
            return applyTexa(state.texa, cpsm, raw & 0x00FFFFFFu);
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            return applyTexa(state.texa, cpsm, Rgba5551ToRgba8888(clut[clutIndex]));
        default:
            break;
    }

    return 0xFFFF00FFu;
}

GS_CPU_INLINE uint32_t GSCpuBackend::SampleTexture(const GSDrawState& __restrict state, float s, float t, float q,
                                                   uint16_t u, uint16_t v, int x, int y) {
    return SampleTextureImpl<false>(state, s, t, q, u, v, x, y);
}

template <bool Common, bool Alpha>
GS_CPU_INLINE uint32_t GSCpuBackend::SampleTextureImpl(const GSDrawState& __restrict state, float s, float t, float q,
                                                       uint16_t u, uint16_t v, int x, int y) {
    auto* tile = gs_cpu::currentTile;
    auto& textureCache = (Common || tile) ? tile->textureCache : m_texturePageCache;
    if ((Common || tile) && (!tile->sampledTexture || tile->lastSampleBatch != tile->batchIndex)) {
        tile->sampledTexture = true;
        tile->lastSampleBatch = tile->batchIndex;
    }
    if (Common || tile) {
        tile->deferredSample = nullptr;
        tile->lastSampleX = x;
        tile->lastSampleY = y;
    }
    const auto& ctx = state.context;
    const auto& tex = ctx.tex0;

    if ((Common || tile) && tile->lastTextureState != &state) {
        const uint64_t key = (uint64_t(tex.psm) << 20) | (uint64_t(tex.cpsm) << 26) | (uint64_t(tex.csm) << 32) |
                             (uint64_t(tex.csa) << 33) | (uint64_t(state.texa.ta0) << 38) |
                             (uint64_t(state.texa.ta1) << 46) | (uint64_t(state.texa.aem) << 54);
        if (tile->palette)
            tile->palette->texture(key);
        tile->lastTextureState = &state;
    }
    const int texW = state.textureWidth;
    const int texH = state.textureHeight;
    const uint64_t clamp = Common ? (Alpha ? 5 : 0) : ctx.clamp;
    const uint8_t wrapU = static_cast<uint8_t>(clamp & 0x3u);
    const uint8_t wrapV = static_cast<uint8_t>((clamp >> 2) & 0x3u);
    const uint16_t minU = static_cast<uint16_t>((clamp >> 4) & 0x3FFu);
    const uint16_t maxU = static_cast<uint16_t>((clamp >> 14) & 0x3FFu);
    const uint16_t minV = static_cast<uint16_t>((clamp >> 24) & 0x3FFu);
    const uint16_t maxV = static_cast<uint16_t>((clamp >> 34) & 0x3FFu);

    float texUf, texVf;
    if (!Common && state.prim.fst) {
        texUf = static_cast<float>(u) / 16.0f;
        texVf = static_cast<float>(v) / 16.0f;
    } else {
        const float invQ = 1.0f / fabsQ(q);
        texUf = s * invQ * static_cast<float>(texW);
        texVf = t * invQ * static_cast<float>(texH);
    }

    auto decodeTexel = [&](u32 out) GS_CPU_LAMBDA_INLINE -> uint32_t {
        if constexpr (Common)
            return (*tile->clut32)[static_cast<uint8_t>(out)];
        switch (tex.psm) {
            case GS_PSM_CT32:
            case GS_PSM_Z32:
            case GS_PSM_CT24:
            case GS_PSM_Z24:
                return applyTexa(state.texa, tex.psm, out);
            case GS_PSM_CT16:
            case GS_PSM_CT16S:
            case GS_PSM_Z16:
            case GS_PSM_Z16S:
                return applyTexa(state.texa, tex.psm, Rgba5551ToRgba8888(out));
            case GS_PSM_T8:
            case GS_PSM_T8H:
            case GS_PSM_T4:
            case GS_PSM_T4HL:
            case GS_PSM_T4HH: {
                if ((Common || tile) && tile->clut32 && tex.cpsm == GS_PSM_CT32 && tex.csa == 0)
                    return (*tile->clut32)[static_cast<uint8_t>(out)];
                auto* palette = tile ? tile->palette : nullptr;
                if (!palette)
                    return LookupCLUT(state, static_cast<u8>(out), tex.cpsm, tex.csm, tex.csa, tex.psm);
                auto& entry = palette->entry(static_cast<u8>(out));
                uint32_t color;
                if (!palette->read(entry, color)) {
                    color = LookupCLUT(state, static_cast<u8>(out), tex.cpsm, tex.csm, tex.csa, tex.psm);
                    palette->write(entry, color);
                }
                return color;
            }
        }
        return 0xFFFF00FFu;
    };
    const auto* replacementImage = tile ? tile->replacement : g_replacement;
    const auto bounded = [](uint8_t mode, unsigned low, unsigned high, unsigned size) {
        return mode <= 1 || (mode == 2 ? low <= high && high < size : (low | high) < size);
    };
    const bool replacementSafe =
        replacementImage && bounded(wrapU, minU, maxU, texW) && bounded(wrapV, minV, maxV, texH);
    auto samplePoint = [&](int sampleU, int sampleV) GS_CPU_LAMBDA_INLINE -> uint32_t {
        sampleU = wrapTextureCoordinate(sampleU, texW, wrapU, minU, maxU);
        sampleV = wrapTextureCoordinate(sampleV, texH, wrapV, minV, maxV);
        if (Common || tile) {
            tile->lastU = static_cast<uint32_t>(sampleU);
            tile->lastV = static_cast<uint32_t>(sampleV);
        }
        const auto raw = GSMem::ReadTexture(textureCache, m_vram, tex.psm, tex.tbp0, tex.tbw, sampleU, sampleV);
        return replacementSafe ? 0 : decodeTexel(raw);
    };

    const auto replace = [&](uint32_t native) GS_CPU_LAMBDA_INLINE -> uint32_t {
        const auto* image = replacementImage;
        if (!image)
            return native;
        const int scaleX = int(image->scaleX), scaleY = int(image->scaleY);
        const auto wrap = [&](int coordinate, int size, int scale, uint8_t mode, uint16_t low, uint16_t high) {
            if (mode == 0)
                return int(uint32_t(coordinate) & uint32_t(size - 1));
            if (mode == 1)
                return clampInt(coordinate, 0, size - 1);
            if (mode == 2)
                return clampInt(coordinate, int(low) * scale, (int(high) + 1) * scale - 1);
            const int cell = int(std::floor(float(coordinate) / float(scale)));
            return ((cell & low) | high) * scale + coordinate - cell * scale;
        };
        const auto point = [&](int ix, int iy) {
            ix = wrap(ix, int(image->width), scaleX, wrapU, minU, maxU);
            iy = wrap(iy, int(image->height), scaleY, wrapV, minV, maxV);
            // Region-repeat can address outside the nominal texture. Retain
            // native behavior rather than reading outside a replacement.
            if (ix < 0 || iy < 0 || ix >= int(image->width) || iy >= int(image->height))
                return native;
            return image->pixels[size_t(iy) * image->width + ix];
        };
        const float su = texUf * scaleX, sv = texVf * scaleY;
        if (!state.linearFilter) {
            if (tile ? tile->preparedNative : g_preparedNative)
                return point(int(su), int(sv));
            return point(int(std::floor(su)), int(std::floor(sv)));
        }
        const float bu = su - 0.5f, bv = sv - 0.5f;
        const int ix = int(std::floor(bu)), iy = int(std::floor(bv));
        return gs_cpu::bilinear(point(ix, iy), point(ix + 1, iy), point(ix, iy + 1), point(ix + 1, iy + 1), bu - ix,
                                bv - iy);
    };
    if (!Common && !state.linearFilter) {
        return replace(samplePoint(static_cast<int>(texUf), static_cast<int>(texVf)));
    }

    const float sampleU = texUf - 0.5f;
    const float sampleV = texVf - 0.5f;
    const int u0 = static_cast<int>(std::floor(sampleU));
    const int v0 = static_cast<int>(std::floor(sampleV));
    const int u1 = u0 + 1;
    const int v1 = v0 + 1;
    const float fx = sampleU - static_cast<float>(u0);
    const float fy = sampleV - static_cast<float>(v0);

    if (Common || tile) {
        const int x0 = wrapTextureCoordinate(u0, texW, wrapU, minU, maxU);
        const int x1 = wrapTextureCoordinate(u1, texW, wrapU, minU, maxU);
        const int y0 = wrapTextureCoordinate(v0, texH, wrapV, minV, maxV);
        const int y1 = wrapTextureCoordinate(v1, texH, wrapV, minV, maxV);
        tile->lastU = static_cast<uint32_t>(x1);
        tile->lastV = static_cast<uint32_t>(y1);
        // Immutable scopes replay the final observed cache address on exit.
        // A ready HD image does not require decoding the discarded native quad.
        if (replacementSafe)
            return replace(0);
        const auto raw =
            GSMem::ReadImmutableQuad(m_vram, Common ? GS_PSM_T8 : tex.psm, tex.tbp0, tex.tbw, x0, y0, x1, y1);
        return replace(gs_cpu::bilinear(decodeTexel(raw[0]), decodeTexel(raw[1]), decodeTexel(raw[2]),
                                        decodeTexel(raw[3]), fx, fy));
    }
    const uint32_t c00 = samplePoint(u0, v0);
    const uint32_t c10 = samplePoint(u1, v0);
    const uint32_t c01 = samplePoint(u0, v1);
    const uint32_t c11 = samplePoint(u1, v1);

    if (replacementSafe)
        return replace(0);
    return replace(gs_cpu::bilinear(c00, c10, c01, c11, fx, fy));
}

bool GSCpuBackend::QueueDraw(const GSPrimitiveBatch& batch) {
    const auto type = batch.state.prim.type;
    const bool sprite = type == GS_PRIM_SPRITE;
    bool gpuBatch = false;
#if defined(PS2X_HAVE_PORTABLE_GPU)
    const char* gpu = std::getenv("DRAKAN_GPU_RASTER");
    gpuBatch = gpu && std::strcmp(gpu, "1") == 0 && !m_gpuFailed && gs_gpu::supported(batch);
#endif
    if ((m_rasterThreads < 2 && !gpuBatch) ||
        (!sprite && type != GS_PRIM_TRIANGLE && type != GS_PRIM_TRISTRIP && type != GS_PRIM_TRIFAN))
        return false;
    if (batch.vertexCount < (sprite ? 2 : 3))
        return false;
    const auto& ctx = batch.state.context;
    if (ctx.scissor.x0 > ctx.scissor.x1 || ctx.scissor.y0 > ctx.scissor.y1)
        return false;
    float x0 = batch.vertices[0].x, x1 = x0, y0 = batch.vertices[0].y, y1 = y0;
    for (unsigned i = 1; i < (sprite ? 2u : 3u); ++i) {
        x0 = std::min(x0, batch.vertices[i].x);
        x1 = std::max(x1, batch.vertices[i].x);
        y0 = std::min(y0, batch.vertices[i].y);
        y1 = std::max(y1, batch.vertices[i].y);
    }
    const int ofx = ctx.xyoffset.ofx >> 4, ofy = ctx.xyoffset.ofy >> 4;
    std::array<int, 4> bounds{clampInt(static_cast<int>(std::floor(x0)) - ofx, ctx.scissor.x0, ctx.scissor.x1),
                              clampInt(static_cast<int>(std::ceil(x1)) - ofx, ctx.scissor.x0, ctx.scissor.x1),
                              clampInt(static_cast<int>(std::floor(y0)) - ofy, ctx.scissor.y0, ctx.scissor.y1),
                              clampInt(static_cast<int>(std::ceil(y1)) - ofy, ctx.scissor.y0, ctx.scissor.y1)};
    const unsigned rounding = rasterRounding();
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (!m_pendingDraws.empty())
        if (const char* gpu = std::getenv("DRAKAN_GPU_RASTER");
            gpu && std::strcmp(gpu, "1") == 0 &&
            gs_gpu::supported(batch) != gs_gpu::supported(m_pendingDraws.front().batch))
            FlushDrawsUnlocked();
#endif
    if (!m_pendingDraws.empty()) {
        const auto& previous = m_pendingDraws.front().batch.state.context;
        const auto usesDepth = [](const GSContext& context) {
            return !context.zbuf.zmask || ((context.test >> 17) & 3) >= 2;
        };
        if (previous.frame.fbp != ctx.frame.fbp || previous.frame.fbw != ctx.frame.fbw ||
            previous.frame.psm != ctx.frame.psm || previous.zbuf.zbp != ctx.zbuf.zbp ||
            previous.zbuf.psm != ctx.zbuf.psm || usesDepth(previous) != usesDepth(ctx) ||
            (m_pendingRounding & 0xffc0u) != (rounding & 0xffc0u))
            FlushDrawsUnlocked();
    }
#if defined(PS2X_HAVE_PORTABLE_GPU)
    // A target/rounding switch above may just have deferred the source image.
    // Resolve it before inspecting VRAM or capturing this draw's texture.
    if (m_gpuDeferred && m_gpuDeferred->touchesSource(batch.state)) {
        FlushDrawsUnlocked();
        FlushGpuUnlocked(GpuFenceReason::Source);
        return QueueDraw(batch);
    }
#endif
    // Destination storage and depth usage are fixed for the pending run. Only
    // distinct texture address ranges need rechecking when its bounds expand.
    const auto sameSource = [](const GSDrawState& a, const GSDrawState& b) {
        if (a.prim.tme != b.prim.tme)
            return false;
        if (!a.prim.tme)
            return true;
        const auto& ta = a.context.tex0;
        const auto& tb = b.context.tex0;
        return a.textureWidth == b.textureWidth && a.textureHeight == b.textureHeight &&
               a.context.clamp == b.context.clamp && ta.psm == tb.psm && ta.tbp0 == tb.tbp0 && ta.tbw == tb.tbw;
    };
    auto combined = bounds;
    bool checked = false;
    if (!m_pendingDraws.empty()) {
        combined[0] = std::min(combined[0], m_pendingBounds[0]);
        combined[1] = std::max(combined[1], m_pendingBounds[1]);
        combined[2] = std::min(combined[2], m_pendingBounds[2]);
        combined[3] = std::max(combined[3], m_pendingBounds[3]);
        bool safe = CanTile(batch.state, combined[0], combined[1], combined[2], combined[3], !m_pendingTextured);
        if (safe && combined != m_pendingBounds)
            for (const auto source : m_pendingSources)
                if (!CanTile(m_pendingDraws[source].batch.state, combined[0], combined[1], combined[2], combined[3],
                             !m_pendingTextured)) {
                    safe = false;
                    break;
                }
        if (!safe) {
            FlushDrawsUnlocked();
            combined = bounds;
        } else {
            checked = true;
        }
    }
    if (!checked && !CanTile(batch.state, combined[0], combined[1], combined[2], combined[3], !m_pendingTextured))
        return false;
    if (!m_tileWorkers)
        m_tileWorkers = gs_cpu::sharedTileWorkers(m_rasterThreads);
    if (m_tileWorkers->size() < 2 && !gpuBatch)
        return false;
    if (m_pendingDraws.empty()) {
        m_pendingRounding = rounding;
        m_pendingSequence = m_pendingCacheSequence = 0;
    }
    if (m_pendingPalettes.empty() || m_pendingPaletteGeneration != m_clutGeneration ||
        m_pendingPalettes.back().sequence != m_pendingCacheSequence) {
        m_pendingPalettes.push_back({m_clut, {}, m_texturePageCache, m_pendingCacheSequence});
        auto& packed = m_pendingPalettes.back().rgba32;
        for (unsigned i = 0; i < packed.size(); ++i)
            packed[i] = uint32_t(m_clut[i]) | (uint32_t(m_clut[i + 256]) << 16);
        m_pendingPaletteGeneration = m_clutGeneration;
    }
    if (std::none_of(m_pendingSources.begin(), m_pendingSources.end(),
                     [&](uint32_t index) { return sameSource(batch.state, m_pendingDraws[index].batch.state); }))
        m_pendingSources.push_back(static_cast<uint32_t>(m_pendingDraws.size()));
    std::shared_ptr<const gsr::PackImage> replacement;
    if (m_texturePack && batch.state.prim.tme) {
        bool reused = false;
        for (auto it = m_pendingDraws.rbegin(); it != m_pendingDraws.rend(); ++it) {
            const auto& a = batch.state;
            const auto& b = it->batch.state;
            if (sameSource(a, b) && it->palette == m_pendingPalettes.size() - 1 &&
                a.context.tex0.cpsm == b.context.tex0.cpsm && a.context.tex0.csa == b.context.tex0.csa &&
                a.context.tex0.csm == b.context.tex0.csm && a.texa.ta0 == b.texa.ta0 && a.texa.ta1 == b.texa.ta1 &&
                a.texa.aem == b.texa.aem) {
                replacement = it->replacement;
                reused = true;
                break;
            }
        }
        if (!reused)
            replacement = PrepareReplacement(batch.state);
    }
    m_pendingBounds = combined;
    m_pendingArea += uint64_t(bounds[1] - bounds[0] + 1) * (bounds[3] - bounds[2] + 1);
    m_pendingDraws.push_back({batch, bounds[2], bounds[3], sprite, bounds[0], bounds[1],
                              static_cast<uint32_t>(m_pendingPalettes.size() - 1), ++m_pendingSequence,
                              std::move(replacement)});
    m_pendingTextured |= batch.state.prim.tme;
    static const uint64_t areaLimit = [] {
        const char* value = std::getenv("PS2X_CPU_RASTER_BATCH_AREA");
        uint64_t defaultArea = 1048576ull;
#if defined(PS2X_HAVE_PORTABLE_GPU)
        if (const char* gpu = std::getenv("DRAKAN_GPU_RASTER"); gpu && std::strcmp(gpu, "1") == 0)
            defaultArea = 16777216ull;
#endif
        return value ? std::clamp(std::strtoull(value, nullptr, 10), 65536ull, 16777216ull) : defaultArea;
    }();
    static const size_t drawLimit = [] {
#if defined(PS2X_HAVE_PORTABLE_GPU)
        if (const char* gpu = std::getenv("DRAKAN_GPU_RASTER"); gpu && std::strcmp(gpu, "1") == 0) {
            const char* value = std::getenv("DRAKAN_GPU_BATCH_DRAWS");
            return size_t(value ? std::clamp(std::strtoul(value, nullptr, 10), 2048ul, 32768ul) : 16384ul);
        }
#endif
        return size_t(2048);
    }();
    if (m_pendingDraws.size() >= (m_gpuFailed ? size_t(2048) : drawLimit) ||
        m_pendingArea >= (m_gpuFailed ? 1048576ull : areaLimit))
        FlushDrawsUnlocked();
    return true;
}

void GSCpuBackend::FlushDrawsUnlocked() {
    if (m_pendingDraws.empty())
        return;
    std::vector<PendingDraw> draws;
    draws.swap(m_pendingDraws);
    m_pendingSources.clear();
    std::vector<PendingPalette> palettes;
    palettes.swap(m_pendingPalettes);
    const auto latestCache = m_texturePageCache;
    const uint64_t lastCacheSequence = m_pendingCacheSequence;

    const auto bounds = m_pendingBounds;
    const auto area = m_pendingArea;
    const bool textured = m_pendingTextured;
    m_pendingArea = 0;
    m_pendingTextured = false;
#if defined(PS2X_HAVE_PORTABLE_GPU)
    const char* gpuSetting = std::getenv("DRAKAN_GPU_RASTER");
    bool gpuComplete = false;
    if (gpuSetting && std::strcmp(gpuSetting, "1") == 0) {
        static unsigned reports = 0;
        if (reports++ < 16) {
            auto rejected =
                std::find_if(draws.begin(), draws.end(), [](const auto& d) { return !gs_gpu::supported(d.batch); });
            const auto& s = (rejected == draws.end() ? draws.front() : *rejected).batch.state;
            std::fprintf(stderr,
                         "[portable-gpu] candidate draws=%zu area=%llu rounding=%x rejected=%d frame=%u depth=%u "
                         "mask=%d aa=%d dthe=%llu scan=%llu fst=%d linear=%d clamp=%llx\n",
                         draws.size(), static_cast<unsigned long long>(area), m_pendingRounding,
                         rejected != draws.end(), unsigned(s.context.frame.psm), unsigned(s.context.zbuf.psm),
                         s.context.zbuf.zmask, s.prim.aa1, static_cast<unsigned long long>(s.dthe),
                         static_cast<unsigned long long>(s.scanmsk), s.prim.fst, s.linearFilter,
                         static_cast<unsigned long long>(s.context.clamp));
        }
    }
    if (!m_gpuFailed && gpuSetting && std::strcmp(gpuSetting, "1") == 0 && (m_pendingRounding & 0x6000u) == 0 &&
        ((draws.size() > 1 && area >= 4096) || gs_gpu::configuredScale() > 1 ||
         (m_gpuDeferred && !m_gpuDeferred->empty())) &&
        (!textured || m_texturePageCache.MatchesVram(m_vram)) &&
        std::all_of(draws.begin(), draws.end(), [](const auto& d) { return gs_gpu::supported(d.batch); })) {
        std::vector<gs_gpu::Draw> commands;
        commands.reserve(draws.size());
        size_t decodedBytes = 0;
        struct RestoreClut {
            std::array<uint16_t, 512>& target;
            std::array<uint16_t, 512> saved;
            ~RestoreClut() { target = saved; }
        } restore{m_clut, m_clut};
        for (const auto& draw : draws) {
            const auto& state = draw.batch.state;
            const auto& tex = state.context.tex0;
            gs_gpu::Draw command{&draw.batch, {draw.minX, draw.maxX, draw.minY, draw.maxY}};
            if (state.prim.tme) {
                if (draw.replacement) {
                    command.replacement = true;
                    command.image = draw.replacement;
                    command.texture = draw.replacement->pixels;
                    command.width = draw.replacement->width;
                    command.height = draw.replacement->height;
                    command.scaleX = draw.replacement->scaleX;
                    command.scaleY = draw.replacement->scaleY;
                } else {
                    const auto found = std::find_if(commands.begin(), commands.end(), [&](const auto& previous) {
                        const auto& old = previous.primitive->state;
                        const auto& t = old.context.tex0;
                        const auto& oldDraw = draws[size_t(&previous - commands.data())];
                        return old.prim.tme && old.textureWidth == state.textureWidth &&
                               old.textureHeight == state.textureHeight && oldDraw.palette == draw.palette &&
                               t.psm == tex.psm && t.tbp0 == tex.tbp0 && t.tbw == tex.tbw && t.cpsm == tex.cpsm &&
                               t.csm == tex.csm && t.csa == tex.csa && old.texa.ta0 == state.texa.ta0 &&
                               old.texa.ta1 == state.texa.ta1 && old.texa.aem == state.texa.aem && !oldDraw.replacement;
                    });
                    command.width = state.textureWidth;
                    command.height = state.textureHeight;
                    if (found != commands.end()) {
                        command.texture = found->texture;
                        command.image = found->image;
                    } else {
                        m_clut = palettes[draw.palette].colors;
                        auto image = DecodeTextureSource(state, palettes[draw.palette].cache);
                        if (!image)
                            break;
                        decodedBytes += image->pixels.size() * sizeof(uint32_t);
                        if (decodedBytes > 128u * 1024u * 1024u)
                            break;
                        command.texture = image->pixels;
                        command.image = std::move(image);
                    }
                }
            }
            commands.push_back(command);
        }
        if (commands.size() == draws.size()) {
            if (!m_gpuDeferred)
                m_gpuDeferred = std::make_shared<gs_gpu::Deferred>();
            gpuComplete = m_gpuDeferred->append(commands, m_pendingRounding);
            if (!gpuComplete) {
                FlushGpuUnlocked(GpuFenceReason::Target);
                if (!m_gpuFailed) {
                    gpuComplete = m_gpuDeferred->append(commands, m_pendingRounding);
                    if (!gpuComplete) {
                        FloatControlScope rounding(m_pendingRounding);
                        if (!m_highResolution && gs_gpu::configuredScale() > 1)
                            m_highResolution = std::make_shared<gs_gpu::HighResolutionCache>();
                        auto* surface = m_highResolution
                                            ? m_highResolution->acquire(commands.front().primitive->state.context)
                                            : nullptr;
                        gpuComplete = gs_gpu::rasterize(commands, bounds, m_vram, nullptr, surface);
                        if (gpuComplete && surface)
                            m_highResolution->reconcile(
                                *surface, std::any_of(commands.begin(), commands.end(), [](const auto& draw) {
                                    return !draw.primitive->state.context.zbuf.zmask;
                                }));
                        m_gpuFailed = !gpuComplete;
                    }
                }
            }
        }
        if (gpuComplete) {
            // Reconcile the final sampled guest cache page without repeating
            // fragment writes. Texture lookup itself retains native addressing.
            m_texturePageCache = latestCache;
            for (auto it = draws.rbegin(); it != draws.rend(); ++it) {
                if (!it->batch.state.prim.tme || it->sequence <= lastCacheSequence)
                    continue;
                const auto& state = it->batch.state;
                const auto& c = state.context;
                const auto& v = it->batch.vertices;
                bool sampled = false;
                m_clut = palettes[it->palette].colors;
                m_texturePageCache = palettes[it->palette].cache;
                const int ox = c.xyoffset.ofx >> 4, oy = c.xyoffset.ofy >> 4;
                if (it->sprite) {
                    const int x0 = std::min(int(v[0].x), int(v[1].x)) - ox;
                    const int y0 = std::min(int(v[0].y), int(v[1].y)) - oy;
                    const int sx = std::max(1, std::abs(int(v[1].x) - int(v[0].x)));
                    const int sy = std::max(1, std::abs(int(v[1].y) - int(v[0].y)));
                    const int x = std::min(int(c.scissor.x1), x0 + sx - 1);
                    const int y = std::min(int(c.scissor.y1), y0 + sy - 1);
                    if (x >= std::max(x0, int(c.scissor.x0)) && y >= std::max(y0, int(c.scissor.y0))) {
                        const float tx = (float(x - x0) + 0.5f) / float(sx), ty = (float(y - y0) + 0.5f) / float(sy);
                        const float u0 =
                            state.prim.fst ? float(v[0].u >> 4) : v[0].s / fabsQ(v[0].q) * state.textureWidth;
                        const float u1 =
                            state.prim.fst ? float(v[1].u >> 4) : v[1].s / fabsQ(v[1].q) * state.textureWidth;
                        const float t0 =
                            state.prim.fst ? float(v[0].v >> 4) : v[0].t / fabsQ(v[0].q) * state.textureHeight;
                        const float t1 =
                            state.prim.fst ? float(v[1].v >> 4) : v[1].t / fabsQ(v[1].q) * state.textureHeight;
                        const float u = u0 + (u1 - u0) * tx, t = t0 + (t1 - t0) * ty;
                        SampleTexture(state, u / state.textureWidth, t / state.textureHeight, 1.0f,
                                      uint16_t(clampInt(int(u * 16 + 0.5f), 0, 65535)),
                                      uint16_t(clampInt(int(t * 16 + 0.5f), 0, 65535)), x, y);
                        sampled = true;
                    }
                } else {
                    const float x0 = v[0].x - ox, y0 = v[0].y - oy, x1 = v[1].x - ox, y1 = v[1].y - oy;
                    const float x2 = v[2].x - ox, y2 = v[2].y - oy;
                    const float denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
                    if (std::fabs(denom) >= 0.001f) {
                        const auto attr = [&](const GSVertex& a) {
                            return state.prim.fst ? std::array<float, 4>{float(a.u), float(a.v), 1, float(a.fog)}
                                                  : std::array<float, 4>{a.s, a.t, a.q, float(a.fog)};
                        };
                        gs_cpu::Interpolator4 attributes(attr(v[0]), attr(v[1]), attr(v[2]));
                        for (int y = it->maxY; y >= it->minY && !sampled; --y)
                            for (int x = it->maxX; x >= it->minX; --x) {
                                const auto w =
                                    gs_cpu::triangleWeights(x, float(y) + 0.5f, x2, y2, y1 - y2, x2 - x1, y2 - y0,
                                                            x0 - x2, denom < 0 ? -1.0f : 1.0f, 1.0f / std::fabs(denom));
                                if (!(w.mask & 1))
                                    continue;
                                const auto a = attributes.values(w.w0[0], w.w1[0], w.w2[0]);
                                SampleTexture(state, a[0], a[1], a[2], uint16_t(a[0]), uint16_t(a[1]), x, y);
                                sampled = true;
                                break;
                            }
                    }
                }
                if (sampled)
                    break;
            }
        }
    }
    if (gpuComplete) {
        draws.clear();
        palettes.clear();
        m_pendingDraws.swap(draws);
        m_pendingPalettes.swap(palettes);
        return;
    }
#endif
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred && !m_gpuDeferred->empty() && std::getenv("DRAKAN_FRAME_METRICS")) {
        static thread_local unsigned reports = 0;
        if (reports++ < 16 || (reports & 255u) == 0) {
            const auto rejected =
                std::find_if(draws.begin(), draws.end(), [](const auto& d) { return !gs_gpu::supported(d.batch); });
            const auto& s = (rejected == draws.end() ? draws.front() : *rejected).batch.state;
            std::fprintf(stderr,
                         "[portable-gpu-cpu] queued draws=%zu area=%llu rejected=%d cache=%d rounding=%x "
                         "frame=%u depth=%u zmask=%d test=%llx fst=%d linear=%d aa=%d scan=%llu clamp=%llx\n",
                         draws.size(), static_cast<unsigned long long>(area), rejected != draws.end(),
                         !textured || m_texturePageCache.MatchesVram(m_vram), m_pendingRounding,
                         unsigned(s.context.frame.psm), unsigned(s.context.zbuf.psm), s.context.zbuf.zmask,
                         static_cast<unsigned long long>(s.context.test), s.prim.fst, s.linearFilter, s.prim.aa1,
                         static_cast<unsigned long long>(s.scanmsk), static_cast<unsigned long long>(s.context.clamp));
        }
    }
#endif
    FlushGpuUnlocked(GpuFenceReason::Cpu); // Preserve ordering before any CPU fragment reads/writes.
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_highResolution)
        for (const auto& draw : draws)
            m_highResolution->invalidateDraw(draw.batch);
#endif
    // Keep tiny runs immediate and preserve retained stale-page behavior.
    if (m_rasterThreads < 2 || !m_tileWorkers || draws.size() == 1 || area < 4096 ||
        (textured && !m_texturePageCache.MatchesVram(m_vram))) {
        FloatControlScope rounding(m_pendingRounding);
        struct RestorePalette {
            std::array<uint16_t, 512>& target;
            std::array<uint16_t, 512> saved;
            ~RestorePalette() { target = saved; }
        } restore{m_clut, m_clut};
        uint64_t paletteSequence = UINT64_MAX;
        for (const auto& draw : draws) {
            const auto& palette = palettes[draw.palette];
            m_clut = palette.colors;
            if (paletteSequence != palette.sequence) {
                m_texturePageCache = palette.cache;
                paletteSequence = palette.sequence;
            }
            DrawPrimitive(draw.batch, draw.replacement);
        }
        if (lastCacheSequence > draws.back().sequence)
            m_texturePageCache = latestCache;
    } else {
        auto& workers = *m_tileWorkers;
        const unsigned count = std::min({m_rasterThreads, workers.size(), unsigned(bounds[3] - bounds[2] + 1)});
        // Small bands let idle workers take more work when screen coverage is uneven.
        // Each band retains primitive order and owns disjoint framebuffer/depth rows.
        const int height = bounds[3] - bounds[2] + 1;
        const unsigned gridThreads = std::min(count, 8u);
        const int rows = std::min(32, std::max(1, (height + int(gridThreads * 2) - 1) / int(gridThreads * 2)));
        const int width = bounds[1] - bounds[0] + 1;
        static const unsigned columnsLimit = [] {
            const char* value = std::getenv("PS2X_CPU_RASTER_COLUMNS");
            return value ? unsigned(std::clamp(std::strtol(value, nullptr, 10), 1l, 4l)) : 4u;
        }();
        const unsigned columns = std::min(columnsLimit, unsigned((width + 63) / 64));
        const unsigned tileCount = unsigned((height + rows - 1) / rows) * columns;
        m_rasterTiles.resize(tileCount);
        auto& tiles = m_rasterTiles;
        for (unsigned i = 0; i < tileCount; ++i) {
            tiles[i].firstColumn = bounds[0] + width * int(i % columns) / int(columns);
            tiles[i].lastColumn = bounds[0] + width * int(i % columns + 1) / int(columns) - 1;
            tiles[i].firstRow = bounds[2] + rows * int(i / columns);
            tiles[i].lastRow = std::min(bounds[3], tiles[i].firstRow + rows - 1);
            tiles[i].textureCache.Invalidate();
            tiles[i].sampledTexture = false;
            tiles[i].batchIndex = tiles[i].lastSampleBatch = 0;
        }
        std::atomic<unsigned> nextTile{0};
        ++gs_cpu::tileDispatches;
        gs_cpu::tilePrimitives += draws.size();
        workers.run([&](unsigned index) {
            if (index >= count)
                return;
            for (;;) {
                const unsigned tileIndex = nextTile.fetch_add(1, std::memory_order_relaxed);
                if (tileIndex >= tileCount)
                    break;
                auto& tile = tiles[tileIndex];
                TileBinding binding(tile, m_pendingRounding, m_vram);
                for (size_t i = 0; i < draws.size(); ++i) {
                    const auto& draw = draws[i];
                    if (draw.minY > tile.lastRow || draw.maxY < tile.firstRow || draw.minX > tile.lastColumn ||
                        draw.maxX < tile.firstColumn)
                        continue;
                    const auto* palette = &palettes[draw.palette].colors;
                    if (tile.clut != palette) {
                        tile.clut = palette;
                        tile.clut32 = &palettes[draw.palette].rgba32;
                        if (tile.palette)
                            tile.palette->reset();
                    }
                    tile.batchIndex = draw.sequence;
                    tile.replacement = draw.replacement.get();
                    if (draw.sprite)
                        DrawSprite(draw.batch);
                    else
                        DrawTriangle(draw.batch);
                }
            }
        });
        const gs_cpu::TileContext* last = nullptr;
        for (unsigned i = 0; i < tileCount; ++i)
            if (tiles[i].sampledTexture &&
                (!last || tiles[i].lastSampleBatch > last->lastSampleBatch ||
                 (tiles[i].lastSampleBatch == last->lastSampleBatch &&
                  (tiles[i].lastSampleY > last->lastSampleY ||
                   (tiles[i].lastSampleY == last->lastSampleY && tiles[i].lastSampleX > last->lastSampleX)))))
                last = &tiles[i];
        if (last && last->lastSampleBatch > lastCacheSequence)
            m_texturePageCache = last->textureCache;
        else
            m_texturePageCache = latestCache;
    }
    draws.clear();
    palettes.clear();
    m_pendingDraws.swap(draws);
    m_pendingPalettes.swap(palettes);
}

bool GSCpuBackend::CanTile(const GSDrawState& state, int minX, int maxX, int minY, int maxY, bool checkCache) const {
    const auto& ctx = state.context;
    const bool usesDepth = !ctx.zbuf.zmask || ((ctx.test >> 17) & 3) >= 2;
    // True-color pixels occupy independent 16/32-bit stores. Nibble-packed
    // writes, wrapped pages, and texture feedback retain serial ordering.
    const bool color16 = ctx.frame.psm == GS_PSM_CT16 || ctx.frame.psm == GS_PSM_CT16S;
    const bool depth16 = ctx.zbuf.psm == GS_PSM_Z16 || ctx.zbuf.psm == GS_PSM_Z16S;
    if ((!color16 && ctx.frame.psm != GS_PSM_CT32 && ctx.frame.psm != GS_PSM_CT24) ||
        (usesDepth && !depth16 && ctx.zbuf.psm != GS_PSM_Z32 && ctx.zbuf.psm != GS_PSM_Z24) || minX < 0 || minY < 0 ||
        maxX >= int(std::max<uint32_t>(ctx.frame.fbw, 1) * 64) || maxY >= 2048)
        return false;
    const uint64_t colorSpan =
        (uint64_t(maxY / (color16 ? 64 : 32)) * std::max<uint32_t>(ctx.frame.fbw, 1) + maxX / 64 + 1) * 8192;
    const uint64_t depthSpan =
        (uint64_t(maxY / (depth16 ? 64 : 32)) * std::max<uint32_t>(ctx.frame.fbw, 1) + maxX / 64 + 1) * 8192;
    const uint64_t colorStart = uint64_t(ctx.frame.fbp) * 8192, depthStart = uint64_t(ctx.zbuf.zbp) * 8192;
    const auto overlaps = [](uint64_t a, uint64_t aSize, uint64_t b, uint64_t bSize) {
        return a < b + bSize && b < a + aSize;
    };
    if (colorStart + colorSpan > GSMem::MEMORY_SIZE ||
        (usesDepth &&
         (depthStart + depthSpan > GSMem::MEMORY_SIZE || overlaps(colorStart, colorSpan, depthStart, depthSpan))))
        return false;
    if (state.prim.tme) {
        const auto& tex = ctx.tex0;
        if (!state.textureWidth || !state.textureHeight || (checkCache && !m_texturePageCache.MatchesVram(m_vram)))
            return false;
        unsigned pageW = 64, pageH = 32;
        switch (tex.psm) {
            case GS_PSM_T8:
                pageW = 128;
                pageH = 64;
                break;
            case GS_PSM_T4:
                pageW = 128;
                pageH = 128;
                break;
            case GS_PSM_CT16:
            case GS_PSM_CT16S:
            case GS_PSM_Z16:
            case GS_PSM_Z16S:
                pageH = 64;
                break;
            case GS_PSM_CT32:
            case GS_PSM_CT24:
            case GS_PSM_Z32:
            case GS_PSM_Z24:
            case GS_PSM_T8H:
            case GS_PSM_T4HL:
            case GS_PSM_T4HH:
                break;
            default:
                return false;
        }
        const auto maximumCoordinate = [](unsigned size, unsigned mode, unsigned low, unsigned high) {
            return mode == 2 ? high : mode == 3 ? (low | high) : size - 1;
        };
        const unsigned maxU =
            maximumCoordinate(state.textureWidth, ctx.clamp & 3, (ctx.clamp >> 4) & 1023, (ctx.clamp >> 14) & 1023);
        const unsigned maxV = maximumCoordinate(state.textureHeight, (ctx.clamp >> 2) & 3, (ctx.clamp >> 24) & 1023,
                                                (ctx.clamp >> 34) & 1023);
        const uint64_t rows = maxV / pageH + 1;
        const uint64_t columns = maxU / pageW + 1;
        const uint64_t textureStart = (uint64_t(tex.tbp0) / 32) * 8192;
        const uint64_t textureSpan = ((rows - 1) * (uint64_t(tex.tbw) * 64 / pageW) + columns + 1) * 8192;
        if (textureStart + textureSpan > GSMem::MEMORY_SIZE ||
            overlaps(textureStart, textureSpan, colorStart, colorSpan) ||
            (usesDepth && overlaps(textureStart, textureSpan, depthStart, depthSpan)))
            return false;
    }
    return true;
}

bool GSCpuBackend::DrawTiles(const GSPrimitiveBatch& batch, int minX, int maxX, int minY, int maxY, bool sprite) {
    if (gs_cpu::currentTile || m_rasterThreads < 2 || maxY - minY < 8 ||
        int64_t(maxX - minX + 1) * (maxY - minY + 1) < 4096)
        return false;
    if (!CanTile(batch.state, minX, maxX, minY, maxY))
        return false;
    if (!m_tileWorkers)
        m_tileWorkers = gs_cpu::sharedTileWorkers(m_rasterThreads);
    auto& workers = *m_tileWorkers;
    const unsigned count = std::min(m_rasterThreads, workers.size());
    if (count < 2)
        return false;
    std::array<gs_cpu::TileContext, gs_cpu::kMaxRasterThreads> tiles;
    for (unsigned i = 0; i < count; ++i) {
        tiles[i].firstColumn = minX;
        tiles[i].lastColumn = maxX;
        tiles[i].firstRow = minY + (maxY - minY + 1) * int(i) / int(count);
        tiles[i].lastRow = minY + (maxY - minY + 1) * int(i + 1) / int(count) - 1;
        tiles[i].textureCache.Invalidate();
        tiles[i].replacement = g_replacement;
        tiles[i].preparedNative = g_preparedNative;
    }
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
    const unsigned rounding = _mm_getcsr();
#else
    const unsigned rounding = 0;
#endif
    ++gs_cpu::tileDispatches;
    ++gs_cpu::tilePrimitives;
    workers.run([&](unsigned index) {
        if (index >= count)
            return;
        TileBinding binding(tiles[index], rounding, m_vram);
        if (sprite)
            DrawSprite(batch);
        else
            DrawTriangle(batch);
    });
    // Preserve the cache left by the final sampled pixel in serial row order.
    for (unsigned i = count; i-- > 0;)
        if (tiles[i].sampledTexture) {
            m_texturePageCache = tiles[i].textureCache;
            break;
        }
    return true;
}

void GSCpuBackend::DrawSprite(const GSPrimitiveBatch& __restrict batch) {
    const GSDrawState& state = batch.state;
    const GSVertex& v0 = batch.vertices[0];
    const GSVertex& v1 = batch.vertices[1];
    const auto& ctx = state.context;

    if (v0.x == v1.x || v0.y == v1.y)
        return;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;
    u32 z1 = static_cast<u32>(v1.z);

    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);

    const int unclippedX0 = x0;
    const int unclippedY0 = y0;
    const int spanX = std::max(1, x1 - x0);
    const int spanY = std::max(1, y1 - y0);
    const int unclippedX1 = unclippedX0 + spanX - 1;
    const int unclippedY1 = unclippedY0 + spanY - 1;

    // If the sprite rectangle is fully outside scissor, nothing should render.
    if (unclippedX1 < ctx.scissor.x0 || unclippedX0 > ctx.scissor.x1 || unclippedY1 < ctx.scissor.y0 ||
        unclippedY0 > ctx.scissor.y1)
        return;

    int drawX0 = clampInt(unclippedX0, ctx.scissor.x0, ctx.scissor.x1);
    int drawY0 = clampInt(unclippedY0, ctx.scissor.y0, ctx.scissor.y1);
    int drawX1 = clampInt(unclippedX1, ctx.scissor.x0, ctx.scissor.x1);
    int drawY1 = clampInt(unclippedY1, ctx.scissor.y0, ctx.scissor.y1);
    if (DrawTiles(batch, drawX0, drawX1, drawY0, drawY1, true))
        return;
    if (gs_cpu::currentTile) {
        drawX0 = std::max(drawX0, gs_cpu::currentTile->firstColumn);
        drawX1 = std::min(drawX1, gs_cpu::currentTile->lastColumn);
        drawY0 = std::max(drawY0, gs_cpu::currentTile->firstRow);
        drawY1 = std::min(drawY1, gs_cpu::currentTile->lastRow);
    }

    const uint64_t alphaReg = ctx.alpha;
    const uint8_t alphaMode = static_cast<uint8_t>(alphaReg & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((alphaReg >> 32) & 0xFFu);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

    if (state.prim.tme) {
        const auto& tex = ctx.tex0;
        const int texW = state.textureWidth;
        const int texH = state.textureHeight;

        float u0f, v0f, u1f, v1f;
        if (state.prim.fst) {
            u0f = static_cast<float>(v0.u >> 4);
            v0f = static_cast<float>(v0.v >> 4);
            u1f = static_cast<float>(v1.u >> 4);
            v1f = static_cast<float>(v1.v >> 4);
        } else {
            const float q0 = fabsQ(v0.q);
            const float q1 = fabsQ(v1.q);
            u0f = (v0.s / q0) * static_cast<float>(texW);
            v0f = (v0.t / q0) * static_cast<float>(texH);
            u1f = (v1.s / q1) * static_cast<float>(texW);
            v1f = (v1.t / q1) * static_cast<float>(texH);
        }

        float spriteW = static_cast<float>(spanX);
        float spriteH = static_cast<float>(spanY);
        if (spriteW < 1.0f)
            spriteW = 1.0f;
        if (spriteH < 1.0f)
            spriteH = 1.0f;

        for (int y = drawY0; y <= drawY1; ++y) {
            float ty = (static_cast<float>(y - unclippedY0) + 0.5f) / spriteH;
            float texVf = v0f + (v1f - v0f) * ty;

            for (int x = drawX0; x <= drawX1; ++x) {
                float tx = (static_cast<float>(x - unclippedX0) + 0.5f) / spriteW;
                float texUf = u0f + (u1f - u0f) * tx;
                uint32_t texel = 0xFFFF00FFu;
                if (state.prim.fst) {
                    const int fixedU = static_cast<int>((texUf * 16.0f) + 0.5f);
                    const int fixedV = static_cast<int>((texVf * 16.0f) + 0.5f);
                    const uint16_t sampleU = static_cast<uint16_t>(clampInt(fixedU, 0, 0xFFFF));
                    const uint16_t sampleV = static_cast<uint16_t>(clampInt(fixedV, 0, 0xFFFF));
                    texel = SampleTexture(state, 0.0f, 0.0f, 1.0f, sampleU, sampleV, x, y);
                } else {
                    texel = SampleTexture(state, texUf / static_cast<float>(texW), texVf / static_cast<float>(texH),
                                          1.0f, 0u, 0u, x, y);
                }

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                WritePixel(state, x, y, z1, color.r, color.g, color.b, color.a, v1.fog);
            }
        }
    } else {
        for (int y = drawY0; y <= drawY1; ++y)
            for (int x = drawX0; x <= drawX1; ++x)
                WritePixel(state, x, y, z1, r, g, b, a, v1.fog);
    }
}

void GSCpuBackend::DrawTriangle(const GSPrimitiveBatch& __restrict batch) {
    const auto& state = batch.state;
    const auto& ctx = state.context;
    const auto* tile = gs_cpu::currentTile;
    const bool common = tile && tile->clut32 && state.prim.iip && state.prim.tme && state.prim.fge && state.prim.abe &&
                        !state.prim.fst && !state.pabe && state.linearFilter && ctx.frame.psm == GS_PSM_CT32 &&
                        ctx.frame.fbmsk == 0 && ctx.zbuf.psm == GS_PSM_Z32 && ctx.fba == 0 &&
                        (ctx.alpha & 255) == 0x44 && ctx.tex0.tfx == 0 && ctx.tex0.psm == GS_PSM_T8 &&
                        ctx.tex0.cpsm == GS_PSM_CT32 && ctx.tex0.csa == 0;
    if (common && !ctx.zbuf.zmask && (ctx.test & ~0xff0ull) == 0x5000d && ctx.clamp == 0)
        DrawTriangleImpl<true, false>(batch);
    else if (common && ctx.zbuf.zmask && (ctx.test & ~0xff0ull) == 0x3000d && ctx.clamp == 5)
        DrawTriangleImpl<true, true>(batch);
    else
        DrawTriangleImpl<false, false>(batch);
}

template <bool Common, bool Alpha>
void GSCpuBackend::DrawTriangleImpl(const GSPrimitiveBatch& __restrict batch) {
    const GSDrawState& state = batch.state;
    const GSVertex& v0 = batch.vertices[0];
    const GSVertex& v1 = batch.vertices[1];
    const GSVertex& v2 = batch.vertices[2];
    const auto& ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    float fx0 = v0.x - static_cast<float>(ofx);
    float fy0 = v0.y - static_cast<float>(ofy);
    float fx1 = v1.x - static_cast<float>(ofx);
    float fy1 = v1.y - static_cast<float>(ofy);
    float fx2 = v2.x - static_cast<float>(ofx);
    float fy2 = v2.y - static_cast<float>(ofy);

    int minX = static_cast<int>(std::floor(std::min({fx0, fx1, fx2})));
    int maxX = static_cast<int>(std::ceil(std::max({fx0, fx1, fx2})));
    int minY = static_cast<int>(std::floor(std::min({fy0, fy1, fy2})));
    int maxY = static_cast<int>(std::ceil(std::max({fy0, fy1, fy2})));

    minX = clampInt(minX, ctx.scissor.x0, ctx.scissor.x1);
    maxX = clampInt(maxX, ctx.scissor.x0, ctx.scissor.x1);
    minY = clampInt(minY, ctx.scissor.y0, ctx.scissor.y1);
    maxY = clampInt(maxY, ctx.scissor.y0, ctx.scissor.y1);

    float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
    if (std::fabs(denom) < 0.001f)
        return;

    const float winding = (denom < 0.0f) ? -1.0f : 1.0f;
    const float invAbsDenom = 1.0f / std::fabs(denom);
    if (DrawTiles(batch, minX, maxX, minY, maxY, false))
        return;
    if (gs_cpu::currentTile) {
        minX = std::max(minX, gs_cpu::currentTile->firstColumn);
        maxX = std::min(maxX, gs_cpu::currentTile->lastColumn);
        minY = std::max(minY, gs_cpu::currentTile->firstRow);
        maxY = std::min(maxY, gs_cpu::currentTile->lastRow);
    }

    auto* tile = gs_cpu::currentTile;
    const unsigned depthMethod = Common ? (Alpha ? 1 : 2) : unsigned((ctx.test >> 17) & 3u);
    const bool earlyDepth = Common ? !Alpha : tile && depthMethod != 1;
    const uint32_t depthBase = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    const uint32_t depthWidth = std::max<uint32_t>(ctx.frame.fbw, 1u);
    const bool fastFragment =
        Common || (m_rasterThreads > 1 && state.prim.tme && state.prim.abe && !state.pabe &&
                   ctx.frame.psm == GS_PSM_CT32 && ctx.frame.fbmsk == 0 && ctx.zbuf.psm == GS_PSM_Z32 && ctx.fba == 0 &&
                   ((ctx.test & ~0xff0ull) == 0x5000d || (ctx.test & ~0xff0ull) == 0x3000d) &&
                   (ctx.alpha & 255) == 0x44 && ctx.tex0.tfx == 0);
    const auto frameBase = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const auto alphaReference = static_cast<uint8_t>((ctx.test >> 4) & 255);
    const auto fogColor = pack32(state.fogR, state.fogG, state.fogB, 0);
    std::optional<gs_cpu::Interpolator4> colors, attributes;
    if (Common || (tile && state.prim.iip)) {
        const auto unpack = [](const GSVertex& v) {
            return std::array<float, 4>{float(v.r), float(v.g), float(v.b), float(v.a)};
        };
        colors.emplace(unpack(v0), unpack(v1), unpack(v2));
    }
    if (Common || (tile && (state.prim.tme || state.prim.fge))) {
        const auto unpack = [&](const GSVertex& v) {
            return (!Common && state.prim.fst) ? std::array<float, 4>{float(v.u), float(v.v), 1.0f, float(v.fog)}
                                               : std::array<float, 4>{v.s, v.t, v.q, float(v.fog)};
        };
        attributes.emplace(unpack(v0), unpack(v1), unpack(v2));
    }
    std::optional<gs_cpu::TriangleRowClip> rowClip;
    if (maxX - minX >= 32)
        rowClip.emplace(minX, maxX, fx2, fy2, fy1 - fy2, fx2 - fx1, fy2 - fy0, fx0 - fx2, winding, invAbsDenom);
    for (int y = minY; y <= maxY; ++y) {
        float py = static_cast<float>(y) + 0.5f;
        const auto span = rowClip ? rowClip->row(py) : gs_cpu::RowSpan{minX, maxX};
        for (int blockX = span.first; blockX <= span.last; blockX += 4) {
            const auto weights = gs_cpu::triangleWeights(blockX, py, fx2, fy2, fy1 - fy2, fx2 - fx1, fy2 - fy0,
                                                         fx0 - fx2, winding, invAbsDenom);
            unsigned mask = weights.mask & ((1u << std::min(4, span.last - blockX + 1)) - 1u);
            while (mask) {
                const unsigned lane = std::countr_zero(mask);
                mask &= mask - 1;
                const int x = blockX + int(lane);
                const float w0 = weights.w0[lane], w1 = weights.w1[lane], w2 = weights.w2[lane];

                uint32_t depth = 0;
                if constexpr (!Common || !Alpha) {
                    const double z = v0.z * w0 + v1.z * w1 + v2.z * w2;
                    depth = static_cast<uint32_t>(z + 0.5);
                }
                if (earlyDepth) {
                    const uint32_t stored = Common ? GSMem::ReadZ32(m_vram, depthBase, depthWidth, x, y)
                                            : depthMethod == 0
                                                ? 0
                                                : ReadVramUnlocked(ctx.zbuf.psm, depthBase, depthWidth, x, y);
                    const bool pass = depthMethod == 2 ? depth >= stored : depthMethod == 3 && depth > stored;
                    if (!pass) {
                        if (Common || state.prim.tme) {
                            tile->lastSampleX = x;
                            tile->lastSampleY = y;
                            tile->deferredSample = &batch;
                            tile->sampleW0 = w0;
                            tile->sampleW1 = w1;
                            tile->sampleW2 = w2;
                            tile->sampledTexture = true;
                            tile->lastSampleBatch = tile->batchIndex;
                        }
                        continue;
                    }
                }

                uint8_t r, g, b, a;
                if (colors) {
                    const auto color = colors->color(w0, w1, w2);
                    r = static_cast<uint8_t>(color);
                    g = static_cast<uint8_t>(color >> 8);
                    b = static_cast<uint8_t>(color >> 16);
                    a = static_cast<uint8_t>(color >> 24);
                } else if (state.prim.iip) {
                    r = clampU8(static_cast<int>(v0.r * w0 + v1.r * w1 + v2.r * w2));
                    g = clampU8(static_cast<int>(v0.g * w0 + v1.g * w1 + v2.g * w2));
                    b = clampU8(static_cast<int>(v0.b * w0 + v1.b * w1 + v2.b * w2));
                    a = clampU8(static_cast<int>(v0.a * w0 + v1.a * w1 + v2.a * w2));
                } else {
                    r = v2.r;
                    g = v2.g;
                    b = v2.b;
                    a = v2.a;
                }

                const auto interpolated = attributes ? attributes->values(w0, w1, w2) : std::array<float, 4>{};
                const uint8_t fog = (Common || state.prim.fge)
                                        ? clampU8(static_cast<int>(
                                              attributes ? interpolated[3] : v0.fog * w0 + v1.fog * w1 + v2.fog * w2))
                                        : 0;
                if (Common || state.prim.tme) {
                    float is, it, iq;
                    uint16_t iu, iv;
                    if (!Common && state.prim.fst) {
                        iu = static_cast<uint16_t>(attributes ? interpolated[0] : v0.u * w0 + v1.u * w1 + v2.u * w2);
                        iv = static_cast<uint16_t>(attributes ? interpolated[1] : v0.v * w0 + v1.v * w1 + v2.v * w2);
                        is = 0.0f;
                        it = 0.0f;
                        iq = 1.0f;
                    } else {
                        // The GS DDA interpolates the homogeneous S, T and Q
                        // values. Texel coordinates are calculated from S/Q and
                        // T/Q only after interpolation.
                        is = attributes ? interpolated[0] : v0.s * w0 + v1.s * w1 + v2.s * w2;
                        it = attributes ? interpolated[1] : v0.t * w0 + v1.t * w1 + v2.t * w2;
                        iq = attributes ? interpolated[2] : v0.q * w0 + v1.q * w1 + v2.q * w2;
                        iu = 0;
                        iv = 0;
                    }

                    uint32_t texel = SampleTextureImpl<Common, Alpha>(state, is, it, iq, iu, iv, x, y);
                    if (fastFragment) {
                        const auto color = gs_cpu::modulateFog(pack32(r, g, b, a), texel, ctx.tex0.tcc != 0,
                                                               Common || state.prim.fge, fog, fogColor);
                        GSMem::WriteFragment32(m_vram, frameBase, depthBase, depthWidth, x, y, depth, color,
                                               alphaReference, earlyDepth || depthMethod == 1,
                                               Common ? !Alpha : !ctx.zbuf.zmask);
                        continue;
                    }

                    uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                    uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                    uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                    uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                    const auto& tex = ctx.tex0;
                    const uint8_t shadeR = r;
                    const uint8_t shadeG = g;
                    const uint8_t shadeB = b;
                    const uint8_t shadeA = a;
                    const TextureCombineResult color =
                        combineTexture(tex, shadeR, shadeG, shadeB, shadeA, tr, tg, tb, ta);

                    r = color.r;
                    g = color.g;
                    b = color.b;
                    a = color.a;
                }

                WritePixel(state, x, y, depth, r, g, b, a, fog, earlyDepth);
            }
        }
    }
}

void GSCpuBackend::DrawLine(const GSPrimitiveBatch& batch) {
    const GSDrawState& state = batch.state;
    const GSVertex& v0 = batch.vertices[0];
    const GSVertex& v1 = batch.vertices[1];
    const auto& ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (totalSteps == 0)
        totalSteps = 1;
    int step = 0;

    for (;;) {
        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        uint8_t r, g, b, a;
        if (state.prim.iip) {
            r = clampU8(static_cast<int>(v0.r + (v1.r - v0.r) * t));
            g = clampU8(static_cast<int>(v0.g + (v1.g - v0.g) * t));
            b = clampU8(static_cast<int>(v0.b + (v1.b - v0.b) * t));
            a = clampU8(static_cast<int>(v0.a + (v1.a - v0.a) * t));
        } else {
            r = v1.r;
            g = v1.g;
            b = v1.b;
            a = v1.a;
        }

        double z = (v0.z + (v1.z - v0.z) * t);
        const uint8_t fog = clampU8(static_cast<int>(v0.fog + (v1.fog - v0.fog) * t));
        if (state.prim.tme) {
            // Interpolate homogeneous STQ before dividing, as for triangles.
            // FST selects the independently interpolated 12.4 UV coordinates.
            const float is = v0.s + (v1.s - v0.s) * t;
            const float it = v0.t + (v1.t - v0.t) * t;
            const float iq = v0.q + (v1.q - v0.q) * t;
            const auto iu = uint16_t(float(v0.u) + (float(v1.u) - float(v0.u)) * t);
            const auto iv = uint16_t(float(v0.v) + (float(v1.v) - float(v0.v)) * t);
            const uint32_t texel = SampleTexture(state, is, it, iq, iu, iv, x0, y0);
            const auto color = combineTexture(ctx.tex0, r, g, b, a, uint8_t(texel), uint8_t(texel >> 8),
                                              uint8_t(texel >> 16), uint8_t(texel >> 24));
            r = color.r;
            g = color.g;
            b = color.b;
            a = color.a;
        }
        WritePixel(state, x0, y0, static_cast<u32>(z), r, g, b, a, fog);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
        ++step;
    }
}

void GSCpuBackend::BeginTransfer(const GSTransferCommand& command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_gpuDeferred) {
        const auto& b = command.bitbltbuf;
        const auto& p = command.trxpos;
        const auto& r = command.trxreg;
        const bool reads = command.direction == 1 || command.direction == 2;
        const bool writes = command.direction == 0 || command.direction == 2;
        if ((reads &&
             m_gpuDeferred->touches(gs_gpu::memoryRange(b.spsm, b.sbp, b.sbw, p.ssax, p.ssay, r.rrw, r.rrh))) ||
            (writes && m_gpuDeferred->touches(gs_gpu::memoryRange(b.dpsm, b.dbp, b.dbw, p.dsax, p.dsay, r.rrw, r.rrh))))
            FlushGpuUnlocked(GpuFenceReason::Transfer);
    }
#endif
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_highResolution && (command.direction == 0 || command.direction == 2)) {
        const auto& b = command.bitbltbuf;
        const auto& p = command.trxpos;
        const auto& r = command.trxreg;
        m_highResolution->invalidateMemory(gs_gpu::memoryRange(b.dpsm, b.dbp, b.dbw, p.dsax, p.dsay, r.rrw, r.rrh));
    }
#endif
    m_transfer = command;
    m_transferState.x = command.trxpos.dsax;
    m_transferState.y = command.trxpos.dsay;
    m_transferState.totalPixels = static_cast<uint32_t>(command.trxreg.rrw) * static_cast<uint32_t>(command.trxreg.rrh);
    m_transferState.copiedPixels = 0u;
    m_transferState.direction = command.direction;
    m_transferState.localToHostPendingBytes = 0u;

    if (command.direction == 2u)
        PerformLocalToLocalTransfer();
    else if (command.direction == 1u)
        PerformLocalToHostTransfer();
}

void GSCpuBackend::UploadImage(const uint8_t* data, uint32_t sizeBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
#if defined(PS2X_HAVE_PORTABLE_GPU)
    const auto& b = m_transfer.bitbltbuf;
    const auto& p = m_transfer.trxpos;
    const auto& r = m_transfer.trxreg;
    if (m_gpuDeferred &&
        m_gpuDeferred->touches(gs_gpu::memoryRange(b.dpsm, b.dbp, b.dbw, p.dsax, p.dsay, r.rrw, r.rrh)))
        FlushGpuUnlocked(GpuFenceReason::Transfer);
#endif
    if (!data || sizeBytes == 0u || !m_vram || m_transferState.direction != 0u)
        return;
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_highResolution)
        m_highResolution->invalidateMemory(gs_gpu::memoryRange(b.dpsm, b.dbp, b.dbw, p.dsax, p.dsay, r.rrw, r.rrh));
#endif
    if (m_transfer.trxreg.rrw == 0u || m_transfer.trxreg.rrh == 0u || m_transferState.totalPixels == 0u)
        return;

    const uint32_t dbp = m_transfer.bitbltbuf.dbp;
    const uint32_t dbw = std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u);
    const uint8_t dpsm = m_transfer.bitbltbuf.dpsm;
    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t dsax = m_transfer.trxpos.dsax;
    uint32_t offset = 0u;

    auto advancePixel = [&](uint32_t count) {
        const uint32_t totalPixels = m_transferState.totalPixels;
        m_transferState.copiedPixels = std::min<uint32_t>(totalPixels, m_transferState.copiedPixels + count);

        if (m_transferState.copiedPixels >= totalPixels) {
            m_transferState.direction = 3u;
            m_transferState.totalPixels = 0u;
            return;
        }

        m_transferState.x = dsax + (m_transferState.copiedPixels % rrw);
        m_transferState.y = m_transfer.trxpos.dsay + (m_transferState.copiedPixels / rrw);
    };

    while (offset < sizeBytes && m_transferState.direction == 0u) {
        switch (dpsm) {
            case GS_PSM_CT32:
            case GS_PSM_Z32: {
                if (sizeBytes - offset < 4u)
                    return;
                uint32_t value = 0u;
                std::memcpy(&value, data + offset, sizeof(value));
                WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
                offset += 4u;
                advancePixel(1u);
                break;
            }
            case GS_PSM_CT24:
            case GS_PSM_Z24: {
                if (sizeBytes - offset < 3u)
                    return;
                const uint32_t value = static_cast<uint32_t>(data[offset]) |
                                       (static_cast<uint32_t>(data[offset + 1u]) << 8u) |
                                       (static_cast<uint32_t>(data[offset + 2u]) << 16u);
                WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
                offset += 3u;
                advancePixel(1u);
                break;
            }
            case GS_PSM_CT16:
            case GS_PSM_CT16S:
            case GS_PSM_Z16:
            case GS_PSM_Z16S: {
                if (sizeBytes - offset < 2u)
                    return;
                uint16_t value = 0u;
                std::memcpy(&value, data + offset, sizeof(value));
                WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
                offset += 2u;
                advancePixel(1u);
                break;
            }
            case GS_PSM_T8: {
                const uint32_t count =
                    std::min({sizeBytes - offset, m_transferState.totalPixels - m_transferState.copiedPixels,
                              rrw - m_transferState.copiedPixels % rrw});
                GSMem::WriteP8Row(m_vram, dbp, dbw, m_transferState.x, m_transferState.y, data + offset, count);
                offset += count;
                m_transferState.x += count - 1;
                advancePixel(count);
                break;
            }
            case GS_PSM_T8H:
                WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, data[offset++]);
                advancePixel(1u);
                break;
            case GS_PSM_T4:
            case GS_PSM_T4HL:
            case GS_PSM_T4HH: {
                const uint8_t packed = data[offset++];
                const uint32_t firstPixel = m_transferState.copiedPixels;
                WriteVramUnlocked(dpsm, dbp, dbw, dsax + (firstPixel % rrw),
                                  m_transfer.trxpos.dsay + (firstPixel / rrw), packed & 0x0Fu);
                if (firstPixel + 1u < m_transferState.totalPixels) {
                    const uint32_t secondPixel = firstPixel + 1u;
                    WriteVramUnlocked(dpsm, dbp, dbw, dsax + (secondPixel % rrw),
                                      m_transfer.trxpos.dsay + (secondPixel / rrw), (packed >> 4u) & 0x0Fu);
                }
                advancePixel(std::min<uint32_t>(2u, m_transferState.totalPixels - firstPixel));
                break;
            }
            default:
                return;
        }
    }
}

void GSCpuBackend::PerformLocalToLocalTransfer() {
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t total = rrw * rrh;
    if (total == 0u) {
        m_transferState.direction = 3u;
        return;
    }

    for (uint32_t pixel = 0; pixel < total; ++pixel) {
        uint32_t x = pixel % rrw;
        uint32_t y = pixel / rrw;
        if ((m_transfer.trxpos.dir & 0x2u) != 0u)
            x = rrw - x - 1u;
        if ((m_transfer.trxpos.dir & 0x1u) != 0u)
            y = rrh - y - 1u;

        const uint32_t value = ReadVramUnlocked(m_transfer.bitbltbuf.spsm, m_transfer.bitbltbuf.sbp,
                                                std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u),
                                                x + m_transfer.trxpos.ssax, y + m_transfer.trxpos.ssay);
        WriteVramUnlocked(m_transfer.bitbltbuf.dpsm, m_transfer.bitbltbuf.dbp,
                          std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u), x + m_transfer.trxpos.dsax,
                          y + m_transfer.trxpos.dsay, value);
    }

    m_transferState.copiedPixels = total;
    m_transferState.direction = 3u;
}

void GSCpuBackend::PerformLocalToHostTransfer() {
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t sbw = std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u);
    const uint8_t spsm = m_transfer.bitbltbuf.spsm;
    const uint32_t bpp = static_cast<uint32_t>(GSMem::BitsPerPixel(static_cast<GSMem::PixelStorageMode>(spsm)));
    const uint32_t total = rrw * rrh;
    m_localToHostBuffer.reserve((static_cast<size_t>(total) * bpp + 7u) / 8u);

    for (uint32_t pixel = 0u; pixel < total; ++pixel) {
        const uint32_t x = pixel % rrw;
        const uint32_t y = pixel / rrw;
        const uint32_t value = ReadVramUnlocked(spsm, m_transfer.bitbltbuf.sbp, sbw, x + m_transfer.trxpos.ssax,
                                                y + m_transfer.trxpos.ssay);
        switch (bpp) {
            case 32:
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 24u));
                break;
            case 24:
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
                break;
            case 16:
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
                break;
            case 8:
                m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
                break;
            case 4: {
                if ((pixel & 1u) != 0u)
                    break;
                uint32_t next = 0u;
                if (pixel + 1u < total) {
                    const uint32_t nextPixel = pixel + 1u;
                    const uint32_t nextX = nextPixel % rrw;
                    const uint32_t nextY = nextPixel / rrw;
                    next = ReadVramUnlocked(spsm, m_transfer.bitbltbuf.sbp, sbw, nextX + m_transfer.trxpos.ssax,
                                            nextY + m_transfer.trxpos.ssay);
                }
                m_localToHostBuffer.push_back(static_cast<uint8_t>((value & 0x0Fu) | ((next & 0x0Fu) << 4u)));
                break;
            }
            default:
                break;
        }
    }

    m_transferState.copiedPixels = total;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size();
}

uint32_t GSCpuBackend::ConsumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!dst || maxBytes == 0u || m_localToHostReadPos >= m_localToHostBuffer.size())
        return 0u;
    const size_t count = std::min<size_t>(maxBytes, m_localToHostBuffer.size() - m_localToHostReadPos);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, count);
    m_localToHostReadPos += count;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size() - m_localToHostReadPos;
    return static_cast<uint32_t>(count);
}

bool GSCpuBackend::ClearFramebuffer(const GSContext& context, uint32_t rgba) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushDrawsUnlocked();
    FlushGpuUnlocked();
    if (!m_vram || context.frame.fbw == 0u)
        return false;

    const uint32_t x0 = context.scissor.x0;
    const uint32_t x1 = std::max<uint32_t>(x0, context.scissor.x1);
    const uint32_t y0 = context.scissor.y0;
    const uint32_t y1 = std::max<uint32_t>(y0, context.scissor.y1);
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (m_highResolution)
        m_highResolution->invalidateMemory(gs_gpu::memoryRange(context.frame.psm, context.frame.fbp * 32,
                                                               context.frame.fbw, x0, y0, x1 - x0 + 1, y1 - y0 + 1));
#endif
    uint8_t r = static_cast<uint8_t>(rgba);
    uint8_t g = static_cast<uint8_t>(rgba >> 8u);
    uint8_t b = static_cast<uint8_t>(rgba >> 16u);
    uint8_t a = static_cast<uint8_t>(rgba >> 24u);
    if ((context.fba & 1ull) != 0ull && context.frame.psm != GS_PSM_CT24)
        a |= 0x80u;

    const uint32_t fbp = GSInternal::framePageBaseToBlock(context.frame.fbp);
    const uint32_t fbw = std::max<uint32_t>(context.frame.fbw, 1u);
    if (context.frame.psm == GS_PSM_CT32 || context.frame.psm == GS_PSM_CT24) {
        const uint32_t source = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8u) |
                                (static_cast<uint32_t>(b) << 16u) | (static_cast<uint32_t>(a) << 24u);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x) {
                uint32_t pixel = source;
                if (context.frame.fbmsk != 0u) {
                    const uint32_t old = ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y);
                    pixel = (pixel & ~context.frame.fbmsk) | (old & context.frame.fbmsk);
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }

    if (context.frame.psm == GS_PSM_CT16 || context.frame.psm == GS_PSM_CT16S) {
        const uint16_t source = encodeFramePixelPSMCT16(r, g, b, a);
        const uint16_t mask = static_cast<uint16_t>(context.frame.fbmsk);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x) {
                uint16_t pixel = source;
                if (mask != 0u) {
                    const uint16_t old = static_cast<uint16_t>(ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y));
                    pixel = static_cast<uint16_t>((pixel & ~mask) | (old & mask));
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }
    return false;
}

bool GSCpuBackend::CopyFrameToHostRgba(const GSFrameReg& frame, uint32_t width, uint32_t height,
                                       std::vector<uint8_t>& outPixels, bool preserveAlpha, bool useLocalMemoryLayout,
                                       bool frameBaseIsPages, uint32_t sourceOriginX, uint32_t sourceOriginY) const {
    if (!m_vram || m_vramSize == 0u)
        return false;

    outPixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
    const uint32_t baseBytes = frameBaseIsPages ? frame.fbp * 8192u : frame.fbp * 256u;
    const uint32_t basePtr = frameBaseIsPages ? GSInternal::framePageBaseToBlock(frame.fbp) : frame.fbp;
    const uint32_t fbw = frame.fbw ? frame.fbw : kHostFrameWidth / 64u;
    const uint32_t bytesPerPixel = (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S) ? 2u : 4u;
    const uint32_t stride = fbw * 64u * bytesPerPixel;

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* dst = outPixels.data() + y * kHostFrameWidth * 4u;
        if (useLocalMemoryLayout && (frame.psm == GS_PSM_CT32 || frame.psm == GS_PSM_CT24)) {
            GSMem::ReadColor32Row(m_vram, basePtr, fbw, sourceOriginX, sourceOriginY + y, dst, width,
                                  preserveAlpha && frame.psm == GS_PSM_CT32);
            continue;
        }
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sx = sourceOriginX + x;
            const uint32_t sy = sourceOriginY + y;
            if (frame.psm == GS_PSM_CT32 || frame.psm == GS_PSM_CT24) {
                uint32_t color = 0u;
                if (useLocalMemoryLayout)
                    color = ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy);
                else {
                    const uint32_t pixelBytes = frame.psm == GS_PSM_CT24 ? 3u : 4u;
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride +
                                            static_cast<uint64_t>(sx) * pixelBytes;
                    if (offset + pixelBytes > m_vramSize)
                        return false;
                    color = m_vram[offset] | (static_cast<uint32_t>(m_vram[offset + 1u]) << 8u) |
                            (static_cast<uint32_t>(m_vram[offset + 2u]) << 16u);
                    if (pixelBytes == 4u)
                        color |= static_cast<uint32_t>(m_vram[offset + 3u]) << 24u;
                }
                dst[x * 4u] = static_cast<uint8_t>(color);
                dst[x * 4u + 1u] = static_cast<uint8_t>(color >> 8u);
                dst[x * 4u + 2u] = static_cast<uint8_t>(color >> 16u);
                dst[x * 4u + 3u] =
                    preserveAlpha && frame.psm != GS_PSM_CT24 ? static_cast<uint8_t>(color >> 24u) : 255u;
            } else if (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S) {
                uint16_t color = 0u;
                if (useLocalMemoryLayout)
                    color = static_cast<uint16_t>(ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy));
                else {
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride +
                                            static_cast<uint64_t>(sx) * 2u;
                    if (offset + 2u > m_vramSize)
                        return false;
                    std::memcpy(&color, m_vram + offset, sizeof(color));
                }
                const uint32_t r = color & 31u;
                const uint32_t g = (color >> 5u) & 31u;
                const uint32_t b = (color >> 10u) & 31u;
                dst[x * 4u] = static_cast<uint8_t>((r << 3u) | (r >> 2u));
                dst[x * 4u + 1u] = static_cast<uint8_t>((g << 3u) | (g >> 2u));
                dst[x * 4u + 2u] = static_cast<uint8_t>((b << 3u) | (b >> 2u));
                dst[x * 4u + 3u] = preserveAlpha ? ((color & 0x8000u) ? 0x80u : 0u) : 255u;
            } else {
                outPixels.clear();
                return false;
            }
        }
    }
    return true;
}

PresentationFrame GSCpuBackend::Present(const GSPresentationRequest& request) {
    // Snapshot local memory under the backend lock, then perform the expensive
    // display conversion without holding the producer-side raster lock.
    thread_local std::vector<uint8_t> snapshot;
    std::shared_ptr<gs_gpu::HighResolutionCache> highResolution;
    {
        std::lock_guard lock(m_mutex);
        FlushDrawsUnlocked();
        FlushGpuUnlocked(GpuFenceReason::Read);
        if (!m_vram || !m_vramSize)
            return {};
        snapshot.assign(m_vram, m_vram + m_vramSize);
#if defined(PS2X_HAVE_PORTABLE_GPU)
        if (m_highResolution)
            highResolution = std::make_shared<gs_gpu::HighResolutionCache>(*m_highResolution);
#endif
    }
    if (snapshot.empty())
        return {};

    thread_local GSCpuBackend snapshotBackend;
    snapshotBackend.Initialize(snapshot.data(), static_cast<uint32_t>(snapshot.size()));
    snapshotBackend.m_highResolution = std::move(highResolution);
    return snapshotBackend.PresentFromLocalMemory(request);
}

PresentationFrame GSCpuBackend::PresentFromLocalMemory(const GSPresentationRequest& request) {
    PresentationFrame result{};
    const GSPmodeState pmode = decodePmode(request.pmode);
    const GSSmode2State smode2 = decodeSMode2(request.smode2);
    const bool fieldMode = smode2.interlaced && !smode2.frameMode;
    const bool oddField = (request.vsyncTick & 1ull) != 0ull;
    const GSFrameReg displayFrame1 = decodeDisplayFrame(request.dispfb1);
    const GSFrameReg displayFrame2 = decodeDisplayFrame(request.dispfb2);
    const GSDisplayReadOrigin origin1 = decodeDisplayReadOrigin(request.dispfb1);
    const GSDisplayReadOrigin origin2 = decodeDisplayReadOrigin(request.dispfb2);
    uint32_t width1 = 0u, height1 = 0u, width2 = 0u, height2 = 0u;
    decodeDisplaySize(request.display1, width1, height1);
    decodeDisplaySize(request.display2, width2, height2);
    const bool valid1 = pmode.enableCrt1 && hasDisplaySetup(request.display1, displayFrame1);
    const bool valid2 = pmode.enableCrt2 && hasDisplaySetup(request.display2, displayFrame2);
    if (!valid1 && !valid2)
        return result;

    auto copySource = [&](const GSFrameReg& displayFrame, const GSDisplayReadOrigin& origin, uint32_t width,
                          uint32_t height, bool allowPreferred, bool preserveAlpha, GSFrameReg& selected,
                          std::vector<uint8_t>& pixels, bool& usedPreferred) -> bool {
        selected = displayFrame;
        pixels.clear();
        usedPreferred = false;
        if (allowPreferred && request.hasPreferredSource && request.preferredDestFbp == displayFrame.fbp &&
            (request.preferredSource.fbw != 0u || request.preferredSource.fbp != displayFrame.fbp) &&
            CopyFrameToHostRgba(request.preferredSource, width, height, pixels, preserveAlpha, true, false, 0u, 0u)) {
            selected = request.preferredSource;
            usedPreferred = true;
        }
        if (pixels.empty() &&
            !CopyFrameToHostRgba(displayFrame, width, height, pixels, preserveAlpha, true, true, origin.x, origin.y))
            return false;

        if (!usedPreferred && displayFrame.fbp == 0u && countNonBlackPixels(pixels, width, height) == 0u) {
            for (const GSFrameReg& candidate : request.contextFrames) {
                if (candidate.fbp == selected.fbp && candidate.fbw == selected.fbw && candidate.psm == selected.psm)
                    continue;
                std::vector<uint8_t> candidatePixels;
                if (!CopyFrameToHostRgba(candidate, width, height, candidatePixels, preserveAlpha, true, true, 0u, 0u))
                    continue;
                if (countNonBlackPixels(candidatePixels, width, height) == 0u)
                    continue;
                selected = candidate;
                pixels.swap(candidatePixels);
                break;
            }
        }
        return true;
    };

    // Blending identical CRT sources leaves RGB unchanged for every PMODE
    // factor. Output alpha is normalized below, so one conversion suffices.
    if (valid1 && valid2 && !pmode.slbg && width1 == width2 && height1 == height2 &&
        displayFrame1.fbp == displayFrame2.fbp && displayFrame1.fbw == displayFrame2.fbw &&
        displayFrame1.psm == displayFrame2.psm && origin1.x == origin2.x && origin1.y == origin2.y) {
        GSFrameReg selected{};
        if (copySource(displayFrame1, origin1, width1, height1, false, false, selected, result.pixels,
                       result.usedPreferred)) {
            result.width = width1;
            result.height = height1;
            if (fieldMode)
                applyFieldPresentation(result.pixels, result.width, result.height, oddField);
            result.displayFbp = displayFrame1.fbp;
            result.sourceFbp = selected.fbp;
#if defined(PS2X_HAVE_PORTABLE_GPU)
            if (!fieldMode && m_highResolution)
                m_highResolution->present(result, selected.fbp * 32, selected.fbw, origin1.x, origin1.y);
#endif
            return result;
        }
    }

    if (valid1 && valid2) {
        GSFrameReg selected1{}, selected2{};
        std::vector<uint8_t> crt1, crt2;
        bool preferred1 = false, preferred2 = false;
        if (copySource(displayFrame1, origin1, width1, height1, false, true, selected1, crt1, preferred1) &&
            copySource(displayFrame2, origin2, width2, height2, false, false, selected2, crt2, preferred2)) {
#if defined(PS2X_HAVE_PORTABLE_GPU)
            if (!fieldMode && m_highResolution) {
                const auto* surface1 = m_highResolution->find(selected1.fbp * 32, selected1.fbw);
                const auto* surface2 = m_highResolution->find(selected2.fbp * 32, selected2.fbw);
                if (surface1 && surface2 && surface1->storedScale != surface2->storedScale)
                    surface2 = nullptr;
                if (surface1 || surface2) {
                    const unsigned scale = surface1 ? surface1->storedScale : surface2->storedScale;
                    const auto readOrigin1 = selected1.fbp == displayFrame1.fbp && selected1.fbw == displayFrame1.fbw &&
                                                     selected1.psm == displayFrame1.psm
                                                 ? origin1
                                                 : GSDisplayReadOrigin{};
                    const auto readOrigin2 = selected2.fbp == displayFrame2.fbp && selected2.fbw == displayFrame2.fbw &&
                                                     selected2.psm == displayFrame2.psm
                                                 ? origin2
                                                 : GSDisplayReadOrigin{};
                    result.width = std::max(width1, width2) * scale;
                    result.height = std::max(height1, height2) * scale;
                    result.pixels.resize(size_t(result.width) * result.height * 4);
                    const uint32_t background = uint32_t(request.bgcolor) | 0xff000000u;
                    for (unsigned y = 0; y < result.height; ++y)
                        for (unsigned x = 0; x < result.width; ++x) {
                            const unsigned nx = x / scale, ny = y / scale, sx = x % scale, sy = y % scale;
                            uint32_t destination = background;
                            if (!pmode.slbg && nx < width2 && ny < height2) {
                                uint32_t native;
                                std::memcpy(&native, crt2.data() + (size_t(ny) * kHostFrameWidth + nx) * 4, 4);
                                destination =
                                    gs_gpu::presentationSample(surface2, nx + readOrigin2.x, ny + readOrigin2.y, sx, sy,
                                                               native, selected2.psm, false);
                            }
                            if (nx < width1 && ny < height1) {
                                uint32_t native;
                                std::memcpy(&native, crt1.data() + (size_t(ny) * kHostFrameWidth + nx) * 4, 4);
                                const auto source =
                                    gs_gpu::presentationSample(surface1, nx + readOrigin1.x, ny + readOrigin1.y, sx, sy,
                                                               native, selected1.psm, true);
                                const auto factor = pmode.mmod ? pmode.alp : std::min(255u, (source >> 24) * 2);
                                destination = gs_cpu::blendDisplay32(source, destination, uint8_t(factor));
                            }
                            std::memcpy(result.pixels.data() + (size_t(y) * result.width + x) * 4, &destination, 4);
                        }
                    result.displayFbp = displayFrame1.fbp;
                    result.sourceFbp = selected1.fbp;
                    if (std::getenv("DRAKAN_FRAME_METRICS")) {
                        static thread_local unsigned reports = 0;
                        if (reports++ < 4 || (reports & 255) == 0)
                            std::fprintf(stderr, "[portable-hires] CRT=2 output=%ux%u scale=%u\n", result.width,
                                         result.height, scale);
                    }
                    return result;
                }
            }
#endif
            result.width = std::max(width1, width2);
            result.height = std::max(height1, height2);
            result.pixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
            const uint8_t bgR = static_cast<uint8_t>(request.bgcolor);
            const uint8_t bgG = static_cast<uint8_t>(request.bgcolor >> 8u);
            const uint8_t bgB = static_cast<uint8_t>(request.bgcolor >> 16u);
            for (uint32_t y = 0; y < result.height; ++y)
                for (uint32_t x = 0; x < result.width; ++x) {
                    uint8_t* dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    dst[0] = bgR;
                    dst[1] = bgG;
                    dst[2] = bgB;
                    dst[3] = 255;
                }
            if (!pmode.slbg)
                for (uint32_t y = 0; y < height2; ++y)
                    std::memcpy(result.pixels.data() + y * kHostFrameWidth * 4u, crt2.data() + y * kHostFrameWidth * 4u,
                                width2 * 4u);
            for (uint32_t y = 0; y < height1; ++y)
                for (uint32_t x = 0; x < width1; ++x) {
                    const uint8_t* src = crt1.data() + (y * kHostFrameWidth + x) * 4u;
                    uint8_t* dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    const uint32_t factor =
                        pmode.mmod ? pmode.alp : std::min<uint32_t>(255u, static_cast<uint32_t>(src[3]) * 2u);
                    uint32_t source, destination;
                    std::memcpy(&source, src, sizeof(source));
                    std::memcpy(&destination, dst, sizeof(destination));
                    const uint32_t color = gs_cpu::blendDisplay32(source, destination, static_cast<uint8_t>(factor));
                    std::memcpy(dst, &color, sizeof(color));
                }
            if (fieldMode)
                applyFieldPresentation(result.pixels, result.width, result.height, oddField);
            result.displayFbp = displayFrame1.fbp;
            result.sourceFbp = selected1.fbp;
            return result;
        }
    }

    const GSFrameReg& displayFrame = valid1 ? displayFrame1 : displayFrame2;
    const GSDisplayReadOrigin& origin = valid1 ? origin1 : origin2;
    result.width = valid1 ? width1 : width2;
    result.height = valid1 ? height1 : height2;
    GSFrameReg selected = displayFrame;
    if (!copySource(displayFrame, origin, result.width, result.height, true, false, selected, result.pixels,
                    result.usedPreferred))
        return {};
    if (fieldMode)
        applyFieldPresentation(result.pixels, result.width, result.height, oddField);
    result.displayFbp = displayFrame.fbp;
    result.sourceFbp = selected.fbp;
#if defined(PS2X_HAVE_PORTABLE_GPU)
    if (!fieldMode && m_highResolution) {
        const bool original =
            !result.usedPreferred && selected.fbp == displayFrame.fbp && selected.fbw == displayFrame.fbw;
        m_highResolution->present(result, result.usedPreferred ? selected.fbp : selected.fbp * 32, selected.fbw,
                                  original ? origin.x : 0, original ? origin.y : 0);
    }
#endif
    return result;
}
