# Packaged updater verification

The tests use local release fixtures because the public release predates the updater. Windows Setup used isolated
installation registrations. Linux ran in Ubuntu 22.04 under WSL. Both rebuilt from the original disc image and used
a scratch copy of a real save. The downloaded fixtures went through the installed updater's checksum verification.

## Rebuild and recovery

| Platform | Successful rebuild | Failed rebuild |
| --- | --- | --- |
| Windows Setup | The builder promoted v0.1.91 to v0.1.92. Both user settings files and all ten save files matched byte for byte. Continue loaded the Surdana save. | An injected compile failure reached the builder's retry and recovery paths. The old executable, version, settings and saves matched their original fingerprints. Continue loaded Surdana from the old game. |
| Linux ZIP | The title button downloaded v0.1.901 from v0.1.900 and rebuilt it from the disc. The executable changed. Saves and user settings matched, with the generated ELF path updated to its new build location. | The title button downloaded v0.1.902 and reached an injected build failure. The v0.1.901 executable, version, settings and saves matched the pre-update fingerprints. Continue loaded the old game. |

The Windows title test exercised normal per-file SmartScreen consent for checksum-verified unsigned Setup fixtures.
One successful-build attempt hit a folder access denial at deployment and preserved the previous install. The next
attempt against that same install published successfully. The process responsible for the denial could not be identified.

The Windows logs and screenshots are in `build/windows-updater-e2e/windows-updater-e2e-foreground/`. The Linux title
and rebuild evidence is in `build/updater-end-to-end/linux/`.

The Linux title then updated v0.1.901 to v0.1.903 with only the distribution's older Node.js on PATH. The bootstrap
downloaded its pinned Node.js archive, built the stock character animations and published the replacement. The game
binary changed and the save and user setting fingerprints matched again. See `build/linux-e2e-pinned-node.log`.

## Builder startup

The packaged Windows and Linux builders checked the stable GitHub release at startup. Older builders reported the new
version and linked to its release notes. Current builders reported that they were up to date. Checks with an unavailable
network still reached the local build preflight. Source checkouts without a release marker skipped the check.

The Windows builder uses Windows' system certificate trust. Linux uses the same release lookup as its in-game updater.
The startup verification logs are in `build/builder-release-proof/`.

## Fixes found during verification

- The builder ZIP omits the developer-only differential tester. CMake now adds the test generator only when its source is present.
- On Ubuntu 22.04, the pinned TOML library could not compile with the default Clang and system C++ library. The Linux
  recompiler now configures with GCC while the game runtime retains Clang.
- Ubuntu 22.04's system Node.js could not build the stock multiplayer character animations. The Linux builder now
  reuses a compatible Node.js or downloads the checksum-pinned build into its tool cache.
- The Windows updater now waits for the installer handoff on its worker thread. The game window remains responsive
  while Windows handles the unsigned installer prompt.
- The title notice uses a wider panel with readable status text and a full-width **Download and rebuild** button.
  **View changes** and **Dismiss** sit below it. The revised panel was checked in the running game at 4:3 and 16:9.
