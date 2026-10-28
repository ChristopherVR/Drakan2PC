# Browser blur and idle camera review

The user reports camera jumps even without mouse input. The reviewed browser had
no connected Gamepad API devices. Short baseline Surdana captures before the fix
show a stable background; they do not reproduce or rule out an intermittent jump.

The former portable path projected raw mouse displacement into an eight-bit right
stick and changed only the game camera mode flag. The new Linux/browser adapter
uses the existing Windows InputModule camera hooks: direct displacement, clearing
controller residual rates before integration, pitch persistence and follow handling.
Button delivery remains in the portable pad sampler. Direct on-foot displacement
is consumed once per engine input frame and never also deflects the right stick.
Flight retains native stick behavior with elapsed-time normalization.

Host focus and controller ownership suspend the adapter. Idle keyboard/mouse
ownership remains active without requiring pointer movement or pointer lock, so
idle camera handling does not toggle off between events. Physical controller
activity transfers ownership until explicit keyboard/mouse activity; axes receive
a radial 0.18 deadzone. Browser capture/focus changes discard pending movement;
fractional DOM counts accumulate rather than truncate each event. Unadjusted
pointer movement is requested with a NotSupportedError fallback.

Read-only browser diagnostic drakan_web_camera_metric indexes 0..7 expose pitch,
yaw, three camera rates and object XYZ; index 8 counts follow-hook snapshots.
These diagnostics do not write guest RAM or provide browser controls.

The visible blur is real. Software GS still rasterizes the native PS2 frame;
replacement textures do not increase geometry/HUD render resolution. CSS scaling
or sharpening does not establish high-resolution rendering. Portable GPU/render
resolution work remains necessary, along with the outstanding enhancement parity.

Validation so far: Windows/Linux host-pad and shared input tests pass, browser
host-pad/web-event tests pass under the pinned Emscripten SDK, and all 118 Python
checks pass. Linux canonical ISO build deployed SHA-256
2bd1bda1171695f5e8584b4ea1bea71292c6347f1d5a2fc48f39b08ed013e604.
Live camera checks and browser/Windows deployment receipts follow below.

The shared input suite also runs in Wasm now: 3,415 checks, zero failures.
It uses the game's PROXY_TO_PTHREAD execution model so concurrent filesystem
reloads can be serviced by the browser/Node main thread, a 2 MiB stack for its
large fake-guest fixture, and EXIT_RUNTIME for deterministic test shutdown.
The original direct-main test configuration could not service a reload worker's
filesystem requests and the default small stack overflowed; these were harness
configuration defects, not passing game evidence.

Initial live measurements with HD streaming enabled: an idle 14.55-second
Surdana window retained exactly zero pitch/yaw/rate range over 328 follow
snapshots. Camera position ranged 1.78125 / 0.359375 / 0.03125 world units.
After mouse movement, another 9.78-second window retained constant pitch
0.0088888891 and zero yaw/rates, but position ranged 10.15625 / 20.21875 / 0.75.
This position excursion is not yet attributed; do not report the intermittent
camera issue fully resolved based only on stable angles. The earlier camera
build had a six-second post-turn window with exactly unchanged angles and XYZ.
Private raw receipts are under build/browser-camera-*.json. Correctness review
ran alongside compilation and host load, and is not a performance measurement.

A new focus regression test found that the bridge cleared motion but retained
pointer-capture state on blur. Focus loss now clears capture too, rejects queued
key/button/lock events while unfocused, and the DOM window blur releases pointer
lock. The focused Wasm event test passes after this fix. A final canonical browser
deployment and transition checks are still pending at this point in the journal.

Further Linux input review found that entering gameplay still required manually
toggling F10 before mouse look worked. The native window thread now uses a
small capture policy driven by atomically published guest gameplay/menu state;
it never reads guest RAM. It automatically captures on gameplay entry, releases
for menus/focus loss, preserves explicit F10 release, suppresses the entire held
recapture click, and drops the cursor-warp delta at transitions. The new policy
passes native Windows/Linux and Wasm host-pad tests. Live Linux capture testing
remains pending until the next canonical deployment. Configured stick-look mode
is also honored when deciding whether to bypass right-stick projection.

