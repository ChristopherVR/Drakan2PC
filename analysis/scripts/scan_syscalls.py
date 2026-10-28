"""Scan .text for every `syscall` instruction and recover the v1 syscall number.

Output: decomp/analysis/syscalls.csv  (stub_addr,number,name)
"""

import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_common import ANALYSIS_DIR, Elf, funct_of, op_of, rd_of, rs_of, rt_of, signed16, words_of
from ps2_syscalls import syscall_name

SYSCALL_WORD = 0x0000000C
JR_RA = 0x03E00008
V1 = 3


def writes_rt(w):
    """True if instruction w writes GPR rt (3 = v1) -- approximate."""
    op = op_of(w)
    if op == 0x00:  # SPECIAL: writes rd
        return rd_of(w) == V1 and funct_of(w) not in (0x08, 0x09)
    if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19) or 0x20 <= op <= 0x26:
        return rt_of(w) == V1
    return False


def is_branch_or_jump(w):
    op = op_of(w)
    if op in (0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17, 0x01):
        return True
    return op == 0x00 and funct_of(w) in (0x08, 0x09)


def find_v1(words, i, window=6):
    """Walk backwards from words[i] looking for the setter of $v1. Returns (value or None, index)."""
    for k in range(i - 1, max(-1, i - window - 1), -1):
        w = words[k]
        if op_of(w) == 0x09 and rs_of(w) == 0 and rt_of(w) == V1:  # addiu v1,zero,imm (li)
            return signed16(w & 0xFFFF), k
        if op_of(w) == 0x0D and rs_of(w) == 0 and rt_of(w) == V1:  # ori v1,zero,imm
            return (w & 0xFFFF), k
        if writes_rt(w) or is_branch_or_jump(w):
            return None, k
    return None, None


def main():
    elf = Elf()
    text = elf.section(".text")
    words = words_of(elf.sect_bytes(".text"))
    rows = []
    unnamed = 0
    jr_ok = 0
    for i, w in enumerate(words):
        if w != SYSCALL_WORD:
            continue
        va = text.addr + 4 * i
        val, setter = find_v1(words, i)
        nxt = words[i + 1] if i + 1 < len(words) else None
        if nxt == JR_RA:
            jr_ok += 1
        if val is None:
            num_s, name = "", "UNKNOWN_NO_V1_SETTER"
            unnamed += 1
        else:
            num_s = ("-0x%02X" % -val) if val < 0 else ("0x%02X" % val)
            name = syscall_name(val)
            if name.startswith("UNKNOWN"):
                unnamed += 1
        rows.append((va, num_s, name, setter))

    out = os.path.join(ANALYSIS_DIR, "syscalls.csv")
    with open(out, "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f)
        wr.writerow(["stub_addr", "number", "name"])
        for va, num_s, name, _ in rows:
            wr.writerow(["0x%08X" % va, num_s, name])

    from collections import Counter

    cnt = Counter(name for _, _, name, _ in rows)
    print("syscall instructions found: %d" % len(rows))
    print("followed by jr $ra: %d" % jr_ok)
    print("without resolvable/known number: %d" % unnamed)
    print("distinct names: %d" % len(cnt))
    for name, n in cnt.most_common(40):
        print("  %4d  %s" % (n, name))
    print("wrote", out)


if __name__ == "__main__":
    main()
