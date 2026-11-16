#!/usr/bin/env bash
# preview_authored.sh <authored json> <container> <pc model name> <name> [height]
# Output (game data, not committed): build/authored/<name>/ (pc/, new/, compare.png). Needs build/ootf/cab_disc (skill
# ootf-model-dump) and Blender (BLENDER overrides the path).
# Fits the authored mesh on the PC model like the converter, renders it and the PC original side by side.
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="${BLENDER:-C:/Program Files/Blender Foundation/Blender 5.3/blender.exe}"
J="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
OUT="$ROOT/build/authored/$4"
H="${5:-1.72}"
mkdir -p "$OUT/new" "$OUT/pc"
cd "$ROOT/editor/packages/formats"
node --experimental-transform-types --no-warnings scripts/dump-pc-model.ts --ootf ../../../build/ootf/cab_disc --container "$2" --out "$OUT/new" "$3" --authored "$J" | grep authored
node --experimental-transform-types --no-warnings scripts/dump-pc-model.ts --ootf ../../../build/ootf/cab_disc --container "$2" --out "$OUT/pc" "$3" > /dev/null
cd "$ROOT"
for d in new pc; do
  obj=$(ls "$OUT/$d"/*.obj | head -1)
  "$B" -b --factory-startup -P analysis/scripts/blender/import_pc_model.py -- "$obj" --clear --height "$H" --render "$OUT/$d/r" > /dev/null 2>&1
done
python - "$OUT" <<'PY'
import sys
from PIL import Image
o = sys.argv[1]
views = ["front", "threequarter", "side"]
rows = []
for d in ["pc", "new"]:
    ims = [Image.open(f"{o}/{d}/r_{v}.png") for v in views]
    rows.append(ims)
w, h = rows[0][0].size
s = Image.new("RGB", (w * 3, h * 2))
for r, ims in enumerate(rows):
    for c, im in enumerate(ims):
        s.paste(im.resize((w, h)), (c * w, r * h))
s.save(f"{o}/compare.png")
print(f"{o}/compare.png")
PY
