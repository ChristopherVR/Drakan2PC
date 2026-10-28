# Menu artwork, loading layout and save previews — 19 October 2026

The title artwork uses the centered native 4:3 canvas, preserving Rynn and
Arokh's proportions on wider displays. Side margins are cleared once per
render-target frame. Menu mouse geometry continues to use that same canvas.
The six title textures are restored together as one continuous image before
being split into their original texture IDs and bottom-up storage orientation.
The local replacement pack retains the original constant-alpha convention.
Artwork is private, derived from the owner's disc, and is not included in the
source patch. Generation prompt and original/restored masters are recorded in
`build/menu-fixes-20261019/background-generation.json`.
Their exact full-color hashes also bypass the recycled-slot volatility guard;
unknown full-color effects retain their original guard. The title handler and
first identified title draw close the startup world phase before GUI mapping.

Loading draws measure and render the decorative native menu face, including
its sharper runtime atlas. A signed offset travels with the ordered loading
render boundary. The label, bar border and fill share that offset and the same
centered canvas, independent of gameplay HUD sizing. Native loading progress
and translated strings remain in the guest. The offset also applies in 4:3.

New successful saves receive a 1024×1024 PNG companion containing an actual
high-resolution gameplay capture. The renderer preserves its last completed
gameplay image before the first menu/loading latch overwrites it; it performs
one GPU copy per transition and reads it back only when a save closes after
successful writes. The frame is fitted to the retail thumbnail aperture without
stretching the scene. The original `.rsg` and its 128×128 thumbnail remain
compatible with the retail format.

Companions are named `<save>.rsg.<content-fingerprint>.preview.png` in the same
slot folder. They are selected only when the current complete save matches
that fingerprint. Replacing a save replaces its generated companion. Read-only
opens and failed writes do not produce previews. Optional image failures never
change the native save result. Missing/corrupt companions fall back to the
original thumbnail, and their decoding uses the existing texture worker pool.
Registrations include the exact native pixel contents and row orientation;
late registrations invalidate an already cached legacy thumbnail once.

Private build, capture, test and installation evidence lives in
`build/menu-fixes-20261019/`. Existing user configuration and memory cards are
preserved; save/write validation uses copied cards.

Validation: 198 full WARP renderer checks, 82 loading/atlas checks at scale 3
(4:3, 16:9 and 21:9; three HUD sizes), 11 companion-preview checks at scale 3,
and 45 existing texture/font checks at scale 3 passed. Native captures verify
the restored title and centered decorative loading label. A successful new save
with DLSS Quality and neural rendering enabled produces its 1024×1024 companion;
a restart verifies two companions in the save browser and loading a saved game.
The release is linked from the installed game's core with the changed Release
menu/save objects and renderer, preserving the existing water, graphics and
widescreen fixes. Installation backup and executable hashes are in
`build/menu-fixes-20261019/deployment.json`.
