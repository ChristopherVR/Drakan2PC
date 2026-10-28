#!/usr/bin/env python3
"""Independent PS-ADPCM reference decoder + comparison for spu2_test.

usage:  adpcm_ref.py <adpcm_in.bin> <adpcm_cpp.bin>
  in   : raw 16-byte ADPCM blocks (decoded as ONE continuous stream, filter history carried across blocks)
  cpp  : int16 little-endian samples produced by spu2::adpcmDecodeBlock for the same blocks
exit 0 = identical, 1 = mismatch, 2 = usage/ data error.

Textbook decoder (nocash PSX-SPX "SPU ADPCM"): 4-bit signed nibble << 12 >> shift, filter table
(0,0) (60,0) (115,-52) (98,-55) (122,-60), history terms rounded with +32 >> 6, clamped to s16.
Blocks with shift > 12 are rejected (hardware/emulators disagree there), the test data never uses them.
"""

import struct
import sys

F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode(data):
    assert len(data) % 16 == 0
    p1 = p2 = 0
    out = []
    for b in range(0, len(data), 16):
        hdr = data[b]
        shift, filt = hdr & 0xF, (hdr >> 4) & 0xF
        if shift > 12 or filt > 4:
            raise ValueError("unsupported block at %d (shift %d filter %d)" % (b, shift, filt))
        f0, f1 = F[filt]
        for byte in data[b + 2 : b + 16]:
            for nib in (byte & 0xF, byte >> 4):
                s = nib - 16 if nib >= 8 else nib
                s = (s << 12) >> shift
                v = s + ((f0 * p1 + f1 * p2 + 32) >> 6)
                v = max(-32768, min(32767, v))
                out.append(v)
                p2, p1 = p1, v
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    data = open(sys.argv[1], "rb").read()
    cpp = open(sys.argv[2], "rb").read()
    ref = decode(data)
    got = list(struct.unpack("<%dh" % (len(cpp) // 2), cpp))
    if len(ref) != len(got):
        print("length mismatch: ref %d cpp %d" % (len(ref), len(got)))
        return 1
    bad = [i for i, (a, b) in enumerate(zip(ref, got)) if a != b]
    print("compared %d samples, %d mismatches" % (len(ref), len(bad)))
    if bad:
        i = bad[0]
        print("first mismatch at %d: ref %d cpp %d" % (i, ref[i], got[i]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
