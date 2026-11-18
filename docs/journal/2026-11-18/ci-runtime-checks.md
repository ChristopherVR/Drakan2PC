# Runtime CI failures

[Build compiler tools](https://github.com/ChristopherVR/Drakan2PC/actions/runs/44245831588) failed its C++ format check
and its Windows and Linux runtime tests. The strip inventory test still expected the Short Wood Bow's quantity bar to
say `Arrows`. The fixture supplies a separate quiver, so the bow's `181 / 200` is durability. The test now checks
that the card shows `Durability` and suppresses the native `Arrows` stat line. The local test went from one failed
assertion to 376 passing checks.

Clang-format 19 flagged `override_test.cpp` and `FarLand.h` in CI. Running the same formatter on the pending checkout
also flagged `SaveRecord.cpp` and `save_upgrade_test.cpp`. All four fixes are whitespace-only.
