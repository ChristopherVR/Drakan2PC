// drakan_card_root.h: the memory cards are in the play folder whatever the ELF's folder or the working directory is,
// and saves an older build left next to the ELF are copied over without replacing or removing anything (issue #6).
// Synthetic folders only; pass a scratch folder as the first argument (default: the system temp folder).
#include "../src/lib/drakan_card_root.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {
namespace fs = std::filesystem;

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Relative file name -> content.
std::map<std::string, std::string> tree(const fs::path& folder) {
    std::map<std::string, std::string> files;
    if (!fs::exists(folder))
        return files;
    for (const auto& entry : fs::recursive_directory_iterator(folder))
        if (entry.is_regular_file())
            files[entry.path().lexically_relative(folder).generic_string()] = read(entry.path());
    return files;
}
} // namespace

int main(int argc, char** argv) {
    using namespace drakan_card_root;
    const std::string slot1 = "BESCES-50006Slot1";

    // The rule: DRAKAN_MEMCARD_DIR, else the play folder, else (no play folder on this platform) the ELF's folder.
    const fs::path elfDir = fs::path("C:/DrakanPC/build/game");
    assert(resolve(nullptr, "C:/DrakanPC/dist/DrakanPC/", elfDir) == fs::path("C:/DrakanPC/dist/DrakanPC/") / "mc0");
    assert(resolve("", "C:/DrakanPC/dist/DrakanPC/", elfDir) == fs::path("C:/DrakanPC/dist/DrakanPC/") / "mc0");
    assert(resolve("D:/scratch/card/mc0", "C:/DrakanPC/dist/DrakanPC/", elfDir) == fs::path("D:/scratch/card/mc0"));
    assert(resolve(nullptr, "", elfDir) == elfDir / "mc0");
    assert(resolve(nullptr, "", fs::path()).empty());
    // The ELF's folder never decides while there is a play folder.
    assert(resolve(nullptr, "C:/play/", "C:/a") == resolve(nullptr, "C:/play/", "D:/b"));

    assert(isCardFolderName("mc0") && isCardFolderName("mc1") && isCardFolderName("MC0"));
    assert(isCardFolderName("mc0_ootf") && isCardFolderName("mc1_testvalley"));
    assert(!isCardFolderName("mc2") && !isCardFolderName("mc0_") && !isCardFolderName("mc0x") &&
           !isCardFolderName("mc"));
    assert(!isCardFolderName("mods") && !isCardFolderName("mc01"));

    const fs::path root = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path()) / "drakan_card_root_policy_test";
    fs::remove_all(root);

    {
        // The reported state: the play folder's mc0 / mc1 are empty, the saves are next to the ELF.
        const fs::path old = root / "a/build/game", play = root / "a/dist/DrakanPC";
        fs::create_directories(play / "mc0");
        fs::create_directories(play / "mc1");
        write(old / "mc0" / slot1 / "Level 1A - Surdana.rsg", "save");
        write(old / "mc0" / slot1 / "icon.sys", "icon");
        write(old / "mc0_ootf" / slot1 / "a.rsg", "campaign");
        write(old / "mc0" / (slot1 + ".adopting") / "half", "an unfinished copy");
        write(old / "SCES_500.06", "elf");
        write(old / "bin" / "drakan.exe", "not a card");
        const auto before = tree(old);
        const fs::file_time_type stamp = fs::last_write_time(old / "mc0" / slot1 / "icon.sys") - std::chrono::hours(48);
        fs::last_write_time(old / "mc0" / slot1 / "icon.sys", stamp);

        const auto adopted = adoptMissingSaves(old, play);
        assert(adopted.size() == 2);
        assert(tree(play / "mc0") == (std::map<std::string, std::string>{{slot1 + "/Level 1A - Surdana.rsg", "save"},
                                                                         {slot1 + "/icon.sys", "icon"}}));
        assert(tree(play / "mc0_ootf") == (std::map<std::string, std::string>{{slot1 + "/a.rsg", "campaign"}}));
        assert(fs::last_write_time(play / "mc0" / slot1 / "icon.sys") == stamp); // the load menu shows the save's time
        assert(!fs::exists(play / "SCES_500.06") && !fs::exists(play / "bin"));
        assert(tree(old) == before); // a copy: the old place is untouched
        // Again: nothing left to do, nothing changes.
        assert(adoptMissingSaves(old, play).empty());
        assert(tree(old) == before);
    }
    {
        // Only the play folder has saves.
        const fs::path old = root / "b/build/game", play = root / "b/dist/DrakanPC";
        write(play / "mc0" / slot1 / "a.rsg", "mine");
        fs::create_directories(old / "mc0");
        assert(adoptMissingSaves(old, play).empty());
        assert(tree(play) == (std::map<std::string, std::string>{{"mc0/" + slot1 + "/a.rsg", "mine"}}));
        // No old place at all.
        assert(adoptMissingSaves(root / "b/none", play).empty());
        assert(adoptMissingSaves(fs::path(), play).empty());
    }
    {
        // Both have saves: the play folder's slot is never replaced, even by a newer one; a slot it lacks is copied.
        const fs::path old = root / "c/build/game", play = root / "c/dist/DrakanPC";
        write(play / "mc0" / slot1 / "a.rsg", "play save");
        write(old / "mc0" / slot1 / "a.rsg", "other save");
        write(old / "mc0" / slot1 / "extra.rsg", "a file the play folder's slot does not have");
        write(old / "mc0" / "BESCES-50006Slot2" / "b.rsg", "second slot");
        fs::last_write_time(old / "mc0" / slot1 / "a.rsg",
                            fs::last_write_time(play / "mc0" / slot1 / "a.rsg") + std::chrono::hours(1));
        const auto adopted = adoptMissingSaves(old, play);
        assert(adopted.size() == 1 && adopted[0].to == play / "mc0" / "BESCES-50006Slot2");
        assert(tree(play / "mc0") == (std::map<std::string, std::string>{{slot1 + "/a.rsg", "play save"},
                                                                         {"BESCES-50006Slot2/b.rsg", "second slot"}}));
        assert(read(old / "mc0" / slot1 / "a.rsg") == "other save");
    }
    {
        // After the builder has brought the saves over (its note is in the old place) the game copies nothing more: a
        // save deleted in the game must not come back from the old copy.
        const fs::path old = root / "e/build/game", play = root / "e/dist/DrakanPC";
        write(old / "mc0" / slot1 / "a.rsg", "already migrated, then deleted in the game");
        fs::create_directories(play / "mc0");
        const std::string playText = fs::absolute(play).generic_string();
        write(old / kBuilderNote, "The saves in this folder were copied ...\r\n\r\n1762900000 " + playText + "/\r\n");
        assert(adoptMissingSaves(old, play).empty());
        assert(tree(play / "mc0").empty());
    }
    {
        // Two play folders name one ELF folder: the note for the first one does not stop the copy for the second, and a
        // note without play folder lines (an unreleased format, or damaged) stops nothing.
        const fs::path old = root / "f/iso", first = root / "f/review/play", second = root / "f/dist/DrakanPC";
        write(old / "mc0" / slot1 / "a.rsg", "save");
        write(old / kBuilderNote, "1762900000 " + fs::absolute(first).string() + "\n");
        assert(adoptMissingSaves(old, second).size() == 1);
        assert(read(second / "mc0" / slot1 / "a.rsg") == "save");
        assert(adoptMissingSaves(old, first).empty());
        const fs::path third = root / "f/third";
        write(old / kBuilderNote,
              "The saves in this folder were copied to the play folder:\n  " + fs::absolute(third).string() + "\n");
        assert(adoptMissingSaves(old, third).size() == 1);
    }
    {
        // A play folder outside ASCII (the builder writes the note in UTF-8) is recognised.
        const fs::path old = root / "g/build/game", play = root / fs::path(u8"g/Spiele/Drakan Übung 文");
        write(old / "mc0" / slot1 / "a.rsg", "settled");
        const std::u8string text = fs::absolute(play).u8string();
        write(old / kBuilderNote, "1762900000 " + std::string(text.begin(), text.end()) + "\n");
        assert(adoptMissingSaves(old, play).empty());
        assert(!fs::exists(play / "mc0" / slot1));
    }
    {
        // The ELF next to the executable (a developer tree): the card folder is its own old place, nothing is copied.
        const fs::path play = root / "d/play";
        write(play / "mc0" / slot1 / "a.rsg", "mine");
        assert(adoptMissingSaves(play, play).empty());
        assert(adoptMissingSaves(play / ".", play).empty());
        assert(tree(play).size() == 1);
    }
    fs::remove_all(root);
    std::puts("card_root_policy: ok");
    return 0;
}
