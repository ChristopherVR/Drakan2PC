"""Drakan .VDB (SRSC container) FMV helpers.
Resource types inside a VDB:
  0x340 (id0)            8 bytes  version-ish {0,1,4,7} (u16 x4)
  0x402 (id0)            4 bytes  per-file header (00 01 00 00)
  0x341 (id1)            u16 len + "Movies"/"Logos" (category name)
  0x342 (id N, sub 1)    movie header: u16 namelen, name (NUL padded), u32 width, u32 height, u32 frames, then
                         frames+2 x u32 absolute byte offsets into the 0x343 blob (offs[0]=0 ; frame i = [offs[i],offs[i+1]) ;
                         the last chunk (offs[frames]..offs[frames+1]) is a 20 byte end-of-stream chunk 00 00 01 b1 + zeros)
  0x343 (id N, sub 1)    concatenated IPU frames, each ends with 00 00 01 b0
"""

import struct


def read_index(f):
    f.seek(0)
    hd = f.read(16)
    assert hd[:4] == b"SRSC"
    idx, cnt = struct.unpack_from("<II", hd, 6)
    f.seek(idx)
    return [struct.unpack("<HHHII", f.read(14)) for _ in range(cnt)]


def movies(f):
    """yield dict(id,name,w,h,frames,offs,data_off,data_size) for each movie in an open VDB."""
    ents = read_index(f)
    E = {(t, i): (o, s) for t, i, sb, o, s in ents}
    out = []
    for (t, i), (o, s) in sorted(E.items()):
        if t != 0x342:
            continue
        f.seek(o)
        h = f.read(s)
        n = struct.unpack_from("<H", h, 0)[0]
        name = h[2 : 2 + n].split(b"\0")[0].decode("latin1")
        k = 2 + n
        w, hh, fr = struct.unpack_from("<3I", h, k)
        offs = struct.unpack_from("<%dI" % (fr + 2), h, k + 12)
        do, ds = E[(0x343, i)]
        assert offs[0] == 0 and offs[-1] == ds and k + 12 + 4 * (fr + 2) == s, (name, offs[-1], ds)
        out.append(dict(id=i, name=name, w=w, h=hh, frames=fr, offs=offs, data_off=do, data_size=ds))
    return out


def ipu_header(w, h, frames):
    """16-byte header understood by FFmpeg's 'ipu' demuxer (ffmpeg >= 5.x): 'ipum', u32 nonzero, u16 w, u16 h, u32 frames."""
    return b"ipum" + struct.pack("<IHHI", 1, w, h, frames)
