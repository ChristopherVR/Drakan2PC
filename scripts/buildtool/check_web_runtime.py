"""Compile and run portable runtime and renderer checks in Wasm without game data."""

import argparse
from pathlib import Path

from prepare_web_deps import ensure_sdk, run

TESTS = (
    "vu_simd_tests",
    "vu_rounding_tests",
    "wasm_vector_exception_tests",
    "web_input_tests",
    "web_iso_tests",
    "software_frame_clock_tests",
    "gs_raster_math_tests",
    "gs_cpu_tiles_tests",
    "gs_cpu_async_tests",
    "texture_core_tests",
    "gs_hd_texture_tests",
    "host_pad_state_tests",
    "input_camera_tests",
)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=root / "build/web-runtime-tests")
    parser.add_argument("--test", choices=TESTS, action="append", help="Run only the selected check (repeatable)")
    parser.add_argument("--raster-threads", type=int, choices=(8, 16), default=8, help="Tile parity worker count")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sdk, emcc, env = ensure_sdk(root)
    node = next((sdk / "node").glob("*/bin/node"))
    runtime = root / "ps2recomp/ps2xRuntime"
    for test in args.test or TESTS:
        target = output / f"{test}.js"
        extra = []
        if test == "web_input_tests":
            extra = [
                runtime / "src/lib/drakan_web.cpp",
                "-pthread",
                "-sPTHREAD_POOL_SIZE=2",
                "-sPROXY_TO_PTHREAD=1",
                "-sEXIT_RUNTIME=1",
                "-sUSE_GLFW=3",
                "--js-library",
                root / "runtime/web/glfw_proxy.js",
                "-UNDEBUG",
            ]
        if test == "web_iso_tests":
            extra = [root / "runtime/hle/src/IsoSourceWeb.cpp", "-pthread", "-UNDEBUG", "-sENVIRONMENT=node,worker"]
        if test in ("gs_cpu_tiles_tests", "gs_cpu_async_tests", "gs_hd_texture_tests"):
            extra = [
                runtime / "src/lib/gs/gs_cpu_backend.cpp",
                runtime / "src/lib/gs/ps2_gs_memory.cpp",
                root / "runtime/gs/src/gs_texup.cpp",
                root / "runtime/gs/src/texture_pack.cpp",
                f"-I{root / 'runtime/gs/src'}",
                "-sFETCH=1",
                "-pthread",
                "-sPTHREAD_POOL_SIZE=24",
                f'-DDRAKAN_TEST_RASTER_THREADS="{args.raster_threads}"',
                "-sSTACK_SIZE=2097152",
                "-sDEFAULT_PTHREAD_STACK_SIZE=2097152",
                "-sSTACK_OVERFLOW_CHECK=2",
                "-sINITIAL_MEMORY=134217728",
                "-DPS2_RUNTIME_LOGS=0",
                "-DAGRESSIVE_LOGS=0",
                "-UNDEBUG",
            ]
        source = runtime / f"tests/{test}.cpp"
        if test == "input_camera_tests":
            source = root / "runtime/input/tests/input_test.cpp"
            extra = [
                *sorted((root / "runtime/input/src").glob("*.cpp")),
                f"-I{root / 'runtime/input/include'}",
                "-sPROXY_TO_PTHREAD=1",
                "-sEXIT_RUNTIME=1",
                "-pthread",
                "-sPTHREAD_POOL_SIZE=2",
                "-sSTACK_SIZE=2097152",
                "-sDEFAULT_PTHREAD_STACK_SIZE=2097152",
                "-sSTACK_OVERFLOW_CHECK=2",
                "-UNDEBUG",
            ]
        if test == "host_pad_state_tests":
            source = root / "scripts/tests/host_pad_test.cpp"
            extra = ["-pthread", "-sPTHREAD_POOL_SIZE=2", "-UNDEBUG"]
        if test == "texture_core_tests":
            source = root / "runtime/gs/tests/test_texup.cpp"
            extra = [
                root / "runtime/gs/src/gs_texup.cpp",
                f"-I{root / 'runtime/gs/src'}",
                "-pthread",
                "-sPTHREAD_POOL_SIZE=4",
                "-sENVIRONMENT=node,worker",
            ]
        if test == "gs_cpu_async_tests":
            extra.append(runtime / "src/lib/gs/gs_cpu_async_backend.cpp")
        run(
            [
                emcc.parent / "em++",
                "-std=c++20",
                "-fwasm-exceptions",
                "-O2",
                "-msimd128",
                "-msse4.1",
                "-ffp-contract=off",
                "-fno-strict-aliasing",
                f"-I{runtime / 'src/lib'}",
                f"-I{runtime / 'include'}",
                f"-I{root / 'runtime/hle/include'}",
                f"-I{root / 'runtime/gs/include'}",
                source,
                *extra,
                "-sWASM_ASYNC_COMPILATION=0",
                "-o",
                target,
            ],
            env=env,
        )
        run([node, target], env=env)


if __name__ == "__main__":
    main()
