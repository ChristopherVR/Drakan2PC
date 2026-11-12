"""Bring saves that an older build wrote outside the play folder into the play folder (issue #6).

Windows builds up to v0.1.11 kept the memory cards next to the boot ELF named in drakan.ini instead of next to
drakan.exe. The builder writes `elf = <build folder>/SCES_500.06`, so an installer build saved into `build/game/mc0`
while `dist/DrakanPC/mc0` stayed empty, and the build folder is deleted by "start over" and by the uninstaller. The
game now always uses `<play folder>/mc0`; this step copies what the old place holds.

Rules. Nothing in the old place is changed or removed (it only gets the note below), and nothing is deleted anywhere:

* A card folder is `mc0`, `mc1` or a campaign card `mc0_<name>` / `mc1_<name>`. Its top-level entries (one folder
  per save slot, one for the settings) are handled one by one; a slot folder is one unit, its newest file decides.
* An entry the play folder does not have is copied.
* An entry that is the same in both places is left alone.
* An entry that differs: the newer one becomes the play folder's entry and the other one is kept under
  `<play folder>/save-backups/<time>/`, `play-folder/...` for a replaced play folder entry and `other-folder/...` for
  an old place's entry that was not used. From a place the game did not save to itself (the build folder when
  drakan.ini names another ELF) the play folder's entry always stays.
* A finished place gets the note `saves-copied-to-play-folder.txt`, one line `<seconds> <play folder>` per play folder
  it was settled for (several play folders can name one ELF). For that play folder the place is read again only when
  a save in it is newer than its line (an old game was run again), or with --before-delete. An entry that is not newer
  than the line was settled already: it never goes back into the card (the player may have deleted or overwritten it
  in the game since), but when it is missing from or differs from the play folder's card it is kept in
  `save-backups/<time>/other-folder/`, so even a wrong clock loses nothing.

Usage: migrate_saves.py --play <play folder> [--work <build folder>] [--before-delete]
"""

import argparse
import filecmp
import os
import re
import shutil
import stat
import sys
import time
from pathlib import Path

CARD = re.compile(r"^mc[01](_.+)?$", re.IGNORECASE)
MARKER = "saves-copied-to-play-folder.txt"
NOTE_LINE = re.compile(r"^(\d+) (.+)$")
BACKUPS = "save-backups"
PARTIAL = (".migrating", ".adopting")  # this script's and the game's own unfinished copies


def is_link(path):
    return path.is_symlink() or getattr(os.path, "isjunction", lambda _: False)(path)


def card_folders(folder):
    """The real card folders directly in `folder` (links and junctions are someone else's cards)."""
    if not folder.is_dir():
        return []
    return sorted(p for p in folder.iterdir() if CARD.match(p.name) and p.is_dir() and not is_link(p))


def unit_files(unit):
    if unit.is_dir():
        return sorted(p for p in unit.rglob("*") if p.is_file())
    return [unit]


def newest(unit):
    return max((p.stat().st_mtime for p in unit_files(unit)), default=unit.stat().st_mtime)


def same(a, b):
    if a.is_dir() != b.is_dir():
        return False
    if not a.is_dir():
        return filecmp.cmp(a, b, shallow=False)
    names = [p.relative_to(a) for p in unit_files(a)]
    if names != [p.relative_to(b) for p in unit_files(b)]:
        return False
    return all(filecmp.cmp(a / name, b / name, shallow=False) for name in names)


def remove(path, ignore_errors=False):
    """Remove a leftover unfinished copy, read-only files included (a copied save keeps its read-only flag)."""

    def writable_again(func, name, _):
        os.chmod(name, stat.S_IWRITE)
        func(name)

    try:
        if path.is_dir() and not is_link(path):
            if sys.version_info >= (3, 12):
                shutil.rmtree(path, onexc=writable_again)
            else:
                shutil.rmtree(path, onerror=writable_again)
        elif path.exists() or is_link(path):
            os.chmod(path, stat.S_IWRITE)
            path.unlink()
    except OSError:
        if not ignore_errors:
            raise


def copy_unit(source, target):
    """Copy under another name first, so a copy that was cut short never looks like a complete save."""
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + PARTIAL[0])
    remove(partial)
    try:
        if source.is_dir():
            shutil.copytree(source, partial, symlinks=True)
        else:
            shutil.copy2(source, partial)
        if target.exists():
            raise FileExistsError(f"{target} appeared while copying; nothing was replaced")
        partial.rename(target)
    except BaseException:
        remove(partial, ignore_errors=True)
        raise


def free_name(path):
    candidate, n = path, 2
    while candidate.exists():
        candidate = path.with_name(f"{path.name}-{n}")
        n += 1
    return candidate


