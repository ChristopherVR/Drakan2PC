# Recompiler evaluation: PS2Recomp vs. own translator

Date: 2026-10-16. Target: SCES_500.06 (stripped R5900 ELF, entry 0x100008, .text 0x100000..0x4d73f4).

## 1. Recommendation

**(b) Fork PS2Recomp (tools/PS2Recomp) and extend it. Do not write a translator from scratch yet.**

Reasoning:
- It already works end to end on our ELF. Analyzer -> TOML -> 8,299 generated C++ files in 17 s, no decode failures. Sampled generated files compile cleanly under MSVC against its runtime.
- It ships what we would otherwise spend months on: a runtime (memory, syscalls, SIF/RPC, a software GS and a VU1 interpreter, a pad stub, an IOP/IRX layer), MMI and FPU semantics, and a per-game override hook.
- GPLv3 is fine for personal, non-distributed use. If we ever distribute binaries, the generated code plus runtime would be GPL.
- Weaknesses are in code quality and size of output (section 4), not in coverage. They can be fixed in the emitter, which is about 10k lines of C++ (ps2xRecomp/src/lib), without touching the runtime.
- Fallback plan (c): if the emitter changes become a rewrite, replace only the emitter with a Python R5900->C++ generator in decomp/recomp/ and keep PS2Recomp's runtime headers and macros. Decision point: after the first successful boot attempt (section 6, step 4). The fork should keep the decoder, macros and runtime as the stable part.

Not recommended: (a) adopt as-is. The generated code is too large and slow, and the hook mechanism is address-based and coarse (section 5).

## 2. State of the field (searched 2026-10-16)

- **ran-j/PS2Recomp** is the only serious PS2 static recompiler found. Press coverage in Jan 2026 (Time Extension, Gigazine, Retro Handhelds, Notebookcheck) calls it experimental and says no game is confirmed playable. The last upstream commit I saw is 2026-10-14 ("Emulate PS2 float semantics for EE FPU results and DIV.S/RSQRT.S (#272)"), so it is actively developed with PR numbers past 270. README: "Performance is very bad for VU and GS. Hardware emulation is partial and many paths are stubbed."
- No competing PS2 recompiler found (one search; no forks of note surfaced). I did not verify booting games or Discord claims.
- **N64Recomp (Mr-Wiseguy)** lessons that PS2Recomp copies in part and we should follow:
  1. TOML-driven function list, per-function stubs/ignore, instruction patches.
  2. Generated functions have a uniform signature `(rdram, ctx, runtime)`. A lookup table maps guest address -> host function for indirect calls and for overrides.
  3. Hooks and patches are written in C/C++ and registered by address, so any function can be replaced or wrapped. Output is split per function for incremental builds.
  4. Overlays are handled by registering sections with load addresses and a section table. This matters here because of the DVP overlay sections.

## 3. PS2Recomp findings

