"""Dump printable strings (min length 4) with virtual addresses, and flag SDK-related ones.

Outputs:
  decomp/analysis/strings_vaddr.csv  (vaddr,string)   all strings from all PROGBITS alloc sections
  decomp/analysis/sdk_strings.csv    (vaddr,string)   subset matching SDK / library / error patterns
"""

import csv
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_common import ANALYSIS_DIR, Elf

MIN_LEN = 4
PRINTABLE = re.compile(rb"[\x20-\x7e]{%d,}" % MIN_LEN)

SDK_PATTERNS = [
    r"PsIIlib",
    r"\bsce[A-Z]",
    r"\bsce[a-z]",
    r"\bSif[A-Z]",
    r"\bsif[A-Z]",
    r"lib(pad|mc|graph|dma|vu0|vu1|vu|cdvd|cd|sd|sdr|kernel|ipu|mpeg|dev|gs|fs|pkt|vif|sif|c|m|pad)",
    r"\bcdrom\d?:",
    r"\\\\IOP|IOP\\\\|\\\\IOP|MODULES",
    r"\.irx\b|\.IRX\b|\.elf\b|\.ELF\b",
    r"\bDeci2|\bdeci2",
    r"\bIOP\b",
    r"\bEE\b",
    r"\bSCE\b|\bSony\b|Sony Computer|Copyright",
    r"\bVU0?1?\b.*\b(micro|Micro)\b",
    r"\bGS(Put|Get|Set|Ini|Reset)",
    r"\bDMA\b|\bdma(c|C)",
    r"[Vv]er(sion)?[ .:]*\d",
    r"\bSDK\b",
    r"\bReset\b.*EE",
    r"\b[Ee]rror\b|\b[Ff]ail|\bnot found\b|\binvalid\b|\btimeout\b|\bTIMEOUT\b|\bcan'?t\b|\bassert",
    r"\bBIOS\b|\bROM\b",
    r"\bPS2\b|\bPlayStation",
    r"\bMC\b|memory card|\bmc[0-9]",
    r"\bpad\b|\bPAD\b|controller",
    r"\bcdvd|\bCDVD|\bDVD\b|\bcd\b",
]
SDK_RE = re.compile("|".join("(?:%s)" % p for p in SDK_PATTERNS))


def main():
    elf = Elf()
    all_rows = []
    sdk_rows = []
    per_section = Counter()
    for sec in elf.alloc_progbits():
        blob = elf.data[sec.off : sec.off + sec.size]
        for m in PRINTABLE.finditer(blob):
            s = m.group().decode("ascii")
            va = sec.addr + m.start()
            all_rows.append((va, s))
            per_section[sec.name] += 1
            if SDK_RE.search(s):
                sdk_rows.append((va, s))

    all_rows.sort()
    sdk_rows.sort()
    with open(os.path.join(ANALYSIS_DIR, "strings_vaddr.csv"), "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f)
        wr.writerow(["vaddr", "string"])
        for va, s in all_rows:
            wr.writerow(["0x%08X" % va, s])
    with open(os.path.join(ANALYSIS_DIR, "sdk_strings.csv"), "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f)
        wr.writerow(["vaddr", "string"])
        for va, s in sdk_rows:
            wr.writerow(["0x%08X" % va, s])

    print("all strings: %d" % len(all_rows))
    print("per section:", dict(per_section))
    print("sdk-pattern strings: %d" % len(sdk_rows))


if __name__ == "__main__":
    main()
