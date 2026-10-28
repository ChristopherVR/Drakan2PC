#!/usr/bin/env python
"""PS2 VU0/VU1 microcode disassembler.

Usage:
  python -I vudis.py <file> [--offset N] [--size N] [--base ADDR] [--elf-ovl]
  python -I vudis.py --elf <SCES_500.06> --outdir <dir>   # Drakan: dump all microcode

Instruction = 64 bits: lower word at the lower address (bytes 0-3), upper at bytes 4-7.
Upper word: bit31 I, bit30 E, bit29 M, bit28 D, bit27 T, 24..21 dest(xyzw), 20..16 ft,
            15..11 fs, 10..6 fd, 5..0 opcode (0x3c-0x3f -> table selected by bits 10..6).
Lower word: bits 31..25 opcode (0x40 -> bits 5..0, 0x3c-0x3f -> bits 10..6).
When the I bit is set, the lower word is a 32-bit float immediate (LOI).
Opcode tables were checked against PCSX2 VUops.cpp.
"""

import os
import struct
import sys

DEST = "wzyx"  # bit 3 = x ... printed in xyzw order


def dest_str(d):
    s = ""
    if d & 8:
        s += "x"
    if d & 4:
        s += "y"
    if d & 2:
        s += "z"
    if d & 1:
        s += "w"
    return s


BC = "xyzw"


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# ---------------------------------------------------------------- upper
UP_MAIN = {}
for i, n in enumerate(["ADD", "SUB", "MADD", "MSUB", "MAX", "MINI", "MUL"]):
    for b in range(4):
        UP_MAIN[i * 4 + b] = (n, "bc", b)
UP_MAIN.update(
    {
        0x1C: ("MUL", "q"),
        0x1D: ("MAX", "i"),
        0x1E: ("MUL", "i"),
        0x1F: ("MINI", "i"),
        0x20: ("ADD", "q"),
        0x21: ("MADD", "q"),
        0x22: ("ADD", "i"),
        0x23: ("MADD", "i"),
        0x24: ("SUB", "q"),
        0x25: ("MSUB", "q"),
        0x26: ("SUB", "i"),
        0x27: ("MSUB", "i"),
        0x28: ("ADD", "vv"),
        0x29: ("MADD", "vv"),
        0x2A: ("MUL", "vv"),
        0x2B: ("MAX", "vv"),
        0x2C: ("SUB", "vv"),
        0x2D: ("MSUB", "vv"),
        0x2E: ("OPMSUB", "vv"),
        0x2F: ("MINI", "vv"),
    }
)
# special tables: key (opcode&3, sel)
UP_SPEC = {}
for n, base in enumerate(["ADDA", "SUBA", "MADDA", "MSUBA"]):
    for b in range(4):
        UP_SPEC[(b, n)] = (base, "bc", b)
for b, nm in enumerate(["0", "4", "12", "15"]):
    UP_SPEC[(b, 4)] = ("ITOF" + nm, "cvt")
    UP_SPEC[(b, 5)] = ("FTOI" + nm, "cvt")
for b in range(4):
    UP_SPEC[(b, 6)] = ("MULA", "bc", b)
UP_SPEC[(0, 7)] = ("MULA", "q")
UP_SPEC[(1, 7)] = ("ABS", "cvt")
UP_SPEC[(2, 7)] = ("MULA", "i")
UP_SPEC[(3, 7)] = ("CLIP", "clip")
UP_SPEC[(0, 8)] = ("ADDA", "q")
UP_SPEC[(1, 8)] = ("MADDA", "q")
UP_SPEC[(2, 8)] = ("ADDA", "i")
UP_SPEC[(3, 8)] = ("MADDA", "i")
UP_SPEC[(0, 9)] = ("SUBA", "q")
UP_SPEC[(1, 9)] = ("MSUBA", "q")
UP_SPEC[(2, 9)] = ("SUBA", "i")
UP_SPEC[(3, 9)] = ("MSUBA", "i")
UP_SPEC[(0, 10)] = ("ADDA", "vv")
UP_SPEC[(1, 10)] = ("MADDA", "vv")
UP_SPEC[(2, 10)] = ("MULA", "vv")
UP_SPEC[(0, 11)] = ("SUBA", "vv")
UP_SPEC[(1, 11)] = ("MSUBA", "vv")
UP_SPEC[(2, 11)] = ("OPMULA", "vv")
UP_SPEC[(3, 11)] = ("NOP", "nop")


