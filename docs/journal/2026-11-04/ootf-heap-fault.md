# Post-opening OotF heap fault

The indexed-terrain pack completes the normal opening, but extended playback still exhausts/corrupts guest memory.
The controlled 60 Hz reproduction uses candidate SHA256
`03374b5e329f2fd468bf4ec2350b19deacf2b939104cd7a1c2fcfb27926c7e5e` and runtime SHA256
`812F55D36E6F14335CEFE59BC6C92C600FA704AB4638DED70AC3B361BD450B11`.

The opt-in watchdog capture emitted one complete 32 MiB RAM file and before/after GPR observations under
`build/scratch/ootf-indexed/heap-fault-60/`. The published watchdog and before-RAM snapshot agree on thread 1,
PC 0x80000080, return address 0x3c7040 and stack pointer 0x1fbf930. The after-RAM PC is 0x3c7040 with the same
return address and stack pointer. This is a best-effort live capture, not an atomic snapshot.

At this point allocator metadata at 0x56d290 contains particle-like position/color records instead of bin pointers.
The dynamic vector header at 0x76a474 is also overwritten. The native routine at 0x3c6da8 attempts to clear a
12-byte vector element using a corrupted base/count, explaining the out-of-RAM destination seen in the registers.
This capture does not establish the earlier run's fast-bin cycle: that run reported PC 0x4acdd0 instead.

There are 829,203 occurrences of the common particle color word in the captured RAM, with runs of 128 records at
44-byte spacing even below the normal heap start, including 0x1100. This supports widespread particle-page writes
after allocation failure or address corruption; it does not by itself distinguish the first invalid write.

Earlier physical heap walks show most growth in live 0x1610-byte chunks (0x1600 user bytes, 128 records of 44 bytes)
and 0x410-byte chunks, rather than free fragmentation. Native FX dust-devil containers own sampled pages. The
converter investigation found class-array defaults retaining native template Spawn Object references, and missing
production wiring for default class-reference relocation. That defect is being corrected with transitive class
discovery and typed owner-relative relocation. The correction still needs a controlled normal-route run before
this fault can be declared resolved. No allocator bypass or enlarged guest heap is used.
