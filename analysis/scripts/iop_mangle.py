"""Re-implementation of RsrcLoader's path-component mangler (RSRCIOP.IRX disc build @0x2398):
 drop everything except [0-9A-Za-z_.], upper-case it; if the base name is longer than 8 chars keep
 first 6 + last 2 of the base (extension kept).  Validated against the DB dependency lists & LEVELLST.TXT.
usage: iop_mangle.py   (self-check over iso/sample/ENGLISH/**.DB + LEVELLST.TXT vs the files extracted under iso/sample)"""

import glob
import os
import re

B = chr(92)
SEP = "[" + re.escape(B) + "/]"


def mangle_component(s):
    out = "".join(c.upper() for c in s if (c.isascii() and c.isalnum()) or c in "_.")
    # RSRCIOP: a1 = index of last '.'; if a1>=9 -> keep 6 + tail from (a1-2); no ext & len>=9 -> same on whole
    k = out.rfind(".")
    if k > 0:
        if k >= 9:
            out = out[:6] + out[k - 2 :]
    elif len(out) >= 9:
        out = out[:6] + out[-2:]
    return out


def mangle_path(p):
    parts = [x for x in re.split(SEP, p) if x not in ("",)]
    return B.join(mangle_component(x) for x in parts)


if __name__ == "__main__":
    iso = set()
    for folder, _, files in os.walk("iso/sample/ENGLISH"):
        rel = os.path.relpath(folder, "iso/sample").replace(os.sep, B)
        iso.update((rel + B + name).upper() for name in files)
    bad = 0
    tot = 0
    root = "iso/sample/ENGLISH"
    for db in glob.glob(root + "/**/*.DB", recursive=True):
        base = os.path.dirname(db)[len(root) :].replace("/", B)  # e.g. \LEVELS\LEVEL5NE
        for l in open(db, encoding="latin1"):
            m = re.match(r"\d+ (.+)\s*$", l.strip())
            if not m or l.startswith("dependencies"):
                continue
            comps = []
            for c in re.split(SEP, m.group(1)):
                if c == "..":
                    comps = comps[:-1] if comps else comps
                elif c != ".":
                    comps.append(c)
            # resolve relative to the db's directory
            bp = [x for x in base.split(B) if x]
            for c in re.split(SEP, m.group(1)):
                if c == "..":
                    bp = bp[:-1]
                elif c != ".":
                    bp.append(mangle_component(c))
            target = "ENGLISH" + B + B.join(bp)
            tot += 1
            ok = any(x.startswith(target.upper() + ".") for x in iso)
            if not ok:
                bad += 1
                print("NO MATCH", os.path.basename(db), m.group(1), "->", target)
    print("dependency paths checked", tot, "unmatched", bad)
    for l in open("iso/sample/ENGLISH/LEVELLST.TXT"):
        l = l.strip()
        if l:
            t = "ENGLISH" + B + mangle_path(l)
            print(l, "->", t, "OK" if t.upper() in iso else "MISSING")
