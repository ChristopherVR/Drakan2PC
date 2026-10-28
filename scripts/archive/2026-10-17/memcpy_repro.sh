#!/bin/bash
# Reproduce / validate the "heap smashed by the scrolling water texture" crash (docs/journal/2026-10-17/crash-memcpy.md).
#
#   scripts/archive/2026-10-17/memcpy_repro.sh <exe> <boots> [tag] [seconds]
#
# Boots the game <boots> times with DRAKAN_AUTOPRESS=4000 and the START-mashing input script build/input_tests/g2.txt (gameplay
# starts at ~28 s, START taps every 60 game frames from frame 760) and counts the boots whose main thread died. Logs: build/memcpy_dbg/logs.
# The death needs the water texture's scroll position to pass 63.5 (about 45 s after the level starts) AND a live heap block right behind
# its pixel buffer; with the old cvt.w.s macro 9-11 of 12 boots died, with the fixed macro 0 of 24.
# Run it from anywhere; <exe> is copied to a private name first (so a rebuild meanwhile does not matter).
set -u
exe=${1:?usage: memcpy_repro.sh <exe> <boots> [tag] [seconds]}
n=${2:?boots}; tag=${3:-run}; secs=${4:-80}
root="$(cd "$(dirname "$0")/../../.." && pwd)"
logs="$root/build/memcpy_dbg/logs"; mkdir -p "$logs"
bin="$(cd "$(dirname "$exe")" && pwd)"; priv="memcpy_${tag}_$$.exe"
cp "$bin/$(basename "$exe")" "$bin/$priv"; trap 'rm -f "$bin/$priv"' EXIT
cd "$bin" || exit 2
export DRAKAN_AUTOPRESS=4000 DRAKAN_INPUT_AUTOSTART=1 PS2X_TRACE_RING=1 PS2X_MAX_SECONDS=$secs PS2X_GS=${PS2X_GS:-d3d11} DRAKAN_WINDOW_POS=100,100
export DRAKAN_INPUT_SCRIPT="$root/build/input_tests/g2.txt" DRAKAN_SHOT_DIR="$logs"
dead=0; : > "$logs/${tag}_summary.txt"
for i in $(seq 1 "$n"); do
  log="$logs/${tag}_$i.log"; export DRAKAN_INPUT_LOGFILE="$logs/${tag}_$i.in.log"
  timeout $((secs + 90)) "./$priv" "$root/iso/SCES_500.06" > "$log" 2>&1
  ring=$(grep -a -c 'trace-ring\] #' "$log")
  tgt=$(grep -a -o 'missing-target\][^=]*kind=[A-Za-z]* op=[A-Za-z ]* source=0x[0-9a-f]* target=0x[0-9a-f]*' "$log" | head -1 | sed 's/.*source=//')
  clamp=$(grep -a -c 'size clamp\|bad size' "$log")
  big=0; t=${tgt##*target=0x}; [ -n "$tgt" ] && [ $((16#$t)) -ge 16 ] && big=1
  if [ "$ring" -gt 0 ] || [ "$big" -eq 1 ]; then dead=$((dead + 1)); st=DEAD; else st=ok; fi
  echo "boot $i: $st ring=$ring memop_clamp=$clamp $tgt" | tee -a "$logs/${tag}_summary.txt"
done
echo "$dead of $n boots lost the main thread" | tee -a "$logs/${tag}_summary.txt"
[ "$dead" -eq 0 ]
