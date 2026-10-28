// Capture only. Header-only so the parent needs no CMake or settings-host edits.
#pragma once
#include "ps2_runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace drakan_inventory_typography {
namespace detail {
inline PS2Runtime::RecompiledFunction original = nullptr;
inline const std::atomic<uint64_t>* tick = nullptr;
inline std::mutex mutex;
inline FILE* output = nullptr;
inline unsigned records = 0, textRecords = 0, glyphRecords = 0, limit = 20000;
inline uint64_t glyphVsync = ~uint64_t{0};
inline unsigned glyphsThisVsync = 0;
inline bool allScreens = false;
struct GlyphHook {
    uint32_t address = 0;
    PS2Runtime::RecompiledFunction original = nullptr;
};
inline GlyphHook glyphHooks[16];
inline unsigned glyphHookCount = 0;
inline bool fontCaptured[32]{};
// Accept physical RDRAM and its documented KSEG aliases, never arbitrary wraparound.
inline bool span(uint32_t address, size_t bytes, uint32_t& physical) {
    const uint32_t segment = address & 0xe0000000u;
    if (segment != 0 && segment != 0x80000000u && segment != 0xa0000000u)
        return false;
    physical = address & 0x1fffffffu;
    return physical >= 0x10000 && physical < PS2_RAM_SIZE && bytes <= PS2_RAM_SIZE - physical;
}
inline bool word(const uint8_t* ram, uint32_t address, uint32_t& value) {
    uint32_t p;
    if (!span(address, 4, p))
        return false;
    std::memcpy(&value, ram + p, 4);
    return true;
}
inline void words(FILE* file, const uint8_t* ram, uint32_t address, unsigned count) {
    uint32_t p;
    if (!address || !span(address, count * 4, p)) {
        std::fputs("null", file);
        return;
    }
    std::fputc('[', file);
    for (unsigned i = 0; i < count; ++i) {
        uint32_t w;
        std::memcpy(&w, ram + p + i * 4, 4);
        std::fprintf(file, "%s%u", i ? "," : "", w);
    }
    std::fputc(']', file);
}
inline void floats(FILE* file, const uint8_t* ram, uint32_t address, unsigned count) {
    uint32_t p;
    if (!address || !span(address, count * 4, p)) {
        std::fputs("null", file);
        return;
    }
    std::fputc('[', file);
    for (unsigned i = 0; i < count; ++i) {
        float f;
        std::memcpy(&f, ram + p + i * 4, 4);
        // Preserve exact bits separately; JSON cannot represent NaN/infinity.
        std::fprintf(file, "%s", i ? "," : "");
        if (std::isfinite(f))
            std::fprintf(file, "%.9g", static_cast<double>(f));
        else
            std::fputs("null", file);
    }
    std::fputc(']', file);
}
inline void glyphHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    PS2Runtime::RecompiledFunction native = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (unsigned i = 0; i < glyphHookCount; ++i)
            if (ctx->pc == glyphHooks[i].address)
                native = glyphHooks[i].original;
        const uint64_t now = tick ? tick->load(std::memory_order_relaxed) : 0;
        if (now != glyphVsync) {
            glyphVsync = now;
            glyphsThisVsync = 0;
        }
        if (native && output && glyphRecords < limit && glyphsThisVsync < 32 && getRegU32(ctx, 31) == 0x286530) {
            uint32_t screen = 0, id = ~0u, p = 0;
            word(ram, 0x4fb60c, screen);
            if (span(screen, 0x1c, p))
                word(ram, p + 0x18, id);
            if (allScreens || id == 21) {
                ++glyphRecords;
                ++glyphsThisVsync;
                std::fprintf(
                    output,
                    "{\"event\":\"glyph\",\"seq\":%u,\"vsync\":%llu,\"screen\":%u,\"pc\":%u,\"ra\":%u,\"args\":[",
                    ++records, static_cast<unsigned long long>(tick ? tick->load(std::memory_order_relaxed) : 0), id,
                    ctx->pc, getRegU32(ctx, 31));
                for (unsigned i = 4; i < 12; ++i)
                    std::fprintf(output, "%s%u", i == 4 ? "" : ",", getRegU32(ctx, i));
                std::fputs("],\"f12_f13\":[", output);
                for (unsigned i = 12; i < 14; ++i) {
                    if (i != 12)
                        std::fputc(',', output);
                    if (std::isfinite(ctx->f[i]))
                        std::fprintf(output, "%.9g", static_cast<double>(ctx->f[i]));
                    else
                        std::fputs("null", output);
                }
                std::fputs("],\"stack_words\":", output);
                words(output, ram, getRegU32(ctx, 29), 3);
                uint32_t color = 0;
                if (span(getRegU32(ctx, 29), 12, p))
                    word(ram, p + 8, color);
                std::fputs(",\"color_words\":", output);
                words(output, ram, color, 1);
                std::fputs(",\"clip_words\":", output);
                words(output, ram, getRegU32(ctx, 11), 4);
                std::fputs("}\n", output);
                std::fflush(output);
            }
        }
    }
    // Only exact registered entries reach this wrapper; never replace an unknown target.
    if (native)
        native(ram, ctx, runtime);
}
inline void watchGlyph(PS2Runtime* runtime, uint32_t target) {
    if (!target || glyphHookCount == 16 || !runtime->hasFunction(target))
        return;
    for (unsigned i = 0; i < glyphHookCount; ++i)
        if (glyphHooks[i].address == target)
            return;
    auto native = runtime->lookupFunction(target);
    if (!native || native == &glyphHook || native == original)
        return;
    glyphHooks[glyphHookCount++] = {target, native};
    runtime->replaceFunction(target, &glyphHook);
}
inline void captureFont(const uint8_t* ram, uint32_t font, uint32_t record, uint32_t object, uint32_t target) {
    uint32_t p = 0;
    uint32_t loaded = 0;
    if (record && !fontCaptured[font] && word(ram, record + 0x1c, loaded) && loaded && target) {
        fontCaptured[font] = true;
        std::fprintf(output,
                     "{\"event\":\"font\",\"seq\":%u,\"font_index\":%u,\"record\":%u,\"object\":%u,\"glyph_target\":%u,"
                     "\"metrics\":",
                     ++records, font, record, object, target);
        words(output, ram, record, 10);
        uint32_t starts = 0, rows = 0, codes = 0;
        word(ram, record, starts);
        word(ram, record + 4, rows);
        word(ram, record + 8, codes);
        unsigned boundaryCount = 0, rowCount = 0;
        uint32_t value = 0;
        while (boundaryCount < 256 && word(ram, starts + boundaryCount * 4, value) && value >= 2 && value < 256)
            ++boundaryCount;
        while (rowCount < 32 && word(ram, codes + rowCount * 4, value)) {
            ++rowCount;
            if (value == 9999)
                break;
        }
        std::fputs(",\"x_boundaries\":", output);
        words(output, ram, starts, boundaryCount);
        std::fputs(",\"row_end_x\":", output);
        words(output, ram, rows, rowCount);
        std::fputs(",\"row_last_codes\":", output);
        words(output, ram, codes, rowCount);
        std::fputs(",\"extended_byte_table\":", output);
        words(output, ram, 0x4eac30, 35);
        // Texture dimensions only; no pixels or arbitrary object data are dumped.
        std::fputs(",\"object_dimensions\":", output);
        if (span(object, 0x2c, p))
            words(output, ram, p + 0x24, 2);
        else
            std::fputs("null", output);
        std::fputs("}\n", output);
        std::fflush(output);
    }
}
inline void hook(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    // Do not touch the context, RAM, scheduler or native return. Continuations pass through.
    if (ctx->pc == 0x286170u) {
        std::lock_guard<std::mutex> lock(mutex);
        uint32_t screen = 0, screenId = ~0u, gui = 0, width = 0, height = 0;
        word(ram, 0x4fb60c, screen);
        uint32_t p;
        if (span(screen, 0x1c, p))
            word(ram, p + 0x18, screenId);
        word(ram, 0x5565c8, gui);
        if (span(gui, 0x100, p)) {
            word(ram, p + 0xf8, width);
            word(ram, p + 0xfc, height);
        }
        if (output && (allScreens || screenId == 21)) {
            const uint32_t font = getRegU32(ctx, 8);
            uint32_t record = font < 32 ? 0x56cad8 + font * 0x28 : 0;
            uint32_t object = 0, vtable = 0, target = 0;
            if (record && word(ram, record + 0x24, object) && span(object, 0x1c, p) && word(ram, p + 0x18, vtable) &&
                span(vtable, 0xc0, p))
                word(ram, p + 0xbc, target);
            captureFont(ram, font, record, object, target);
            if (!getRegU32(ctx, 11))
                watchGlyph(runtime, target);
        }
        if (output && textRecords < limit && (allScreens || screenId == 21)) {
            ++textRecords;
            const unsigned id = ++records;
            uint32_t a[8];
            for (unsigned i = 0; i < 8; ++i)
                a[i] = getRegU32(ctx, 4 + i);
            // Emit bytes only after a complete bounded NUL-terminated string is validated.
            size_t length = 0;
            bool valid = false;
            uint32_t textPhysical = 0;
            if (span(a[2], 1, textPhysical)) {
                const size_t bound = std::min<size_t>(512, PS2_RAM_SIZE - textPhysical);
                for (; length < bound; ++length)
                    if (!ram[textPhysical + length]) {
                        valid = true;
                        break;
                    }
            }
            std::fprintf(output,
                         "{\"event\":\"text\",\"seq\":%u,\"vsync\":%llu,\"pc\":%u,\"ra\":%u,\"branch_pc\":%u,\"insns\":"
                         "%llu,\"screen_ptr\":%u,\"screen\":%u,\"width\":%u,\"height\":%u,\"measure\":%s,\"args\":[",
                         id, static_cast<unsigned long long>(tick ? tick->load(std::memory_order_relaxed) : 0), ctx->pc,
                         getRegU32(ctx, 31), ctx->branch_pc, static_cast<unsigned long long>(ctx->insn_count), screen,
                         screenId, width, height, a[7] ? "true" : "false");
            for (unsigned i = 0; i < 8; ++i)
                std::fprintf(output, "%s%u", i ? "," : "", a[i]);
            std::fprintf(output, "],\"x\":%d,\"y\":%d,\"spacing\":%d,\"text_valid\":%s,\"text_hex\":\"",
                         static_cast<int32_t>(a[0]), static_cast<int32_t>(a[1]), static_cast<int32_t>(a[3]),
                         valid ? "true" : "false");
            if (valid)
                for (size_t i = 0; i < length; ++i)
                    std::fprintf(output, "%02x", ram[textPhysical + i]);
            std::fputs("\",\"color_words\":", output);
            words(output, ram, a[5], 1);
            // Native text takes a packed color pointer; float views are diagnostic only.
            std::fputs(",\"color_float_view\":", output);
            floats(output, ram, a[5], 1);
            std::fputs(",\"clip_words\":", output);
            words(output, ram, a[6], 4);
            std::fputs(",\"clip_float_view\":", output);
            floats(output, ram, a[6], 4);
            std::fputs(",\"f12_f15\":[", output);
            for (unsigned i = 12; i < 16; ++i) {
                if (i != 12)
                    std::fputc(',', output);
                if (std::isfinite(ctx->f[i]))
                    std::fprintf(output, "%.9g", static_cast<double>(ctx->f[i]));
                else
                    std::fputs("null", output);
            }
            std::fputs("],\"font_words\":", output);
            uint32_t record = 0;
            if (a[4] < 32)
                record = 0x56cad8 + a[4] * 0x28;
            if (record)
                words(output, ram, record, 10);
            else
                std::fputs("null", output);
            uint32_t object = 0, vtable = 0, target = 0;
            if (record && word(ram, record + 0x24, object) && span(object, 0x1c, p) && word(ram, p + 0x18, vtable) &&
                span(vtable, 0xc0, p))
                word(ram, p + 0xbc, target);
            std::fprintf(output, ",\"glyph_target\":%u}\n", target);
            // Font records can be lazy-loaded by the first original call. Subsequent
            // calls discover the loaded target; first-call glyph coverage is not claimed.
            if (!a[7])
                watchGlyph(runtime, target);
            std::fflush(output);
            if (textRecords == limit)
                std::fprintf(stderr, "[inventory-text] capture limit reached (%u)\n", limit);
        }
    }
    original(ram, ctx, runtime);
}
} // namespace detail

