"""Stage private launch configuration and keep existing user settings."""

import argparse
import re
from pathlib import Path


def relocate_managed_elf(path, previous, current):
    with path.open(encoding="utf-8", newline="") as stream:
        lines = stream.readlines()
    section = ""
    changed = False
    for index, line in enumerate(lines):
        header = re.fullmatch(r"\s*\[([^]]+)\]\s*", line)
        if header:
            section = header[1].strip().lower()
        entry = re.fullmatch(r"([ \t]*elf[ \t]*=[ \t]*)(.*?)([ \t]*(?: ;[^\r\n]*)?)(\r?\n)?", line, re.I)
        if section == "general" and entry and entry[2] == previous:
            lines[index] = f"{entry[1]}{current}{entry[3]}{entry[4] or ''}"
            changed = True
    if changed:
        with path.open("w", encoding="utf-8", newline="") as stream:
            stream.writelines(lines)


def prepare(repo, output, iso, elf, renderer=None, relocate_elf_from=None, modern_title=False):
    iso, elf = Path(iso).resolve(strict=True), Path(elf).resolve(strict=True)
    # The runtime INI parser treats this sequence as a trailing comment.
    if any(" ;" in str(p) or "\n" in str(p) or "\r" in str(p) for p in (iso, elf)):
        raise ValueError("ISO/ELF path cannot be represented by the runtime INI parser")
    source = (Path(repo) / "runtime/input/src/Config.cpp").read_text(encoding="utf-8")
    template = re.search(r'return R"INI\((.*?)\)INI";', source, re.S)
    if not template:
        raise ValueError("Cannot find the authored built-in input template")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    files = {
        "drakan.ini": f"; Private local build paths\n[general]\niso = {iso}\nelf = {elf}\nshow_issue_reporting = 1\n",
        "drakan_input.ini": template.group(1),
    }
    if modern_title:
        files["drakan.ini"] += "title_menu = modern\n"
    if renderer:
        files["drakan.ini"] += f"\n[video]\nrenderer = {renderer}\n"
    for name, content in files.items():
        path = output / name
        try:
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(content)
            print(f"Created {path}")
        except FileExistsError:
            if name == "drakan.ini" and relocate_elf_from:
                relocate_managed_elf(path, relocate_elf_from, str(elf))
            print(f"Preserved existing {path}; verify its local paths/settings before launch")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("repo-root", "output-directory", "iso", "elf"):
        parser.add_argument(f"--{option}", required=True, type=Path)
    parser.add_argument("--renderer", choices=("sw", "d3d11", "webgpu"))
    parser.add_argument("--relocate-elf-from")
    parser.add_argument("--modern-title", action="store_true")
    args = parser.parse_args()
    prepare(
        args.repo_root,
        args.output_directory,
        args.iso,
        args.elf,
        args.renderer,
        args.relocate_elf_from,
        args.modern_title,
    )
