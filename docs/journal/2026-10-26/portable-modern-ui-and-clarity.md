# Portable modern UI and clarity

The user prioritises blur reduction before performance and requires the modern
window/PC settings experience on Linux, browser and Windows with 1:1 behaviour.
Windows remains the reference; performance and portable high-resolution scene
rendering are not established by this work.

The portable backend now links the existing platform-free settings and gameui
components. It reuses the native title/pause/save transaction hooks from
drakan_input_glue, retaining the separate portable camera owner. Physical controls
use the same InputModule singleton the modern menu glue consumes. Hook definitions
are source-scoped to avoid recompiling unrelated VU code. Windows keeps its D3D11
overlay backend and native request-close path.

The new raylib presentation-thread backend uploads the user's ISO UI textures,
bakes the shared screen fonts at the physical output dimensions, and draws the
same TitleScreen, PauseScreen, SaveLoadScreen and ModernSettings schema. Missing
portable menu preferences migrate to modern using the comment-preserving store;
explicit native preferences remain respected. Unimplemented renderer capabilities
remain disabled rather than reporting successful no-op changes.

The first actual Linux capture exposed the rlgl mode switch resetting the texture
binding: labels and art rendered as solid rectangles. Binding the texture after
rlBegin fixes that cause. It also exposed that injected input snapshots never
advanced frameNo, which modern menu transaction deadlines need. The new
beginExternalFrame entry advances once without changing Windows' normal beginFrame
or the low-level injected test API. Shared input/settings checks pass on Windows
and Linux; Wasm passes 3,429 input checks and browser key/pointer/focus checks.

Portable final presentation now ports the Windows presentTaps reconstruction:
sharp-bilinear enlargement and area-weighted reduction. This reduces interpolation
softness; it does not add missing geometric detail. The shared Sharp scaling
setting changes it live. Real UI/presentation captures and browser integration
are still being checked. No performance parity is claimed.

The next Linux capture confirms readable text and game-themed art on the modern
title and PC Settings landing pages (`build/modern4-title-linux.png`,
`build/modern4-settings-linux.png`). The modern slot action confirms loading
through the native transaction executor. This visual review also exposes a
presenter/storage mismatch: the legacy texture was padded to 640 pixels but the
new presenter described it as 512. Upload now creates storage at the actual
scanout dimensions, eliminating both padded-coordinate mismatch and the fixed
640x512 upload truncation. Browser visual validation confirms the full picture
and crisp modern PC Settings text. Linux deployment of this correction follows.

Browser compilation exposes a libc++ file-clock conversion returning a 128-bit
nanosecond duration while system_clock uses 64-bit microseconds. An explicit
time_point_cast to system_clock::duration preserves the timestamp conversion.
This follows the earlier libstdc++ clock_cast replacement; Windows retains its
original clock_cast path. Both portable platforms now compile the shared hooks.

The modern Linux pause capture also confirms location/objectives/status and PC
key prompts. The first browser navigation check uncovered lost fast key taps:
the host had already drained raylib's event queue before the overlay sampled it.
A separate UI input accumulator now retains the same host snapshots independently
of the EE input consumer, including mouse buttons, wheel and blur releases.
Binding reload also updates the separate direct-camera module, retaining its
independent idle-camera ownership. Fast taps now navigate the browser Audio page.
The portable mapping also matches Windows' WASD, Q/E, X/Delete, F, Z/C,
Backspace, I/M and controller actions; it supports navigation repeat and double
clicks. S then Enter opens Audio and E reaches Controls in the browser.

The browser SettingsStore initially ignored DRAKAN_INI/DRAKAN_INPUT_INI and wrote
into the temporary root. It now shares Windows' iniPath/environment resolution.
The verified flow changed volume 100 to 95, stopped normally, reloaded at 95 and
restored 100. All three modern preferences survived. A 150% DPR change rebuilt
fonts with a 1500x1080 canvas over a 1000x720 CSS rectangle, remaining readable
(`build/modern-settings-browser-dpr15.jpg`).