// Call once after generated functions/overrides are registered, before EE starts.
// The caller owns the vsync counter for the runtime lifetime.
inline void install(PS2Runtime& runtime, const std::atomic<uint64_t>* vsync) {
    using namespace detail;
    const char* enabled = std::getenv("DRAKAN_INVENTORY_TEXT_CAPTURE");
    if (!enabled || std::strcmp(enabled, "1") || original)
        return;
    if (!runtime.hasFunction(0x286170)) {
        std::fprintf(stderr, "[inventory-text] missing native draw function\n");
        return;
    }
    const char* path = std::getenv("DRAKAN_INVENTORY_TEXT_LOG");
    if (!path || !*path) {
        std::fprintf(stderr, "[inventory-text] set DRAKAN_INVENTORY_TEXT_LOG to a new JSONL path\n");
        return;
    }
    // Exclusive create avoids overwriting a previous capture.
    output = std::fopen(path, "wx");
    if (!output) {
        std::fprintf(stderr, "[inventory-text] cannot create log; hook disabled\n");
        return;
    }
    const char* max = std::getenv("DRAKAN_INVENTORY_TEXT_LIMIT");
    if (max && *max) {
        char* end = nullptr;
        unsigned long n = std::strtoul(max, &end, 10);
        if (end && !*end && n > 0)
            limit = static_cast<unsigned>(std::min<unsigned long>(n, 100000));
    }
    const char* all = std::getenv("DRAKAN_INVENTORY_TEXT_ALL_SCREENS");
    allScreens = all && !std::strcmp(all, "1");
    tick = vsync;
    original = runtime.lookupFunction(0x286170);
    runtime.replaceFunction(0x286170, &hook);
    std::fprintf(stderr,
                 "[inventory-text] capture only, %u text + %u glyph records (32 glyphs/vsync), font metadata "
                 "independent, screen %s\n",
                 limit, limit, allScreens ? "all" : "21");
}
} // namespace drakan_inventory_typography
