// CPU affinity for the game process: which logical processors the game's threads may run on.
//
// Setting: drakan.ini [general] cpu_affinity (env DRAKAN_CPU_AFFINITY), one of
//   auto | all | (empty)   leave the affinity as Windows gave it (default)
//   0x<hex>                a mask of logical processors, bit 0 = processor 0 (0xFFF = the first twelve)
//   <hex digits>           the same without the prefix is NOT accepted: "12" is ambiguous (count or mask)
//   cores:<n>              the first n logical processors of the process's allowed set
// A mask is intersected with what the process may use. A mask that shares no processor with the allowed set is
// ignored (the game would otherwise have nowhere to run). Windows only, first processor group (up to 64 logical
// processors); a no-op elsewhere. The parser is portable so it can be tested everywhere.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace drakan_input {

struct AffinitySpec {
    enum class Kind { Auto, Mask, Cores } kind = Kind::Auto;
    uint64_t value = 0; // Mask: the mask; Cores: the count
};

// Parses the setting text. Returns false and sets `error` (a sentence a player can act on) on bad input.
bool parseAffinity(std::string_view text, AffinitySpec& out, std::string* error);

// The mask to apply given the processors the process may use (`allowed`). 0 = leave unchanged (auto, or nothing in
// common); `note` explains a clipped or ignored request.
uint64_t resolveAffinity(const AffinitySpec& spec, uint64_t allowed, std::string* note);

// Parses `text` and applies it to the current process. Returns a one-line log message ("" when nothing changed and
// nothing is worth saying). No-op off Windows.
std::string applyProcessAffinity(std::string_view text);

} // namespace drakan_input
