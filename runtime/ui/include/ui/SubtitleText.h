// Display-only cleanup of a bounded native Windows-1252 subtitle line.
#pragma once
#include <cstddef>
#include <cstring>

namespace drakan_ui {
inline void tidySubtitleLine(char* text, size_t capacity) {
    if (!text)
        return;
    const char* end = static_cast<const char*>(std::memchr(text, 0, capacity));
    if (!end)
        return; // leave incomplete guest buffers untouched
    char* out = text;
    bool space = false;
    for (const char* p = text; p != end; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            space = out != text;
            continue;
        }
        if (space)
            *out++ = ' ';
        *out++ = *p;
        space = false;
    }
    *out = 0;
}
} // namespace drakan_ui
