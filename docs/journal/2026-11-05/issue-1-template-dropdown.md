# Issue 1: Order of the Flame template dropdown

The import dialog uses `showModal()`, which places it in the browser's top
layer. The shared dropdown upgrades its select to a custom listbox and normally
appends that popup to `document.body`. This leaves the popup below the modal and
in the inert document outside it; increasing its z-index cannot fix that boundary.

Marked the import dialog with the existing `data-dd-host` attribute. The shared
dropdown now appends its fixed-position popup inside this modal, retaining its
existing positioning, filtering, keyboard controls, and removal observer. The
conversion pipeline is unchanged by this fix.

Validation used the Vite app in Chromium with the real import command and a
synthetic host exposing 16 template names. No disc or Order of the Flame data was
read. Confirmed the popup is a dialog descendant, receives focus, and passes an
`elementFromPoint` hit test on an option outside the dialog's body. Filtering and
clicking selected template 12; ArrowDown/Enter selected template 13; Escape closed
the popup; closing the dialog with an open popup left zero popup elements.
Screenshot: ignored `build/issue-1-template-dropdown.png`.

`npm run typecheck -w @drakan/editor-app`, Prettier check of `ootfimport.ts`, and
the two `ootfdisc.test.ts` tests passed. Native Tauri/WebView2 and actual game-data
conversion were not exercised in this check.
