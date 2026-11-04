# OotF native rotation encoding

Ordinary Ruined Village navigation exposed a static building alignment discrepancy after its missing draw lists were restored. Source Tavern object 370 and Tavern Entrance object 387 both have source Y rotation 270 degrees, normalized by the converter to -90. Their emitted LVL positions and rotations matched the parsed source, with no scale. Resource-index identity resolves them uniquely at position (217088, 18750, 184320), resources 0x344b8 and 0x344bd. Full runtime bases are identical from fresh RV15 before control through route 01, 04 and 07 snapshots: orthonormal, Y unchanged, measured yaw -163.99747 degrees. There is no rotation drift or physics evidence.

Native level-object loader 0x1c9f40 reads each rotation with the two-byte reader 0x12cfe8 into an unsigned halfword, converts that value to radians, applies Y minus 90 degrees, then X and Z. A serialized -90 is therefore read as 65446 degrees. Because 65536 modulo 360 is 16, normalizing source 270 to signed -90 changes the native yaw from 180 to 196 degrees (equivalent to -164). This predicts the measured matrix discrepancy to float precision. The defect is rotation encoding, not rounding or a proposed collision workaround. Private native extracts and RAM comparisons remain under build; no original game blobs are committed.

The narrow formats fix preserves the source's unsigned degree words when parsing PC objects. It retains the exported signedDegrees utility for API compatibility, with its misleading native-storage comment corrected. The native editor parser still preserves legacy signed bit patterns for roundtrip. A regression sends 181, 270 and 359 degrees through real PC parsing, level conversion and PS2 serialization on each of the three axes, checking the emitted unsigned words independently of the editor's parser. Focused 13 tests, all 348 formats tests and TypeScript passed. Only src/pc.ts, a src/lvl.ts interface comment and test/pc.test.ts are changed in formats.

An audit of all 23 source levels covers 16,312 objects and 48,936 rotation components. Every component is between 0 and 359, so preserving the source words stays within the writer's supported range. No normalization of unfamiliar high-bit values is needed for these levels. The private result is `source-angle-audit.json`.

A new controlled private conversion is running under build/scratch/ootf-rotation-fixed, from the same preserved baseline plus only this parser change. The 23AE draw-list control remains immutable. Class/model identities and every non-rotation payload still need an independent pack comparison, followed by native matrix verification and a fresh normal New Game. Earlier earned saves serialize the old transforms and cannot establish the fix; no compatibility or completed-campaign claim is made from those saves.


## Controlled pack audit

Candidate `7DBB6D420C6A15EF64915D9C63573AF5F7B9484961BF26FA60993545B3F72AA9` completed with 10,105 entries. Compared with immutable draw-list control `23AE`, only 21 LVL files change; no paths are added or removed. Every class, model, texture, sequence and other non-LVL payload is byte-identical. Inside each changed LVL, only the object-list entry differs. Re-serializing the old objects with each negative angle replaced by its equivalent source value in 0..359 exactly reproduces the new payload: 2,374 objects and 2,541 rotation components change, with no other object fields changed. Private audit: `build/scratch/ootf-rotation-fixed/pack-audit.json`.

A fresh normal New Game is now running against this pack on the previously validated stable executable. Its script waits for actual gameplay for up to 600 seconds within a 900-second overall budget before saving, rather than relying on the earlier short cinematic timeout. Fresh matrix, equipment and save evidence is still pending.


## Fresh normal native validation

`normal-rotation-fixed/` completed the full original opening from normal title/New Game selection on the stable `6D747D` executable, with a fresh empty card and only the audited `7DBB` pack enabled. No scene skips or position, health or item overrides were used. The harness ended through scripted quit with actual process exit 0 after 316 wall seconds.

Fresh RV15 RAM identifies Tavern and Tavern Entrance at the same exact source position, now with basis `[-1,0,-0.0000004768; 0,1,0; 0.0000004768,0,-1]` and yaw -179.9999727 degrees. The old fresh control measured -163.9974697 degrees. The corrected complete bases survive native save/reload. Evidence: `basis-audit.json` and `basis-reload-audit.json` under `build/scratch/ootf-rotation-fixed/`.

Normal control returned and the first native F5 completed at 295.688 seconds. Enter dismissed the first-use equipment tutorial; native E equipped Atimar. A second F5 and native F8 returned to gameplay at 307.666 seconds. Native cell 0 retains resource 0x344db, class 0x1000e and equipped flag 1 after the carried actor is recreated; health remains 100 and saved/reloaded player coordinates match exactly. The fresh earned card is `normal-rotation-fixed/card/mc0_ootf`. Earlier cards remain controls only. Reports and screenshots are in that run folder; no fresh enemy-damage acceptance is inferred from the ready-pose attack samples.

All 11 RAM samples have valid heap walks to the top and no unintended Dust Devil actors or generators. Live occupancy ranges from 13.18 MB at RV15 to 17.85 MB around equipment, then 16.66 MB after reload. This bounds the observed opening and save/reload run, not long-term campaign stability. Ordinary quest traversal now resumes from this fresh correctly transformed checkpoint.
