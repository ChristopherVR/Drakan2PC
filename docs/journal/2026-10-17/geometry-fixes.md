# Geometry fixes (branch `drakan-geo` of tools/PS2Recomp, worktree `tools/PS2Recomp-geo`)

## 1. Rynn's braid stretched to the ground: root cause = `SQRT.S` read the wrong register

The R5900 encodes `SQRT.S fd, ft` (source in the **FT** field; compilers leave FS = 0). `fpu_translator.cpp` emitted
`FPU_SQRT_S(ctx->f[fs])`, so **all 329 `sqrt.s` in the game computed sqrt($f0)** (the generated code had `FPU_SQRT_S(ctx->f[0])` in every
one). Every vector length / normalisation / quaternion-to-matrix in the EE code (skeleton + hair chain, bone lengths) was fed garbage.
Fix: emit `ctx->f[ft]`. Needs regeneration (the operand is baked into `gen/`), not just a rebuild.

Evidence (same scripted run, `build/input_tests/geo1.txt`, build/recomp_geo/run/run.sh):
* `build/screenshots/r4_hair_before_c.png` (gen with the old sqrt operand): Rynn is not drawn at all in this build.
* `r4_hair_after_b/c/d.png`: Rynn drawn, braid short and natural, also when turning/idle-animating.
Note: the user's dist exe draws Rynn with a stretched braid, i.e. the same bug in a milder form; both come from the same wrong sqrt.

## 2. Other emitter fixes (VU0 macro mode, 6 EE functions)
* `VDIV`: x/0 gave Q = 0; now PS2 semantics (+-FLT_MAX) via `ps2_fdiv`.
* `VRSQRT`: ignored FS (Q = 1/sqrt(ft), 0 for ft <= 0); now `fs / sqrt(|ft|)` via `ps2_frsqrt`.
* `VSQRT`: uses `|ft|` with the PS2 helper.
* `VFTOI*`: positive overflow saturates to 0x7FFFFFFF (SSE gives 0x80000000).

## 3. Audited and found correct
All COP1 ops (ADDA/MADD/MSUB/RSQRT/CVT/compares/BC1x), SPECIAL mult/div/shifts/MOVZ/N, LWL/LWR/SWL/SWR/LDL/LDR, MMI MADD/MULT1/DIV1, COP2 field masks
(dest bit order), VMADDA/VMULA families, ITOF; libm stub ABIs (sinf/cosf use f12/f13 + a0 exactly like the guest code; double functions use the GPR soft-double ABI).
VCLIP in macro mode has a suspicious bit order (bit0 = x<-w) but is not used by the game.

## 4. Not reached
Arokh blurry/clipping textures and smoke textures: no root cause found in the EE side; they look like GS-side issues (mip/LOD/filtering, alpha blend
of sprite particles) owned by the gs agent. No geo_gs.patch produced. Arokh is not reachable in Level 0 by script. Re-test smoke with this build first:
particle matrices also used the broken sqrt.

## 5. Merge notes
Commit 997ee7a on `drakan-geo` touches only `ps2xRecomp/src/lib/fpu_translator.cpp` and `vu_translation_helpers.cpp`. **Regenerate** `gen/` (the recompiler changed), then rebuild.
Build dir: `build/recomp_geo` (setup.ps1, gen.ps1, build.ps1); `build/recomp_geo_old` = A/B build with the old sqrt operand (sed on gen).
