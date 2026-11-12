"""Drop files from the Windows file cache, so the next run reads them from the disc ("cold" run), without admin rights.

    python scripts/perf/evict_file_cache.py <file or folder> [...]

Opening a file with FILE_FLAG_NO_BUFFERING makes Windows discard the pages it caches for that file when no one else has
it open. Folders are walked. Check the effect with `--time`, which reads each file once before and once after (a cached
read runs at memory speed, an evicted one at the speed of the disc). Windows only.
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import time
from pathlib import Path


def evict(path: Path) -> bool:
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = ctypes.c_void_p
    u32, ptr = ctypes.c_uint32, ctypes.c_void_p
    kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, u32, u32, ptr, u32, u32, ptr]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    # GENERIC_READ, share read/write/delete, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING
    handle = kernel32.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x20000000, None)
    if handle in (None, ctypes.c_void_p(-1).value):
        return False
    kernel32.CloseHandle(handle)
    return True


def read_seconds(path: Path) -> float:
    start = time.perf_counter()
    with path.open("rb", buffering=0) as f:
        while f.read(8 << 20):
            pass
    return time.perf_counter() - start


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--time", action="store_true", help="read every file before and after, print the two times")
    a = ap.parse_args()
    if sys.platform != "win32":
        raise SystemExit("Windows only")
    files: list[Path] = []
    for p in a.paths:
        files += [f for f in p.rglob("*") if f.is_file()] if p.is_dir() else [p]
    failed = 0
    for f in files:
        before = read_seconds(f) if a.time else 0.0
        ok = evict(f)
        failed += not ok
        if a.time:
            print(f"{f}: {before:.3f} s cached, {read_seconds(f):.3f} s after eviction{'' if ok else ' (not evicted)'}")
    print(f"{len(files) - failed} of {len(files)} files evicted")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
