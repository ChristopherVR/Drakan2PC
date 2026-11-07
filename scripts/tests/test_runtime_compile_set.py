"""Policy: the source check CI and the release gate run compiles what the shipped game compiles.

The Rabe v0.1.6 failure (drakan_overrides.cpp C3481) shipped because no job compiled the runtime sources from a clean
checkout. These tests keep the `runtime-check` CMake preset (DRAKAN_CHECK_RUNTIME_SOURCES) honest:

  * the check builds the same ps2_runtime target the game links, so a source cannot be listed for the game and not
    for the check; only files that include the locally generated VU image are skipped, and only under src/lib/vu;
  * every tracked runtime .cpp is reachable from a CMake list (no orphan that nothing compiles);
  * the builder kit and installer ship every one of those sources and the CMake files that list them;
  * CI and the Release workflow actually run the check, and the release publishes only after it passed.
No game data and no compiler needed.
"""

import importlib.util
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = "ps2recomp/ps2xRuntime"
VU_IMAGE_INCLUDE = re.compile(r"ps2_vu1_(image|aot_slice)\.inc")


def tracked(*pathspec):
    out = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z", "--", *pathspec], text=True)
    return [p for p in out.split("\0") if p]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def cmake_text():
    """Everything that can name a runtime source: the runtime CMake list and the shared cmake modules."""
    parts = [read(f"{RUNTIME}/CMakeLists.txt")] + [read(p) for p in tracked("cmake/*.cmake")]
    return "\n".join(parts)


class RuntimeCompileSetTests(unittest.TestCase):
    def test_every_runtime_source_is_listed_somewhere(self):
        text = cmake_text()
        orphans = []
        for path in tracked(f"{RUNTIME}/src/*.cpp"):
            rel = path[len(RUNTIME) + 1 :]
            if rel.startswith("src/lib/Kernel/"):  # file(GLOB_RECURSE ... Kernel/*.cpp)
                continue
            if rel not in text and rel[len("src/") :] not in text and Path(rel).name not in text:
                orphans.append(rel)
        self.assertEqual(orphans, [], "runtime sources no CMake list compiles; list them or delete them")

    def test_only_vu_image_users_can_be_skipped_by_the_check(self):
        users = [
            p for p in tracked(f"{RUNTIME}/src/*.cpp") if VU_IMAGE_INCLUDE.search(read(p)) and "/src/lib/vu/" not in p
        ]
        self.assertEqual(
            users, [], "a file outside src/lib/vu includes the VU image, so the source check would silently skip it"
        )

    def test_check_builds_the_games_own_targets(self):
        cmake = read(f"{RUNTIME}/CMakeLists.txt")
        self.assertIn("add_library(ps2_runtime STATIC", cmake)
        # The check adds nothing of its own to ps2_runtime; it only marks VU-image users header-only.
        block = cmake[cmake.index("if(DRAKAN_CHECK_RUNTIME_SOURCES") :]
        block = block[: block.index("\nendif()\n")]
        self.assertIn("HEADER_FILE_ONLY", block)
        self.assertNotIn("target_sources(ps2_runtime", block)
        self.assertIn("RUNNER_SRC_FILES", block, "the runner's own sources must be compiled by the check too")
        presets = read("CMakePresets.json")
        self.assertIn('"name": "runtime-check"', presets)
        self.assertIn('"inherits": "game"', presets[presets.index('"name": "runtime-check"') :][:400])

    def test_kit_ships_every_runtime_source_and_cmake_list(self):
        spec = importlib.util.spec_from_file_location("kit", ROOT / "scripts/buildtool/package_builder_kit.py")
        kit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(kit)
        needed = [
            *tracked(f"{RUNTIME}/src/*", f"{RUNTIME}/include/*", "runtime/*", "cmake/*"),
            "CMakeLists.txt",
            "CMakePresets.json",
            f"{RUNTIME}/CMakeLists.txt",
        ]
        dropped = [p for p in needed if p.startswith(kit.EXCLUDED_PREFIXES) or p in kit.EXCLUDED_FILES]
        self.assertEqual(dropped, [], "the builder kit would leave out files the game build needs")

    def test_ci_runs_the_check_and_the_release_waits_for_it(self):
        ci = read(".github/workflows/build-tools.yml")
        self.assertIn("--preset runtime-check", ci)
        self.assertRegex(ci, r"(?s)runtime-source-check:.*?windows-2022")
        release = read(".github/workflows/release.yml")
        self.assertIn("--preset runtime-check", release)
        # The check must come before anything is published, and it must run on the packaged kit, not the checkout.
        self.assertLess(release.index("--preset runtime-check"), release.index("gh release create"))
        self.assertIn("DrakanPC-Builder", release[: release.index("--preset runtime-check")])

    def test_workflows_are_valid_yaml(self):
        # An unquoted "key: value" inside a step name made GitHub reject build-tools.yml and run no CI at all.
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML is not installed")
        for path in tracked(".github/workflows/*.yml"):
            with self.subTest(path=path):
                self.assertIsInstance(yaml.safe_load(read(path)), dict)


if __name__ == "__main__":
    unittest.main()