def read_elf(play):
    """`[general] elf` of the play folder's drakan.ini: the old card place was this file's folder."""
    try:
        text = (play / "drakan.ini").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    section = ""
    for line in text.splitlines():
        line = line.split(" ;")[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
        elif section == "general" and re.match(r"^elf\s*=", line, re.IGNORECASE):
            value = line.split("=", 1)[1].strip()
            return Path(value) if value else None
    return None


def old_places(play, work=None):
    """[(folder, prefer_newer)]: the folder the old game saved to first, then the build folder as a rescue only."""
    places = []
    elf = read_elf(play)
    if elf is not None and elf.is_absolute() and elf.parent.is_dir():
        places.append((elf.parent.resolve(), True))
    if work is not None and Path(work).is_dir():
        work = Path(work).resolve()
        if not places:
            places.append((work, True))
        elif all(work != folder for folder, _ in places):
            places.append((work, False))
    return places


def folder_key(path):
    return os.path.normcase(str(Path(path).resolve()))


def note_entries(folder):
    """{play folder key: (seconds, play folder as written)} from the note in `folder` (none: not settled for anyone).
    A note without such lines (written by an unreleased build of this script) settles nothing."""
    try:
        text = (folder / MARKER).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    entries = {}
    for line in text.splitlines():
        match = NOTE_LINE.match(line.strip())
        if match:
            entries[folder_key(match.group(2))] = (int(match.group(1)), match.group(2))
    return entries


def settled_at(folder, play):
    entry = note_entries(folder).get(folder_key(play))
    return entry[0] if entry else None


def write_note(folder, play, when):
    entries = note_entries(folder)
    entries[folder_key(play)] = (when, str(Path(play).resolve()))
    lines = [
        "The saves in this folder were copied to the play folder(s) below. A current game started from one of",
        "them reads and writes its saves there, not here; nothing in this folder was changed or removed.",
        "Each line: the time of the copy (seconds since 1970) and the play folder. Do not edit it.",
        "",
    ]
    lines += [f"{seconds} {shown}" for seconds, shown in sorted(entries.values(), key=lambda e: e[1].lower())]
    temporary = folder / (MARKER + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, folder / MARKER)


def already_done(folder, cards, play):
    done = settled_at(folder, play)
    if done is None:
        return False
    return not any(p.stat().st_mtime > done for card in cards for p in card.rglob("*") if p.is_file())


def migrate(play, places, stamp=None, say=print, before_delete=False, now=None):
    """Apply the rules of the module text. Returns the actions as (action, card/entry, detail) tuples:
    copied / replaced (now in the play folder's card), kept (set aside in save-backups), same."""
    play = Path(play).resolve()
    stamp = stamp or time.strftime("%Y%m%d-%H%M%S")
    actions = []
    backup_root = None

    def backup(kind, card, unit):
        nonlocal backup_root
        if backup_root is None:
            backup_root = free_name(play / BACKUPS / stamp)
        return free_name(backup_root / kind / card.name / unit.name)

    for folder, prefer_newer in places:
        folder = Path(folder).resolve()
        if folder == play:
            continue
        cards = card_folders(folder)
        if not any(any(card.iterdir()) for card in cards):
            continue
        if not before_delete and already_done(folder, cards, play):
            continue
        settled = settled_at(folder, play)
        started = int(time.time() if now is None else now)
        say(f"Saves found outside the play folder, in {folder}")
        for card in cards:
            for unit in sorted(card.iterdir()):
                if unit.name.endswith(PARTIAL) or is_link(unit):
                    continue
                label = f"{card.name}/{unit.name}"
                target = play / card.name / unit.name
                was_settled = settled is not None and newest(unit) <= settled
                if not target.exists() and not was_settled:
                    copy_unit(unit, target)
                    actions.append(("copied", label, str(target)))
                    say(f"  copied {label} into the play folder")
                    continue
                if target.exists() and same(unit, target):
                    actions.append(("same", label, str(target)))
                    continue
                if not was_settled and prefer_newer and newest(unit) > newest(target):
                    kept = backup("play-folder", card, unit)
                    kept.parent.mkdir(parents=True, exist_ok=True)
                    target.rename(kept)
                    try:
                        copy_unit(unit, target)
                    except BaseException:
                        kept.rename(target)  # put the play folder's own save back
                        raise
                    actions.append(("replaced", label, str(kept)))
                    say(f"  {label}: the save outside the play folder is newer and is used now; the play folder's")
                    say(f"    older one is kept in {kept}")
                    continue
                kept = backup("other-folder", card, unit)
                copy_unit(unit, kept)
                actions.append(("kept", label, str(kept)))
                if target.exists():
                    say(f"  {label}: the play folder's save stays; the other one is kept in {kept}")
                else:
                    say(f"  {label}: was copied before and is not in the play folder any more; kept in {kept}")
        try:
            write_note(folder, play, started)
        except OSError as error:
            say(f"  (could not leave a note in {folder}: {error})")
    if backup_root is not None:
        lines = [
            "Saves that existed both in the play folder and in the folder an older game version saved to.",
            "The newer one of each is in the play folder's card (mc0, mc1, mc0_<campaign>). The other one is here:",
            "  play-folder/   the play folder's save that a newer one replaced",
            "  other-folder/  a save from the other folder that was not put into the card (older than the play",
            "                 folder's, or copied once before and since deleted or overwritten in the game)",
            "To use one of them, close the game and copy its folder back into the card folder of the same name.",
            "",
        ]
        lines += [
            f"{action}: {label} -> {detail}" for action, label, detail in actions if action in ("replaced", "kept")
        ]
        (backup_root / "README.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return actions


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--play", required=True, type=Path, help="the play folder (where drakan.exe is)")
    parser.add_argument("--work", type=Path, help="the build folder (WorkDir)")
    parser.add_argument(
        "--before-delete",
        action="store_true",
        help="the caller deletes the build folder next: read every old place again, whatever its note says",
    )
    args = parser.parse_args(argv)
    # A play folder that does not exist yet is made when there is something to copy: the caller may be about to
    # delete the build folder.
    actions = migrate(args.play, old_places(args.play, args.work), before_delete=args.before_delete)
    into_card = sum(1 for a in actions if a[0] in ("copied", "replaced"))
    kept = sum(1 for a in actions if a[0] == "kept")
    play = args.play.resolve()
    if into_card:
        print(f"Your saves are in {play} now ({into_card} save folder(s) brought over).")
    if kept:
        print(f"{kept} other version(s) of saves are kept in {play / BACKUPS} (see the README.txt there).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
