"""Write the SHA-256 lists that let the repository guard accept the authored models' own textures.

    python scripts/buildtool/pin_model_textures.py

The textures are original art baked in Blender (analysis/scripts/blender/*_model.py) and saved as PNG beside the model
JSON in scripts/buildtool/kit/models/ (shipped) and models-draft/ (not loaded). Each folder gets a `<folder>.sha256`
list (`<sha256>  <file name>`), read by scripts/repository_guard.py. Run it after every bake and commit the lists with
the pictures.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

KIT = Path(__file__).resolve().parent / "kit"


def pin(folder: Path) -> int:
    pngs = sorted(folder.glob("*.png"))
    lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in pngs]
    manifest = folder.with_name(folder.name + ".sha256")
    if lines:
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    elif manifest.exists():
        manifest.unlink()
    return len(lines)


def main() -> int:
    for name in ("models", "models-draft"):
        folder = KIT / name
        if folder.is_dir():
            print(f"{name}: {pin(folder)} texture(s) pinned")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
