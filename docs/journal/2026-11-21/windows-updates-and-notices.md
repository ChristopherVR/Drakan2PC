# Windows updates and release notices

Drakan PC can check for updates using the existing GitHub releases. The first version should tell players when a
game update is available and open its release page. A later **Download and rebuild** action can reuse the Windows
builder after the player approves the update.

## What an update replaces

The release ships `DrakanPC-Setup.exe` and `DrakanPC-Builder.zip`. Both contain the builder. Each player supplies
their own disc image and compiles `drakan.exe` locally. The builder remembers the disc path, play folder, and build
options. A release update can reuse those values and the existing incremental build. The disc stays on the player's
PC. [Build and release process](../../../BUILDING.md#releases) · [Windows installation guide](../../guides/install.md)

The kit records its source commit in `KIT-VERSION.txt`. Setup uses `1.0.<commit count>` as its installer version.
The update check needs the public release tag for the running game. Have the release workflow pass its tag to
the packager and carry that version through the local build. Advance the running game's version only after the
new build has been deployed successfully. This lets a newer builder coexist with an older playable build after an
interruption. Existing unversioned builds need a one-time manual upgrade to gain the check.
[Kit packager](../../../scripts/buildtool/package_builder_kit.py) · [Release workflow](../../../.github/workflows/release.yml)

## Tell players what changed

Check for a newer stable release in the background when a packaged game reaches the title screen. Cache the check
for a day. Show a dismissible notice once per offered release, with **View changes** and a persistent **Check for
updates** action. Keep startup and gameplay independent of GitHub. An unavailable service should leave the background
check quiet and show a retryable status for a manual check. Let players disable background checks. The modern title
screen is a starting point for PAL builds. A builder or Start menu command can cover installations that use other
menus, including the experimental USA build.
[Title menu bridge](../../../ps2recomp/ps2xRuntime/src/lib/drakan_menu_screens.inl) · [USA build support](../../guides/install.md#usa-disc-support-experimental)

GitHub's public latest-release API supplies the tag, asset names, and release URL. Compare version numbers
numerically and require the game asset for the player's installation type. git-cliff already groups changes by Game,
Editor, and Save Editor. The first notice can link to the tagged release. For a later in-game **What's new** panel,
publish a short Game summary generated from the same commits, with a link to full notes for players who skipped
releases. Use a Game-change marker from the release workflow to find the newest applicable release, even when a
newer tag contains only editor changes. Players who want alerts outside the game can choose
**Watch → Custom → Releases** on GitHub.
[GitHub Releases API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release) · [Changelog configuration](../../../cliff.toml) · [GitHub notification settings](https://docs.github.com/en/subscriptions-and-notifications/get-started/configuring-notifications#configuring-your-watch-settings-for-an-individual-repository)

## Install with the player's approval

Start with Windows Setup installations. Show the download size and tell the player that the update rebuilds locally.
Verify the version and payload against signed release metadata, and check the installer's signature against the
expected publisher before launching it. Today's published checksums check the download against GitHub's metadata.
The current release is unsigned, so publisher verification needs a signing step before enabling in-app installation.
[Current release](https://github.com/ChristopherVR/Drakan2PC/releases/tag/v0.1.32) · [Windows signature verification](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)

Run the update after the game exits and serialize it with other builds for that installation. Stage the new build and
keep the previous playable build available until deployment succeeds. The current deployment preserves the save,
settings, mod, and texture paths, then cleans and copies the other files in place. A failed copy can therefore leave
an incomplete play folder. Reuse the current preservation rules and give the updater a recoverable deployment step.
Exercise cancellation, a missing or moved ISO, insufficient disk space, and a failed deployment before release. A retry
should resume or restore a playable build and preserve the player's files.
[Saved build choices](../../../scripts/buildtool/Build-Drakan.ps1#L243) · [Deployment and preservation rules](../../../scripts/buildtool/Build-LocalGame.ps1#L556)

ZIP users can follow the release page for the matching kit. The desktop editors need their own version metadata if
they gain update checks. The editor workflow can replace its assets under an existing game tag, while the editors use
their own commit-count build version. Tauri has a signed updater for desktop editor builds.
[Editor release workflow](../../../.github/workflows/editor-release.yml) · [Tauri updater](https://v2.tauri.app/plugin/updater/)

## Verification

The live latest-release API returned `v0.1.32` with both game packages and Game notes. The Setup and ZIP digests in
the API matched the published `SHA256SUMS.txt`. A read-only check offered that release to an older `v0.1.9` build,
skipped a current, newer, or unversioned build, and ignored a prerelease or a missing package. These checks used
release metadata. An installed update and recovery still need an end-to-end test.
