#pragma once
// Custom game modes ([mode <id>] sections, runtime/net GameModes.h) known to the game: the compiled-in list of
// configs/mp_maps.ini, a player's mp_maps.ini and the mod packs' maps/*.ini, in that order (later wins). Header only.
// A menu lists customModes().modes(); the server rule execution for them is partial (docs/reference/netcode.md
// "Custom game modes").
#include "net/ArenaMaps.h"
#include "net/GameModes.h"

#include <string>

namespace drakan_modes {

inline drakan::net::ModeCatalog& customModes() {
    static drakan::net::ModeCatalog catalog;
    return catalog;
}

// Adds the mode sections of one map file; the first call with the built-in text happens at startup.
inline size_t addModes(const std::string& text, const std::string& source) {
    std::vector<std::string> errors;
    const size_t n = customModes().add(text, source, &errors);
    for (const std::string& e : errors)
        std::fprintf(stderr, "[modes] %s\n", e.c_str());
    return n;
}

} // namespace drakan_modes