def dis_upper(w):
    op = w & 0x3F
    dst = (w >> 21) & 0xF
    ft = (w >> 16) & 31
    fs = (w >> 11) & 31
    fd = (w >> 6) & 31
    d = dest_str(dst)
    if op >= 0x3C:
        ent = UP_SPEC.get((op & 3, (w >> 6) & 31))
    else:
        ent = UP_MAIN.get(op)
    if ent is None:
        return "??? upper=%08x" % w
    name, kind = ent[0], ent[1]
    # names ending with A that are accumulator forms (ADDA, SUBA, MADDA, MSUBA, MULA, OPMULA)
    isacc = name in ("ADDA", "SUBA", "MADDA", "MSUBA", "MULA", "OPMULA")
    if kind == "nop":
        return "NOP"
    if kind == "cvt":
        return "%s.%s vf%02d, vf%02d" % (name, d, ft, fs)
    if kind == "clip":
        return "CLIP.xyz vf%02dxyz, vf%02dw" % (fs, ft)
    if kind == "bc":
        mn = name + BC[ent[2]]
        src = "vf%02d%s" % (ft, BC[ent[2]])
    elif kind == "q":
        mn = name + "q"
        src = "Q"
    elif kind == "i":
        mn = name + "i"
        src = "I"
    else:
        mn = name
        src = "vf%02d" % ft
    if name == "OPMSUB":
        return "OPMSUB.xyz vf%02dxyz, vf%02dxyz, vf%02dxyz" % (fd, fs, ft)
    if name == "OPMULA":
        return "OPMULA.xyz ACCxyz, vf%02dxyz, vf%02dxyz" % (fs, ft)
    if isacc:
        # mnemonic like ADDAx / ADDAq / ADDA ; strip trailing A handled by name already
        return "%s.%s ACC%s, vf%02d, %s" % (mn, d, d, fs, src)
    return "%s.%s vf%02d, vf%02d, %s" % (mn, d, fd, fs, src)


# ---------------------------------------------------------------- lower
def fsf(w):
    return BC[(w >> 21) & 3]


def ftf(w):
    return BC[(w >> 23) & 3]


