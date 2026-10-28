"""Generate tests/expected.txt - ground truth for hle_test, computed with the project's Python reference
implementations (analysis/scripts/iop_mangle.py, iop_srsc.py) and 7-Zip for raw file bytes.
usage: python -I gen_expected.py <iso> <out expected.txt>      (7-Zip at C:\\Program Files\\7-Zip\\7z.exe)
Line formats (tab separated):
  MANGLE <long path> <mangled path with \\ENGLISH prefix>
  FILE   <iso path> <size> <sha1 of first min(size,4MiB) bytes>
  SRSC   <iso path> <indexOff> <count> <sha1 of whole index bytes>
  RES    <iso path> <type> <id> <sub> <size> <sha1 of entry data (first min(size,64KiB))>
"""

import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SC = os.path.abspath(os.path.join(HERE, "..", "..", "..", "analysis", "scripts"))
sys.path.insert(0, SC)
import iop_mangle
import iop_srsc

Z = r"C:\Program Files\7-Zip\7z.exe"
iso, out = sys.argv[1], sys.argv[2]


def extract(p):
    return subprocess.run([Z, "e", "-so", iso, p], capture_output=True).stdout


L = []
# LEVELLST
lst = extract("ENGLISH\\LEVELLST.TXT").decode("latin1")
for l in lst.splitlines():
    l = l.strip()
    if l:
        L.append("MANGLE\t%s\tENGLISH\\%s" % (l, iop_mangle.mangle_path(l)))
# extra mangle samples
for s in [
    "\\Common\\Interface\\Interface.txd",
    "Drakan 2.rrc",
    "LevelList.txt",
    "\\Common\\Resources\\Resources.db",
    "..\\..\\World Common\\Mountain World Common\\Mountain World Common.mod",
]:
    L.append("MANGLE\t%s\t%s" % (s, iop_mangle.mangle_path(s) if ".." not in s else "SKIP"))
files = [
    "LEVELLST.TXT",
    "DRAKAN2.RRC",
    "COMMON\\INTERFCE\\INTERFCE.TXD",
    "COMMON\\SYSTEM\\SYSTEM.DB",
    "COMMON\\SOUNDS\\SOUND.SDB",
]
for f in files:
    d = extract("ENGLISH\\" + f)
    if not d:
        continue
    L.append("FILE\tENGLISH\\%s\t%d\t%s" % (f, len(d), hashlib.sha1(d[: 4 << 20]).hexdigest()))
    if d[:4] == b"SRSC":
        magic, ver, idx, cnt, ents = iop_srsc.parse(d)
        L.append("SRSC\tENGLISH\\%s\t%d\t%d\t%s" % (f, idx, cnt, hashlib.sha1(d[idx : idx + 14 * cnt]).hexdigest()))
        picks = [ents[0], ents[len(ents) // 2], ents[-1]]
        for t, i, s, o, sz in picks:
            L.append(
                "RES\tENGLISH\\%s\t%d\t%d\t%d\t%d\t%s"
                % (f, t, i, s, sz, hashlib.sha1(d[o : o + min(sz, 65536)]).hexdigest())
            )
open(out, "w", newline="\n").write("\n".join(L) + "\n")
print(len(L), "lines")
