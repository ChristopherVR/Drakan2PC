# OOTF native cinematic readiness probe

The source-only identity/exit pack still has an intermittent or long Intro-to-RV
loading/cinematic stall. Resource heap scans cannot distinguish current cache
entries from recycled ones, so a bounded runtime observation was added rather
than overriding ready flags or clipping the original source timeline.

`DRAKAN_STOMP_PROBE=1` opts into four PAL-only hooks: cinematic manager tick,
look-ahead preloader, actor readiness and resource readiness. The last two log
only completed calls returning pending2. Cinematic preparation resource calls
are included even before a preloader context exists. Logs identify sequence,
source object, actor handle, resource manager/id, exact hash-cache descriptor,
state flags and caller. Tick includes elapsed time, pause, action/preload cursors,
schedule count, native delta time and current GUI pointer.

Default maximum512 detail lines; `DRAKAN_STOMP_PROBE_LINES` clamps1–4096.
Repeated pending keys and tick state are limited to1Hz, and deduplication state
has a512-key bound. Once exhausted, wrappers pass through silently. No guest
calls, ready overrides, resource loads or timing writes are made. Guest yields
return at internal PCs; these results are not inferred. Resume execution passes
through the original function. Probe install requires all four known functions
and PAL identity addresses. Existing language changes were preserved.

Checks: new files pass clang-format dry-run with Werror and scoped git diff check.
The shared build passed; staging executable SHA256
`734E78F391743AD39A6D451A3C190FD36047D4099900A715346DB28C8988619A`.
Normal title selection -> OOTF NewGame -> natural Intro exit diagnostic is running
under `build/scratch/ootf-startup-probe/normal-source-only`, with a private card,
only the137,794,044-byte baseline pack, pause_unfocused0 and1536-line cap.
No speech or character-motion candidate is mounted. Recipe and wrapper are in
the parent scratch directory. Actual startup/control acceptance remains pending.

## First live result

The first normal run completed cleanly. The final probe output was at70.175s,
only210 detail lines, so neither the1536-line cap nor a full512-key set explains
the silence. RV+15 andRV+75 RAM snapshots both show scene1.1 elapsed0.0,
action47/preload54, pause0, native delta0.0166675. Exact native cache traversal
finds no pending class, animation, sound or SSD resources. Two texture entries
are pending; the persistent12007f is globalbank18, native Interface/Interface
TXD127. It must not be mistaken for a missing OOTF bank. The other pending
texture changes while rendering continues, so pending status alone is not causal.

The second probe adds phase entry/return/resume/yield observations for actor
pose, camera and model getter, restricted to RV1.1 while a GUI is open. Resource
pending observations recover the active sequence after guest yields and include
all callers during that loading state. A completed call is distinguished from
a yielded call by its actual guest PC, without redirecting returns. Shared build
SHA256 `9573C6575D92A3B00AC13D8CFA861A3248946C5578E5EAC943F1D37009EF217D`
passed. Second run: `build/scratch/ootf-startup-probe/normal-phase`.
