#!/usr/bin/env python
"""Timing-sensitive reads in the VU1 microcode image.

For every instruction whose result depends on how many cycles passed since an earlier instruction (Q readers after
DIV/SQRT/RSQRT, MAC-flag readers after FMAC ops, clip-flag readers after CLIP, branches reading a VI register written
by the previous pair), walks the control-flow graph backwards and lists the producers within a window with their
minimum and maximum pair distance over all paths. Without stalls one pair is one cycle, so a producer whose minimum
distance is at least its latency is always visible and one whose maximum distance is below it is never visible.

Usage: vu_timing_reads.py <vu1_micromem.bin> [window]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vudis

FMAC = ("ADD", "SUB", "MUL", "MADD", "MSUB", "OPMULA", "OPMSUB")


def mnemonic(text):
    return text.split()[0] if text else ""


def base_op(m):
    # ADDx / MADDAw / MULq / ADDA ... -> the arithmetic family
    for f in ("OPMULA", "OPMSUB", "MADD", "MSUB", "ADD", "SUB", "MUL"):
        if m.startswith(f):
            return f
    return None


def classify(ins):
    up = mnemonic(ins["upper"]).split(".")[0]
    lo = mnemonic(ins["lower"]).split(".")[0]
    c = {}
    if ins["up"] & 0x80000000:
        lo = "LOI"
    if base_op(up) in FMAC:
        c["mac_w"] = 4
    if up == "CLIP":
        c["clip_w"] = 4
    if lo in ("DIV", "SQRT"):
        c["q_w"] = 7
    if lo == "RSQRT":
        c["q_w"] = 13
    if lo == "WAITQ":
        c["q_barrier"] = True
    if ", Q" in ins["upper"] or ins["upper"].rstrip().endswith("Q"):
        c["q_r"] = True
    if lo in ("FMAND", "FMEQ", "FMOR"):
        c["mac_r"] = True
    if lo in ("FCGET", "FCAND", "FCEQ", "FCOR"):
        c["clip_r"] = True
    return c


def main():
    buf = open(sys.argv[1], "rb").read()
    window = int(sys.argv[2]) if len(sys.argv) > 2 else 24
    ins, _, _ = vudis.disassemble(buf)
    n = len(ins)
    lo_m = [mnemonic(x["lower"]).split(".")[0] if not (x["up"] & 0x80000000) else "LOI" for x in ins]
    succ = [set() for _ in range(n)]
    returns = set()
    for i in range(n):
        if lo_m[i] in ("BAL", "JALR"):
            returns.add(i + 2)
    for i in range(n):
        if "E" in ins[i]["flags"]:
            if i + 1 < n:
                succ[i].add(i + 1)
            continue
        if i > 0 and "E" in ins[i - 1]["flags"]:
            continue  # delay slot of an E bit: the program stops
        if i > 0 and ins[i - 1]["kind"] is not None:
            # delay slot of a branch: continue at the target and/or the fall-through
            k, t = ins[i - 1]["kind"], ins[i - 1]["target"]
            if t is not None:
                succ[i].add(t // 8)
            if k == "bc" and i + 1 < n:
                succ[i].add(i + 1)
            if lo_m[i - 1] in ("JR", "JALR"):
                succ[i].update(r for r in returns if r < n)
            continue
        if i + 1 < n:
            succ[i].add(i + 1)
    pred = [[] for _ in range(n)]
    for i in range(n):
        for s in succ[i]:
            pred[s].append(i)
    cls = [classify(x) for x in ins]

    def producers(r, key, barrier=None):
        # (producer, distance) for every path; distance = pairs between issue of producer and reader
        found = {}
        stack = [(p, 1) for p in pred[r]]
        seen = set()
        while stack:
            p, d = stack.pop()
            if d > window or (p, d) in seen:
                continue
            seen.add((p, d))
            if key in cls[p]:
                lo_d, hi_d = found.get(p, (d, d))
                found[p] = (min(lo_d, d), max(hi_d, d))
                continue  # the nearest producer on this path decides
            if barrier and cls[p].get(barrier):
                found.setdefault(("barrier", p), (d, d))
                continue
            stack.extend((q, d + 1) for q in pred[p])
        return found

    report = []
    for r in range(n):
        c = cls[r]
        for rk, wk, bk in (("q_r", "q_w", "q_barrier"), ("mac_r", "mac_w", None), ("clip_r", "clip_w", None)):
            if not c.get(rk):
                continue
            f = producers(r, wk, bk)
            items = []
            ambiguous = False
            for p, (mn, mx) in sorted(f.items(), key=lambda kv: kv[1]):
                if isinstance(p, tuple):
                    items.append("WAITQ@%04x d=%d" % (p[1] * 8, mn))
                    continue
                lat = cls[p][wk]
                state = "visible" if mn >= lat else "hidden" if mx < lat else "MIXED"
                ambiguous |= state == "MIXED" or (state == "hidden" and mx >= lat - 2)
                items.append("%04x d=%d..%d lat=%d %s" % (p * 8, mn, mx, lat, state))
            report.append((ambiguous, "%04x %-5s %-40s <- %s" % (r * 8, rk, ins[r]["lower"][:40], "; ".join(items))))
    # branches reading a VI written by the previous pair (VU reads the old value)
    for r in range(n):
        if ins[r]["kind"] is None or not pred[r]:
            continue
        report.append((False, "%04x branch preds=%s" % (r * 8, ",".join("%04x" % (p * 8) for p in pred[r]))))
    for amb, line in report:
        if "branch" in line:
            continue
        print(("!! " if amb else "   ") + line)


if __name__ == "__main__":
    main()
