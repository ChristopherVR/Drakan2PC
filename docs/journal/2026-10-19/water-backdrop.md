# Camera backdrop and river reflections

The sharp foreground reflection transition in the snowy Surdana river is present
in the water pass, before HUD composition. Coverage debug mode 3 shows continuous
water coverage across it. The earlier description as a water coverage boundary
was incorrect.

Tracing the captured scene identifies a generated model as the nearer hit in
place of the cliff. In this snapshot it is object `0xa5eca0`, model resource 3,
material 11: a sphere of radius 10240 centred exactly on the camera. These IDs
are diagnostic evidence, not production exclusion keys. The sphere cuts off
reflection rays around image row 600 and also participates in shadow rays.

`drakan_scene_models.h` now conservatively excludes camera-centred background
spheres from the physical model snapshot, shared by DXR and neural guides.
The check requires a generated resource namespace, no animation controller,
native flag `0x1000`, a centre within 0.125 native units of the camera, vertices
on a large spherical shell within 0.1% radial tolerance, and coverage of both
extremes on every axis. It does not change the native raster sky drawing.

This is a geometric fallback, not a proven native sky-owner classification.
An explicitly exported native background owner would be preferable. Unusual
generated spherical scenery satisfying all these conditions could be excluded;
authored resource namespaces, off-centre spheres, small objects, incomplete
shells and non-spherical models are retained. Other sky shapes remain unchanged.
Reflection misses still use the existing approximate sky-radiance fallback.

Validation and candidate captures are in `build/water-boundary-20261019`.
The focused six-test renderer suite passes. The real RAM snapshot test with
the ISO passes: 1266 physical instances, 115 meshes, 75662 triangles and 75537
textured triangles; the sky sphere is excluded and the posed character remains.
The RAM-only invocation without disc texture loading fails its pre-existing
80% textured-coverage assertion; with the ISO, texture coverage exceeds 99%.

Release verification completed on 2026-10-19. The first linked candidate reused
an older renderer library and still showed the cutoff. Rebuilding the renderer
and its embedded shaders, then linking the updated scene capture, removed the
hard reflection transition in the stationary snowy-river capture:
`build/water-boundary-20261019/release-water-water-on.png`.

The deployed executable SHA-256 is
`c3c10da209274e4fa63dada2356291001dd42d9088b9ebff836a61a03647ef98`.
This is a Release link using the validated generated game core, snapshotted
menu/runtime inputs, compiled scene capture and a rebuilt renderer. It is not
a completed full rebuild of all concurrent experimental runtime changes.
`candidate.json` records frozen link-input hashes; `source-drift-at-deploy.json`
records subsequent edits by another chat, which are not silently incorporated.

Matched water and shadow checks changed 173995 and 158274 world-region pixels.
The moving-model run produced 28981 pixels of independent model motion, finite
SR/NR buffers, and a byte-exact 1920x1080 swapchain check. All scripted runs exited
successfully and preserved source, user and private save files. An additional
moving snowy-river check passed numerical and presentation validation, but
native terrain/water polygon edges remain visible at that moved camera. These
are scene-limited checks, not whole-game visual or performance certification.

Deployment enables Quality DLSS, neural processing, modern water, ray reflections
and ray shadows. The previous executable, settings and build receipt are backed
up in `build/water-boundary-20261019/deployment-backup`. See `deployment.json`
for the installed smoke result. Temporary hit-normal/depth shader diagnostics
were removed before the release build.
