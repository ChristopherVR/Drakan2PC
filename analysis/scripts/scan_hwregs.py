"""Find `lui` instructions in .text that load EE hardware-register upper halves.

Outputs:
  decomp/analysis/hwregs.csv        (insn_addr,upper,effective_addr,block,region_bucket)
  decomp/docs/reference/hwregs-summary.md (top 50 code regions by 0x1000-byte bucket)

effective_addr is computed from the first load/store/addiu/ori that uses the lui result
as its base within the next few instructions (blank if none found). The block labels are
approximate, from the PS2 EE memory map as commonly documented.
"""

import csv
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_common import ANALYSIS_DIR, DECOMP_DIR, Elf, op_of, rs_of, rt_of, signed16, words_of

LUI_OP = 0x0F
TARGET_UPPERS = {0x1000, 0x1001, 0x1100, 0x1200, 0x7000, 0xB000, 0xB200}
# Opcodes whose base register is rs and which can complete an address computation.
ADDR_OPS = {0x09: "addiu", 0x0D: "ori"}
MEM_OPS = {
    0x20,
    0x21,
    0x22,
    0x23,
    0x24,
    0x25,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2A,
    0x2B,
    0x2E,
    0x2F,
    0x30,
    0x31,
    0x37,
    0x38,
    0x39,
    0x3F,
    0x1A,
    0x1B,
}
WINDOW = 10


def block_of(ea):
    """Approximate label for an EE hardware address (physical or KSEG1 alias stripped)."""
    if ea is None:
        return ""
    a = ea & 0x1FFFFFFF
    if 0x70000000 <= ea < 0x70004000:
        return "Scratchpad"
    if 0x10000000 <= a < 0x10000800:
        return "Timers T0-T1"
    if 0x10000800 <= a < 0x10002000:
        return "Timers T2-T3"
    if 0x10002000 <= a < 0x10003000:
        return "IPU control"
    if 0x10003000 <= a < 0x10003800:
        return "GIF control"
    if 0x10003800 <= a < 0x10003C00:
        return "VIF0 control"
    if 0x10003C00 <= a < 0x10004000:
        return "VIF1 control"
    if 0x10004000 <= a < 0x10005000:
        return "VIF0 FIFO"
    if 0x10005000 <= a < 0x10006000:
        return "VIF1 FIFO"
    if 0x10006000 <= a < 0x10007000:
        return "GIF FIFO (PATH3)"
    if 0x10007000 <= a < 0x10008000:
        return "IPU FIFO"
    if 0x10008000 <= a < 0x1000E000:
        return "DMAC channels"
    if 0x1000E000 <= a < 0x1000F000:
        return "DMAC control"
    if 0x1000F000 <= a < 0x10010000:
        return "INTC / DMAC misc"
    if 0x11000000 <= a < 0x12000000:
        return "VU0/VU1 memory & regs"
    if 0x12000000 <= a < 0x13000000:
        return "GS privileged"
    return "other"


def _is_branch(w):
    op = op_of(w)
    if op in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):
        return True
    return op == 0x00 and (w & 63) in (0x08, 0x09)


def effective_addr(words, i, rt):
    """Find the first address-forming use of GPR rt after words[i].

    Returns (effective_address or None, how) where how is 'addiu'/'ori'/'mem' for a direct
    base-register use, or 'value' if rt is used as a plain value (e.g. an R-type op).
    Stops at a branch/jump or when rt is overwritten.
    """
    upper = (words[i] & 0xFFFF) << 16
    for k in range(i + 1, min(len(words), i + 1 + WINDOW)):
        w = words[k]
        op = op_of(w)
        if op != 0x00 and rs_of(w) == rt:
            if op in ADDR_OPS:
                lo = signed16(w & 0xFFFF) if op == 0x09 else (w & 0xFFFF)
                return (upper + lo) & 0xFFFFFFFF, ADDR_OPS[op]
            if op in MEM_OPS:
                return (upper + signed16(w & 0xFFFF)) & 0xFFFFFFFF, "mem"
        uses = op == 0x00 and (rs_of(w) == rt or rt_of(w) == rt)
        if uses:
            return None, "value"
        if _is_branch(w):
            break
        # overwritten by an I-type ALU/load result?
        if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F) or 0x20 <= op <= 0x26:
            if rt_of(w) == rt:
                break
    return None, None


