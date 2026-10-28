// Host-only font texture generation; native text layout and guest font data stay intact.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace drakan_ui {
struct FontAtlasCell {
    uint32_t codepoint = 0, x = 0, y = 0, width = 0, height = 0; // upright/native source coordinates
};
struct FontAtlas {
    uint32_t width = 0, height = 0, glyphsReplaced = 0;
    std::vector<uint32_t> rgba; // stored bottom-up rows; R low byte, GS alpha 0..128
    std::vector<FontAtlasCell> cells;
};
// Recognizes the plain 256x256 atlas by marker layout, not dimensions alone.
// Copies all source pixels 8x. Only ASCII !..~ ink cells are replaced using
// the installed TTF, fitted to the original glyph's ink bounds. Prompts,
// accents, markers, spacing and native UVs are preserved. Never writes assets.
bool buildFont97Atlas(const uint32_t* source, uint32_t w, uint32_t h, const std::string& ttfPath, FontAtlas& out);
bool font97AsciiCells(const uint32_t* source, uint32_t w, uint32_t h, std::vector<FontAtlasCell>& cells);
// Decorative menu font, identified by both native marker rows. Keeps native
// cell positions/advances so radio labels and measurement remain consistent.
bool font96AsciiCells(const uint32_t* source, uint32_t w, uint32_t h, std::vector<FontAtlasCell>& cells);
bool buildFont96Atlas(const uint32_t* source, uint32_t w, uint32_t h, const std::string& ttfPath, FontAtlas& out);

// Inventory page-switch tile of the icon atlas INTERFCE.TXD 94 (128x128, upright texels). The two arrow buttons show
// only these windows of it (UVs from a draw log, docs/reference/ui-prompts.md); each arrow tip reaches a few texels
// further out at the middle row.
struct TileWindow {
    float x0, y0, x1, y1;
};
inline constexpr TileWindow kPageTileRect{49, 85, 85, 128};              // the stone tile
inline constexpr TileWindow kPageTileNext{56.1f, 90.4f, 71.3f, 105.7f};  // right button (R1), tip to x 76
inline constexpr TileWindow kPageTilePrev{58.5f, 106.3f, 74.1f, 123.3f}; // left button (L1), tip to x 52

// HD overlay for gsr::registerRuntimeOverlay: the stone tile of `source` (the generated native atlas, stored rows,
// GS alpha) upscaled `factor` times with the two key captions drawn as caps from the TTF at that resolution.
// coverage is 255 over the tile, 0 elsewhere (the rest of the atlas comes from the HD pack image).
struct TileOverlay {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> rgba;
    std::vector<uint8_t> coverage;
};
bool buildPageTileOverlay(const uint32_t* source, uint32_t w, uint32_t h, uint32_t factor, const std::string& nextText,
                          const std::string& prevText, const std::string& ttfPath, TileOverlay& out);
} // namespace drakan_ui