def dis_lower(w, pc):
    """pc = address of this instruction in bytes. Returns (text, branch_target or None, kind)."""
    op = w >> 25
    dst = (w >> 21) & 0xF
    d = dest_str(dst)
    it = (w >> 16) & 31
    is_ = (w >> 11) & 31
    id_ = (w >> 6) & 31
    ft, fs = it, is_
    imm11 = sx(w, 11)
    imm12 = (w & 0x7FF) | ((w >> 10) & 0x800)
    imm15 = sx((w & 0x7FF) | ((w >> 10) & 0x7800), 15)
    imm24 = w & 0xFFFFFF
    btgt = pc + 8 + imm11 * 8
    T = lambda s, t=None, k=None: (s, t, k)
    if op == 0x00:
        return T("LQ.%s vf%02d, %d(vi%02d)" % (d, ft, imm11, is_))
    if op == 0x01:
        return T("SQ.%s vf%02d, %d(vi%02d)" % (d, fs, imm11, it))
    if op == 0x04:
        return T("ILW.%s vi%02d, %d(vi%02d)" % (d, it, imm11, is_))
    if op == 0x05:
        return T("ISW.%s vi%02d, %d(vi%02d)" % (d, it, imm11, is_))
    if op == 0x08:
        return T("IADDIU vi%02d, vi%02d, %d" % (it, is_, imm15))
    if op == 0x09:
        return T("ISUBIU vi%02d, vi%02d, %d" % (it, is_, imm15))
    if op == 0x10:
        return T("FCEQ vi01, 0x%06x" % imm24)
    if op == 0x11:
        return T("FCSET 0x%06x" % imm24)
    if op == 0x12:
        return T("FCAND vi01, 0x%06x" % imm24)
    if op == 0x13:
        return T("FCOR vi01, 0x%06x" % imm24)
    if op == 0x14:
        return T("FSEQ vi%02d, 0x%03x" % (it, imm12))
    if op == 0x15:
        return T("FSSET 0x%03x" % imm12)
    if op == 0x16:
        return T("FSAND vi%02d, 0x%03x" % (it, imm12))
    if op == 0x17:
        return T("FSOR vi%02d, 0x%03x" % (it, imm12))
    if op == 0x18:
        return T("FMEQ vi%02d, vi%02d" % (it, is_))
    if op == 0x1A:
        return T("FMAND vi%02d, vi%02d" % (it, is_))
    if op == 0x1B:
        return T("FMOR vi%02d, vi%02d" % (it, is_))
    if op == 0x1C:
        return T("FCGET vi%02d" % it)
    if op == 0x20:
        return T("B 0x%04x" % btgt, btgt, "b")
    if op == 0x21:
        return T("BAL vi%02d, 0x%04x" % (it, btgt), btgt, "bal")
    if op == 0x24:
        return T("JR vi%02d" % is_, None, "jr")
    if op == 0x25:
        return T("JALR vi%02d, vi%02d" % (it, is_), None, "jalr")
    if op == 0x28:
        return T("IBEQ vi%02d, vi%02d, 0x%04x" % (it, is_, btgt), btgt, "bc")
    if op == 0x29:
        return T("IBNE vi%02d, vi%02d, 0x%04x" % (it, is_, btgt), btgt, "bc")
    if op == 0x2C:
        return T("IBLTZ vi%02d, 0x%04x" % (is_, btgt), btgt, "bc")
    if op == 0x2D:
        return T("IBGTZ vi%02d, 0x%04x" % (is_, btgt), btgt, "bc")
    if op == 0x2E:
        return T("IBLEZ vi%02d, 0x%04x" % (is_, btgt), btgt, "bc")
    if op == 0x2F:
        return T("IBGEZ vi%02d, 0x%04x" % (is_, btgt), btgt, "bc")
    if op == 0x40:
        f = w & 0x3F
        if f == 0x30:
            return T("IADD vi%02d, vi%02d, vi%02d" % (id_, is_, it))
        if f == 0x31:
            return T("ISUB vi%02d, vi%02d, vi%02d" % (id_, is_, it))
        if f == 0x32:
            return T("IADDI vi%02d, vi%02d, %d" % (it, is_, sx(w >> 6, 5)))
        if f == 0x34:
            return T("IAND vi%02d, vi%02d, vi%02d" % (id_, is_, it))
        if f == 0x35:
            return T("IOR vi%02d, vi%02d, vi%02d" % (id_, is_, it))
        if f >= 0x3C:
            sel = (w >> 6) & 31
            n = f & 3
            if n == 0:
                if sel == 12:
                    return T("NOP" if dst == 0 else "MOVE.%s vf%02d, vf%02d" % (d, ft, fs))
                if sel == 13:
                    return T("LQI.%s vf%02d, (vi%02d++)" % (d, ft, is_))
                if sel == 14:
                    return T("DIV Q, vf%02d%s, vf%02d%s" % (fs, fsf(w), ft, ftf(w)))
                if sel == 15:
                    return T("MTIR vi%02d, vf%02d%s" % (it, fs, fsf(w)))
                if sel == 16:
                    return T("RNEXT.%s vf%02d, R" % (d, ft))
                if sel == 25:
                    return T("MFP.%s vf%02d, P" % (d, ft))
                if sel == 26:
                    return T("XTOP vi%02d" % it)
                if sel == 27:
                    return T("XGKICK vi%02d" % is_)
                if sel == 28:
                    return T("ESADD P, vf%02dxyz" % fs)
                if sel == 29:
                    return T("EATANxy P, vf%02d" % fs)
                if sel == 30:
                    return T("ESQRT P, vf%02d%s" % (fs, fsf(w)))
                if sel == 31:
                    return T("ESIN P, vf%02d%s" % (fs, fsf(w)))
            if n == 1:
                if sel == 12:
                    return T("MR32.%s vf%02d, vf%02d" % (d, ft, fs))
                if sel == 13:
                    return T("SQI.%s vf%02d, (vi%02d++)" % (d, fs, it))
                if sel == 14:
                    return T("SQRT Q, vf%02d%s" % (ft, ftf(w)))
                if sel == 15:
                    return T("MFIR.%s vf%02d, vi%02d" % (d, ft, is_))
                if sel == 16:
                    return T("RGET.%s vf%02d, R" % (d, ft))
                if sel == 26:
                    return T("XITOP vi%02d" % it)
                if sel == 28:
                    return T("ERSADD P, vf%02dxyz" % fs)
                if sel == 29:
                    return T("EATANxz P, vf%02d" % fs)
                if sel == 30:
                    return T("ERSQRT P, vf%02d%s" % (fs, fsf(w)))
                if sel == 31:
                    return T("EATAN P, vf%02d%s" % (fs, fsf(w)))
            if n == 2:
                if sel == 13:
                    return T("LQD.%s vf%02d, (--vi%02d)" % (d, ft, is_))
                if sel == 14:
                    return T("RSQRT Q, vf%02d%s, vf%02d%s" % (fs, fsf(w), ft, ftf(w)))
                if sel == 15:
                    return T("ILWR.%s vi%02d, (vi%02d)" % (d, it, is_))
                if sel == 16:
                    return T("RINIT R, vf%02d%s" % (fs, fsf(w)))
                if sel == 28:
                    return T("ELENG P, vf%02dxyz" % fs)
                if sel == 29:
                    return T("ESUM P, vf%02d" % fs)
                if sel == 30:
                    return T("ERCPR P, vf%02d%s" % (fs, fsf(w)))
                if sel == 31:
                    return T("EEXP P, vf%02d%s" % (fs, fsf(w)))
            if n == 3:
                if sel == 13:
                    return T("SQD.%s vf%02d, (--vi%02d)" % (d, fs, it))
                if sel == 14:
                    return T("WAITQ")
                if sel == 15:
                    return T("ISWR.%s vi%02d, (vi%02d)" % (d, it, is_))
                if sel == 16:
                    return T("RXOR R, vf%02d%s" % (fs, fsf(w)))
                if sel == 28:
                    return T("ERLENG P, vf%02dxyz" % fs)
                if sel == 30:
                    return T("WAITP")
    return T("??? lower=%08x" % w)


