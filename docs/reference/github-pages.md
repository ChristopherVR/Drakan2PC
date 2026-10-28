# GitHub Pages documentation site

## Public structure

GitHub Pages hosts a source-only project website: a landing page, PS2-to-PC feature comparison, player guides, developer documentation and the local-file browser launcher.

- `docs/guides/`: player-facing Markdown and developer entry points.
- `docs/site/pages.json`: explicit publication list, sidebar order and page descriptions.
- `docs/site/`: shared template, landing page, CSS, JavaScript and original vector mark.
- `scripts/buildtool/build_pages.py`: Markdown rendering, source-link conversion, search-index generation and local link/fragment validation.
- `runtime/web/pages/`: existing browser launcher and service worker sources.
- `build/github-pages/`: generated, ignored website output. This is the only directory uploaded by the Pages workflow.

The site uses one pinned build dependency, Python-Markdown. There is no frontend dependency installation, remote font, analytics script or game-data build. Existing `BUILDING.md` and the portability reference are rendered from their canonical files rather than copied into a second document.

The visual design follows the port's game menus: bronze frames, warm dark panels, cream serif text and a red/gold selected row. The palette is informed by `runtime/settings/src/Menu.cpp`. The mark and stone texture are original SVG source. Display text uses local Castellar with serif fallbacks; body text uses local Goudy Old Style with Palatino/Georgia fallbacks.

## Screenshots and animations

Documentation captures are hosted separately as attachments to the `site-media-20261028` GitHub release. No captures are committed or copied into the Pages artifact. `docs/site/media.json` records each screenshot's URL, dimensions, local source, SHA-256 and caption. The README uses the same public URLs. Keep source captures and conversion outputs under ignored `build/`.

Guides use ordinary Markdown image links; the builder expands recognized URLs into captioned figures. The homepage uses `<!-- media:ID -->` markers. Images open in an accessible dialog, with ordinary image links as a no-JavaScript fallback. GIFs load only after Play animation is selected, and can be stopped. README animations are placed in collapsed details blocks.

Captions distinguish Windows, Linux and browser captures and identify sampled development frames. Debug counters and sampled GIFs are not frame-rate benchmarks. The short movie excerpt illustrates a locally generated pack; full movie packs, subtitle transcripts and playable game files are not distributed with the site. New media requires deliberate review and upload; the site build never uploads or generates it automatically.

## Build and preview

From the repository root, use Python 3.11 or newer:

```powershell
python -m pip install -r docs/site/requirements.txt
python scripts/buildtool/build_pages.py
python -m http.server 8765 --bind 127.0.0.1 --directory build/github-pages
```

Open `http://127.0.0.1:8765/`. This previews documentation and the launcher UI; actually playing still requires a private browser build and ISO. Shut down the server with Ctrl+C when finished.

The default published base path is `/Drakan2PC/`. Normal pages use relative links and can be served from a repository subpath or a local root. Set `--base-path /` when testing the 404 page at a local root, or supply another repository path when deploying elsewhere. The custom 404 uses absolute base-path links so it also works for deeply nested unknown URLs.

## Add or update a page

1. Edit the canonical Markdown file. New player guides go under `docs/guides/`.
2. Add new published pages to `docs/site/pages.json`, with a unique URL, title, section and description.
3. Use ordinary Markdown source-relative links. Published sources become site links; other existing repository files become GitHub links.
4. Build the site. Missing source links, broken generated links/fragments, duplicate IDs and unexpected artifact suffixes fail the build.
5. Preview desktop and narrow layouts, and check search and navigation after changing the site UI.

The builder only publishes listed Markdown, explicit template/assets and four allowlisted launcher files. It does not recursively copy the repository, `dist`, a game build, journals or private assets. Review additions to the allowlist accordingly. Site output must be a dedicated folder under `build/`.

## Browser-player compatibility

The launcher UI lives at **`browser.html`**, while `loader.js`, `handles.js` and `sw.js` retain their original root-relative locations. The virtual game path stays **`play/`**, the service-worker scope stays the site root and the IndexedDB names stay unchanged. Existing root bookmarks open the new homepage, whose navigation links to Browser player.

Do not move `play/`, change database names or instruct players to clear site storage as part of a site redesign. The worker intercepts only `play/`, leaving documentation requests to the network. It continues to read the player's chosen files locally. The legacy standalone launcher source in `runtime/web/pages/index.html` is also retained.

## Deployment

`.github/workflows/pages.yml` builds and validates the site on relevant pushes and pull requests. The deploy job runs only for pushes or manual dispatches in the public **ChristopherVR/Drakan2PC** repository, preventing a development-repository push or pull request from publishing accidentally. Use GitHub Pages' **GitHub Actions** publishing source in repository settings.

The workflow retains pinned checkout and Pages actions and uploads `build/github-pages`. A documentation change must reach the public repository's `main` or `public` branch for automatic deployment. Local edits and previews do not publish anything.

The build and deploy jobs are separate so a failed renderer or link check blocks publication. Consult the Pages workflow run and deployment URL for evidence of publication.

## Checks

```powershell
python -m unittest discover -s scripts/tests -p test_build_pages.py
python scripts/buildtool/build_pages.py
python scripts/repository_guard.py
```

Site tests skip when the optional Markdown dependency is absent from a general developer environment. The Pages workflow installs it and runs the tests before building. The tests protect repository-subpath URLs, 404 links, local-link failure behavior and preservation of browser-player assets.
