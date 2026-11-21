# Builder package contents

DrakanPC-Builder.zip and DrakanPC-Setup.exe use one allowlist for the player build. It covers the native and browser
builds, optional extras, runtime assets, and licence notices. Both packages omit repository metadata, tests, editor
applications, draft models, and release tooling.

The packager checks the pinned FFmpeg archive before extracting the five sets of headers and import libraries used by
the game. The DLL list includes their transitive imports, traced with `dumpbin /dependents`. Packaged builds read
`deps/ffmpeg` directly. Local builds can still pass the upstream archive to `-DependencyDirectory`.

Setup ships the Node.js runtime and its licence without npm or Corepack. The portable Python and PowerShell tools
retain their licences. Setup reuses the game's icon and embeds the welcome text without installing a second copy.

Local packages built against the same source revision and pinned tools measured as follows.

| Package | Before | After |
| --- | ---: | ---: |
| DrakanPC-Builder.zip | 167.89 MiB, 2,893 files | 143.38 MiB, 1,638 files |
| DrakanPC-Setup.exe | 269.10 MiB | 238.31 MiB |

The runtime compiled from the extracted zip with the bundled raylib and FFmpeg files. Windows loaded all five FFmpeg
libraries from the zip, and the original archive passed the CMake extraction check. The browser bootstrap started
under WSL. An isolated Setup install passed the guided builder's preflight with its bundled tools and detected the PAL
disc. The Order of the Flame converter ran from the extracted zip with the bundled Python and Node.js and produced
the 23-level pack. The Python suite ran 596 tests with one skip and no failures.
