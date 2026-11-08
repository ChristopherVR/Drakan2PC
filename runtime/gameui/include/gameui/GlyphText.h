// The game's button glyph escapes inside strings the modern screens read from guest memory.
//
// The game font draws eight button pictures for the text escapes \030..\037 (Circle, Square, Triangle, Cross, then
// the left stick directions Left, Right, Up, Down). The strings carry them either as the control bytes 0x18..0x1f or
// as the four characters backslash, 0, 3, digit (the form the XDB text tables store). The modern screens use their own
// font, which has no such pictures, so the escapes are replaced by the name of the control the player presses on the
// device in use. std only.
#pragma once
#include <array>
#include <string>
#include <string_view>

namespace gameui {

constexpr int kGlyphCount = 8;

// What each escape \030 + i reads as, and how a run of two or more direction glyphs reads.
struct GlyphNames {
    std::array<std::string, kGlyphCount> name;
    // Non-empty: a run of two or more direction escapes (\034..\037) becomes this one phrase ("Left Stick") instead of
    // the single names joined by spaces ("A D W S").
    std::string stickRun;
};

// PS2 pad names, used until the host knows the player's device.
GlyphNames defaultGlyphNames();
// The names the modern screens substitute; set once the prompts are known (host start).
void setGlyphNames(GlyphNames names);
GlyphNames glyphNames();

// Replaces every glyph escape (both forms) in `text`. Other backslashes and control bytes are kept.
std::string expandGlyphEscapes(std::string_view text, const GlyphNames& names);
// Same with the names set by setGlyphNames().
std::string expandGlyphEscapes(std::string_view text);

} // namespace gameui