The extended read-only camera diagnostic is restricted to the controlled camera
and to DRAKAN_FRAME_METRICS=1. Additional indices: 9..11 player render XYZ,
12 player heading, 13 player anchor Y, 14 camera flags, 15 follow strength,
16 player flags, 17..18 raw mouse counts. Normal play does not collect snapshots.
This extension is for attributing the position-only excursion before changing
native collision/animation follow behavior.

The Windows compatibility rebuild invalidated the large VU AOT units. Four
simultaneous compiler workers nearly filled 64 GiB host RAM while the user's
other game remained running. A private build-only helper retained two compiler
processes in suspension, trimmed their working sets and automatically resumes
workers as slots open, running two at a time. It restores suspended workers in
its finally block. No other application was stopped or reprioritized.

The pad latch previously released its mutex between consuming a pad sample and
copying mouse ownership into the frame. A concurrent host publication could mix
an old physical-stick sample with newer mouse ownership. Consumption and frame
latching now run under one lock; a concurrent-publication invariant check passes
on Windows/Linux (Wasm run pending at this point). This is a real race fix, not
proof that it accounts for the user's entire idle-camera report.

Camera diagnostic index 19 is an odd/even snapshot revision. A reader accepts
indices 0..18 only when the before/after revisions match and are even, avoiding
mixed-frame pose/camera observations during the upcoming correlation check.

Canonical builds subsequently deployed browser Wasm
fe7c8262214d2326d9e0df768a5b566063f369e30295031da2c46d255a75c7f9,
Linux a9bd5fc49a812d8fe0998c5750429a4ce482fbf55833c56ca658b4f133921969,
and Windows 67fde13bd6440b1ed555d710108f23721085b45b92254aeadd9a184736b9c137.
Windows booted through GSR, captured 3840x2160 boot images and exited normally.

Coherent browser idle data: 19.59 seconds, 478 controlled-camera snapshots,
zero angular/rate/heading/raw-input range. Camera XYZ ranges 1.75/0.390625/0;
player Y range 0.265625 and anchor Y range 11.41084. Post-turn data over
20.03 seconds / 484 snapshots also retain unchanged pitch, yaw, rates and heading;
camera XYZ ranges 5.09375/1.8125/0.28125 while anchor Y ranges 14.19652.
Camera X versus animated anchor Y correlations are 0.9833 idle / 0.9777 post-turn;
post-turn camera Y/Z correlations are 0.9467/0.9653. This supports animation-follow
as the source of the small position variation, not a phantom angular input.
It does not establish a full-game camera equivalence proof.

The scoped HD pacing receipt still fails: 24.13838 completed frames/uploads per
second, game/wall 0.999212, 100% late render intervals and 98.9919% late uploads.
No own compilers or other test games ran during that window. The user's Call of
Duty process consumed 123.140625 CPU seconds over 22.1793 wall seconds (about
5.55 logical cores). This remains a contended sample, not clean capacity evidence.

The browser focus transition reproduced a separate real issue: blur let native
stick centering take over and pull pitch to zero without mouse motion. The adapter
now distinguishes physical input focus from idle direct-view ownership. Blur still
clears buttons/motion and releases pointer lock, while mouse view ownership stays
active until controller or scripted/native camera ownership takes over. The change
passes Windows/Linux/Wasm host-pad checks and Linux/Wasm syntax checks; live
validation of the new deployment follows. Diagnostic index 20 also records SMODE2
to check whether field presentation contributes to the visible blur/jitter.

Linux X-server grab tests verify automatic gameplay capture, release for pause,
recapture on resume, F10 release, held-click recapture, focus release and waiting
for a recapture click on focus return. Physical pointer coordinates were an invalid
proxy for capture with raw input; the corrected probe queries actual X grab state.
A retained fast recapture tap was then deterministically lost on the old binary:
queueing press and release while the owned QA process was briefly stopped, then
resuming it, left the cursor uncaptured even after an event-processing wait.
The source now chains the GLFW mouse callback and retains press edges before
raylib collapses a complete down/up pair into a released final level. The normal
raylib callback still receives each event. Live regression validation is pending
the current canonical rebuild.

