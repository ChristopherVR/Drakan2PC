# OotF harness evidence review (2026-11-04)

Reviewed prior campaign reports and later puzzle-state notes. A historical load/control PASS is not proof of visual fidelity or campaign completion; earlier creature shots sometimes fired in the D2 intro before the converted level loaded.

Fixed two false-positive paths in `ootf_coop_campaign.py`: control cannot count before an absent target level, and verdicts require the expected host/joiner evidence. Reanalysis now respects requested co-op mode when a joiner log is missing. Missing host reports can render without raising KeyError.

Validation: three synthetic regression tests in `scripts/tests/test_ootf_campaign_evidence.py` pass. Live baseline smoke results are recorded separately after completion. Remaining chain progression, rendering fidelity, and creature/puzzle coverage must be verified independently of load/control checks.
`review-20261104`: deployed baseline, solo Ruined Village live smoke PASS (Intro then expected level, control, no crash/stall/hang). Steady completed flips min/median 253/353 per sampled interval. Evidence: build/harness/ootf-coop/review-20261104/report.json. This predates the new game build and does not prove chain progression or visual fidelity.
