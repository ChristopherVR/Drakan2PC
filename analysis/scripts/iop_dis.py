"""Disassemble IRX .text with capstone, annotating import calls. Run with tools/pyenv python.
usage: iop_dis.py X.IRX out.asm"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_irxinfo as I
from capstone import *

NAMES = {
    "sysmem": {4: "AllocSysMemory", 5: "FreeSysMemory", 6: "QueryFreeMemSize", 7: "QueryMaxFreeMemSize", 14: "Kprintf"},
    "loadcore": {5: "RegisterLibraryEntries", 6: "ReleaseLibraryEntries", 3: "GetLoadcoreInfo"},
    "intrman": {
        4: "RegisterIntrHandler",
        5: "ReleaseIntrHandler",
        6: "EnableIntr",
        7: "DisableIntr",
        9: "CpuEnableIntr",
        17: "CpuSuspendIntr",
        18: "CpuResumeIntr",
        23: "QueryIntrContext",
    },
    "stdio": {4: "printf"},
    "thbase": {
        4: "CreateThread",
        5: "DeleteThread",
        6: "StartThread",
        7: "StartThreadArgs",
        8: "ExitThread",
        9: "ExitDeleteThread",
        10: "TerminateThread",
        14: "ChangeThreadPriority",
        20: "GetThreadId",
        22: "ReferThreadStatus",
        23: "iReferThreadStatus",
        24: "SleepThread",
        25: "WakeupThread",
        26: "iWakeupThread",
        33: "DelayThread",
        34: "GetSystemTime",
        35: "SetAlarm",
        39: "USec2SysClock",
        40: "SysClock2USec",
    },
    "thsemap": {4: "CreateSema", 5: "DeleteSema", 6: "SignalSema", 7: "iSignalSema", 8: "WaitSema", 9: "PollSema"},
    "thevent": {
        4: "CreateEventFlag",
        5: "DeleteEventFlag",
        6: "SetEventFlag",
        7: "iSetEventFlag",
        8: "ClearEventFlag",
        9: "iClearEventFlag",
        10: "WaitEventFlag",
    },
    "sysclib": {
        5: "longjmp",
        8: "look_ctype_table",
        12: "memcpy",
        13: "memmove",
        14: "memset",
        19: "sprintf",
        20: "strcat",
        22: "strcmp",
        23: "strcpy",
        27: "strlen",
        29: "strncmp",
        30: "strncpy",
        36: "strtol",
    },
    "ioman": {4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek", 9: "ioctl", 20: "AddDrv", 21: "DelDrv"},
    "sifcmd": {
        14: "sceSifInitRpc",
        15: "sceSifBindRpc",
        16: "sceSifCallRpc",
        17: "sceSifRegisterRpc",
        18: "sceSifCheckStatRpc",
        19: "sceSifSetRpcQueue",
        20: "sceSifGetNextRequest",
        21: "sceSifExecRequest",
        22: "sceSifRpcLoop",
        23: "sceSifGetOtherData",
        24: "sceSifRemoveRpc",
        25: "sceSifRemoveRpcQueue",
        4: "sceSifInitCmd",
        13: "isceSifSendCmd",
    },
    "sifman": {5: "sceSifInit?", 7: "sceSifSetDma", 8: "sceSifDmaStat", 29: "sceSifCheckInit?"},
    "cdvdman": {
        4: "sceCdInit",
        5: "sceCdStandby?",
        6: "sceCdRead",
        8: "sceCdGetError",
        10: "sceCdSearchFile",
        11: "sceCdSync",
        13: "sceCdDiskReady",
        37: "cdvdman_37",
        51: "cdvdman_51",
        75: "cdvdman_75",
    },
}


def main(path, out):
    d, secs = I.parse(path)
    imps, exps = I.tables(d, secs)
    tmap = {}
    for lib, ver, fn, at in imps:
        for idx, addr in fn:
            tmap[addr] = "%s.%s" % (lib, NAMES.get(lib, {}).get(idx, "#%d" % idx))
    text = next(s for s in secs if s[6] == ".text")
    code = d[text[4] : text[4] + text[5]]
    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 + CS_MODE_LITTLE_ENDIAN)
    md.skipdata = True
    with open(out, "w") as f:
        for ins in md.disasm(code, 0):
            note = ""
            if ins.mnemonic in ("jal", "j"):
                t = int(ins.op_str, 16)
                note = tmap.get(t, "")
            if ins.address in tmap:
                f.write("\n%s:   ; IMPORT STUB\n" % tmap[ins.address])
            f.write("%06x:  %-8s %s%s\n" % (ins.address, ins.mnemonic, ins.op_str, ("   ; " + note) if note else ""))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
