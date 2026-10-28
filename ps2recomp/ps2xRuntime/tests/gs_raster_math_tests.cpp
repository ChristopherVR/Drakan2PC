#include "gs/gs_cpu_raster_math.h"
#include <bit>
#include <cassert>
#include <cstdio>

int main() {
    uint32_t random = 0x64e2a1b3;
    const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    unsigned modes = 1;
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
    const unsigned saved = _mm_getcsr();
    modes = 4;
#endif
    uint64_t count = 0;
    for (unsigned mode = 0; mode < modes; ++mode) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
        _mm_setcsr((saved & ~0x6000u) | (mode << 13));
#endif
        for (unsigned i = 0; i < 2000000; ++i) {
            const uint32_t a = next(), b = next(), c = next(), d = next();
            const float x = float(next() & 0xffffff) / 16777216.0f;
            const float y = float(next() & 0xffffff) / 16777216.0f;
            const uint32_t expected = gs_cpu::bilinearScalar(a, b, c, d, x, y);
            const uint32_t actual = gs_cpu::bilinear(a, b, c, d, x, y);
            if (actual != expected) {
                std::printf("Mismatch mode=%u colors=%08x %08x %08x %08x xy=%08x %08x got=%08x expected=%08x\n", mode,
                            a, b, c, d, std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y), actual, expected);
                return 1;
            }
            const auto unpack = [](uint32_t value) {
                return std::array<float, 4>{float(value & 255), float((value >> 8) & 255), float((value >> 16) & 255),
                                            float(value >> 24)};
            };
            const auto ca = unpack(a), cb = unpack(b), cc = unpack(c);
            const gs_cpu::Interpolator4 interpolate(ca, cb, cc);
            const float w0 = x * 2.0f, w1 = y * 2.0f, w2 = 1.0f - w0 - w1;
            const auto channels = interpolate.values(w0, w1, w2);
            uint32_t expectedColor = 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
                const float value = ca[channel] * w0 + cb[channel] * w1 + cc[channel] * w2;
                assert(std::bit_cast<uint32_t>(channels[channel]) == std::bit_cast<uint32_t>(value));
                expectedColor |= uint32_t(std::clamp(int(value), 0, 255)) << (8 * channel);
            }
            assert(interpolate.color(w0, w1, w2) == expectedColor);
            const uint64_t alpha = uint64_t(next()) | (uint64_t(next()) << 32);
            const uint8_t sourceAlpha = uint8_t(next());
            const bool fixedDestinationAlpha = (next() & 1) != 0;
            if (gs_cpu::blendIsSource(alpha, sourceAlpha, fixedDestinationAlpha)) {
                const unsigned asel = unsigned(alpha & 3), bsel = unsigned((alpha >> 2) & 3);
                const unsigned csel = unsigned((alpha >> 4) & 3), dsel = unsigned((alpha >> 6) & 3);
                for (int source : {0, 1, 127, 128, 254, 255})
                    for (int destination : {0, 3, 128, 255})
                        for (int destinationAlpha : {0, 128, 255}) {
                            const auto pick = [&](unsigned selector) {
                                return selector == 0 ? source : selector == 1 ? destination : 0;
                            };
                            const int factor = csel == 0   ? sourceAlpha
                                               : csel == 1 ? (fixedDestinationAlpha ? 128 : destinationAlpha)
                                                           : int((alpha >> 32) & 255);
                            assert(std::clamp(((pick(asel) - pick(bsel)) * factor >> 7) + pick(dsel), 0, 255) ==
                                   source);
                        }
            }
            const bool textureAlpha = next() & 1, fogEnabled = next() & 1;
            const uint8_t fog = uint8_t(next());
            const uint32_t fogColor = next();
            uint32_t shaded = 0, blended = a & 0xff000000u;
            for (unsigned shift = 0; shift < 32; shift += 8) {
                unsigned value = std::min(255u, (((a >> shift) & 255) * ((b >> shift) & 255)) >> 7);
                if (shift == 24 && !textureAlpha)
                    value = a >> 24;
                if (shift != 24 && fogEnabled)
                    value = ((value * fog) >> 8) + ((((fogColor >> shift) & 255) * (255 - fog)) >> 8);
                shaded |= value << shift;
                if (shift < 24) {
                    const int src = (a >> shift) & 255, dst = (b >> shift) & 255;
                    blended |= uint32_t(std::clamp(((src - dst) * int(a >> 24) >> 7) + dst, 0, 255)) << shift;
                }
            }
            assert(gs_cpu::modulateFog(a, b, textureAlpha, fogEnabled, fog, fogColor) == shaded);
            assert(gs_cpu::blendSourceAlpha128(a, b) == blended);
            uint32_t display = 0xff000000u;
            for (unsigned shift = 0; shift < 24; shift += 8) {
                const int src = (a >> shift) & 255, dst = (b >> shift) & 255;
                display |= uint32_t(std::clamp(dst + ((src - dst) * int(sourceAlpha)) / 255, 0, 255)) << shift;
            }
            assert(gs_cpu::blendDisplay32(a, b, sourceAlpha) == display);
            ++count;
        }
        for (unsigned i = 0; i < 500000; ++i) {
            const auto coord = [&] { return float(int(next() & 0x1ffff) - 65536) / 16.0f; };
            const int x = int(next() & 2047);
            const float py = float(next() & 2047) + 0.5f;
            const float fx2 = coord(), fy2 = coord(), dy0 = coord(), dx0 = coord(), dy1 = coord(), dx1 = coord();
            const float winding = next() & 1 ? 1.0f : -1.0f;
            const float inverse = 1.0f / (1.0f + float(next() & 0xffffff));
            const auto actual = gs_cpu::triangleWeights(x, py, fx2, fy2, dy0, dx0, dy1, dx1, winding, inverse);
            for (unsigned lane = 0; lane < 4; ++lane) {
                const float px = float(x + int(lane)) + 0.5f;
                const float w0 = (((dy0 * (px - fx2) + dx0 * (py - fy2)) * winding) * inverse);
                const float w1 = (((dy1 * (px - fx2) + dx1 * (py - fy2)) * winding) * inverse);
                const float w2 = 1.0f - w0 - w1;
                assert(std::bit_cast<uint32_t>(actual.w0[lane]) == std::bit_cast<uint32_t>(w0));
                assert(std::bit_cast<uint32_t>(actual.w1[lane]) == std::bit_cast<uint32_t>(w1));
                assert(std::bit_cast<uint32_t>(actual.w2[lane]) == std::bit_cast<uint32_t>(w2));
                assert(bool(actual.mask & (1u << lane)) == !(w0 < -1.0e-4f || w1 < -1.0e-4f || w2 < -1.0e-4f));
            }
        }
        for (unsigned triangle = 0; triangle < 1000; ++triangle) {
            const auto coord = [&] { return float(int(next() & 0x1ffff) - 65536) / 16.0f; };
            const float fx0 = coord(), fy0 = coord(), fx1 = coord(), fy1 = coord(), fx2 = coord(), fy2 = coord();
            const float denominator = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
            if (std::fabs(denominator) < 0.001f)
                continue;
            const float winding = denominator < 0 ? -1.0f : 1.0f;
            const float inverse = 1.0f / std::fabs(denominator);
            const gs_cpu::TriangleRowClip clip(0, 2047, fx2, fy2, fy1 - fy2, fx2 - fx1, fy2 - fy0, fx0 - fx2, winding,
                                               inverse);
            for (unsigned row = 0; row < 8; ++row) {
                const float py = float(next() & 2047) + 0.5f;
                const auto span = clip.row(py);
                for (int x = 0; x < 2048; x += 4) {
                    const auto weights = gs_cpu::triangleWeights(x, py, fx2, fy2, fy1 - fy2, fx2 - fx1, fy2 - fy0,
                                                                 fx0 - fx2, winding, inverse);
                    for (unsigned lane = 0; lane < 4; ++lane)
                        assert(!(weights.mask & (1u << lane)) ||
                               (x + int(lane) >= span.first && x + int(lane) <= span.last));
                }
            }
        }
        for (float x : {0.0f, 1.0f, 0.5f, std::nextafter(0.5f, 0.0f), std::nextafter(0.5f, 1.0f)})
            for (float y : {0.0f, 1.0f, 0.5f, std::nextafter(0.5f, 0.0f), std::nextafter(0.5f, 1.0f)})
                assert(gs_cpu::bilinear(0x0000ffff, 0x80808080, 0xff000000, 0xffffffff, x, y) ==
                       gs_cpu::bilinearScalar(0x0000ffff, 0x80808080, 0xff000000, 0xffffffff, x, y));
    }
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
    _mm_setcsr(saved);
#endif
    std::printf("GS bilinear: %llu exact packed-color comparisons\n", static_cast<unsigned long long>(count));
}
