#include "Common.h"
#include "Font.h"

// Sony's eFont library is not used by Drakan. These entry points stay bindable but only report success; the
// upstream versions were translations of another title's library code and are not carried in this repository.
namespace ps2_stubs {
static void fontUnsupported(R5900Context* ctx, const char* name) {
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::fprintf(stderr, "[stub] %s: eFont is not implemented\n", name);
    }
    setReturnS32(ctx, 0);
}

void sceeFontInit(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontInit");
}
void sceeFontLoadFont(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontLoadFont");
}
void sceeFontGenerateString(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontGenerateString");
}
void sceeFontPrintfAt(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontPrintfAt");
}
void sceeFontPrintfAt2(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontPrintfAt2");
}
void sceeFontClose(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontClose");
}
void sceeFontSetColour(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontSetColour");
}
void sceeFontSetMode(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontSetMode");
}
void sceeFontSetFont(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontSetFont");
}
void sceeFontSetScale(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    fontUnsupported(ctx, "sceeFontSetScale");
}
} // namespace ps2_stubs
