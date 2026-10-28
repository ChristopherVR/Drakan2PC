"""Read-only corroboration of render-object identity; not a live hook trace."""

import argparse
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("begin", type=Path)
parser.add_argument("end", type=Path)
args = parser.parse_args()
a, b = args.begin.read_bytes(), args.end.read_bytes()
assert len(a) == len(b) == 32 * 1024 * 1024
u32 = lambda data, offset: struct.unpack_from("<I", data, offset)[0]
vtable = struct.pack("<I", 0x51B8B0)


def instances(data):
    found, start = set(), 0
    while (offset := data.find(vtable, start)) >= 0:
        if offset % 16 == 0 and offset + 0x120 <= len(data):
            found.add(offset)
        start = offset + 4
    return found


aa, bb = instances(a), instances(b)
samples = []
for address in (0x86D560, 0xA5B6A0, 0xA5B7C0, 0xB94710, 0xA6A780, 0xA6A9C0):
    assert address in aa & bb
    before = struct.unpack_from("<3f", a, address + 0x14)
    after = struct.unpack_from("<3f", b, address + 0x14)
    assert before != after
    samples.append(
        dict(
            address=hex(address),
            vtable="0x51b8b0",
            packed_translation_begin=before,
            packed_translation_end=after,
            word_before_object=hex(u32(a, address - 4)),
        )
    )
print(
    json.dumps(
        dict(
            aligned_vtable_candidates_begin=len(aa),
            aligned_vtable_candidates_end=len(bb),
            common_addresses=len(aa & bb),
            samples=samples,
            limitation="Snapshots corroborate persistent render nodes, not live draw eligibility or lifetime across reuse.",
        ),
        indent=2,
    )
)
