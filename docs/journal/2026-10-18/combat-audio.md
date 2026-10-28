# Combat and audio verification — 18 October 2026

Later focused [native parry investigation](combat-parry.md) establishes
the accepted dagger action's short timing window and explains the unsuccessful
long hold. It adds no successful timed damage-reduction or production MKB fix
claim; the scheduled tap comparison was interrupted for the GPU handoff.

Bounded runs used private copies of the existing hostile and bow fixtures.
Original, source-fixture and private card hashes are unchanged in all three
runs. No production input change was justified or made. Parent owns the separate
native/SRNR 23-check integration runs and [followup.md](followup.md).

| Run | Actual mode | Established outcome | Normal exit |
| --- | --- | --- | --- |
| `audio77-melee01` | Native, internal scale 5, audio output + WAV | Same enemy HP 40→22; five LMB down/up pairs; RMB held 120 frames then released; named attack output evidence | 52.411 s, quit f930 |
| `audio77-bow-mkb01` | Native, internal scale 5, audio output + WAV | Equipment 7/7; movement/bow 9/9; ammo 200→199→198; two named Arrow launch key-ons | 68.390 s, quit f1448 |
| `final54-srnr-melee-audio-melee` | **SR Quality + NR**, internal scale 4, pacing report interval 2 s, audio output + WAV | Same enemy HP 40→18; five LMB down/up pairs; RMB held 120 frames then released; six nonzero mixed-audio windows among seven selected named attack key-ons | 64.361 s, quit f931 |

Executable SHA-256:

- Baseline: `77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`.
- Combined: `54F082B55C3A768391EC5B39DB48033F2AA77DEA6B51CDFBBE1E81A61D92D557`.

The final executable came from `build/rt-upload-20261018/candidate.exe`, copied
privately beside integration DLLs/INIs and the existing `nr` bundle. Its copied
hash is verified. Logs record **420 successful SR and 420 successful NR
evaluations**, with no logged evaluation failures. The existing one-shot NR
validation reports **12,499,928 / 12,582,912 changed RGB components**, zero
nonfinite components, at 2048×2048. This establishes actual combined evaluation,
including during this combat run, rather than a requested mode alone.

The baseline mouse segment measured heading −0.08000002 rad and pitch
+0.04000000 rad. Opposite motion returned both within 1.3×10⁻⁸ rad. Forward and
strafe produced distinct displacement; Quick Inventory released to inactive.
Three bow attempts delivered input, but only two consumed arrows. Acceptance
timing was not isolated.

Named swish/Rynn attack voices have nonzero volume words. SPU frame counters
locate the mixed-WAV windows; input and sound-manager clocks are not equated.
The final run has one later selected attack key-on with a silent following mixed
window. Rynn reaches zero HP by its last snapshot. That silence's cause, survival
and death recovery are not established. Detailed voice/window reports remain
private under ignored `build/` paths.

## Limits and harness checks

No successful timed parry or sustained block protection is established. All
eight baseline and ten final damage-reducer calls returned incoming damage
unchanged; the reduction window is clear in every scheduled RAM snapshot. Final
pad input is neutral. Exact combos, subjective Order of the Flame parity, full
weapon coverage, Arokh combat and natural later progression remain unverified.
Named key-ons and mixed output **do not establish isolated audibility or a human
listening verdict**.

The final wrapper attempted to unset verbose DLSS logging using a null
environment value. This PowerShell host retained an empty variable, which
enabled presence-gated logging. The actual final run therefore contains verbose
DLSS logs. Its successful evaluation counts are direct observations. Durations
are completion receipts, not performance comparisons: baseline/final modes,
scales and diagnostics differ. No new game was launched after GPU release.

The harness is now fixed for future runs: null assignments and restores explicitly
remove the environment entry; empty and nonempty strings use
`SetEnvironmentVariable`. An asset-free regression executes the wrapper's actual
assignment/restoration loops and probes a synthetic child's C `getenv`: all nine
absent/empty/nonempty combinations pass, including exact restoration. This fix
does not change the final run's recorded verbose-logging status.

Asset-free harness checks: input driver 9/9; summarizer unit tests 5/5; synthetic
unchanged/tampered cards 2/2. Save mismatches raise after all eight encounter
environment values are restored. The summarizer accepts demonstrated released
pad edges when release builds omit the older command probe, and still rejects
held input or absent evidence. Only the environment regression above was added
and run after the final game; no gameplay rerun was needed.

[Sanitized aggregate receipt](data/combat-audio-20261018.json).
Local detailed evidence: `build/combat_audio_20261018/t/` and
`build/combat_audio_verification/final54-srnr-melee-audio/`. These contain the exact
recipes, copied input configurations, manifests, completion receipts, RAM/audio
reports and private bundle hashes. No raw game assets accompany the public receipt.
