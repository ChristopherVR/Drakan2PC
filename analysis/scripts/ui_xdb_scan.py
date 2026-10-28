"""List XDB strings (all languages) matching controller wording. ui_xdb_scan.py ROOT [regex]"""

import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import xdb_dump as X

root = sys.argv[1]
pat = re.compile(
    sys.argv[2]
    if len(sys.argv) > 2
    else r"\bL[123]\b|\bR[123]\b|SELECT|START|analog|anal[oó]g|stick|directional|DUAL|vibrat|controll|manette|Steuer|\bpad\b|\0[23][0-7]|button|Taste|touche|tasto|tecla|bouton|botón|boton|Knopf|Richtungs|croix|croce|cruz|Kreuz|Dreieck|Viereck|Kreis|rumble|Gamepad|joypad|croisillon|direzionali|direccionales|D-pad|Cross|Circle|Square|Triangle|Mando|Controlador|Joystick|Kontroller|Pad",
    re.I,
)
for path in sorted(glob.glob(os.path.join(root, "**", "*.XDB"), recursive=True)):
    d = open(path, "rb").read()
    for e in X.parse_xdb(d):
        for li, s in enumerate(e["strings"][:5]):
            t = s.decode("latin1")
            if pat.search(t):
                print("%s|%s|%d|%s|%s" % (os.path.basename(path), e["id"], li, e.get("name") or "", t))
