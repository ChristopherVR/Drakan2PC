# Ghidra setup and function list

## Versions
- Ghidra 12.1.4 PUBLIC (20260921) in `tools/ghidra/ghidra_12.1.4_PUBLIC/` (zip kept as `tools/ghidra/g.zip`)
- ghidra-emotionengine-reloaded v2.1.38 (build for Ghidra 12.1.4), unzipped into `Ghidra/Extensions/ghidra-emotionengine-reloaded/` (zip: `tools/ghidra/ext.zip`)
- JDK 21.0.12.1 (`%LOCALAPPDATA%\Programs\jdk-21.0.12.1+1`)
- Language id: `r5900:LE:32:default`. The .sla is compiled on first use (no manual step).
- Project: `tools/ghidra_proj/Drakan` (program `/SCES_500.06`), gitignored (`tools/*/`).

## Re-run (Git Bash, from decomp/)
```
export JAVA_HOME="~/AppData/Local/Programs/jdk-21.0.12.1+1"; export PATH="$JAVA_HOME/bin:$PATH"
G=tools/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat
# 1. import + full auto-analysis (~2 min)
$G "$(cygpath -w tools/ghidra_proj)" Drakan -import iso/SCES_500.06 -processor r5900:LE:32:default -overwrite -max-cpu 8
# 2. fix missed functions, then export CSVs (re-runnable; saves fixes into the project)
$G "$(cygpath -w tools/ghidra_proj)" Drakan -process SCES_500.06 -noanalysis -scriptPath "$(cygpath -w analysis/ghidra_scripts)" \
   -postScript FixFunctions.java -postScript ExportFunctions.java "$(cygpath -w analysis)"
```
Export only (no changes): add `-readOnly` and drop FixFunctions.

## Scripts (analysis/ghidra_scripts/, Java, work headless)
- `FixFunctions.java`: creates functions at (a) `jal` targets, (b) data pointers into .text that start with an `addiu sp,-N` prologue, (c) iteratively, at the start of any gap after a function that contains a `jr ra` before the next function.
- `ExportFunctions.java <outdir>`: writes `functions.csv`, `gaps.csv` (gaps > 64 bytes), `jumptables.csv`.

## Outputs
- `functions.csv`: start_addr_hex,end_addr_hex,size,name,num_callers,is_thunk (.text only, sorted; end is exclusive; for non-contiguous bodies start/end is the range containing the entry; num_callers = distinct call-type xref sites)
- `gaps.csv`: start,end,size,all_zero_or_nop
- `jumptables.csv`: jr_addr_hex,table_addr_hex,num_targets,targets_hex (`;`-separated, from Ghidra's computed-jump refs; table addr is best effort from nearby data refs)

## Stats
| stage | functions | .text coverage | gaps >64B |
|---|---|---|---|
| Ghidra auto-analysis only | 8816 | 96.76% | 518 (98 KB) |
| after FixFunctions | 12173 | 99.45% | 13 (1.5 KB) |

Final: 12173 functions (~12.5k `jr ra` in the ELF, so roughly consistent), 185 jump tables.

## Known issues
- Auto-analysis alone missed ~3.4k functions (mostly never `jal`'d directly: virtual methods / fn pointers). FixFunctions creates them heuristically: a gap starting right after a function that contains `jr ra` is assumed to be a function. Some may be fragments of a larger function (e.g. code only reachable through an unrecovered switch) or hand-written/multi-entry code; ~6.8k functions have 0 callers (expected for vtable/callback targets). 1316 functions are <=8 bytes (`jr ra; nop` stubs).
- Jump tables: 211 non-`ra` `jr` instructions exist in .text, 185 have recovered targets in jumptables.csv; ~26 (e.g. 0x2532e0, 0x253374, 0x25d0f8, 0x26640c, 0x2a0eb8) have none (could be tail-calls through registers/vtables rather than switches). Verify against the instruction before relying on it. Targets start at the jr's delay-slot+4 for some entries, i.e. Ghidra lists case labels, not the table contents - re-read the table words from the ELF for exact recompilation.
- Remaining 13 gaps (e.g. 0x4c4db0, 0x4b809c) are likely data/padding.
- Headless log has many "Unable to read bytes at ram:005xxxxx" decompiler warnings (.bss reads); harmless.
- Function names are all `FUN_xxxxxxxx` (stripped binary); `entry` at 0x100008.
