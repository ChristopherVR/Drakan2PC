# Hillside lighting gap: fixture correction

The previously reported 34-frame hillside lighting gap came from the private
`fixture-cave-water` save. Its player height was 158800, while native terrain at
the saved X/Z was 216102.203125. The camera started far below solid terrain.
The exporter correctly found no eligible terrain layer beneath that camera.

Guest RAM captures from the installed executable show the camera 55284 units
below layer 425 at the first sample and 33036 units below at sample 20. At sample
40 it was 1740 units above the layer. The native sun vector was present and
unchanged throughout: approximately (-0.674, 0.469, -0.571). This rules out missing
sun data as the cause in this fixture. Selecting an above-camera layer or
inventing fallback lighting would conceal the invalid spawn.

A separate private fixture sets the player height to 216202.203125 and moves
the saved cameras by the same offset. The original fixture and user saves are
preserved. Fixture provenance and measurements are under
`build/lighting-gap-20261021`.

The corrected walking/strafing/camera-turn run on installed SHA-256
`711e2052c5a902eb3a26bb68efde2bce8cfc153851609147d8766eee26d8e136`
passed:

- Native shadows active from epoch 1, across all 350 logged world frames.
- 25826 independent model-motion pixels at the captured frame.
- Finite SR/NR output and exact presentation.
- Source, private and original save hashes unchanged.

The screenshot shows a hillside, not a cave-water scene. It does not validate
cave reflections. Coarse original terrain facets remain visible. This correction
does not remesh terrain or change model detail.

No executable rebuild or replacement was needed for this fixture correction.
Compiler processes overlapped the entire run; its FPS overlay is not a benchmark.
Clean comparative performance remains unresolved.

Evidence: `early-lighting.json`, `spawn-ground.json`, `fixture-hillside/fixture.json`
and `corrected-validation.json`. The corrected run's appended RAM-dump labels
were processed late and are not used to establish early-frame camera positions;
the shadow-coverage conclusion uses the renderer's explicit epoch logs.
