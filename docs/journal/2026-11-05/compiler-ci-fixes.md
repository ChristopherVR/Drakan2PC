# Compiler workflow fixes

Run: https://github.com/ChristopherVR/Drakan2PC/actions/runs/41924355390

The Windows and Linux compiler jobs failed before building the translator. Both
`HarnessAssetLinksTests` cases created temporary directories beneath `build/`,
which does not exist in a fresh checkout. The fixture now creates that parent in
`setUpClass`, retaining the harness requirement that private play folders stay
beneath `build/`. The tests also work when invoked independently of the full suite.

The lint job reported two comment-spacing violations in `drakan_overrides.cpp`.
Applying clang-format 19 corrects those lines without changing runtime behavior.
The run's Windows, Linux and Wasm runtime jobs already passed.

Local validation:

- `python scripts/run_tests.py`: 256 tests passed.
- Both asset-link tests passed with the test and harness roots redirected to a
  new temporary directory with no `build/` child. Fixture cleanup left the new
  build directory empty.
- Ruff 0.17.1 lint and formatting passed for all 223 tracked Python files.
- clang-format 19.1.7 passed for all 878 tracked C/C++ files selected by the
  workflow, using LF-normalized input to match the Linux checkout. A direct
  Windows working-tree scan encounters pre-existing CRLF endings in some files.
- Repository guard: 2,329 approved tracked files, no prohibited files.

No game rebuild is needed for test setup and comment spacing. These results are
local validation; the GitHub workflow has not been rerun with the fixes.
