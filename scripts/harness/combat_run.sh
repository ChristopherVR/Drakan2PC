#!/bin/bash
# Scripted gameplay run for the combat / input tests (docs/reference/combat-controls.md).
#   scripts/harness/combat_run.sh <input script> <name> [exe=drakan.exe] [bin dir]
# Loads the save in iso/mc0 through DRAKAN_AUTOPRESS (Cross every 4 s until 30 s; Load Game is the first title entry), writes
# <bin>/../t/<name>.log (game stderr, [cmd]/[cmb]/[ent] probe lines when DRAKAN_CMD_LOG / DRAKAN_COMBAT_LOG are set), <name>.in.log (input log) and the script's `shot` BMPs.
# The game stalls in the intro/load sometimes: the run is retried up to 6 times until the script reaches `quit`.
script=${1:?script}; name=${2:?name}; exe=${3:-drakan.exe}
root="$(cd "$(dirname "$0")/../.." && pwd)"
bin=${4:-"$root/build/recomp_combat/bin"}
t="$bin/../t"; mkdir -p "$t"
export PS2X_MAX_SECONDS=${PS2X_MAX_SECONDS:-400} PS2X_GS=${PS2X_GS:-d3d11} DRAKAN_AUDIO=${DRAKAN_AUDIO:-0} DRAKAN_WINDOW_POS=${DRAKAN_WINDOW_POS:-300,100} DRAKAN_INPUT_AUTOSTART=0
export DRAKAN_AUTOPRESS=4000 DRAKAN_AUTOPRESS_NOSTART=1 DRAKAN_AUTOPRESS_UNTIL=30000
export DRAKAN_INPUT_SCRIPT="$script" DRAKAN_SHOT_DIR="$t" DRAKAN_INPUT_LOGFILE="$t/$name.in.log"
cd "$bin" || exit 2
for try in 1 2 3 4 5 6; do
  "./$exe" "${COMBAT_ELF:-../../../iso/SCES_500.06}" > "$t/$name.log" 2>&1 &
  pid=$!
  sleep 20
  while kill -0 $pid 2>/dev/null; do
    a=$(stat -c %Y "$t/$name.in.log" 2>/dev/null || echo 0); n=$(date +%s)
    if [ $((n-a)) -gt 45 ]; then kill -9 $pid 2>/dev/null; echo "attempt $try hung"; break; fi
    sleep 5
  done
  wait $pid 2>/dev/null
  if grep -aq "script quit" "$t/$name.in.log"; then echo "attempt $try ok"; exit 0; fi
done
exit 1
