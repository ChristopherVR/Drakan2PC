#include "gameui/GlyphText.h"

#include <mutex>

namespace gameui {
namespace {

std::mutex g_mutex;
GlyphNames g_names;
bool g_set = false;

// Index 0..7 of the glyph at text[i], and the number of characters it uses; 0 when there is none.
size_t glyphAt(std::string_view text, size_t i, int& index) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c >= 0x18 && c <= 0x1f) {
        index = c - 0x18;
        return 1;
    }
    if (c == '\\' && i + 3 < text.size() && text[i + 1] == '0' && text[i + 2] == '3' && text[i + 3] >= '0' &&
        text[i + 3] <= '7') {
        index = text[i + 3] - '0';
        return 4;
    }
    return 0;
}

} // namespace

GlyphNames defaultGlyphNames() {
    GlyphNames n;
    n.name = {"Circle", "Square", "Triangle", "Cross", "Left", "Right", "Up", "Down"};
    n.stickRun = "Left Stick";
    return n;
}

void setGlyphNames(GlyphNames names) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_names = std::move(names);
    g_set = true;
}

GlyphNames glyphNames() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_set ? g_names : defaultGlyphNames();
}

std::string expandGlyphEscapes(std::string_view text, const GlyphNames& names) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        int index = 0;
        size_t n = glyphAt(text, i, index);
        if (!n) {
            out.push_back(text[i++]);
            continue;
        }
        // Gather the run of consecutive glyphs.
        int count = 1;
        size_t end = i + n;
        std::array<int, 64> run{index};
        for (int k; end < text.size() && (n = glyphAt(text, end, k)) != 0 && count < 64; end += n)
            run[count++] = k;
        size_t dirs = 0;
        for (int k = 0; k < count; ++k)
            dirs += run[k] >= 4 ? 1 : 0;
        if (count == dirs && count >= 2 && !names.stickRun.empty()) {
            out += names.stickRun;
        } else {
            for (int k = 0; k < count; ++k) {
                if (k)
                    out.push_back(' ');
                out += names.name[run[k] & 7];
            }
        }
        i = end;
    }
    return out;
}

std::string expandGlyphEscapes(std::string_view text) {
    return expandGlyphEscapes(text, glyphNames());
}

} // namespace gameui
