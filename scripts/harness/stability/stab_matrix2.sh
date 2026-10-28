#!/bin/bash
# Stability matrix v2 (docs/reference/stability.md): boots of one exe across the scenarios a..e (stab_genscript.py), STAB_PAR parallel slots (default 4).
#
#   scripts/harness/stability/stab_matrix2.sh <exe> <tag> "<scenarios, e.g. a b c d e>" <rounds> [VAR=value ...]     (extra env for the exe, e.g. PS2X_FUZZ_UNWIND=40)
#
# <exe> is copied into build/recomp_stab/stab/slotN (hardlinked DLLs, private ELF copy so mc0 is per slot) under a unique name.
# Per boot: DRAKAN_AUDIO=0, PS2X_TRACE_RING=1, PS2X_GS_SCALE=1, log in build/recomp_stab/stab/logs/<tag>_<scen>_<round>.log, one line in <tag>_summary.txt.
# Verdicts (stab_verdict.sh): OK | CRASH | DEAD | DEAD_EXC | MISSING | HANG | EXITn | NOEXIT.   Wall-clock caps per scenario: STAB_CAP_<s> (seconds).
set -eu
exe=${1:?exe}; tag=${2:?tag}; scens=${3:?scenarios}; rounds=${4:?rounds}; shift 4
EXTRA=("$@")
root="$(cd "$(dirname "$0")/../../.." && pwd)"
st="$root/build/recomp_stab/stab"; logs="$st/logs"; mkdir -p "$logs" "$st/scripts"
PAR=${STAB_PAR:-4}
FIRST=${STAB_SLOT_FIRST:-1}
[[ "$tag" =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'Invalid tag' >&2; exit 2; }
[[ "$PAR" =~ ^[1-9][0-9]*$ && "$FIRST" =~ ^[1-9][0-9]*$ && "$rounds" =~ ^[1-9][0-9]*$ ]] || exit 2
[ -r "$exe" ] || { echo "Missing exe: $exe" >&2; exit 2; }
[ ! -e "$logs/${tag}_summary.txt" ] || { echo "Tag already used: $tag" >&2; exit 2; }
for ((s=FIRST; s<FIRST+PAR; ++s)); do
  [ -r "$st/slot$s/SCES_500.06" ] || { echo "Prepare private slot$s first" >&2; exit 2; }
done
declare -a jobs=()
for r in $(seq 1 "$rounds"); do for s in $scens; do jobs+=("$s $r"); done; done
(set -o noclobber; : > "$logs/${tag}_summary.txt") || exit 2
cap() { case $1 in a) echo "${STAB_CAP_a:-720}";; b) echo "${STAB_CAP_b:-300}";; c) echo "${STAB_CAP_c:-600}";; d) echo "${STAB_CAP_d:-420}";; e) echo "${STAB_CAP_e:-480}";; *) echo 300;; esac; }
run_slot() {
  local slot=$1; local n=${#jobs[@]}
  local d="$st/slot$slot"; local priv="s_${tag}_$slot.exe"
  STAB_LOCK_DIR="$d/.stab-running"
  mkdir "$STAB_LOCK_DIR" || { echo "Slot $slot is busy (or has a stale .stab-running lock)" >&2; return 2; }
  trap 'rmdir -- "$STAB_LOCK_DIR"' EXIT
  cp "$exe" "$d/$priv"
  for ((i=slot-FIRST; i<n; i+=PAR)); do
    local jb=(${jobs[$i]}); local s=${jb[0]} r=${jb[1]}
    local base="${tag}_${s}_${r}"; local sc="$st/scripts/$base.txt"
    python "$root/scripts/harness/stability/stab_genscript.py" "$s" "$r" > "$sc" || return
    local max; max=$(cap "$s")
    ( cd "$d"; rm -f crash_*.dmp
      env DRAKAN_AUDIO=0 PS2X_TRACE_RING=1 PS2X_MAX_SECONDS=$max PS2X_GS_SCALE=1 DRAKAN_INPUT_SCRIPT="$sc" DRAKAN_INPUT_LOGFILE="$logs/$base.in.log" "${EXTRA[@]+"${EXTRA[@]}"}" \
        timeout $((max + 120)) "./$priv" ./SCES_500.06 > "$logs/$base.log" 2>&1 && rc=0 || rc=$?; echo "$rc" > "$logs/$base.rc" )
    local rc; rc=$(cat "$logs/$base.rc")
    local v; v=$(bash "$root/scripts/harness/stability/stab_verdict.sh" "$logs/$base.log" "$rc" "$logs/$base.in.log")
    local reached; reached=$(grep -a -c 'script quit' "$logs/$base.in.log" 2>/dev/null || true)
    echo "$base $v rc=$rc quit=$reached bad=$(grep -a -c 'bad-resume' "$logs/$base.log") clamp=$(grep -a -c 'bad size\|size clamp' "$logs/$base.log") unwound=$(grep -a -c 'unwound at its own entry' "$logs/$base.log")" >> "$logs/${tag}_summary.txt"
    for dump in "$d"/crash_*.dmp; do
      [ ! -f "$dump" ] || mv "$dump" "$logs/${base}_$(basename "$dump")"
    done
  done
}
pids=()
for ((s=FIRST; s<FIRST+PAR; ++s)); do run_slot "$s" & pids+=("$!"); sleep 7; done
failed=0
for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
echo "== $tag summary"; cat "$logs/${tag}_summary.txt"; echo "ok boots: $(grep -c ' OK ' "$logs/${tag}_summary.txt") of $(wc -l < "$logs/${tag}_summary.txt")"
[ "$failed" -eq 0 ] && [ "$(wc -l < "$logs/${tag}_summary.txt")" -eq "${#jobs[@]}" ] && ! grep -qv ' OK ' "$logs/${tag}_summary.txt"
