// drakan::common: ASCII text helpers and UTF-8 path conversion.
#include "common/Path.h"
#include "common/Text.h"

#include <cstdio>

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)
} // namespace

int main() {
    using namespace drakan::common;

    CHECK(asciiLower(std::string("MiXeD_09")) == "mixed_09");
    CHECK(asciiUpper(std::string("MiXeD_09")) == "MIXED_09");
    // Bytes >= 0x80 (UTF-8 / Windows-1252 text) are never changed.
    const std::string accented = "\xC3\x89t\xE9";
    CHECK(asciiLower(accented) == "\xC3\x89t\xE9");
    CHECK(asciiUpper(accented) == "\xC3\x89T\xE9");

    CHECK(trim("  a b \t\r\n") == "a b");
    CHECK(trim("\v\fx\f") == "x");
    CHECK(trim("   ").empty());
    CHECK(trim("").empty());
    CHECK(trimView(" key ") == "key");

    CHECK(iequals("Mouse", "MOUSE"));
    CHECK(!iequals("Mouse", "Mous"));
    CHECK(!iequals("\xE9", "\xC9")); // not ASCII: no case folding
    CHECK(iequals("", ""));

    CHECK(startsWith("ACT:0x17", "ACT:"));
    CHECK(!startsWith("AC", "ACT:"));
    CHECK(endsWith("save.bin", ".bin"));
    CHECK(!endsWith("bin", ".bin"));

    // UTF-8 round trip through a path, including characters outside any single ANSI code page.
    const std::string utf8 = "Spiele/\xD0\x94\xD1\x80\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBD/\xE9\xBE\x8D/drakan_input.ini";
    const std::filesystem::path p = pathFromUtf8(utf8);
    CHECK(utf8FromPath(p) == utf8);
    CHECK(utf8FromPath(p.filename()) == "drakan_input.ini");
    CHECK(utf8FromPath(p.parent_path().filename()) == "\xE9\xBE\x8D");

    if (g_failures)
        std::printf("%d failure(s)\n", g_failures);
    else
        std::printf("common_test: all passed\n");
    return g_failures ? 1 : 0;
}