The rebuilt Linux grab regression now passes every transition, including the
previously lost complete fast tap, and exits normally. The rebuilt Windows game
also renders through GSR and exits normally. The next browser focus sample was
inconclusive because synthetic movement did not establish nonzero pitch; its
receipt was renamed browser-focus-hold-inconclusive.json, not retained as a pass.

Further display evidence: live guest SMODE2 is 1, enabling the software backend's
bob path. It selected one parity of rows from each completed framebuffer, losing
half the vertical detail and changing image position when VSync parity changed.
The portable host request now clears interlace for full completed-frame scanout;
guest registers and VRAM remain unchanged. Low-level GS field presentation is
retained. DRAKAN_SOFTWARE_FIELDS=1 is a private legacy comparison override.
A striped-row fixture verifies full row detail, even/odd invariance, unchanged
request/VRAM and the distinct old bob output. Windows, Linux and Wasm GS tile/
VRAM suites pass with that fixture. No public VU/runtime headers were changed.

The extended live run exposed another issue: the EE frame count continued while
successful uploads remained fixed at9155, and stop/input could not progress.
A worker CPU profile identified the application thread waiting in the synchronous
_glfwGetTime browser-thread proxy (Wasm15761 -> _glfwGetTime ->15775 ->917 ->1063
on A4B2A5...). The owned QA filesystem was synced before reloading the stalled
page. Timer queries now use one C++ steady-clock origin and atomic offset shared
by all workers; GLFW get/set time are explicitly excluded from the UI proxy.
The real JS clock bindings pass20,000 monotonic worker calls, cross-thread origin/
offset and invalid-value tests in the proxied pthread test harness. Live soak and
stop/input checks on the updated browser binary are still pending here.


Final deployment checks
-----------------------
Linux c0b77bc80847b9068e1060a17f85de83ac00abe9c4f1e27ea0661fa591a5a951
passes all live pointer-grab transitions, including the complete fast recapture tap,
and exits normally. Windows a56ad847493bef1f26552a043e4fed7da17754e7e9f3429b65720c369a3c45d1
boots through GSR with a fresh 3840x2160 capture and normal exit. Canonical builds
use the existing final-game work directory and their deployed play folders.

Browser 222a5c5ab0ed885842f6361ea912e85dfeeea80a12b05af80110302ad06fca98
passes nonzero-pitch blur/recapture both with legacy fields and default progressive
scanout: pitch 0.03333333507180214 before blur, during blur and after recapture.
The test uses the real mouse bridge after actual pointer capture; CDP movement
alone did not reliably establish pitch with unadjusted pointer lock.

The same-binary display comparison has identical 688x516 game bounds: legacy bob
repeats 259 of 515 adjacent row pairs, while progressive repeats only 3. Images:
build/browser-legacy-fields.png and build/browser-progressive-fields.png; statistics
in build/browser-scanline-comparison.json. This demonstrates retained vertical
source detail, not higher-resolution geometry. Linux progressive output was also
visually checked for a single complete image.

The new browser clock binding passes 20,000 monotonic worker calls, cross-thread
origin and setter/invalid-value cases. An initial live stop completes with persisted
save sync and no reported save error. A longer default-mode soak follows below.

Default progressive browser soak reaches 398.16 seconds since
page start with 9943 successful uploads; uploads advance throughout
the recorded snapshots. No additional invalid timing or failed copies accumulate.
Final idle observation: 966 coherent samples have exactly constant
pitch, yaw, rates and raw mouse counts. The nonzero pitch remains held after the
focus test. Normal stop reports Game closed and saveError=false at 398.78 seconds.
This is a scoped six-minute soak, not proof against every long-session stall.

Final Python suite: 118 passed. Targeted ruff and git diff --check pass. The working
copy source audit flags third_party/nvidia/dlss/nvngx_dlss.dll: local SHA
0ab4ffc430543d46a0df93e2b910eda1faed981269067f317811dfc4fd5fcb17 differs from
pinned/committed c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e.
No change was made to that unrelated local binary or its allowlist.
