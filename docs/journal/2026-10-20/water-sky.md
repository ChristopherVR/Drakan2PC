# Shallow shoreline reflections and directional sky retention

The shoreline shader previously blended back to the original blue water through
a minimum 96-world-unit thickness band. Confirmed shallow water therefore lost
its reflected contribution over a visible band. Coverage now fades only through
a projected-pixel thickness, with a one-unit numerical minimum. Authored water
opacity, foreground occlusion and depth-dependent absorption remain in use.

The sky fallback now retains six world-oriented colour lobes plus the global
weather average. Visible sky probes update the lobes; unseen directions retain
their observations. Reflections blend the lobes continuously by ray direction,
and all lobes reset on level changes. A camera turn no longer turns the retained
sky into a single screen-dependent tint. This is a low-frequency approximation,
not a full sky cubemap or reconstruction of unseen clouds.

## Regression evidence

The hardware test verifies that shallow water retains at least 80% of the deep
reference's reflection contribution beyond the subpixel intersection. It also
learns differently coloured sky directions, hides the sky, verifies their
separate reflection colours, turns the camera, and verifies world anchoring.
Level replacement clears every lobe. The previous renderer fails the added
regressions. Existing depth filtering, offscreen geometry, alpha, shadow,
model-motion and neural-guide checks remain required.

## Build and gameplay evidence

`build/water-sky-20261020` contains build receipts, focused test logs, screenshots,
raw neural/motion/presentation validation, timing samples and deployment backup.
The final candidate replaces only the water renderer object in the previous
validated renderer library. Menu, neural motion texture optimisation and runtime
inputs are preserved. Concurrent experimental CPU/VU work is excluded.

The sky-only exploratory build is separate from the final combined shoreline
and sky candidate. Only the `accepted-*` runs validate the final candidate.
Performance uses separate scripted runs without validation readbacks.

Final candidate SHA-256:
`2697efefec9790f909896fc5dfee74b0383c2bd153a35524697738af6944e50e`.
All six focused renderer tests pass. River, pond and camp movement runs have
34895, 25200 and 31488 independent model-motion pixels respectively. All three
retain native lighting/shadows for 349 captured world frames, with finite SR/NR
RGB and exact 1920x1080 presentation verification. Save hashes remain unchanged.
The clear river capture shows the blue shoreline band reduced; the underlying
sharp bank outline remains. Diagnostic capture FPS overlays are not benchmarks.

## Installation and performance limits

The final candidate is installed at `dist/DrakanPC/drakan.exe`; the installed
smoke passes finite neural output, native shadows, exact presentation and save
integrity checks. The prior executable, settings and receipt are backed up in
`build/water-sky-20261020/deployment-backup`.

The candidate's moving sample without detected game/compiler overlap measured
17.61 FPS; the baseline repeat measured 19.96 FPS. These timed-input routes can
reach different poses at different frame rates, so this is a possible regression,
not an isolated measurement of shader cost. The candidate repeat was contended.
A subsequent fixed-view pair measured 19.62 and 18.85 FPS under overlapping work;
both are excluded from clean comparison. There is no speedup claim and no
conclusive matched-view performance verdict. The receipt preserves all these
limitations. Further timing needs a quiet machine with the same view.

## Remaining geometry limitations

The original water-disabled river capture also contains the sharp shoreline
outline and coarse terrain facets. Shader coverage can remove the blue
transition band, but does not reshape that native geometry. Terrain silhouette
reconstruction, original model polygon counts, heuristic background ownership
and coverage beyond the tested Surdana routes remain open. Neural reconstruction
does not replace the original meshes. No whole-game or 60 FPS claim is made.
