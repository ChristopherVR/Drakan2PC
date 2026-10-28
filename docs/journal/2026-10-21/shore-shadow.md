# Water-edge shadow continuity — 2026-10-21

Partially covered water previously blended its shaded water result against the
original unshadowed scene colour. As coverage approached zero, it became bright
again, then abruptly returned to the shadowed receiver at zero coverage. This
could produce a bright seam along shadowed banks and translucent water edges.

The final water blend now starts from the already-shadowed output. The original
alpha is preserved. This does not alter native terrain topology or collision.

A hardware regression compares zero and 0.001 water opacity beneath a blocker.
The old shader fails the colour-continuity assertion; the corrected shader passes.
All six focused renderer tests pass, including native shadows, moving-model
guides, scene geometry, neural guides and temporal presentation.

The candidate replaces only the water-renderer object in the installed
renderer library, retaining the deployed runtime and model processing.
Evidence, private test fixtures, build hashes and deployment backup are under
`build/shore-shadow-20261021`.

Expanded checks include a hillside reached by the existing `fixture-cave-water`
save and swimming in `fixture-coast`. Fixture names are not proof of visible
cave water: the hillside capture does not validate reflections in a cave.
Swimming produced 22545 independent model-motion pixels in the candidate and
28443 in the baseline; this is coverage evidence, not a quality ranking.

The first pond run failed the single-frame independent-motion assertion at
epoch 200 (zero pixels), although other logged frames contained object motion.
Its evidence is retained separately; the repeat passed with 24909 independent
model-motion pixels.
No renderer change to model motion is inferred from that one sample.

The accepted river, pond repeat, camp, hillside and swimming captures contain
35811, 24909, 31493, 26601 and 22545 independent model-motion pixels respectively.
All have finite SR/NR RGB, exact presentation and unchanged save hashes.
The hillside fixture lacks native lights for epochs 1–34, then has native shadows
for 315 frames. The other routes have native shadows throughout the logged
world frames. This startup lighting gap remains recorded, not counted as
continuous shadow coverage.

Follow-up: the startup gap was traced to an invalid fixture spawn, not missing
sun data. See [lighting-fixture.md](lighting-fixture.md).
The corrected hillside fixture has native shadows from epoch 1.

Deployed executable SHA-256:
`711e2052c5a902eb3a26bb68efde2bce8cfc153851609147d8766eee26d8e136`.
The previous executable, settings and build receipt are backed up in
`build/shore-shadow-20261021/deployment-backup`.
The installed smoke test passed: finite neural output, exact presentation,
active native shadows and unchanged saves. Settings and launchers were preserved.

The final fixed-view 1080p timing pair measured 19.02 FPS baseline and 18.58 FPS
candidate, with Quality DLSS, neural rendering, reflections and shadows enabled.
Both overlapped compiler work and are marked noncomparable in `performance.json`.
The earlier ABBA retry was also contended. Neither establishes a speedup or
isolates the cost of this shader correction.

Remaining limits: coarse native terrain silhouettes, original model polygon
counts, heuristic background classification, low-frequency retained sky, and
coverage beyond the tested fixtures. Neural reconstruction is not a replacement
mesh. A clean performance verdict also requires runs without competing work.