| Item | Finding |
|---|---|
| License | GPL-3.0 |
| Layout | ps2xAnalyzer (ELF scan, TOML writer), ps2xRecomp (decoder + C++ emitter), ps2xRuntime (memory, syscalls, GS, VU1, kernel stubs, raylib window), ps2xIOP (R3000A IRX execution + HLE), ps2xStudio, ps2xTest, vita/android ports |
| Dependencies | Fetched by CMake from GitHub at configure time: ELFIO, toml11, fmt, libdwarf, rabbitizer, nlohmann_json, raylib 5.5, optionally imgui. Needs network on first configure. |
| Build | Builds clean with VS 2022 + the bundled CMake. See section 7. |
| Stripped ELFs | Works without symbols. Function discovery is heuristic (8,287 functions found, 0 symbols). The analyzer has an SCE SDK signature scanner that matched about 194 library functions (sceDma*, scePad*, sceGs*, ...) and emitted `name@0xADDR` stub bindings. It also accepts a Ghidra export (`general.ghidra_output` CSV plus TOML, via ps2xRecomp/tools/ghidra/ExportPS2Functions.java), which the README names as the preferred route for retail games. Our functions.csv may need a format adapter. |
| R5900 coverage | Decoder is about 1k lines with 0 decode failures on our ELF. MMI 128-bit ops are done with SSE intrinsics (PEXT*, PADD*, PMADD*, PLZCW and others). GPRs are stored as `__m128i`, so LQ/SQ and 128-bit ops are native. Multiply-accumulate (MADD/MADDU, pipeline 1 variants) is present in the decoder and macros. |
| FPU | Recently added PS2 float emulation (no NaN/Inf, denormals flushed, clamping) with a fast path (`ps2_fadd` and similar, slow path only for special values). The accumulator is `ctx->f_acc`. Rounding-mode and flag details are not verified. |
| COP2 / VU0 | Macro mode is translated to SSE (vu_translation_helpers, vu_translator). Coverage and correctness for our game must be tested. Micro mode (VCALLMS/VCALLMSR) goes to a VU interpreter. |
| VU1 | ps2_vu1_core (interpreter), ps2_vu1_lower, and a VIF1 interpreter. Documented as slow. We have .vutext and the analysis in decomp/analysis/vu, so an alternative is to HLE the game's microprograms to native C++. |
| Jump tables / indirect | The analyzer reported "0 jump tables detected". Instead, for each function with an unresolved JR or JALR, the emitter "promotes fallback entries": a `switch(ctx->pc) { case ...: goto label }` at function entry covering every instruction. Result on our ELF: 6,896 functions with unresolved JR/JALR and 1,418,946 fallback entries. There are 728,650 resumable entry points, and 363 MB of generated C++. This works but bloats the output, slows compile time and hurts optimization. A real jump-table recognizer (lw/addu/jr patterns from lui/addiu table + sltiu bounds) would shrink this dramatically. A stub, `generateJumpTableSwitch`, exists but is not driving this. |
| Indirect calls | `runtime->lookupFunction(addr)` for unresolved JAL/J and for JALR. A function table is emitted in register_functions.cpp. |
| Memory model | Macros READ8..128 / WRITE8..128. Each expands to a lambda that checks `PS2Runtime::isSpecialAddress(addr)` and then either goes to `runtime->Load32/Store32` (MMIO, scratchpad, GS, DMA, and so on) or does a fast RDRAM access via `rdram` base pointer. Scratchpad (0x70000000) and 0x1000xxxx/0x1200xxxx hardware register dispatch exist in ps2_memory.cpp (about 2.5k lines, with timers, DMA, GIF and GS CSR handling). |
| Codegen quality | Very literal. Every instruction writes `ctx->pc`. Branch delay slots are emitted twice (as a duplicated path), each with `ctx->in_delay_slot` toggling. Every GPR access goes through `_mm_extract_epi*` and a ternary for r0. `ADD32` wraps to uint32 for addresses. Expect roughly 3-10x slower than a clean lowering (to be measured). |
| Runtime shipped | Software GS (gs_frontend.cpp, gs_cpu_backend.cpp), raylib for window and presentation, syscall dispatcher, EE scheduler (threads, semaphores), SIF RPC, CD/file IO stubs, memory card, MPEG stub, audio (miniaudio), pad, debug panel. IOP side has an R3000A interpreter for IRX files and generic HLE fallbacks. |
| Hooks | `PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)` runs `applyFn(runtime)` at loadELF. `bindAddressHandler(runtime, addr, "handler")` binds to existing named stubs only. We can write arbitrary function replacements through the runtime function table (`registerFunction` at an address), but there is no first-class "call the original" or pre/post hook API, as N64Recomp has. |
| Maturity | No confirmed booting commercial game. Single-digit contributors. Treat the runtime as scaffolding with many stubs. |

## 4. Gap list for Drakan

Ordered by priority. Estimates are working days for one engineer, rough.

