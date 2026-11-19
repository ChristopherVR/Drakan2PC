# Order of the Flame renderer performance

## The Ruined Village bottleneck

The figures count newly completed world frames. Direct3D 11 used the installed 3840 × 2160 settings, auto internal scale 8 in gameplay, 16× anisotropy, HD textures, and DLSS off. Each comparison kept the pose and pack fixed. The test host had other work running, and benchmark runs alternated variants with one game at a time.

Ruined Village averaged 45.3 FPS in the diagnostic baseline. Nine GPU samples during the measured window all read 100% utilization. The game passed about 45 frames per second to Direct3D. The previous benchmark table used the guest's flip event count, about 90 per second, as its presentation column. The table now labels that count and shows measured presentation calls separately.

Keeping the same output size and lowering internal scale to 4 raised throughput to 81.8 FPS. Median GPU utilization fell to 62%, and time in the Direct3D present call fell from 10.45 to 0.11 ms per frame. Disabling anisotropic filtering at scale 8 raised throughput to 76.8 FPS. Each was a single diagnostic run. Changing window mode, foreground status, overlay visibility, or present handoff did not produce a comparable gain. These tests identify the GPU pixel workload and the cost of the texture sampling path.

The renderer's GPU pass timer can include drawing and waiting in the interval it labels `overlay`. Use utilization, actual present calls, and controlled changes to the render workload when diagnosing this scene.

## Separate the shader entry points

Most draws sample one UV projection. The former pixel shader carried the code and temporary registers for up to three projections and for the mountain snow loop on every draw. Runtime flags controlled the branches, so the compiler kept all of that code in its output. An early-alpha probe and a zero-weight mip probe added branches and slowed down the common path even with their switches off. Their game throughput was below the 45 FPS baseline.

Direct3D 11 now has a single-projection color shader that shares the original texture filtering, alpha coverage, blending, and base terrain relief. Draws with snow or side projection use the full shader. The material-detail and velocity passes keep their existing coverage and restore the appropriate color variant between passes.

Disassembling the cached hardware-blend pixel shaders showed 66 temporary registers and four indexed temporary arrays in the full shader. The single-projection shader has 35 temporary registers and no indexed temporary arrays. The static instruction slot count is the same.

The first selector used the single-projection shader for every eligible draw. Five interleaved Ruined Village pairs measured 44.76 FPS (44.36–45.05) with the full shader and 70.53 FPS (69.68–74.38) with that selector. Median frame duration fell from 20.53 to 14.04 ms. The 99th-percentile duration fell from 30.99 to 18.84 ms. Median time in the Direct3D present call fell from 10.85 to 0.16 ms.

The broad selector measured lower in the Islands and Surdana. The final selector uses the short shader only for terrain detail or cliff relief with one projection. It applies the same rule in every level.

With the final selector, five more Ruined Village pairs measured 44.4 FPS (44.1–45.1) against 70.6 FPS (67.0–75.8). Median frame duration fell from 20.56 to 14.15 ms in the aggregate. The 99th-percentile duration fell from 31.9 to 19.3 ms. The final selector improved throughput and median frame duration in all five pairs.

Two Islands flight pairs with the final selector measured 115.9 FPS (113.4–118.4) against 115.2 FPS (112.8–117.7). The median frame times were 8.34 and 8.31 ms. This pose showed no clear throughput change.

The full and single-projection synthetic runs each passed 395 checks. All four output PNGs had matching SHA-256 hashes. Seven additional checks covered the draw-state cache, terrain detail, alpha, motion, and three device-loss cases.

The local build was installed with the final selector enabled. It loaded the existing Ruined Village quicksave from a private card copy and drew the snowy Wartok bridge view while turning and moving. The 11 campaign saves, four INI files, and the Order of the Flame pack matched their pre-build hashes.

The logs and captures are under `build/ootf-engine-20261119/`.
