"""List the SRSC index and movies of a Drakan .VDB.   usage: python -I vdb_list.py file.VDB [fps=25]"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vdb_common as V

fn = sys.argv[1]
fps = float(sys.argv[2]) if len(sys.argv) > 2 else 25
with open(fn, "rb") as f:
    for t, i, s, o, sz in V.read_index(f):
        print("type=%#06x id=%d sub=%d off=%#x size=%d" % (t, i, s, o, sz))
    for m in V.movies(f):
        print(
            "movie id=%d %-20r %dx%d frames=%d size=%.1f MB  %.1f s @%gfps  avg frame %d B"
            % (
                m["id"],
                m["name"],
                m["w"],
                m["h"],
                m["frames"],
                m["data_size"] / 1e6,
                m["frames"] / fps,
                fps,
                m["data_size"] // max(1, m["frames"]),
            )
        )
