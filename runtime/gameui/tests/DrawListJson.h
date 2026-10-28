// Writes a DrawList as JSON lines for render_drawlist.py (gameui_preview, settings_pages_preview).
#pragma once
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "gameui/Draw.h"
#include "gameui/Layout.h"

namespace gameui::test {

inline std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\')
            o += '\\';
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        o += c;
    }
    return o;
}

inline std::string rect(const Rect& r) {
    char b[128];
    std::snprintf(b, sizeof b, "[%.2f,%.2f,%.2f,%.2f]", r.x, r.y, r.w, r.h);
    return b;
}

inline void write(const std::filesystem::path& dir, const std::string& name, const Viewport& vp, const char* scene,
                  const Rect& mapImage, const DrawList& d) {
    std::ofstream f(dir / (name + ".jsonl"), std::ios::binary);
    f << "{\"viewport\":" << rect({vp.x, vp.y, vp.w, vp.h}) << ",\"scene\":\"" << scene
      << "\",\"mapImage\":" << rect(mapImage) << "}\n";
    for (const DrawCmd& c : d.commands()) {
        f << "{\"k\":" << int(c.kind) << ",\"r\":" << rect(c.rect) << ",\"c\":" << c.color << ",\"c2\":" << c.color2
          << ",\"t\":" << c.thickness << ",\"rd\":" << c.rounding << ",\"p\":" << int(c.panel) << ",\"img\":["
          << int(c.image.kind) << "," << c.image.index << "],\"fb\":[" << int(c.fallback.kind) << ","
          << c.fallback.index << "],\"f\":" << int(c.font) << ",\"fs\":" << c.fontSize << ",\"a\":" << int(c.align)
          << ",\"s\":\"" << esc(c.text) << "\"}\n";
    }
    std::printf("wrote %s.jsonl (%zu commands)\n", name.c_str(), d.size());
}

} // namespace gameui::test
