"""Stage private launch configuration; never replace an existing user file."""

import argparse
import re
from pathlib import Path


def prepare(repo, output, iso, elf, renderer=None):
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
        "drakan.ini": f"; Private local build paths\n[general]\niso = {iso}\nelf = {elf}\n",
        "drakan_input.ini": template.group(1),
    }
    if renderer:
        files["drakan.ini"] += f"\n[video]\nrenderer = {renderer}\n"
    for name, content in files.items():
        path = output / name
        try:
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(content)
            print(f"Created {path}")
        except FileExistsError:
            print(f"Preserved existing {path}; verify its local paths/settings before launch")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("repo-root", "output-directory", "iso", "elf"):
        parser.add_argument(f"--{option}", required=True, type=Path)
    parser.add_argument("--renderer", choices=("sw", "d3d11"))
    args = parser.parse_args()
    prepare(args.repo_root, args.output_directory, args.iso, args.elf, args.renderer)