1. **Function discovery on a stripped binary**: feed Ghidra's functions.csv through the `ghidra_output` path (adapter for CSV format if needed), and compare against the heuristic 8,287. Misses cause "function not found" at runtime. 1-3 d.
2. **Jump-table recovery**: implement table pattern recognition so JR through a table becomes a bounded switch, and promoted-entry fallbacks are limited to genuinely unknown cases. Target: output under 50 MB, compile under 10 min. 3-5 d. Also covers C++ virtual calls (JALR via vtable), which need the function-table lookup, not switches.
3. **Emitter efficiency**: only write `ctx->pc` where needed (calls, syscalls, branches into unknown targets), remove duplicated delay-slot paths, use plain `uint64_t r[32]` (or lo/hi split) plus separate 128-bit upper halves for the rare MMI ops, replace READ/WRITE lambdas with inline functions. 5-10 d. Quality gate is a micro benchmark, e.g. 60 fps on the game's main loop in the debug build.
4. **Exception handling in the C++ game** (stripped, exceptions): the exception tables live in .eh_frame / .gcc_except_table and are driven by `_Unwind_*` code that walks the stack via registers. Native recompilation cannot unwind through generated frames. Plan: either HLE `__throw`/`_Unwind_RaiseException` with setjmp/longjmp-like machinery over our own frame records, or check whether the game actually throws at runtime (catch first, probably rarely). Unknown until we trace. 2-10 d, highest risk.
5. **Overlays / DVP sections**: confirm what in the DVP sections is code (EE overlay loaded at runtime) vs. data. If the game loads code at runtime from the CD, it must be recompiled separately and registered in the function table after load. Check how ps2xRecomp handles non-.text sections. 2-4 d.
6. **SDK / syscall / SIF**: the analyzer matched 194 SDK functions. 258 more were detected without runtime handlers (analyzer output). Need handlers for each used (sceCd*, SifRpc, sceGs*, sceDma*, mc, pad, sound via IOP). Drakan uses a CD-streamed FMV and sound; check the IOP side. 5-15 d.
7. **GS rendering**: the software GS is slow ("very bad for VU and GS"). Options: keep it for first boot to validate, then add a GPU backend (D3D11 or OpenGL via raylib/own), or HLE the game's GIF packet submission to a modern renderer. 10-30 d. Largest unknown.
8. **VU1 microcode**: use the interpreter initially. Later, translate our game's microprograms (we have them in .vutext) to native C++ or HLE the common ones. 5-20 d.
9. **Input and camera**: see section 5. Hook the game's pad read path to a host-input layer, and overrides for camera. 3-5 d once the game runs.
10. **Hardware-accurate bits that games rely on**: timers/COUNT, VBlank interrupts and callbacks (AddIntcHandler), DMA completion polling on 0x1000xxxx, GS CSR VSYNC, cache ops (CACHE instruction is patched out, which is fine), and unaligned load/store (LWL/LWR/LDL/LDR), which should be checked in the emitter. 5 d.

Sum: about 45-120 working days to a playable build, dominated by GS/VU and exception handling. A from-scratch translator saves nothing in the runtime items and re-spends about 20-30 d on decode and emit, which is why (c) is only a fallback.

## 5. Proposed architecture for generated code

Keep PS2Recomp's top-level shape, change the details.

```
// recomp_context.h
struct R5900Ctx {
    uint64_t r[32];        // low 64 bits; r[0] always 0 (emit constants instead of reads)
    uint64_t r_hi[32];     // upper 64 bits, only touched by MMI/LQ/SQ/PCPYH etc.
    uint64_t hi, lo, hi1, lo1; uint32_t sa;
    float    f[32]; float facc; uint32_t fcr31;   // COP1
    uint32_t vi[16]; float vf[32][4]; ...           // VU0 macro state (COP2)
    uint32_t pc;           // only written before calls/syscalls/dynamic jumps
};
using GuestFn = void(*)(uint8_t* rdram, R5900Ctx* ctx, Runtime* rt);
```

Memory access (all inline functions, address is a 32-bit guest vaddr):
```
// KSEG0/1 mirror: strip top 3 bits for 0x8/0xA... segments.
inline uint32_t phys(uint32_t va) { return va & 0x1FFFFFFF; }
inline uint32_t rd32(R& c, uint32_t va) {
    uint32_t p = va & 0x1FFFFFFF;
    if (likely(p < 0x02000000)) return *(uint32_t*)(c.rdram + p);   // 32 MB RDRAM
    return slow_rd32(c, va);   // scratchpad 0x70000000..0x70003FFF (not masked: check va first),
                               // 0x1000xxxx timers/DMA/VIF/GIF/IPU, 0x1200xxxx GS privileged regs,
                               // 0x1C00xxxx IOP RAM, BIOS ROM 0x1FC00000
}
```
Notes: scratchpad is a separate 16 KB allocation tested with `(va >> 14) == 0x1C000` before masking. Cap addresses to the exact rdram size with a debug build that traps out-of-range accesses. In release, RDRAM can be mirrored with a 64 MB guard mapping.

Calls:
- Direct JAL to a known function: a direct C++ call `fn_00123456(rdram, ctx, rt)` after setting `ctx->r[31]`.
- JALR / JR (not `$ra`) / unresolved JAL: `rt->call(ctx->r[x])` through a function table. Layout: an array indexed by `(addr - 0x100000) >> 2` of 4.9 MB entries for .text, plus a hash map for overlay/runtime-loaded code. Entries default to a "missing function" trap that logs the address (this also shows missed functions during bring-up).
- Real jump tables: switch inside the function. Unknown JR inside a function: dispatch via function table; fall back to the promoted-entry switch only for functions in an allowlist.
- `JR $ra`: plain `return`. Mid-function entry points (setjmp-style longjmp, thread switches) require context save/restore. The scheduler runs each guest thread on a host thread or fiber, so the guest "return into another function" is rare.
- Branch delay slots: emit the slot once before the branch condition is applied (using a temporary for the condition computed before the slot), except for the branch-likely case where the slot is conditional.

