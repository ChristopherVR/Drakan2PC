# Equipped Bow replacement: ownership and readiness

Replacing the equipped Short Bow with a ground-template Long Bow preserved the
inventory equipped marker but lost the serialized owner reference. Native firing
then had owner handle 0, so the port's local-owner aiming guard correctly declined
to adjust it. This is a Save Editor regression, separate from the bow controls
committed in 63fcf1a.

## Proven ownership repair

Private fixture generation: `build/ootf/make-longbow-fixture.ts`, source card
`build/ootf/bow-openfield/mc0`, real Long Bow template record 691. The replacement
card and failed normal-load run remain at `build/ootf/bow-longbow/mc0` and
`build/harness/bow-long53`. Native inventory unequip/re-equip in
`bow-long-equip54` restored ownership and firing; its native save is Slot33 under
that harness card. No original fixtures were overwritten.

Native Bow DATA+0x60 is a runtime 16-bit owner handle. The serializer writes a
separate owner record slot after the raw custom payload: item self-handle+0x154.
The loader 34d960 uses that reference to reconstruct the owner, replacing the raw
runtime word. Preserving the raw word alone did not repair ownership
(`bow-owner57`). Preserving the appended reference to the current Rynn record
did (`bow-owner-reference58`): owner 4 resolved to the actual context player in
every snapshot. That run used ordinary native load/sight/fire, no inventory E,
and cleanly quit.

The editor repair applies only when both bodies have the verified PAL Bow layout
(raw class 0x4f, base-item and Bow behaviour tables), the inventory cell is equipped,
and the donor owner slot equals this save's Rynn record ID minus 1. It copies only
the appended owner reference across each record's own prefix. Template counts,
ammunition reference, nocked-arrow reference and state remain untouched. The
identity helper shared with Add does not inherit this owner preservation.

The updated real `replaceItem` path generated
`build/ootf/bow-longbow-generated-owner/mc0`. All 868 decompressed records are
byte-for-byte equal to the live-proven owner-reference58 candidate, as checked
by `build/ootf/compare-generated-owner.ts`. Thus this is connected to native
evidence without another identical game run.

Validation: 32 focused save-item tests pass; editor-core typecheck and formatting
pass. Tests cover shifted Bow prefixes, wrong owner/class/vtable, truncated
targets, distinct donor/template counts and ammunition, untouched nock/state,
unequipped cells, and the actual Add path with an equipped Bow identity donor.

## Readiness is a separate issue

Ownership alone leaves the ground template's nock empty. In owner-reference58,
the first press kept ammo 300 and created a Long Arrow; the second fired to 299.
The ownership change therefore does not claim full first-shot readiness.

Native Bow state 154 bit 2 initializes the load animation (20e338â†’210920); it is
not a loaded-arrow enum. The actual missing-arrow creator is Rynn 20f9c8, which
uses the replacement Bow's own ammunition resource. Native 20ff00 calls it during
reload; native 31ca20 sets the pending-reload flag at Rynn AB0 bit 0x200000 and
305460 clears that flag afterward.

Private probe `bow-reload59` adds only that source-backed pending-reload flag to
the owner-reference candidate. On sight-closed load the native engine created
Long Arrow 337af before the first 100 ms snapshot, cleared the flag back to the
original 0x100040 and left physics flags94 at 0. Both presses fired 300â†’299â†’298,
without E or copying an incompatible Short Arrow. This probe is not an editor
implementation: sight-open saves use a separate native branch and remain under
investigation. No readiness flags are changed by the ownership repair.
