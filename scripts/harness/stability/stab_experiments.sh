#!/bin/bash
# Race / resume-machinery stress experiments (docs/reference/stability.md section 6). Usage: scripts/harness/stability/stab_experiments.sh <exe> [rounds]
# Every experiment is a stab_matrix2.sh run with extra environment; results in build/recomp_stab/stab/logs/<tag>_summary.txt
exe=${1:?exe}; r=${2:-3}
here="$(cd "$(dirname "$0")" && pwd)"
run() { local tag=$1 sc=$2; shift 2; bash "$here/stab_matrix2.sh" "$exe" "$tag" "$sc" "$r" PS2X_VU1_CTXCHECK=1 "$@"; }
run fz64  "b c"   PS2X_FUZZ_UNWIND=64          # random unwinding at ~1 of 64 safe points
run fz8   "b"     PS2X_FUZZ_UNWIND=8
run lat0  "b c e" DRAKAN_RES_LATENCY_US=0      # HLE completions as early as possible
run vu1t0 "b c"   PS2X_VU1_THREAD=0            # VU1 inline on the EE thread
run gsa0  "b c"   PS2X_GS_ASYNC=0              # GS renderer inline
# positive control: the unwind-at-entry bug of docs/journal/2026-10-17/crash-listener.md must be found by the same machinery
run ctl   "b"     PS2X_UNWIND_FIX=0 PS2X_FUZZ_UNWIND=16 DRAKAN_RES_LATENCY_US=0
