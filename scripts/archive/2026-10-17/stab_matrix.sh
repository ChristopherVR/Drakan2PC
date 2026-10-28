#!/bin/bash
# Stability matrix (docs/reference/stability.md): N boots of one exe across the scenarios a..e (stab_genscript.py), 4 parallel slots.
#
#   scripts/archive/2026-10-17/stab_matrix.sh <exe> <tag> "<scenario list, e.g. a b c d e>" <rounds> [extra env assignments...]
#
# <exe> is copied into build/recomp_stab/stab/slotN (hardlinked DLLs, private ELF copy so mc0 is per slot) under a unique name.
# Each boot: DRAKAN_AUDIO=0, PS2X_TRACE_RING=1, stderr -> build/recomp_stab/stab/logs/<tag>_<scen>_<round>.log, summary line in <tag>_summary.txt
# Verdict per boot: OK | CRASH(host) | DEAD(main thread ended / trace-ring) | MISSING(guest-branch missing-target >= 0x10) | HANG(timeout) | EXIT(nonzero)
set -u
exe=${1:?exe}; tag=${2:?tag}; scens=${3:?scenarios}; rounds=${4:?rounds}; shift 4
EXTRA=("$@")
root="$(cd "$(dirname "$0")/../../.." && pwd)"
st="$root/build/recomp_stab/stab"; logs="$st/logs"; mkdir -p "$logs" "$st/scripts"
PAR=${STAB_PAR:-4}
declare -a jobs=()
for r in $(seq 1 "$rounds"); do for s in $scens; do jobs+=("$s $r"); done; done
: > "$logs/${tag}_summary.txt"
run_slot() {
  local slot=$1; local n=${#jobs[@]}
  local d="$st/slot$slot"; local priv="s_${tag}_$slot.exe"
  cp "$exe" "$d/$priv"
  for ((i=slot-1; i<n; i+=PAR)); do
    local jb=(${jobs[$i]}); local s=${jb[0]} r=${jb[1]}
    local base="${tag}_${s}_${r}"; local sc="$st/scripts/$base.txt"
    python "$root/scripts/harness/stability/stab_genscript.py" "$s" "$r" > "$sc"
    local max=${STAB_MAX_a:-420}; case $s in a) max=${STAB_MAX_a:-520};; b) max=200;; c) max=420;; d) max=300;; e) max=330;; esac
    ( cd "$d"; rm -f crash_*.dmp
      env DRAKAN_AUDIO=0 PS2X_TRACE_RING=1 PS2X_MAX_SECONDS=$max PS2X_GS_SCALE=1 DRAKAN_INPUT_SCRIPT="$sc" DRAKAN_INPUT_LOGFILE="$logs/$base.in.log" "${EXTRA[@]+"${EXTRA[@]}"}" \
        timeout $((max + 120)) "./$priv" ./SCES_500.06 > "$logs/$base.log" 2>&1; echo $? > "$logs/$base.rc" )
    local log="$logs/$base.log" rc; rc=$(cat "$logs/$base.rc")
    local crash ring tgt big hang frames
    crash=$(grep -a -c '^\[crash\]' "$log"); ring=$(grep -a -c 'trace-ring\] #' "$log")
    tgt=$(grep -a -o 'missing-target\][^=]*kind=[A-Za-z]* op=[A-Za-z ]* source=0x[0-9a-f]* target=0x[0-9a-f]*' "$log" | head -1 | sed 's/.*target=//')
    big=0; [ -n "$tgt" ] && [ $((tgt)) -ge 16 ] && big=1
    frames=$(grep -a -o '\[gsr\] exit: frames=[0-9]*' "$log" | head -1 | sed 's/.*=//')
    local v=OK
    if [ "$crash" -gt 0 ]; then v="CRASH"; elif [ "$ring" -gt 0 ]; then v="DEAD"; elif [ "$big" -eq 1 ]; then v="MISSING"; elif [ "$rc" = 124 ]; then v="HANG"; elif [ "$rc" != 0 ]; then v="EXIT$rc"; elif [ -z "$frames" ]; then v="NOEXIT"; fi
    local reached; reached=$(grep -a -c 'script quit' "$logs/$base.in.log" 2>/dev/null)
    echo "$base $v rc=$rc frames=${frames:-?} quit=$reached tgt=${tgt:-} bad=$(grep -a -c 'bad-resume' "$log") clamp=$(grep -a -c 'bad size\|size clamp' "$log") unwound=$(grep -a -c 'unwound at its own entry' "$log")" >> "$logs/${tag}_summary.txt"
    [ -f "$d"/crash_*.dmp ] && mv "$d"/crash_*.dmp "$logs/$base.dmp" 2>/dev/null
  done
}
for s in $(seq 1 "$PAR"); do run_slot "$s" & sleep 7; done
wait
echo "== $tag summary"; cat "$logs/${tag}_summary.txt"; grep -c ' OK ' "$logs/${tag}_summary.txt" | sed 's/^/ok boots: /'
