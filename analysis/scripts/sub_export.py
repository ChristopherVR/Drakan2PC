r"""Export every dialogue subtitle line of one language tree to TSV.
usage: python -I sub_export.py <language-root-dir-containing-LEVELS/COMMON> out.tsv
Columns: file, res_id, res_sub, sound_name, adpcm_bytes, rate, text   (text keeps the game's "\<seconds>" page-break markers)
Data come from the SDB sound descriptors (type 0x302, see sub_sdb_text.py); ADPCM size = SDB type 0x304 of the same id/sub."""

import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc
import sub_sdb_text as S


def main(root, out):
    n = 0
    with open(out, "w", encoding="utf-8") as f:
        f.write("file\tid\tsub\tname\tadpcm_bytes\trate\ttext\n")
        for p in sorted(glob.glob(os.path.join(root, "**", "*.SDB"), recursive=True)):
            d = open(p, "rb").read()
            _, _, _, _, ents = iop_srsc.parse(d)
            sz = {(i, s): z for t, i, s, o, z in ents if t == 0x304}
            for e in S.iter_lines(p):
                if not e["text"]:
                    continue
                base = 2 + struct.unpack_from("<H", e["rec"], 0)[0]
                rate = struct.unpack_from("<I", e["rec"], base + 8)[0]
                f.write(
                    "%s\t%d\t%d\t%s\t%d\t%d\t%s\n"
                    % (
                        os.path.basename(p),
                        e["id"],
                        e["sub"],
                        e["name"],
                        sz.get((e["id"], e["sub"]), 0),
                        rate,
                        e["text"].replace("\t", " ").replace("\n", "\n"),
                    )
                )
                n += 1
    print(out, n, "lines")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