Linux build 6 completed the modern title -> load -> gameplay -> pause -> save
screen -> back -> resume flow and exited 0. Its settings test used 5 ms key taps
and restored volume 100. Windows' canonical rebuild and private-card gameplay
harness exited 0 with both requested captures (65 seconds). These were functional
checks with other workloads active, not performance measurements.

Browser build 6 saved into previously empty slot 5, stopped/reloaded and loaded it
via Continue across the page boundary. All 16 files in the original four slots
retained their SHA-256 hashes. The test-only slot was removed afterwards and the
removal synced; the original slots were untouched. F1 during gameplay paused via
the native pause menu and closing settings left that pause menu available to
resume. Idle bridge-driven camera angles remained identical over six seconds.
The automation browser rejected real pointer-lock requests with WrongDocumentError;
the bridge check does not establish physical pointer-lock integration.

The portable live FOV row now calls the already installed shared projection hook.
Unsupported smoothing of native highlights remains disabled. Browser build 8
changed FOV 80 to 81 and restored 80; a rendered projection comparison remains
pending. Windows' renderer was not changed by that portable host addition.

Frequent live settings edits exposed a real IDBFS race. FileIO atomically renames
drakan.ini.tmp / drakan_input.ini.tmp, while IDBFS enumerates before awaiting its
database lookup and later reads entries. A disappeared temporary file returns
Emscripten ENOENT (errno 44), which showed the HTML storage-error panel and blocked
subsequent UI keys. Concurrent syncs plus rapid FOV edits reproduced that exact
ErrnoError. The persistence queue now retries only errno 44 up to three times after
16 ms, taking a fresh snapshot and retaining its serialization slot. Other failures
and exhausted retries still report errors. Regression cases cover successful retry,
queued operation ordering, bounded exhaustion and the existing restore protection.
The rebuilt browser completed 70 flushes alongside 15 right/left FOV pairs. The
instrumented sync callback observed 11 errno-44 races; all recovered, with no
manual/automatic failure reports or storage-error panel. FOV ended at 80. A full
stop/reload restored FOV 80, volume 100 and all modern preferences, with no panel.
All original 16 save files still matched their hashes and the test-only slot was
absent. The final session stopped normally. Linux build 8 also booted, displayed
the modern Display page with its enabled FOV row and exited 0.

Deployed binaries for this checkpoint:

| Artifact | SHA-256 |
| --- | --- |
| Windows drakan.exe | `73FF65EBB01BFF0A165255D2BF2347FB221D97996621EDD177D5794AAD0C9A0D` |
| Linux drakan | `C5AA7ECFA9573710FB5A80444E7BAE356C6C94D3E9CD8A6227C12A81E5435BBD` |
| Browser drakan.js | `BF57006C7B7506F5D24249D63ABF533B4C80010807355AF8D00CBB6DECCE6343` |
| Browser drakan.wasm | `1E091CC7FB9EC4152E9F3CDA01523F74857AD030AEB4E76ABD77165BC9E15254` |

Logs: `build/modern-portable-windows-build.log`,
`build/modern-portable-windows-smoke.log`, `build/modern-portable-linux-build8.log`,
`build/modern-ui-probe6-result.log`, `build/modern-ui-probe8-result.log`, and
`build/modern-portable-web-build9.log`. The source audit passes (1,366 approved,
zero prohibited tracked files), as do the persistence regression tests and diff
whitespace check. Full graphics/performance parity remains unfinished: portable
high-resolution scene geometry, modern inventory/map rendering, applicable
enhancements and broad performance/fidelity comparisons are still required.

The portable sharp presenter was compared with Windows' reconstruction formula
using actual WebGL output for seven cases (integer/fractional enlargement, 1:1,
reduction and mixed-axis scaling). All pass within one UNORM byte. This checks
presentation filtering only, not higher-resolution scene geometry or performance.
