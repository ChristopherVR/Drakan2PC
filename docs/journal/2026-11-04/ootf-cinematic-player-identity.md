# OotF cinematic actors: preserve player identity through aliases

Follow-up to the normal Intro -> Ruined Village startup investigation. The ordinary run has no host exit probe or
placement aid. Timed RAM snapshots (`build/scratch/ootf-normal-flow`) show the RV opening is executing rather than
simply frozen: at RV+15s the player is (268910,20508,203960), at +45 (267305,20242,205488), at +75
(267380,20635,205511). Camera mode is1, the scripted word is0, no GUI is active and the player controller is present.
W at +~80s has no visible effect and the HUD is absent. That timing is still within the source opening sequence.

The actual source opening is substantially longer than earlier notes implied: scene1.1 ends at61.19s, sends Play
Sequence to the1.2 trigger;1.2a ends at52.85s and starts1.3;1.3 ends at90.76s. The full chain is about205s. Tests that
quit at RV+23/+75s cannot establish that its final control release is broken. A `sync play180` barrier means180
consecutive gameplay frames, not a180-second timeout, so a harness waiting on the native-look predicate may never
issue later movement or quit events. Timed snapshots and unconditional later inputs are used for the long proof.

Found a real identity mismatch at the source release point: the final1.3 player actor is named **Wrynn**, referring to
the same placed player1740 as earlier actors named Rynn. The converter only classified special actors by their display
name, so Wrynn became generic object mode0, whereas Rynn uses player mode1. This matters for carried/restored players
after a level transition: cinematic actions must address the current player rather than an arbitrary placed object.

The pipeline now passes mapped ids of the source engine-class9 player,28 mount and27 camera into sequence conversion.
`resolveObject` recognizes those identities regardless of actor labels, preserving the existing name fallback for
assigned/non-object actors. This also recognizes aliases of cameras for camera setup insertion. No guest flags or
control pointers are forced. Synthetic regression checks a Wrynn resume and an aliased camera; 29 sequence/campaign
tests and the formats TypeScript check pass.

Regenerated combined pack: `build/ootf-run/implementation/mods/ootf-levels.zip`,137,794,044bytes, SHA256
`789D3C54444511C8F5E0CFE67F5247368A270EF3D26CDEAB00B940BB31DAF585`. Actual packed SSD audit shows Wrynn mode1
in RV1.3 (ref1740) and Volcanic9.6c /9.1 (ref1). Conversion log: `build/scratch/ootf-cinematic-convert.log`.

Normal title -> OotF New Game -> Intro timer -> RV long-run verification is in progress under
`build/scratch/ootf-normal-alias`. It waits beyond the full source chain before sending W, with RAM snapshots around
the expected release. An emitted record with mode1 is format evidence; it is not by itself a successful gameplay proof.

## Long-run result and native readiness investigation

The completed `ootf-normal-alias` run **did not prove release**. At RV+245 seconds the
player remained stationary without HUD and unconditional W did not move it. Camera
phase matching identifies scene1.1 at source48s around RV+75, and scene1.2a at
source14–18s around RV+245. This is an inter-scene readiness delay, not uniform
time scaling. Identity recognition remains a format correction; it is not a
complete startup fix. Only the combined OOTF pack was mounted, excluding the
separate generated character-motion pack.

Read-only PAL native analysis rules out changing the SSD first word to duration:
sequence start `0x1a3d48` explicitly combines that word with instance flags. At the
pre-release RAM snapshot the1.2a SSD is loaded, its instance elapsed time is
`-dt`, its action cursor is0 and it has no asynchronous SSD load task. Initial
preloader `0x1a49f0` checks each scheduled actor readiness and referenced animation
or sound resource. Return2 holds time at the start. Base actor readiness
`0x17a540` recursively checks attachments, model dependencies and active animation
resources. Model readiness `0x176160` calls `0x16f9f8`, which checks all model
dependency resources. Static heap scans include stale pool entries and cannot
reliably identify which resource is pending; bounded runtime logging is needed.

Speech conversion is a separate, unpromoted candidate under
`build/scratch/ootf-speech/mods`:343 descriptor/sample pairs and370 mapped sequence
sound actions, with every packed action reference audited against its descriptor
and sample. Existing host-ID holes were exhausted for9 banks and4 long Alwarren
clips exceed the existing upload cap. These warnings are explicit and must be
resolved before claiming complete speech import. Animation misses now include
the exact source resource reference instead of only aggregate counts.

The long-clip cap is now addressed without truncation: optional `fitRates` in
`convertPcSounds` selects the highest permitted rate that fits the full clip.
The pipeline permits the disc-proven22050/11025Hz rates. A regression verifies
full sample-buffer conversion and equal duration;18 sound/sequence tests and
formats typecheck pass. The regenerated separate candidate
`build/scratch/ootf-speech-fit` no longer reports oversized clips, but still
reports9 exhausted-bank allocations. It remains unpromoted.

Sound allocation previously treated the Resources SDB maximum608 as a fixed
ceiling, borrowing an ODB class restriction. Sound-specific native evidence is
different: descriptor loader `0x1b3860` requests SRSC302 by full resource id via
the generic resource index, and resource cache `0x12b640` hashes full ids. No
SDB0300 high-water read occurs in that descriptor path. `soundIdAllocator` now
reserves existing302–305 ids, reuses holes, then extends to65535. Synthetic
allocation coverage includes orphan samples/lipsync ids and exhaustion.19
sound/sequence tests and the formats typecheck pass. Full candidate regeneration
is separate at `build/scratch/ootf-speech-full`; an above608 descriptor/sample
request must be verified in the live engine before promoting it.