def decode(buf, off, base=0):
    """returns dict for the instruction at buf[off:off+8]; addr in bytes (base + off)."""
    lo, up = struct.unpack_from("<II", buf, off)
    pc = base
    flags = []
    if up & 0x80000000:
        flags.append("I")
    if up & 0x40000000:
        flags.append("E")
    if up & 0x20000000:
        flags.append("M")
    if up & 0x10000000:
        flags.append("D")
    if up & 0x08000000:
        flags.append("T")
    utxt = dis_upper(up)
    if up & 0x80000000:
        f = struct.unpack("<f", struct.pack("<I", lo))[0]
        ltxt, tgt, kind = "LOI %r (0x%08x)" % (f, lo), None, None
    else:
        ltxt, tgt, kind = dis_lower(lo, pc)
    return dict(pc=pc, lo=lo, up=up, flags=flags, upper=utxt, lower=ltxt, target=tgt, kind=kind)


def disassemble(buf, base=0, labels_extra=()):
    n = len(buf) // 8
    ins = [decode(buf, i * 8, base + i * 8) for i in range(n)]
    labels = {}
    for x in ins:
        if x["target"] is not None:
            labels.setdefault(x["target"], "L_%04x" % x["target"])
    for a in labels_extra:
        labels.setdefault(a, "L_%04x" % a)
    lines = []
    for x in ins:
        if x["pc"] in labels:
            lines.append("%s:" % labels[x["pc"]])
        fl = "".join(x["flags"])
        lt = x["lower"]
        if x["target"] is not None and x["target"] in labels:
            lt = lt.replace("0x%04x" % x["target"], labels[x["target"]])
        lines.append(
            "%04x: %08x %08x  %-5s [%-34s] [%s]"
            % (x["pc"], x["up"], x["lo"], ("{" + fl + "}") if fl else "", x["upper"], lt)
        )
    return ins, labels, "\n".join(lines) + "\n"


