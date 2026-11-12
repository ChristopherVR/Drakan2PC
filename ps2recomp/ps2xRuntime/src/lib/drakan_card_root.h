// Where the memory cards (the saves) live, and the saves an older build left somewhere else (issue #6).
//
// The cards belong to the play folder (the folder of drakan.exe): <play>/mc0, <play>/mc1 and the campaign cards
// <play>/mc0_<campaign>. Windows builds up to v0.1.11 put them next to the boot ELF instead, and drakan.ini names the
// ELF of the build folder, so an installer build saved into build\game\mc0, a folder the builder may delete.
// DRAKAN_MEMCARD_DIR still overrides the place (test runs, the browser build).
//
// Standard library only: no platform API, no game state.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace drakan_card_root {
namespace fs = std::filesystem;

// Card 1's folder. `overrideDir` is DRAKAN_MEMCARD_DIR, `playDir` the folder of the executable (empty on a platform
// that has none of its own, which keeps the old <ELF folder>/mc0). Empty when nothing is known.
inline fs::path resolve(const char* overrideDir, const std::string& playDir, const fs::path& elfDirectory) {
    if (overrideDir && *overrideDir)
        return fs::path(overrideDir);
    if (!playDir.empty())
        return fs::path(playDir) / "mc0";
    return elfDirectory.empty() ? fs::path() : elfDirectory / "mc0";
}

// mc0, mc1 and the campaign cards mc0_<name> / mc1_<name> (any case).
inline bool isCardFolderName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.size() < 3 || name.compare(0, 2, "mc") != 0 || (name[2] != '0' && name[2] != '1'))
        return false;
    return name.size() == 3 || (name.size() > 4 && name[3] == '_');
}

// Written into the old place by migrate_saves.py (MARKER) once its saves are in a play folder: one line
// "<seconds> <play folder>" (UTF-8) per play folder it was settled for, since several play folders can name one ELF.
inline constexpr const char* kBuilderNote = "saves-copied-to-play-folder.txt";

struct Adopted {
    fs::path from;
    fs::path to;
};

namespace detail {
inline bool sameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec) && !ec)
        return true;
    ec.clear();
    return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

inline fs::path fromUtf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

// True when the builder's note in `legacyDir` names `playDir`. A note that names only other play folders (or none,
// or cannot be read) does not stop the copy for this one.
inline bool noteNames(const fs::path& legacyDir, const fs::path& playDir) {
    std::ifstream note(legacyDir / kBuilderNote, std::ios::binary);
    for (std::string line; note && std::getline(note, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])))
            ++digits;
        if (digits == 0 || digits + 1 >= line.size() || line[digits] != ' ')
            continue;
        if (sameFolder(fromUtf8(line.substr(digits + 1)), playDir))
            return true;
    }
    return false;
}

// Regular files and folders only (a link is never followed out of the card); file times are kept.
inline bool copyTree(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(from, ec);
    if (ec)
        return false;
    if (fs::is_directory(status)) {
        fs::create_directories(to, ec);
        if (ec)
            return false;
        for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec))
            if (!copyTree(it->path(), to / it->path().filename()))
                return false;
        return !ec;
    }
    if (!fs::is_regular_file(status))
        return true;
    if (!fs::copy_file(from, to, fs::copy_options::none, ec) || ec)
        return false;
    const auto written = fs::last_write_time(from, ec);
    if (!ec)
        fs::last_write_time(to, written, ec);
    return true;
}
} // namespace detail

// Copies the saves of the old place into the play folder: every top-level entry (a save slot folder, the settings
// folder) of every card folder in `legacyDir` that the same card in `playDir` does not have. An entry the play folder
// already has is left alone whatever it holds, and nothing in `legacyDir` is changed or removed, so this can run on
// every start. The builder (scripts/buildtool/migrate_saves.py) settles the entries both places have and then leaves
// its note (kBuilderNote) in the old place: from then on nothing is copied into a play folder the note names, so a
// save the player deletes in the game does not come back.
inline std::vector<Adopted> adoptMissingSaves(const fs::path& legacyDir, const fs::path& playDir) {
    std::vector<Adopted> adopted;
    std::error_code ec;
    if (legacyDir.empty() || playDir.empty() || !fs::is_directory(legacyDir, ec) ||
        detail::sameFolder(legacyDir, playDir) || detail::noteNames(legacyDir, playDir))
        return adopted;
    const std::string kPartial = ".adopting";
    for (fs::directory_iterator cards(legacyDir, ec), end; !ec && cards != end; cards.increment(ec)) {
        const std::string card = cards->path().filename().string();
        std::error_code cardEc;
        if (!isCardFolderName(card) || !fs::is_directory(cards->symlink_status(cardEc)) || cardEc)
            continue;
        for (fs::directory_iterator units(cards->path(), cardEc), unitEnd; !cardEc && units != unitEnd;
             units.increment(cardEc)) {
            const std::string name = units->path().filename().string();
            if (name.size() > kPartial.size() &&
                name.compare(name.size() - kPartial.size(), kPartial.size(), kPartial) == 0)
                continue;
            const fs::path target = playDir / card / name;
            std::error_code unitEc;
            // Anything but "certainly not there" (a save, a link, an unreadable entry) is the play folder's own.
            if (fs::symlink_status(target, unitEc).type() != fs::file_type::not_found)
                continue;
            unitEc.clear();
            // Copy under another name first: a copy that was cut short never looks like a complete save.
            const fs::path partial = playDir / card / (name + kPartial);
            fs::remove_all(partial, unitEc);
            unitEc.clear();
            fs::create_directories(playDir / card, unitEc);
            if (unitEc || !detail::copyTree(units->path(), partial)) {
                fs::remove_all(partial, unitEc);
                continue;
            }
            fs::rename(partial, target, unitEc);
            if (unitEc) {
                fs::remove_all(partial, unitEc);
                continue;
            }
            adopted.push_back({units->path(), target});
        }
    }
    return adopted;
}
} // namespace drakan_card_root
