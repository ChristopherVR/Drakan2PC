# Sony / middleware libraries present in SCES_500.06

Source: `decomp/analysis/sdk_strings.csv` (pattern-matched subset) and `strings_vaddr.csv` (all strings),
produced by `decomp/analysis/scripts/scan_strings.py`. Addresses are virtual addresses.

## Sony PS2 SDK (libraries identified by name)

| Library | Evidence (vaddr) | Notes |
|---|---|---|
| libgraph | `PsIIlibgraph2300` @ 0x005153E0 | |
| libdma | `PsIIlibdma  2300` @ 0x00515430; `libdma: sync timeout` @ 0x00553C18 | |
| libpad | `PsIIlibpad  2300` @ 0x005154B0; `libpad: ...` messages 0x00553C30-0x00553CF8; `[libpad.a = %d.%d, padman.irx = %d.%d]` @ 0x00553CA0 | Has version-mismatch check against padman.irx |
| libcdvd | `PsIIlibcdvd 2300` @ 0x00515500; `sceCdCbfunc= %d ...` @ 0x005549D8 | |
| libsdr (sound RPC) | `PsIIlibsdr  2300` @ 0x00516F70; `sceSdRemoteInit() RPC bind error!` @ 0x00554B88; `Error Initialising Remote Sound Library!` @ 0x00553BE8 | |
| libmc (memory card) | `PsIIlibmc   2320` @ 0x00516FA8; `libmc: too old release of mcserv.irx` @ 0x00554C30; `...mcman.irx` @ 0x00554C58; `sceMcRead` error @ 0x00553B80; `bind error libmc` @ 0x00554C18 | Only library with a different tag (2320) |
| libscf | `PsIIlibscf  2300` @ 0x00516FC0 | |
| libipu | `PsIIlibipu  2300` @ 0x00516FF0 | |
| libkernl | `PsIIlibkernl2300` @ 0x0051706F; `## internel error in libkernl.a!` @ 0x00554D78 | |
| sceRead / sceWrite | error strings @ 0x00553AB8, 0x00553B08 | |
| IOP boot | `cdrom0:\IOP\IOPRP23.IMG` @ 0x00519550; `cdrom0:\` @ 0x005193B8, `cdrom0:` @ 0x00519530 | IOP reset image ("RP23"), DVD/CD boot path |
| IOP RPC | `Unable to bind to IOP RPC function 0x%x!` @ 0x00553BB8 | |
| Build stamp | `May  3 2002 13:24:48` @ 0x00519538 | Matches the ELF file date (May 3 2002) |

Summary: the game is built against a Sony PS2 SDK circa 2002 with the `PsIIlib*` 2300-series
libraries (the trailing number is consistent across libraries and is most likely the SDK release
tag; not independently verified). `libmc` is tagged 2320. `padman.irx`, `mcserv.irx`, `mcman.irx`
and `IOPRP23.IMG` are IOP-side modules loaded from the disc.

Unidentified: the `cdrom0:` paths point to the IOP module set, but the individual IRX names are not
present as strings beyond the ones listed above.

## Third-party middleware / code

| Item | Evidence (vaddr) |
|---|---|
| Surreal Software PS2 Display Driver | `Surreal Software's PS2 Display Driver` @ 0x0051ED48; `Surreal PS2 Display` @ 0x004E8EA0 |
| zlib 1.1.3 (deflate / inflate) | `deflate 1.1.3 Copyright 1995-1998 Jean-loup Gailly` @ 0x005528B8; `inflate 1.1.3 ... Mark Adler` @ 0x005536B0; `1.1.3` @ 0x005195B8; zlib error strings 0x00553480-0x00553690 |
| Game ID | `BESCES-50006` @ 0x00523668 |

## Hardware-access check against the identified SDK

`docs/reference/hwregs-summary.md` has 398 hardware-related `lui` sites. The DMAC, INTC, IPU,
timer, GS and VU groups correspond to the libraries above (libdma, libgraph, libipu, libkernl).
