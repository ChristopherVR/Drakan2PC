#!/bin/bash
# Scripted gameplay run with sound diagnostics (docs/reference/sound-debug.md).
#   scripts/harness/snd_test_run.sh <exe in build/<x>/bin> <input script> <name> [bin dir]
# Writes <bin>/../t/<name>.log (stderr of the game: DRAKAN_SND_LOG=3 key-on / voice-volume lines, [play]/[gain]/[setgain] lines, [hle] resource
# lines), <name>.in.log, <name>.wav (DRAKAN_AUDIO_DUMP, 48 kHz stereo) and the input script's `shot` screenshots.
# The game stalls or dies in the intro/load sometimes (docs/journal/2026-10-17/crash-listener.md): the run is retried up to 6 times until the script reaches `quit`.
# Menu navigation is done by DRAKAN_AUTOPRESS (Cross every 4 s until 30 s): "Load Game" is the first title entry, so a save in iso/mc0 is loaded.
exe=${1:?exe}; script=${2:?script}; name=${3:?name}
root="$(cd "$(dirname "$0")/../.." && pwd)"
bin=${4:-"$root/build/recomp_snd/bin"}
t="$bin/../t"; mkdir -p "$t"
export PS2X_MAX_SECONDS=${PS2X_MAX_SECONDS:-400} PS2X_GS=${PS2X_GS:-d3d11} DRAKAN_AUDIO=${DRAKAN_AUDIO:-0} DRAKAN_WINDOW_POS=300,100 DRAKAN_INPUT_AUTOSTART=0
export DRAKAN_HLE_LOG=100000 DRAKAN_SND_LOG=${DRAKAN_SND_LOG:-3} DRAKAN_SND_DBG=${DRAKAN_SND_DBG:-1}
export DRAKAN_AUTOPRESS=4000 DRAKAN_AUTOPRESS_NOSTART=1 DRAKAN_AUTOPRESS_UNTIL=30000
export DRAKAN_INPUT_SCRIPT="$script" DRAKAN_SHOT_DIR="$t" DRAKAN_INPUT_LOGFILE="$t/$name.in.log" DRAKAN_AUDIO_DUMP="$t/$name.wav"
cd "$bin" || exit 2
for try in 1 2 3 4 5 6; do
  "./$exe" "../../../iso/SCES_500.06" > "$t/$name.log" 2>&1 &
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
