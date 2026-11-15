// Magic Crystal table parsing and the use test (drakan_crystals_policy.h).
#include "../src/lib/drakan_crystals_policy.h"
#include <cstdio>
#include <cstdlib>

using namespace drakan_crystals;
static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

int main() {
    std::map<uint32_t, Crystal> table;
    const char* text = "format = 1\n"
                       "\n"
                       "[crystal 0x34203]\n"
                       "effect = 0x20c07\n"
                       "kind = fire\n"
                       "name = Fire Crystal\n"
                       "\r\n"
                       "[crystal 0x34204] ; comment\n"
                       "effect = 205546 # decimal\n"
                       "kind = ice\n"
                       "[crystal 0x34205]\n"
                       "kind = lightning\n" // no effect: skipped
                       "[bow 0x342f5]\n"
                       "effect = 0x1\n" // another section: ignored
                       "[crystal nonsense]\n"
                       "effect = 0x2\n"
                       "[crystal 0x34206]\n"
                       "effect = 0x31234";
    check(parseCrystalTable(text, table) == 3, "three complete rows");
    check(table.size() == 3, "three crystals");
    check(table.at(0x34203).effect == 0x20c07 && table.at(0x34203).kind == "fire" &&
              table.at(0x34203).name == "Fire Crystal",
          "fire row");
    check(table.at(0x34204).effect == 205546 && table.at(0x34204).kind == "ice", "decimal effect, comments");
    check(!table.count(0x34205), "a row without an effect is skipped");
    check(table.at(0x34206).effect == 0x31234, "last row without a newline");

    // A later file wins for the same crystal.
    check(parseCrystalTable("[crystal 0x34203]\neffect = 0x99\n", table) == 1 && table.at(0x34203).effect == 0x99,
          "later file wins");
    check(parseCrystalTable("", table) == 0 && table.size() == 3, "empty file");

    check(isUseMessage(0x10cb, 0x1234), "use with a user");
    check(!isUseMessage(0x10cb, 0), "use without a user");
    check(!isUseMessage(0x1045, 0x1234), "another message");
    std::printf("crystals_policy: %u checks passed\n", checks);
    return 0;
}
