#include "../src/lib/drakan_language_policy.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    const char* names[] = {"english", "german", "french", "italian", "spanish"};
    const unsigned ids[] = {1, 4, 2, 5, 3};
    for (unsigned i = 0; i < 5; ++i) {
        const auto pal = drakan_language::select(names[i], nullptr, 5);
        assert(!pal.fallback && pal.osdLanguage == ids[i]);
        const auto usa = drakan_language::select(names[i], nullptr, 1);
        assert(usa.osdLanguage == 1 && usa.fallback == (i != 0));
    }
    const auto env = drakan_language::select("english", " /GeRmAn\\ ", 5);
    assert(env.osdLanguage == 4 && std::strcmp(env.root, "\\GERMAN") == 0 && !env.fallback);
    assert(drakan_language::select("german", "french", 5).osdLanguage == 2);
    for (const char* invalid : {"", "japanese", "GERMAN/LEVELS", "../GERMAN"}) {
        const auto result = drakan_language::select("german", invalid, 5);
        assert(result.fallback && result.osdLanguage == 1);
    }
    assert(drakan_language::select("invalid", nullptr, 5).fallback);
    drakan_language::setEffectiveLanguage(4);
    assert(std::strcmp(drakan_language::effectiveName(), "german") == 0);
    drakan_language::setEffectiveLanguage(1);
    assert(std::strcmp(drakan_language::effectiveName(), "english") == 0);
    std::cout << "Language policy tests passed\n";
}
