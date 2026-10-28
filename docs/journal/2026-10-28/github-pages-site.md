# GitHub Pages site and PC enhancement guide

## Review

The public site previously published only `runtime/web/pages`: a working local-file browser launcher with no project navigation, user guide or developer documentation. The root README introduced the project but did not explain the full set of PC changes.

Player-facing information was spread across the installer kit, runtime READMEs and technical reference documents. Several technical documents mix current behavior with historical proposals. New player guides therefore use the current input/settings implementation and build pipeline as well as those references, and link to technical notes for detail.

## Changes

- Added a 16-page static site: homepage, launcher, 404, ten guide/developer entry pages and three canonical reference/build pages.
- Added a PS2-to-PC comparison to the README and website, including frame-rate modes, optional graphics, direct input, modern bow aiming, dual wielding, Tab inventory, M map, I strip, PC menus, HD movies, narration subtitles and save conveniences.
- Documented conditions: hardware-dependent 60+ FPS, Windows versus portable feature coverage, separate locally generated HD packs and locally supplied narration SRTs. The bow option is named Modern bow aiming in the code; it does not add a crossbow weapon.
- Added searchable Markdown pages, source links, page contents, desktop sidebar, mobile navigation, scrollable tables and copyable command blocks.
- Reworked the visual design following user feedback: game-menu-style bronze frames, warm dark panels, serif text, centered title and red/gold selected rows, informed by `runtime/settings/src/Menu.cpp` and local title/options captures. No captured or extracted game artwork is included; the SVG mark and stone texture are original source.
- Updated the Pages workflow to build and validate documentation before deployment; pull requests and the development repository validate without publishing.

## Compatibility and publication boundary

The hosted launcher moves to `browser.html`, but its JavaScript, service worker, `play/` URL and database names remain unchanged. All four original launcher assets are copied byte-for-byte. Game builds, ISOs, packs, saves and journals are not site inputs. The builder uses an explicit page list and produces only `build/github-pages`.

The public site is not changed by this local implementation. Publishing requires the changes to reach the public repository's `main` or `public` branch, where the existing Pages deployment runs.

## Validation

- Site builds with Python-Markdown 3.11.2; every generated local link and fragment passes validation.
- Five builder regression tests pass, covering exact artifact contents, unchanged player assets, nested page links, search indexing, 404 paths at root and repository hosting, invalid links/fragments, payload rejection and output-path guards.
- Ruff check/format and JavaScript syntax checks pass for the new code; Git whitespace checks pass.
- Desktop and 390-pixel mobile browser review covers the homepage, comparison guide, controls guide and launcher. Revised homepage and comparison layouts have no page-level horizontal overflow; wide tables scroll inside their region.
- Search returns relevant subtitle and dual-wield results and a clear no-results state. Escape closes the dialog; the mobile guide menu toggles its visible state and `aria-expanded`. Launcher has all three file pickers and keeps Play disabled before a build and ISO are chosen.
- Existing tracked-file repository audit passes. No native game rebuild or ISO-backed gameplay test is needed for this documentation change; actual game playback was not rerun.

Maintenance instructions: [GitHub Pages documentation site](../../reference/github-pages.md).

## Follow-up: Linux, media and publication

- Added a dedicated Install on Linux guide based on the existing ISO bootstrap and portable runtime. The site now builds 17 pages.
- Renamed the comparison navigation and title to PC enhancements to avoid implying a chronological changelog.
- Added 13 screenshots and two compact GIFs to a separate documentation-media release. Source captures and conversions remain ignored under `build/`; Git and the Pages artifact contain only source and external URLs. The installer remains the latest release.
- Added captioned figures throughout the comparison and relevant guides, a homepage gallery, and README screenshots and collapsed animation sections. Website animations require an explicit Play action; screenshots can be enlarged.
- Six site regression tests pass, including initial still-image rendering, unknown-media rejection and Linux guide publication. Ruff, JavaScript syntax and whitespace checks pass. Desktop and 390-pixel browser checks confirm image loading, enlarge/Escape, animation toggling and no horizontal overflow on the comparison and Linux guide.
- User requested committing and pushing all changes. Publish through both established remotes while preserving the public repository's separate source-only history; verify the Pages deployment after pushing.
