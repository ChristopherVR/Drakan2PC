# Issue 1 review against main

Reviewed the [original report](https://github.com/ChristopherVR/Drakan2PC/issues/1), the maintainer acknowledgment and the [reporter's follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6637936574) on 5 November 2026. Local and remote main both pointed to `8ae96142`. The latest published release was v0.1.2, the version the reporter used. The thread contains no reporter retest. Local renderer scheduling edits are outside the reviewed revision.

| Report | Evidence on main | Remaining check |
| --- | --- | --- |
| High-pitched audio | SPU2 idle sample correction removed a measured 11 kHz tone from internal PCM. Post-fix system loopback agreed. | Reporter AE-7 hardware. |
| Duplicate shadows | Native-projected characters are excluded from direct ray casting. DX11 and WebGPU tests and gate captures support single-shadow ownership. | Exact cliff view. |
| Camera-following circle | The exporter removed sky billboards that incorrectly cast shadows. | Confirm the circle disappears at the first Surdana cliff with the reporter's camera angle. |
| German text and voice | PAL startup selects German archives. Native and modern menus show German, and the intro opens German resources. | Listen to an identifiable German spoken line. |
| German workaround stutter | The original disc works directly. Four stationary EN/DE runs showed no presentation gap over 50 ms in their final 20 seconds. | The file-swapped image and reporter's gameplay scene were unavailable. |
| D1 export and campaign selection | All 23 levels converted from English D1 data. With the target set to German, the editor-built two-level pack offered the campaign choice and reached playable Ruined Village. | The Intro cinematic renders black with HUD. The German D1 source and packaged editor still need an end-to-end run. |
| Template Level dropdown | Modal popup layering fixed. Mouse and keyboard selection passed in Chromium. | Native portable WebView2 shell. |
| WASD melee and A/D spells | All four WASD attacks matched the arrow-key clips with movement restored afterward. Both inputs completed the same Fireball gesture and consumed mana. | Reported input cases exercised. |
| City pickup freeze | Native pickup registration, inventory cleanup and claim handling corrected. Both peers collected a near-spawn Blade and kept moving. | Walk to the stock City pickups at their authored coordinates. |
| Surdana Meadow cutscene | Campaign actors filtered before arena initialization. Menu-hosted match reached free movement. | Reported start path exercised. |
| Inquisitor W/S | With complete controller settings, host and client moved in opposite directions on W/S with zero unwanted yaw. Teleport velocity reset also prevented client landing death on spawn. | Reporter-specific settings if turning recurs. |
| SP to MP freeze | New Game to title to locally hosted Surdana repeatedly reached gameplay and movement. | Loaded-save and remote-join variants. |
| Host leaves | Two-instance manual host quit returned client to title with a connection-loss message. Client could host again. | Abrupt process loss over a real connection. |
| Remote lag or warping | Paired motion remained stable under the tested delay, jitter and 4.91% packet loss. A baseline position jump spanned a startup reload. | A real-network trace if the symptom recurs. |
| Low-poly remote avatars | Near Rynn used all 1,628 vertices on both peers and the distant mesh used 208. Automatic texture mip bias persistence was fixed separately. | Match the reporter's avatar, view and appearance settings. |
| Build CPU affinity | Both Windows builders accept a hex mask, with inheritance and restoration tested. Installer field wired. | Published installer end-to-end. |

During this review, 16 selected Release CTests passed in the existing runtime test build. The live results above come from the recorded [original fixes](../2026-11-04/issue-1-integration.md), [shadow ownership](../2026-11-04/issue-1-shadow-fix.md), [modern menus](../2026-11-04/modern-menu-localization.md), [first follow-up](issue-1-followup-integration.md) and [second gameplay validation](issue-1-followup-validation.md). A packaged post-v0.1.2 retest would give the reporter a common baseline.

Later on 5 November the [cliff, Intro and stock City pickup checks](issue-1-cliff-intro-city.md) updated three of these verdicts. The [ready-to-paste comment](issue-1-comment-draft.md) reflects the later runs.

The [multiplayer and language follow-up](issue-1-multiplayer-language.md) then verified German narration, exercised a saved-game client joining an ongoing lobby, and identified specific fixes for controller startup, live thumbnail teleports and nearby mesh detail.