def main():
    elf = Elf()
    text = elf.section(".text")
    words = words_of(elf.sect_bytes(".text"))

    rows = []
    for i, w in enumerate(words):
        if op_of(w) != LUI_OP or rs_of(w) != 0:
            continue  # lui is encoded with rs=0
        upper = w & 0xFFFF
        if upper not in TARGET_UPPERS:
            continue
        rt = rt_of(w)
        ea, how = effective_addr(words, i, rt)
        va = text.addr + 4 * i
        rows.append(
            {
                "va": va,
                "upper": upper,
                "ea": ea,
                "block": block_of(ea) if ea is not None else "",
                "bucket": va & ~0xFFF,
                "how": how or "",
            }
        )

    csv_path = os.path.join(ANALYSIS_DIR, "hwregs.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f)
        wr.writerow(["insn_addr", "upper", "effective_addr", "block", "region_bucket"])
        for r in rows:
            wr.writerow(
                [
                    "0x%08X" % r["va"],
                    "0x%04X" % r["upper"],
                    ("0x%08X" % r["ea"]) if r["ea"] is not None else "",
                    r["block"],
                    "0x%08X" % r["bucket"],
                ]
            )

    # per-bucket aggregation
    per_bucket = defaultdict(Counter)  # bucket -> Counter(upper)
    blocks_bucket = defaultdict(Counter)
    for r in rows:
        per_bucket[r["bucket"]][r["upper"]] += 1
        if r["block"]:
            blocks_bucket[r["bucket"]][r["block"]] += 1
    total_by_upper = Counter(r["upper"] for r in rows)
    total_by_block = Counter(r["block"] for r in rows if r["block"])

    ranked = sorted(per_bucket.items(), key=lambda kv: (-sum(kv[1].values()), kv[0]))[:50]

    md = []
    md.append("# EE hardware-register access summary (Drakan SCES_500.06)\n")
    md.append("Generated by `decomp/analysis/scripts/scan_hwregs.py`. Source: `lui` instructions in `.text`")
    md.append("whose upper half is one of 0x1000, 0x1001, 0x1100, 0x1200, 0x7000, 0xB000, 0xB200.")
    md.append("Detail per instruction: `decomp/analysis/hwregs.csv`.\n")
    md.append("Caveats: a `lui` is only counted as an access if the instruction shown is the lui itself;")
    md.append("the effective address is recovered from a following addiu/ori/load/store on the same register")
    md.append("(about 4-instruction window), so some sites have no block label. Block names are approximate.")
    md.append("Code regions are 0x1000-byte buckets of the `lui` instruction's address.\n")
    md.append("## Totals\n")
    md.append("Total hardware-related `lui` sites: **%d**\n" % len(rows))
    md.append("| upper | sites |")
    md.append("|---|---|")
    for up, n in sorted(total_by_upper.items()):
        md.append("| 0x%04X | %d |" % (up, n))
    md.append("")
    md.append("Sites with a resolved effective address, by block:\n")
    md.append("| block | sites |")
    md.append("|---|---|")
    for b, n in total_by_block.most_common():
        md.append("| %s | %d |" % (b, n))
    md.append("")
    md.append("## Top 50 code regions (0x1000-byte buckets)\n")
    md.append("| rank | bucket (vaddr) | total | by upper | main blocks |")
    md.append("|---|---|---|---|---|")
    for rank, (bucket, uc) in enumerate(ranked, 1):
        total = sum(uc.values())
        by_up = ", ".join("0x%04X:%d" % (u, c) for u, c in sorted(uc.items()))
        blk = ", ".join("%s:%d" % (b, c) for b, c in blocks_bucket[bucket].most_common(3))
        md.append("| %d | 0x%08X-0x%08X | %d | %s | %s |" % (rank, bucket, bucket + 0xFFF, total, by_up, blk or "-"))
    md.append("")
    md_path = os.path.join(DECOMP_DIR, "docs", "reference", "hwregs-summary.md")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(md))

    print("hw lui sites: %d" % len(rows))
    print("by upper:", {"0x%04X" % k: v for k, v in sorted(total_by_upper.items())})
    print("buckets with hw access: %d" % len(per_bucket))
    print("wrote", csv_path, "and", md_path)


if __name__ == "__main__":
    main()
