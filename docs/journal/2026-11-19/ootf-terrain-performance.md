# Order of the Flame terrain performance

The installed Order of the Flame pack used the disc's texture limit per terrain chunk. The converter split texture-heavy regions into many small chunks. The PAL executable already supported up to 32 textures per chunk, but the converted pack needed to declare and use that layout.

The test kept the executable, camera placement, and graphics settings fixed between runs. It used the installed Direct3D 11 settings at 3840 × 2160 with HD textures and DLSS off. The benchmark counted world frames between two screenshots. It alternated pack order and ran one game at a time on a busy host.

At the rope bridge in Wartok Canyons, five pairs measured a median of 66.5 FPS with the disc limit and 84.7 FPS with the wider layout. All five throughput pairs favored the wider layout. The median of the individual frame times fell in four pairs. The median 99th-percentile frame time fell from 27.5 ms to 17.6 ms. Rare long frames still occurred. Renderer draw calls per world frame fell from roughly 1,130 to 434.

A first Surdana run measured 119.1 FPS. A Wartok survey measured 69.1 FPS. The reduced GS CPU work option and GPU vertex processing both measured lower in that single-run survey.

| Other comparison | Pairs | Disc limit FPS | Wide FPS | Disc limit median frame | Wide median frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Islands arrival | 2 | 99.5 | 98.2 | 10.08 ms | 10.20 ms |
| Islands in flight | 2 | 119.1 | 119.4 | 8.20 ms | 8.56 ms |
| Ruined Village | 2 | 45.3 | 44.8 | 20.49 ms | 22.29 ms |
| Surdana | 1 | 113.5 | 111.9 | 8.41 ms | 8.84 ms |

The Ruined Village median frame time increased in both pairs. Its wide draws also used more GPU world time. The one Surdana pair serves as a playability check. Earlier testing found comparable draw counts with both layouts on disc levels.

Two more Ruined Village pairs compared the normal GS readback poll with `PS2X_READBACK=eager`. Blocking immediately reduced CPU time on the emulated main processor from 19.48 to 12.47 ms per world frame. World throughput stayed around 46 FPS and median frame time increased from 21.05 to 21.90 ms. The local update uses the existing readback policy.

The two packs share save build `493b53de681b8791` and revision 1. Comparing their level resource tables found only terrain layer payload changes. Textures, classes, objects, audio, and the original map previews match the installed pack.

The wide pack loaded the byte-identical copy of the mounted Wartok save. Arokh completed the recorded walk and turns in the cave.

The local update uses the wider terrain in Wartok Canyons. The staged archive takes `OOTF03NS.LVL` and the compatible manifest from the wide conversion. Those are the only two members that differ from the original archive. `Build-LocalGame.ps1` installed the verified pack via `-ModDirectory`. The 11 original campaign saves, four settings files, and game executable matched their pre-update hashes. The installed game loaded the copied save and captured the river during the walk and after the turn.

The run logs and matched captures are under `build/ootf-perf-20261119/`.
