#!/bin/bash
# Reproduce / validate the "main thread dies after START" crash family (docs/journal/2026-10-17/crash-listener.md).
#
#   scripts/harness/listener_repro.sh <exe> <boots> [tag] [seconds]
#
# Boots the game <boots> times with DRAKAN_AUTOPRESS (START+CROSS tapped every 4 s: logo movie, New Game, intro movie skipped with
# START, level load) and reports how many boots lost their main thread. Run it from anywhere; logs go to build/crash_listener/logs.
#
#   exe      path of the built game, e.g. build/recomp_out/bin/drakan.exe (copy it to a private name first if you rebuild meanwhile)
#   boots    number of boots (each takes <seconds>+10 s of wall time)
#   tag      log name prefix (default: run)
#   seconds  PS2X_MAX_SECONDS per boot (default 30; the intro skip happens at ~20 s)
#
# Environment knobs:
#   DRAKAN_RES_LATENCY_US=0   STRESS MODE: the HLE resource completions arrive without the 2 ms guest-time latency, so every completion lands
#                             while the main thread polls the resource cache. ~50 % of the boots died with the unfixed runtime, 0 with the fix.
#   PS2X_UNWIND_FIX=0         run the patched runtime with the OLD behaviour (A/B test with the same binary); the first occurrences of the
#                             faulty path are printed as "[unwind-at-entry] ..." in the log.
#   DRAKAN_AUTOPRESS=<ms>     tap period (default 4000)
#
# A boot counts as DEAD when the runtime flushed the control-transfer ring ("[trace-ring] ..." = the main thread ended: null JALR at
# 0x12c4d8 / 0x490bd4, jump to pc 0..3, or the movie player calling a destroyed listener: "0x440624 -> 0x0") or when a missing-target report
# shows a non-trivial garbage jump target (the listener call through a recycled screen: "source=0x440624 target=0xbd3e7095"). A
# "[guest-branch:missing-target] source=0x1/0x2" line WITHOUT a ring flush is the shutdown artifact of the unfixed runtime and does not count.
set -u
exe=${1:?usage: listener_repro.sh <exe> <boots> [tag] [seconds]}
n=${2:?boots}
tag=${3:-run}
secs=${4:-30}
root="$(cd "$(dirname "$0")/../.." && pwd)"
logs="$root/build/crash_listener/logs"
mkdir -p "$logs"
exe="$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")"
cd "$(dirname "$exe")" || exit 2
iso="$root/iso/SCES_500.06"
export DRAKAN_AUTOPRESS=${DRAKAN_AUTOPRESS:-4000} PS2X_TRACE_RING=1 PS2X_MAX_SECONDS=$secs PS2X_GS=${PS2X_GS:-d3d11} DRAKAN_WINDOW_POS=100,100
dead=0
: > "$logs/${tag}_summary.txt"
for i in $(seq 1 "$n"); do
  log="$logs/${tag}_$i.log"
  timeout $((secs + 60)) "$exe" "$iso" > "$log" 2>&1
  ring=$(grep -a -c 'trace-ring\] ' "$log")
  what=$(grep -a -o 'missing-target\][^=]*kind=[A-Za-z]* op=[A-Za-z ]* source=0x[0-9a-f]* target=0x[0-9a-f]*' "$log" | head -1)
  listener=$(grep -a -c 'kind=3 0x440624 -> 0x0' "$log")
  unw=$(grep -a -c 'unwind-at-entry' "$log")
  # real death = main thread ended (ring flushed) or a jump to a non-trivial garbage target; targets 0..3 without a ring flush are only the
  # stop-request artifact of the unfixed runtime and are reported but do not count
  tgt=$(grep -a -o 'missing-target\][^=]*kind=[A-Za-z]* op=[A-Za-z ]* source=0x[0-9a-f]* target=0x[0-9a-f]*' "$log" | head -1 | sed 's/.*target=0x//')
  big=0; [ -n "$tgt" ] && [ $((16#$tgt)) -ge 16 ] && big=1
  if [ "$ring" -gt 0 ] || [ "$big" -eq 1 ]; then dead=$((dead + 1)); state=DEAD; else state=ok; fi
  echo "boot $i: $state ring=$ring listener_null=$listener unwind_at_entry=$unw $what" | tee -a "$logs/${tag}_summary.txt"
done
echo "$dead of $n boots lost the main thread  (logs: $logs/${tag}_*.log)" | tee -a "$logs/${tag}_summary.txt"
[ "$dead" -eq 0 ]
