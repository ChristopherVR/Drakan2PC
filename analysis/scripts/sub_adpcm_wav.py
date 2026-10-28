r"""Decode a SDB sound (type 0x304 PS2 SPU-ADPCM, 16-byte blocks) to a mono 16-bit WAV (for speech-to-text of the intro/outro narration).
usage: sub_adpcm_wav.py FILE.SDB <res_id> <res_sub> out.wav [rate]      e.g. LEVEL0RO.SDB 99 14 intro.wav
The rate comes from the 0x302 descriptor (22050 for all speech). Blocks: byte0 = shift|predictor<<4, byte1 = flags (1 = end, 4 = loop start), 14 data bytes = 28 nibbles."""

import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc

F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode(b):
    out = []
    s1 = s2 = 0
    for i in range(0, len(b) - 15, 16):
        sh = b[i] & 15
        pr = min(b[i] >> 4, 4)
        fl = b[i + 1]
        if fl == 7 and False:
            break
        f0, f1 = F[pr]
        for j in range(14):
            for nib in (b[i + 2 + j] & 15, b[i + 2 + j] >> 4):
                v = nib - 16 if nib > 7 else nib
                s = (v << 12) >> sh
                s = s + ((s1 * f0 + s2 * f1 + 32) >> 6) if False else s + ((s1 * f0) >> 6) + ((s2 * f1) >> 6)
                s = max(-32768, min(32767, s))
                out.append(s)
                s2 = s1
                s1 = s
        if fl & 1:
            break
    return out


if __name__ == "__main__":
    path, rid, sub, outp = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    d = open(path, "rb").read()
    _, _, _, _, ents = iop_srsc.parse(d)
    rate = int(sys.argv[5]) if len(sys.argv) > 5 else 22050
    for t, i, s, o, z in ents:
        if t == 0x304 and i == rid and s == sub:
            pcm = decode(d[o : o + z])
            w = wave.open(outp, "wb")
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
            w.close()
            print(outp, len(pcm), "samples", len(pcm) / rate, "s")
            break
    else:
        print("not found")
