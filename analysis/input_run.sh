#!/bin/bash
# usage: input_run.sh [-w] script args...   (run from decomp/). -w = write (no -readOnly)
export JAVA_HOME="${JAVA_HOME:-$HOME/AppData/Local/Programs/jdk-21.0.12.1+1}"; export PATH="$JAVA_HOME/bin:$PATH"
RO=-readOnly; if [ "$1" = "-w" ]; then RO=; shift; fi
S=$1; shift
tools/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat tools/ghidra_proj Drakan -process SCES_500.06 -noanalysis $RO -scriptPath analysis/ghidra_scripts -postScript $S "$@" 2>&1 | grep -vE "Unable to read bytes|^$"
