"""Saves that an older build wrote outside the play folder are copied into it (issue #6). Synthetic folders only."""

import importlib.util
import io
import os
import re
import shutil
import stat
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILDTOOL = ROOT / "scripts/buildtool"
spec = importlib.util.spec_from_file_location("migrate_saves", BUILDTOOL / "migrate_saves.py")
saves = importlib.util.module_from_spec(spec)
spec.loader.exec_module(saves)

SLOT1 = "BESCES-50006Slot1"
SETTINGS = "BESCES-50006Settings"


def write(path, text, age=0):
    """A file whose modification time is `age` seconds in the past."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    when = 1_700_000_000 - age
    os.utime(path, (when, when))
    return path


def tree(folder):
    return {p.relative_to(folder).as_posix(): p.read_text(encoding="utf-8") for p in folder.rglob("*") if p.is_file()}


class MigrateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.play, self.work = root / "dist/DrakanPC", root / "build/game"
        self.play.mkdir(parents=True)
        self.work.mkdir(parents=True)
        self.lines = []

    def tearDown(self):
        self.temp.cleanup()

    def run_migration(self, places=None, stamp="T1"):
        places = places if places is not None else [(self.work, True)]
        return saves.migrate(self.play, places, stamp=stamp, say=self.lines.append)

    def test_only_the_old_place_has_saves(self):
        # The reported state: empty mc0 / mc1 in the play folder, the real saves in the build folder.
        (self.play / "mc0").mkdir()
        (self.play / "mc1").mkdir()
        write(self.work / "mc0" / SLOT1 / "Level 1A - Surdana.rsg", "save", age=50)
        write(self.work / "mc0" / SLOT1 / "icon.sys", "icon", age=50)
        write(self.work / "mc0" / SETTINGS / "settings", "options")
        write(self.work / "mc0_ootf" / SLOT1 / "a.rsg", "campaign save")
        (self.work / "mc1").mkdir()
        before = tree(self.work)
        actions = self.run_migration()
        self.assertEqual({a[0] for a in actions}, {"copied"})
        self.assertEqual(len(actions), 3)
        self.assertEqual(tree(self.play / "mc0"), tree(self.work / "mc0"))
        self.assertEqual(tree(self.play / "mc0_ootf"), {f"{SLOT1}/a.rsg": "campaign save"})
        # File times travel with the save (the load menu shows them).
        copied = self.play / "mc0" / SLOT1 / "icon.sys"
        self.assertEqual(int(copied.stat().st_mtime), 1_700_000_000 - 50)
        # A copy, never a move: the old place keeps everything and gains only the note.
        after = tree(self.work)
        self.assertIn(saves.MARKER, after)
        del after[saves.MARKER]
        self.assertEqual(after, before)
        self.assertFalse((self.play / saves.BACKUPS).exists())

    def test_only_the_play_folder_has_saves(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "mine")
        (self.work / "mc0").mkdir()
        before = tree(self.play)
        self.assertEqual(self.run_migration(), [])
        self.assertEqual(tree(self.play), before)
        self.assertFalse((self.work / saves.MARKER).exists())  # nothing was there: no note, nothing changes
        self.assertEqual(self.lines, [])

    def test_no_card_folder_at_all(self):
        write(self.work / "SCES_500.06", "elf")
        write(self.work / "mcdonald" / "x", "not a card")
        self.assertEqual(self.run_migration(), [])
        self.assertEqual(tree(self.play), {})

    def test_both_have_the_slot_and_the_old_place_is_newer(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "old play save", age=500)
        write(self.play / "mc0" / "BESCES-50006Slot2" / "b.rsg", "only in play", age=10)
        write(self.work / "mc0" / SLOT1 / "a.rsg", "newer save from the build folder", age=5)
        write(self.work / "mc0" / "BESCES-50006Slot3" / "c.rsg", "only in the build folder", age=5)
        actions = self.run_migration()
        self.assertEqual(sorted(a[0] for a in actions), ["copied", "replaced"])
        card = tree(self.play / "mc0")
        self.assertEqual(card[f"{SLOT1}/a.rsg"], "newer save from the build folder")
        self.assertEqual(card["BESCES-50006Slot2/b.rsg"], "only in play")
        self.assertEqual(card["BESCES-50006Slot3/c.rsg"], "only in the build folder")
        # Nothing is lost: the replaced play folder save is in save-backups, with a note naming it.
        backup = self.play / saves.BACKUPS / "T1"
        self.assertEqual(
            (backup / "play-folder" / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "old play save"
        )
        self.assertIn(f"replaced: mc0/{SLOT1}", (backup / "README.txt").read_text(encoding="utf-8"))
        self.assertEqual(
            (self.work / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "newer save from the build folder"
        )

    def test_both_have_the_slot_and_the_play_folder_is_newer(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "newer play save", age=5)
        write(self.work / "mc0" / SLOT1 / "a.rsg", "older stray save", age=500)
        actions = self.run_migration()
        self.assertEqual([a[0] for a in actions], ["kept"])
        self.assertEqual(tree(self.play / "mc0"), {f"{SLOT1}/a.rsg": "newer play save"})
        kept = self.play / saves.BACKUPS / "T1" / "other-folder" / "mc0" / SLOT1 / "a.rsg"
        self.assertEqual(kept.read_text(encoding="utf-8"), "older stray save")
        self.assertEqual((self.work / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "older stray save")

    def test_a_slot_is_one_unit(self):
        # One file of the slot is newer in the old place, another newer in the play folder: the slot with the newest
        # file wins as a whole, files of two saves are never mixed.
        write(self.play / "mc0" / SLOT1 / "a.rsg", "play a", age=100)
        write(self.play / "mc0" / SLOT1 / "icon.sys", "play icon", age=300)
        write(self.work / "mc0" / SLOT1 / "a.rsg", "stray a", age=200)
        write(self.work / "mc0" / SLOT1 / "icon.sys", "stray icon", age=50)
        self.run_migration()
        self.assertEqual(tree(self.play / "mc0" / SLOT1), {"a.rsg": "stray a", "icon.sys": "stray icon"})
        backup = self.play / saves.BACKUPS / "T1" / "play-folder" / "mc0" / SLOT1
        self.assertEqual(tree(backup), {"a.rsg": "play a", "icon.sys": "play icon"})

    def test_identical_saves_are_left_alone(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "same", age=10)
        write(self.work / "mc0" / SLOT1 / "a.rsg", "same", age=900)
        self.assertEqual([a[0] for a in self.run_migration()], ["same"])
        self.assertFalse((self.play / saves.BACKUPS).exists())

    def test_a_rescue_place_never_replaces_a_play_folder_save(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "play", age=500)
        write(self.work / "mc0" / SLOT1 / "a.rsg", "test run save", age=5)
        write(self.work / "mc0" / "BESCES-50006Slot4" / "d.rsg", "missing one", age=5)
        actions = self.run_migration(places=[(self.work, False)])
        self.assertEqual(sorted(a[0] for a in actions), ["copied", "kept"])
        self.assertEqual((self.play / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "play")
        self.assertTrue((self.play / "mc0" / "BESCES-50006Slot4" / "d.rsg").is_file())
        kept = self.play / saves.BACKUPS / "T1" / "other-folder" / "mc0" / SLOT1 / "a.rsg"
        self.assertEqual(kept.read_text(encoding="utf-8"), "test run save")

    def test_a_second_run_does_nothing_until_the_old_place_changes(self):
        write(self.work / "mc0" / SLOT1 / "a.rsg", "save", age=50)
        self.run_migration()
        # The player goes on playing from the play folder.
        write(self.play / "mc0" / SLOT1 / "a.rsg", "played on")
        self.assertEqual(self.run_migration(stamp="T2"), [])
        self.assertEqual((self.play / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "played on")
        self.assertFalse((self.play / saves.BACKUPS).exists())
        # An old game is run once more and saves into the old place: that save is brought over, the play one is kept.
        marker = (self.work / saves.MARKER).stat().st_mtime
        newer = self.work / "mc0" / SLOT1 / "a.rsg"
        newer.write_text("old game saved again", encoding="utf-8")
        os.utime(newer, (marker + 100, marker + 100))
        os.utime(self.play / "mc0" / SLOT1 / "a.rsg", (marker + 50, marker + 50))
        self.assertEqual([a[0] for a in self.run_migration(stamp="T3")], ["replaced"])
        self.assertEqual((self.play / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "old game saved again")
        backup = self.play / saves.BACKUPS / "T3" / "play-folder" / "mc0" / SLOT1 / "a.rsg"
        self.assertEqual(backup.read_text(encoding="utf-8"), "played on")

    def test_unfinished_copies_are_not_saves(self):
        write(self.work / "mc0" / (SLOT1 + ".adopting") / "a.rsg", "half")
        write(self.work / "mc0" / SLOT1 / "a.rsg", "whole")
        write(self.play / "mc0" / (SLOT1 + ".migrating") / "junk", "left by a stopped run")
        self.run_migration()
        self.assertEqual(tree(self.play / "mc0"), {f"{SLOT1}/a.rsg": "whole"})

    def test_two_play_folders_that_name_one_elf_folder_each_get_the_saves(self):
        # The developer tree: dist/DrakanPC and a review copy both name iso/SCES_500.06. Settling the copy must not stop
        # the real play folder from getting the saves.
        second = self.play.parent / "second-play"
        write(self.work / "mc0" / SLOT1 / "a.rsg", "save", age=50)
        self.run_migration()
        self.assertEqual(saves.migrate(second, [(self.work, True)], stamp="T2", say=self.lines.append)[0][0], "copied")
        self.assertEqual((second / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "save")
        entries = saves.note_entries(self.work)
        self.assertEqual(set(entries), {saves.folder_key(self.play), saves.folder_key(second)})
        # Both are settled now.
        self.assertEqual(self.run_migration(stamp="T3"), [])
        self.assertEqual(saves.migrate(second, [(self.work, True)], stamp="T4", say=self.lines.append), [])

    def test_a_note_without_play_folder_lines_settles_nothing(self):
        # The note format of the unreleased first version of this script (it was written into iso/ by a review build).
        write(self.work / "mc0" / SLOT1 / "a.rsg", "save", age=50)
        (self.work / saves.MARKER).write_text(
            f"The saves in this folder were copied to the play folder:\n  {self.play.parent / 'other'}\n", "utf-8"
        )
        self.assertEqual([a[0] for a in self.run_migration()], ["copied"])

    def test_a_play_folder_outside_ascii_is_recognised_in_the_note(self):
        play = self.play.parent / "Spiele Übung 文"
        write(self.work / "mc0" / SLOT1 / "a.rsg", "save", age=50)
        saves.migrate(play, [(self.work, True)], stamp="T1", say=self.lines.append)
        self.assertIn(str(play.resolve()), (self.work / saves.MARKER).read_text(encoding="utf-8"))
        self.assertIsNotNone(saves.settled_at(self.work, play))
        self.assertEqual(saves.migrate(play, [(self.work, True)], stamp="T2", say=self.lines.append), [])

    def test_a_save_deleted_in_the_game_does_not_come_back_on_a_later_run(self):
        write(self.work / "mc0" / SLOT1 / "a.rsg", "deleted later", age=500)
        write(self.work / "mc0" / "BESCES-50006Slot2" / "b.rsg", "kept on playing", age=500)
        self.run_migration()
        settled = saves.settled_at(self.work, self.play)
        shutil.rmtree(self.play / "mc0" / SLOT1)  # deleted in the game
        # An old game saves into another slot of the old place afterwards: the place is read again.
        newer = write(self.work / "mc0" / "BESCES-50006Slot3" / "c.rsg", "new old-game save")
        os.utime(newer, (settled + 100, settled + 100))
        actions = {a[1]: a[0] for a in self.run_migration(stamp="T2")}
        self.assertEqual(actions["mc0/BESCES-50006Slot3"], "copied")
        self.assertEqual(actions[f"mc0/{SLOT1}"], "kept")
        self.assertFalse((self.play / "mc0" / SLOT1).exists())
        kept = self.play / saves.BACKUPS / "T2" / "other-folder" / "mc0" / SLOT1 / "a.rsg"
        self.assertEqual(kept.read_text(encoding="utf-8"), "deleted later")

    def test_before_a_delete_everything_not_in_the_play_folder_is_kept_even_with_a_wrong_clock(self):
        write(self.work / "mc0" / SLOT1 / "a.rsg", "first", age=500)
        self.run_migration()
        settled = saves.settled_at(self.work, self.play)
        # The player goes on in the play folder; an old game also saved into the old place, but the PC clock was behind
        # so that save looks older than the note: a normal run skips the place.
        write(self.play / "mc0" / SLOT1 / "a.rsg", "played on")
        os.utime(self.play / "mc0" / SLOT1 / "a.rsg", (settled + 10, settled + 10))
        late = write(self.work / "mc0" / "BESCES-50006Slot2" / "b.rsg", "old game, wrong clock")
        os.utime(late, (settled - 1000, settled - 1000))
        self.assertEqual(self.run_migration(stamp="T2"), [])
        # Before the build folder is deleted every save that is not in the play folder is kept somewhere.
        actions = {a[1]: a[0] for a in saves.migrate(self.play, [(self.work, True)], "T3", self.lines.append, True)}
        self.assertEqual(actions, {f"mc0/{SLOT1}": "kept", "mc0/BESCES-50006Slot2": "kept"})
        backup = self.play / saves.BACKUPS / "T3" / "other-folder" / "mc0"
        self.assertEqual((backup / "BESCES-50006Slot2" / "b.rsg").read_text(encoding="utf-8"), "old game, wrong clock")
        self.assertEqual((backup / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "first")
        self.assertEqual((self.play / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "played on")
        self.assertIn("README.txt", os.listdir(self.play / saves.BACKUPS / "T3"))

    def test_loose_files_on_a_card_are_units_too(self):
        # Order of the Flame saves are loose files on the campaign card (mc0_ootf/OotF 01 - Ruined Village.rsg).
        write(self.work / "mc0_ootf" / "OotF 01 - Ruined Village.rsg", "newer", age=5)
        write(self.work / "mc0_ootf" / "OotF 01 - Ruined Village.rsg.mods", "mods")
        write(self.play / "mc0_ootf" / "OotF 01 - Ruined Village.rsg", "older", age=500)
        actions = {a[1]: a[0] for a in self.run_migration()}
        self.assertEqual(actions["mc0_ootf/OotF 01 - Ruined Village.rsg"], "replaced")
        self.assertEqual(actions["mc0_ootf/OotF 01 - Ruined Village.rsg.mods"], "copied")
        kept = self.play / saves.BACKUPS / "T1" / "play-folder" / "mc0_ootf" / "OotF 01 - Ruined Village.rsg"
        self.assertEqual(kept.read_text(encoding="utf-8"), "older")

    def test_a_read_only_leftover_of_a_stopped_copy_is_cleared(self):
        leftover = write(self.play / "mc0" / (SLOT1 + ".migrating") / "a.rsg", "half")
        os.chmod(leftover, stat.S_IREAD)
        source = write(self.work / "mc0" / SLOT1 / "a.rsg", "whole")
        os.chmod(source, stat.S_IREAD)
        try:
            self.run_migration()
            self.assertEqual(tree(self.play / "mc0"), {f"{SLOT1}/a.rsg": "whole"})
        finally:
            for p in (self.work / "mc0" / SLOT1 / "a.rsg", self.play / "mc0" / SLOT1 / "a.rsg"):
                if p.exists():
                    os.chmod(p, stat.S_IWRITE | stat.S_IREAD)

    def test_the_play_folder_itself_is_never_a_source(self):
        write(self.play / "mc0" / SLOT1 / "a.rsg", "mine")
        self.assertEqual(self.run_migration(places=[(self.play, True)]), [])
        self.assertFalse((self.play / saves.MARKER).exists())


class OldPlaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.play, self.work, self.other = root / "play", root / "build/game", root / "iso"
        for folder in (self.play, self.work, self.other):
            folder.mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def ini(self, elf):
        text = f"[video]\nelf = wrong section\n[general]\n; elf = commented\niso = D:\\disc.iso\nelf = {elf}  ; note\n"
        (self.play / "drakan.ini").write_text(text, encoding="utf-8")

    def test_installer_layout_the_elf_is_in_the_build_folder(self):
        self.ini(self.work / "SCES_500.06")
        self.assertEqual(saves.old_places(self.play, self.work), [(self.work.resolve(), True)])

    def test_another_elf_folder_comes_first_and_the_build_folder_is_a_rescue(self):
        self.ini(self.other / "SCES_500.06")
        self.assertEqual(
            saves.old_places(self.play, self.work), [(self.other.resolve(), True), (self.work.resolve(), False)]
        )

    def test_without_an_ini_the_build_folder_is_the_old_place(self):
        self.assertEqual(saves.old_places(self.play, self.work), [(self.work.resolve(), True)])
        self.assertEqual(saves.old_places(self.play), [])

    def test_command_line_end_to_end(self):
        self.ini(self.work / "SCES_500.06")
        write(self.work / "mc0" / SLOT1 / "a.rsg", "save")
        self.assertEqual(saves.main(["--play", str(self.play), "--work", str(self.work)]), 0)
        self.assertEqual((self.play / "mc0" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "save")

    def test_the_summary_tells_copied_and_kept_saves_apart(self):
        self.ini(self.work / "SCES_500.06")
        write(self.work / "mc0" / SLOT1 / "a.rsg", "only here")
        write(self.work / "mc0" / "BESCES-50006Slot2" / "b.rsg", "older", age=500)
        write(self.play / "mc0" / "BESCES-50006Slot2" / "b.rsg", "newer", age=5)
        out = io.StringIO()
        with redirect_stdout(out):
            saves.main(["--play", str(self.play), "--work", str(self.work)])
        text = out.getvalue()
        self.assertIn("(1 save folder(s) brought over)", text)
        self.assertIn("1 other version(s) of saves are kept in", text)

    def test_a_relative_elf_line_is_not_a_place(self):
        # The game resolves it from its own working directory; guessing here could pick another folder's cards.
        self.ini("SCES_500.06")
        self.assertEqual(saves.old_places(self.play, self.work), [(self.work.resolve(), True)])

    def test_a_missing_play_folder_is_made_for_the_rescue(self):
        play = self.play.parent / "new-play"
        write(self.work / "mc1" / SLOT1 / "a.rsg", "save")
        self.assertEqual(saves.main(["--play", str(play), "--work", str(self.work)]), 0)
        self.assertEqual((play / "mc1" / SLOT1 / "a.rsg").read_text(encoding="utf-8"), "save")


class BuilderWiringTests(unittest.TestCase):
    """The builders call the migration where saves could otherwise be lost."""

    def test_build_drakan_rescues_saves_before_every_build_folder_delete(self):
        text = (BUILDTOOL / "Build-Drakan.ps1").read_text(encoding="utf-8")
        self.assertIn("'migrate_saves.py') --play $DistDirectory --work $WorkDir", text)
        deletes = [line for line in text.splitlines() if re.search(r"Remove-Item -LiteralPath \$WorkDir\b", line)]
        self.assertEqual(len(deletes), 2)
        for line in deletes:
            index = text.index(line)
            rescue = text.rfind("& $rescueSaves", 0, index + len(line))
            self.assertGreater(rescue, -1)
            # The rescue is the statement right before the delete (same line, or the line above).
            between = text[rescue : index + line.index("Remove-Item")]
            self.assertLessEqual(between.count("\n"), 1, between)
        # A failed rescue stops the script before the delete.
        self.assertRegex(text, r"if \(\$LASTEXITCODE\) \{\s*throw \"Saves in \$WorkDir could not be copied")

    def test_build_local_game_copies_the_saves_and_keeps_the_backups(self):
        text = (BUILDTOOL / "Build-LocalGame.ps1").read_text(encoding="utf-8")
        keep = next(line for line in text.splitlines() if line.strip().startswith("$keep = "))
        for name in ("'mc0'", "'mc1'", "'mc0_*'", "'mc1_*'", "'save-backups'", "'*.ini'"):
            self.assertIn(name, keep)
        call = text.index("'migrate_saves.py'),'--play',$DistDirectory,'--work',$WorkDir")
        self.assertGreater(call, text.index("Copying the game into the play folder"))
        # The build never deletes its own WorkDir, so the old place survives a failed copy.
        self.assertNotRegex(text, r"Remove-Item -LiteralPath \$WorkDir\b")

    def test_the_uninstaller_copies_the_saves_before_it_deletes_the_build_folder(self):
        text = (BUILDTOOL / "kit/DrakanPC-Setup.iss").read_text(encoding="utf-8")
        self.assertIn('Type: filesandordirs; Name: "{app}\\build"', text)
        self.assertIn("function InitializeUninstall(): Boolean;", text)
        self.assertIn("scripts\\buildtool\\migrate_saves.py", text)
        # It reads every old place again (the build folder goes next), stops for any card folder, campaign cards
        # included, and a silent uninstall answers No.
        self.assertIn("--before-delete", text)
        self.assertIn("(Code <> 0) and HasCardFolder(Work)", text)
        self.assertIn("'mc0_'", text)
        self.assertRegex(text, r"SuppressibleMsgBox\([^;]*IDNO\) = IDYES;")
        # ISPP reads a line that starts with # as a directive: the message's line breaks must not start a line.
        code = text[text.index("function InitializeUninstall") :]
        self.assertFalse([line for line in code.splitlines() if line.lstrip().startswith("#")])

    def test_the_rescue_before_a_delete_reads_settled_places_again(self):
        text = (BUILDTOOL / "Build-Drakan.ps1").read_text(encoding="utf-8")
        self.assertIn("--play $DistDirectory --work $WorkDir --before-delete", text)

    def test_only_windows_builds_migrate(self):
        # Native Linux always kept its cards next to the executable; its WorkDir cards are not a Linux player's.
        text = (BUILDTOOL / "Build-LocalGame.ps1").read_text(encoding="utf-8")
        call = text.index("'migrate_saves.py'),'--play',$DistDirectory")
        self.assertIn("if ($TargetPlatform -eq 'windows') {", text[call - 800 : call])

    def test_the_game_knows_the_builders_note(self):
        # The game stops copying saves from the old place once this note is there (a deleted save must not come back).
        header = (ROOT / "ps2recomp/ps2xRuntime/src/lib/drakan_card_root.h").read_text(encoding="utf-8")
        self.assertIn(f'kBuilderNote = "{saves.MARKER}"', header)
        runtime = (ROOT / "ps2recomp/ps2xRuntime/src/lib/ps2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("drakan_card_root::resolve(mc, playDir, paths.elfDirectory)", runtime)
        self.assertNotIn('paths.mcRoot = paths.elfDirectory / "mc0";\n    }\n#if defined(__linux__)', runtime)
        # Only Windows copies (native Linux never saved next to the ELF), and a failure never stops the boot.
        adopt = runtime.index("drakan_card_root::adoptMissingSaves(")
        self.assertEqual(runtime.rfind("#if defined(_WIN32)\n", 0, adopt), runtime.rfind("#if", 0, adopt))
        self.assertIn("catch (const std::exception& error)", runtime[adopt : adopt + 800])
        self.assertIn("detail::noteNames(legacyDir, playDir)", header)


if __name__ == "__main__":
    unittest.main()
