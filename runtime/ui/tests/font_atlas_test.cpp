// CPU-only atlas validation. Reads owned disc/font files, never exports pixels.
#include "ui/FontAtlas.h"
#include "ui/Prompts.h"
#include "ui/Txd.h"
#include "hle/IsoFs.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL %s\n", what);
        }
    };
    drakan_ui::FontAtlas atlas;
    std::vector<drakan_ui::FontAtlasCell> cells;
    check(!drakan_ui::buildFont97Atlas(nullptr, 256, 256, "", atlas), "null rejected");
    std::vector<uint32_t> blank(256 * 256);
    check(!drakan_ui::font97AsciiCells(blank.data(), 256, 256, cells), "unidentified atlas rejected");
    if (argc < 3) {
        std::printf("%u checks, %u failures (disc/font cases require arguments)\n", checks, failures);
        return failures ? 1 : 0;
    }
    hle::IsoFs fs;
    std::string error;
    if (!fs.openIso(argv[1], &error)) {
        std::fprintf(stderr, "ISO: %s\n", error.c_str());
        return 2;
    }
    fs.setLanguageRoot("\\ENGLISH");
    hle::IsoFs::Node node;
    if (!fs.lookup("COMMON\\INTERFCE\\INTERFCE.TXD", node))
        return 2;
    const auto container = fs.readAll(node);
    if (container.size() < 16)
        return 2;
    uint32_t index, count;
    std::memcpy(&index, container.data() + 6, 4);
    std::memcpy(&count, container.data() + 10, 4);
    for (uint32_t i = 0; i < count; ++i) {
        if (static_cast<uint64_t>(index) + 14ull * (i + 1) > container.size())
            return 2;
        const auto* e = container.data() + index + i * 14;
        uint16_t type, id;
        uint32_t offset, size;
        std::memcpy(&type, e, 2);
        std::memcpy(&id, e + 2, 2);
        std::memcpy(&offset, e + 6, 4);
        std::memcpy(&size, e + 10, 4);
        if (type != 0x40 || (id != 96 && id != 97 && id != 124))
            continue;
        if (static_cast<uint64_t>(offset) + size > container.size())
            return 2;
        std::vector<uint8_t> blob(container.begin() + offset, container.begin() + offset + size);
        if (id == 97)
            blob = drakan_ui::PromptOverride::patchFont(blob, drakan_ui::Bindings::defaults());
        drakan_ui::TxdEdit tex;
        if (!tex.load(blob))
            return 2;
        std::vector<uint32_t> source(256 * 256);
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < 256; ++x) {
                const auto c = tex.get(x, 255 - y);
                source[y * 256 + x] = c.r | (c.g << 8) | (c.b << 16) | (c.a << 24);
            }
        if (id == 96) {
            check(drakan_ui::font96AsciiCells(source.data(), 256, 256, cells), "decorative 96 recognized");
            check(cells.size() == 95, "decorative 95 ASCII cells");
            check(!drakan_ui::font97AsciiCells(source.data(), 256, 256, cells), "decorative distinct from plain atlas");
            check(drakan_ui::buildFont96Atlas(source.data(), 256, 256, argv[2], atlas),
                  "decorative high resolution atlas builds");
            check(atlas.width == 2048 && atlas.height == 2048 && atlas.glyphsReplaced == 93,
                  "decorative printable glyph coverage");
            if (!atlas.rgba.empty()) {
                unsigned badMarkers = 0;
                for (unsigned y = 0; y < 256; y += 21)
                    for (unsigned x = 0; x < 256; ++x)
                        for (unsigned yy = 0; yy < 8; ++yy)
                            for (unsigned xx = 0; xx < 8; ++xx)
                                if (atlas.rgba[((255 - y) * 8 + yy) * 2048 + x * 8 + xx] != source[(255 - y) * 256 + x])
                                    ++badMarkers;
                check(!badMarkers, "decorative native marker rows unchanged");
            }
            continue;
        }
        if (id != 97) {
            check(!drakan_ui::font97AsciiCells(source.data(), 256, 256, cells), "96/controller cannot impersonate 97");
            continue;
        }
        check(drakan_ui::font97AsciiCells(source.data(), 256, 256, cells), "patched font97 recognized");
        check(cells.size() == 95, "95 ASCII metrics");
        check(cells[1].x == 9 && cells[1].y == 0 && cells[1].width == 3, "native ! source rectangle");
        check(!drakan_ui::buildFont97Atlas(source.data(), 256, 256, "missing-font.ttf", atlas) && atlas.rgba.empty(),
              "missing TTF falls back cleanly");
        check(drakan_ui::buildFont97Atlas(source.data(), 256, 256, argv[2], atlas), "TTF builds from installed font");
        if (atlas.rgba.empty())
            continue;
        check(atlas.width == 2048 && atlas.height == 2048, "8x atlas dimensions");
        unsigned sourceInkCells = 0;
        for (const auto& c : cells)
            if (c.codepoint != 32) {
                bool ink = false;
                for (unsigned y = 1; y < c.height; ++y)
                    for (unsigned x = 0; x < c.width; ++x)
                        ink = ink || (source[(255 - c.y - y) * 256 + c.x + x] >> 24) > 8;
                if (ink)
                    ++sourceInkCells;
            }
        std::printf("font97 replaced=%u source_ink_cells=%u\n", atlas.glyphsReplaced, sourceInkCells);
        check(atlas.glyphsReplaced == sourceInkCells, "all printable glyphs with native ink rasterized");
        std::vector<uint8_t> editable(256 * 256);
        for (const auto& c : cells)
            if (c.codepoint != 32)
                for (unsigned y = 1; y < c.height; ++y)
                    for (unsigned x = 0; x < c.width; ++x)
                        editable[(255 - c.y - y) * 256 + c.x + x] = 1;
        unsigned outsideChanges = 0, alphaErrors = 0, changed = 0;
        for (unsigned y = 0; y < 2048; ++y)
            for (unsigned x = 0; x < 2048; ++x) {
                const auto p = atlas.rgba[y * 2048 + x], s = source[(y / 8) * 256 + x / 8];
                if (p != s) {
                    ++changed;
                    if (!editable[(y / 8) * 256 + x / 8])
                        ++outsideChanges;
                }
                if ((p >> 24) > 128)
                    ++alphaErrors;
            }
        check(changed > 1000, "glyph ink actually changed");
        check(outsideChanges == 0, "prompt/nonASCII/marker/spacing copy exact outside editable glyphs");
        check(alphaErrors == 0, "GS alpha range preserved");
        std::printf("CELLS [");
        for (size_t j = 0; j < cells.size(); ++j) {
            const auto& c = cells[j];
            std::printf("%s[%u,%u,%u,%u,%u]", j ? "," : "", c.codepoint, c.x, c.y, c.width, c.height);
        }
        std::printf("]\n");
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
