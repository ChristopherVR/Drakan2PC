#include "hle/SaveBackup.h"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace fs = std::filesystem;

static std::string contents(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path workspace = fs::canonical(argv[1]);
    const fs::path root = workspace / ("save-backup-test-" + std::to_string(stamp));
    assert(fs::create_directory(root));
    const fs::path card = root / "mc0_ootf";
    const fs::path slot = card / "Slot5";
    fs::create_directories(slot);
    const fs::path primary = slot / "Book.rsg", companion = slot / "Village.rlt";
    std::ofstream(primary, std::ios::binary) << "old save";
    std::ofstream(slot / "Book.rsg.mods", std::ios::binary) << "old build";
    std::ofstream(companion, std::ios::binary) << "old companion";

    const std::array<uint8_t, 8> original{'o', 'l', 'd', ' ', 's', 'a', 'v', 'e'};
    const std::array<uint8_t, 3> changed{'n', 'e', 'w'};
    assert(!drakan_save_backup::archiveSlot(card, primary, changed));
    fs::create_directory(slot / "nested");
    assert(!drakan_save_backup::archiveSlot(card, primary, original));
    fs::remove(slot / "nested");
    assert(drakan_save_backup::archiveSlot(card, primary, original));
    assert(drakan_save_backup::archiveSlot(card, primary));
    const fs::path archives = root / "save-backups" / "ootf-auto" / "mc0_ootf" / "Slot5";
    auto it = fs::directory_iterator(archives);
    const fs::path first = it->path();
    assert(++it == fs::directory_iterator{});
    assert(contents(first / primary.filename()) == "old save");
    assert(contents(first / "Book.rsg.mods") == "old build");
    assert(contents(first / companion.filename()) == "old companion");
    assert(contents(primary) == "old save");

    std::ofstream(primary, std::ios::binary) << "different version";
    assert(drakan_save_backup::archiveSlot(card, primary));
    std::set<std::string> versions;
    for (const auto& entry : fs::directory_iterator(archives))
        versions.insert(contents(entry.path() / primary.filename()));
    assert((versions == std::set<std::string>{"old save", "different version"}));

    const fs::path level = card / "Village.rlt";
    std::ofstream(level, std::ios::binary) << "card root state";
    assert(drakan_save_backup::archiveOriginal(card, level));
    assert(!drakan_save_backup::archiveSlot(card, root / "outside.rsg"));
    fs::remove_all(root);
}