# ---------------------------------------------------------------- Drakan ELF helpers
def vif_stream_parse(raw):
    """Parse a DMA/VIF stream containing MPG uploads (as in .vutext). Returns list of (loadaddr_instr, bytes)."""
    chunks = []
    o = 0
    # leading DMA tags: tag(8) + 2 vif codes(8). walk by scanning for 'cmd 0x4a' codes
    while o + 16 <= len(raw):
        tag = struct.unpack_from("<Q", raw, o)[0]
        qwc = tag & 0xFFFF
        idn = (tag >> 28) & 7
        o += 8
        end = o + 8 + qwc * 16 - 0  # tag qword includes 2 vif codes
        # vif codes follow
        while o < end and o + 4 <= len(raw):
            code = struct.unpack_from("<I", raw, o)[0]
            cmd = (code >> 24) & 0x7F
            o += 4
            if cmd == 0x4A:
                num = (code >> 16) & 0xFF or 256
                addr = code & 0xFFFF
                chunks.append((addr, raw[o : o + num * 8]))
                o += num * 8
            elif o >= end:
                break
        if idn in (0, 7) and qwc == 0:
            break
        break
    return chunks


def drakan_dump(elf, outdir):
    d = open(elf, "rb").read()
    vt = d[0x3D8400 : 0x3D8400 + 0x2FE0]
    # ovlytab: 6 entries of (nameptr, vma, vu_offset)
    ents = [struct.unpack_from("<III", d, 0x45786C + i * 12) for i in range(6)]
    os.makedirs(outdir, exist_ok=True)
    info = ["ovlytab entries: (name vma, image vaddr in .vutext, VU1 micromem byte offset)"]
    for e in ents:
        info.append("  name@%#x image@%#x vu_off=%#x" % e)
    # build micro memory by walking MPG packets (first tag at 0x4d7400 covers the whole thing)
    mem = bytearray(0x4000)
    top = 0
    o = 16 - 4  # skip DMA tag + vifcode0; next is MPG
    o = 0
    tag = struct.unpack_from("<Q", vt, 0)[0]
    info.append("DMA tag: qwc=%#x id=%d" % (tag & 0xFFFF, (tag >> 28) & 7))
    o = 8
    chunks = []
    while o + 4 <= len(vt):
        code = struct.unpack_from("<I", vt, o)[0]
        cmd = (code >> 24) & 0x7F
        if cmd == 0x4A:
            num = ((code >> 16) & 0xFF) or 256
            addr = (code & 0xFFFF) * 8
            data = vt[o + 4 : o + 4 + num * 8]
            chunks.append((addr, o + 4, len(data)))
            mem[addr : addr + len(data)] = data
            top = max(top, addr + len(data))
            o += 4 + num * 8
        else:
            o += 4
            if code == 0x70000000 or (code == 0 and o >= len(vt) - 16):
                pass
    for a, off, n in chunks:
        info.append(
            "MPG: load micromem byte %#06x (instr %d) n=%d instr, image@%#x" % (a, a // 8, n // 8, 0x4D7400 + off)
        )
    ins, labels, text = disassemble(bytes(mem[:top]), 0)
    hdr = "".join("# " + l + "\n" for l in info)
    open(os.path.join(outdir, "vu1_all.txt"), "w").write(hdr + text)
    for k, (a, off, n) in enumerate(chunks):
        _, _, t = disassemble(bytes(mem[a : a + n]), a, labels_extra=[])
        # relabel using global labels
        open(os.path.join(outdir, "vu1_ovl%d_%04x.txt" % (k, a)), "w").write(
            "# overlay %d: micromem %#x..%#x\n" % (k, a, a + n) + t
        )
    open(os.path.join(outdir, "vu1_micromem.bin"), "wb").write(bytes(mem[:top]))
    return ins, labels, chunks


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--elf":
        drakan_dump(a[1], a[a.index("--outdir") + 1])
    else:
        f = a[0]
        off = int(a[a.index("--offset") + 1], 0) if "--offset" in a else 0
        size = int(a[a.index("--size") + 1], 0) if "--size" in a else None
        base = int(a[a.index("--base") + 1], 0) if "--base" in a else 0
        buf = open(f, "rb").read()[off : (off + size) if size else None]
        sys.stdout.write(disassemble(buf, base)[2])
