#!/bin/bash
# stab_verdict.sh <log> [rc]  -> prints "<VERDICT> <details>" for one boot log (used by stab_matrix.sh; can be re-run on old logs)
# CRASH   host exception ([crash] line from the unhandled-exception filter)
# DEAD    the guest main thread ended: [trace-ring] flush, "Error during program execution", "Game thread returned" before the end of the run, [terminate]
# MISSING guest branch to a non-function target >= 0x10 ([guest-branch:missing-target])
# HANG / EXITn / NOEXIT  process-level problems
log=${1:?log}; rc=${2:-0}; input=${3:-}
if [ ! -r "$log" ]; then echo 'NOLOG frames=? tgt= gt=0'; exit 0; fi
crash=$(grep -a -c '\[crash\]' "$log"); ring=$(grep -a -c 'trace-ring\] #' "$log")
exc=$(grep -a -c 'Error during program execution\|\[terminate\]\|\[main\] fatal' "$log")
tgt=''; big=0
while IFS= read -r candidate; do
  if [ $((candidate)) -ge 16 ]; then tgt=$candidate; big=1; break; fi
done < <(grep -a 'guest-branch:missing-target\]' "$log" | grep -oE 'target=0x[0-9a-fA-F]+' | cut -d= -f2)
frames=$(grep -a -o '\[gsr\] exit: frames=[0-9]*' "$log" | head -1 | sed 's/.*=//')
gt=$(grep -a -c 'Game thread returned' "$log")
stopped=$(grep -a -c 'PS2X_MAX_SECONDS reached\|script quit\|quit requested' "$log")
quit=0; [ -n "$input" ] && quit=$(grep -a -c 'script quit' "$input" 2>/dev/null || true)
v=OK
if [ "$crash" -gt 0 ]; then v=CRASH
elif [ "$exc" -gt 0 ]; then v=DEAD_EXC
elif [ "$ring" -gt 0 ]; then v=DEAD
elif [ "$big" -eq 1 ]; then v=MISSING
elif [ "$rc" = 124 ]; then v=HANG
elif [ "$rc" != 0 ]; then v=EXIT$rc
elif [ -z "$frames" ]; then v=NOEXIT
elif [ "$frames" -eq 0 ]; then v=NOPROGRESS
elif [ -n "$input" ] && [ "${quit:-0}" -eq 0 ]; then v=INCOMPLETE
elif [ "$gt" -gt 0 ] && [ "$stopped" -eq 0 ] && [ "${quit:-0}" -eq 0 ]; then v=INCOMPLETE
fi
echo "$v frames=${frames:-?} tgt=${tgt:-} gt=$gt"