Hooks and overrides (N64Recomp-style):
- The function table is the single source of truth. Each generated entry is `FnEntry{ GuestFn impl; GuestFn override; }`.
- Registration in C++: `HOOK_REPLACE(0x0012ab40, my_func)` and `HOOK_BEFORE/AFTER(addr, fn)`. `call_original(addr)` is available inside replacements.
- Direct calls to hookable functions go through the table when the function is listed in a TOML `[hooks]` section (the emitter emits an indirect call for these addresses only), so hot, unhooked paths stay direct. Alternative: a build-time flag emits all calls indirect for easy debugging.
- Hand-written C++ lives in decomp/runtime/ (our code, separate from the PS2Recomp tree) as game_overrides: input (mouse/keyboard mapped to the pad state buffer or to the game's pad-read function), camera (replace the camera update function and write the view matrix), plus bug fixes. These attach through the existing `PS2_REGISTER_GAME_OVERRIDE` module, which gives per-ELF CRC matching.
- Input path: the game reads pads via `scePadRead` (SDK, already stubbed by the runtime in Pad.cpp). Plan A: feed host input into the pad buffer there (analog sticks and buttons). Plan B (mouse look): override the camera/player turn function(s) and add mouse deltas to the yaw/pitch state. Identify via Ghidra once running.

Function-lookup granularity: one `.cpp` per function (like now) is bad for compile times with 8k files. Use batches of about 50-200 functions per translation unit (unity-style, as the runtime's own CMake does) for faster full builds, and one TU per hooked function for incremental rebuilds. Keep generated code out of git (decomp/build/).

## 6. Proposed plan

1. Fork the tool into decomp/tools/PS2Recomp (done, pristine clone). Keep local changes as a patch series, or commit to a branch if we create a repo for the tool.
2. Run with the Ghidra function list. Compare coverage against the heuristic list.
3. Get a boot attempt on a debug build with the unchanged emitter (link the 8k files plus runtime). Expected: link time and size problems, missing SDK stubs; fix top blockers.
4. **Decision point**: if performance or compile time is unacceptable, do gap items 2 and 3 in the emitter (preferably) or move to (c).
5. Add input/camera overrides.
6. Replace the GS and hot VU paths when the game boots.

## 7. What was tried (decomp/build/ps2recomp_try, gitignored)

- Tools: VS 2022 Community at `C:\Program Files\Microsoft Visual Studio\2022\Community`. Bundled CMake at `...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`. No ninja (used the Visual Studio generator).
- Build scripts reviewed first: only CMake with FetchContent from GitHub (ELFIO, toml11, fmt, libdwarf, rabbitizer, nlohmann_json, raylib 5.5). No custom commands or downloaded binaries observed (not exhaustive: the repo also contains prebuilt Vita .suprx modules, which we did not run).
- Recompiler build (`-DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF`): configure about 95 s, build OK, in `b/ps2xRecomp/Release/ps2_recomp.exe` and `b/ps2xAnalyzer/Release/ps2_analyzer.exe`.
- `ps2_analyzer iso/SCES_500.06 config.toml` ran in 3 s: 194 SDK functions stubbed by address, 258 more SDK-like functions without runtime handlers, 0 jump tables, 0 patches. Written to `config.toml` (814 lines).
- `ps2_recomp config.toml`: 17 s. 8,287 functions, 8,153 recompiled, 134 stubs, 0 decode failures, 8,296 generated functions, 728,650 extra entry points, 6,896 functions with unresolved JR/JALR, 1,418,946 fallback entries. Output: 8,299 files, 363 MB in `output/`.
- Runtime build (`rt/`, debug UI off): `ps2_runtime.lib` and `ps2EntryRunner.exe` built OK.
- Sample compile: `sub_004C7B60` and `sub_004CD5F0` (a 229 KB function) compile with cl /O2 against the runtime headers (needs include dirs: runtime include, runtime src/lib, src/lib/Kernel, src/lib/Kernel/Stubs, ps2xIOP/include). `t.bat` there reproduces it. The full link of all 8k files and a run were NOT attempted (no boot test done).
- Caveat: I did not verify the runtime's behavior at all; coverage statements above come from reading source and the generated output, not from running the game.
