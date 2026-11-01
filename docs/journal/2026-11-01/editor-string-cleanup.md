# Editor string cleanup

Rules applied to every user-visible string in `editor/packages/app` and the desktop menu. Follow them for new UI.

- No repo references: no `.md` files, `docs/` or `journal` paths, source files, function names, harness names.
- No guest addresses, `DRAKAN_*` env vars, `mods.ini`, `mod.toml` or file-layout trivia. Say "turn it on in the game's Mods settings".
- No shell snippets or "copy this command" boxes. Desktop gets a real button (Play, Write to mods folder); the browser gets one short instruction.
- No "proven in game", "inferred", "unverified", confidence badges or status chatter. If a feature is missing, say so in one short line.
- Names first. Hex ids, class ids, ODB/SSD/SDB/TXD terms and raw offsets go in a collapsed "Advanced" or "Raw data" block. Internal issue codes are not shown (kept hidden on validation rows).
- Search placeholders say what is searched ("Search classes", "Search records"), never "hex", "id" or "entry number".
- Empty states and toasts: one short sentence. Tooltips: a few words.
- Help text lives in `editor-core/src/guides.ts`; regenerate `docs/reference/editor-guide.md` with `UPDATE_GUIDE=1 npx vitest run packages/editor-core/test/guides.test.ts`. The in-app help does not link to repo docs.
- Out of scope: strings inside `packages/formats` (for example record type names and the "Container keeps N gap bytes" save issue, which the Repair panel now hides because it is info severity only).
