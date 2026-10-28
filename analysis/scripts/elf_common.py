"""Shared ELF helpers for the Drakan PS2 (EE / MIPS R5900) analysis scripts.

Run with:  python -I <script>.py
"""

import os
import struct

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ANALYSIS_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
DECOMP_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
ELF_PATH = os.path.join(DECOMP_DIR, "iso", "SCES_500.06")

SHF_ALLOC = 0x2
SHT_NOBITS = 8


class Section:
    def __init__(self, name, sh_type, flags, addr, off, size):
        self.name = name
        self.type = sh_type
        self.flags = flags
        self.addr = addr
        self.off = off
        self.size = size


class Elf:
    def __init__(self, path=ELF_PATH):
        with open(path, "rb") as f:
            self.data = f.read()
        if self.data[:4] != b"\x7fELF":
            raise ValueError("not an ELF file")
        shoff = struct.unpack_from("<I", self.data, 0x20)[0]
        shnum = struct.unpack_from("<H", self.data, 0x30)[0]
        shstrndx = struct.unpack_from("<H", self.data, 0x32)[0]
        raw = []
        for i in range(shnum):
            raw.append(struct.unpack_from("<IIIIII", self.data, shoff + i * 40))
        strtab_off = raw[shstrndx][4]

        def cstr(off):
            end = self.data.index(b"\0", off)
            return self.data[off:end].decode("latin-1")

        self.sections = []
        self.by_name = {}
        for n, t, fl, a, o, s in raw:
            sec = Section(cstr(strtab_off + n), t, fl, a, o, s)
            self.sections.append(sec)
            self.by_name[sec.name] = sec

    def section(self, name):
        return self.by_name[name]

    def sect_bytes(self, name):
        s = self.by_name[name]
        return self.data[s.off : s.off + s.size]

    def vaddr_to_off(self, va):
        for s in self.sections:
            if (s.flags & SHF_ALLOC) and s.type != SHT_NOBITS and s.addr <= va < s.addr + s.size:
                return s.off + (va - s.addr)
        return None

    def alloc_progbits(self):
        return [s for s in self.sections if (s.flags & SHF_ALLOC) and s.type != SHT_NOBITS and s.size > 0]


def words_of(blob):
    n = len(blob) // 4
    return struct.unpack_from("<%dI" % n, blob, 0)


def signed16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# MIPS field helpers
def op_of(w):
    return w >> 26


def rs_of(w):
    return (w >> 21) & 31


def rt_of(w):
    return (w >> 16) & 31


def rd_of(w):
    return (w >> 11) & 31


def funct_of(w):
    return w & 63
