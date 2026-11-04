# Implementation follow-up (2026-11-04)

User correctly rejected the prior narrow pass as completion. Three GPT 6.1 Sol agents resumed substantive editor, multiplayer-motion, and bow implementation. Root owns deployment and OotF startup/harness integration.

Root findings:

- `dist/DrakanPC/mods` was absent. Canonical deployment cleanup preserved neither mods nor campaign-specific mc0_* / mc1_* cards. Existing private harness packs bypassed this broken installed experience. Preserve mods, saves and campaign cards; add explicit -ModDirectory installation with activation merge preserving unrelated settings. New generated character-motion pack will install through the same canonical builder.
- Fresh combined OotF conversion: 23 levels, 9217 files. Source build/ootf/cab_disc, output build/ootf-run/implementation/mods. No game assets tracked.
- Campaign harness accepted relative pack paths, resolved by the game against its private folder. This silently fell back to Drakan2. Chain verdict still passed D2 loads and one exit. Fixed absolute pack resolution and require OotF loading plus requested transition counts.
- Chain sequence [0, Ruined Village, Wartok,...] attempted a self-transition after initial Intro->Ruined Village. Fixed to [0, Wartok,...]. The intermediate implementation-chain-fixed failure (1/12, stall) used the old duplicate destination sequence, so it cannot establish a campaign engine regression.

Validation in progress. No full campaign completion or exact OotF combat fidelity claimed. Previous bow owner patch compared actor to actor data; bow agent identified and corrected this with a more faithful fixture.

Follow-up implementation:

- Generated exit detectors used SYSTEM Trigger Detector default message1; normal exits require5. Converter now explicitly sets task6/message5 for foot and mounted detectors. Regenerated combined pack audited:52 synthesized detectors across12 levels contain the correct fields. See agent journal for detailed tests.
- Default title_menu=native hid campaign selection even with packs installed. titleModern now exposes the campaign-capable menu when offerChoice() is true; ordinary pack-free native title stays unchanged.
- Canonical build now takes an exclusive per-WorkDir writer lock (released on exit). Multiple running games remain supported. This follows actual linker failures caused by an unrelated simultaneous build replacing generated objects.
- Isolated play folder dist/DrakanPC-implementation uses the existing build/final-game executable/tree; no separate compile tree. User's game remains running. Live visual tests identified duplicate root displacement in generated NPC side-jump motions; agent is correcting pack generation before acceptance.

Normal installed startup run `implementation-ootf-start-fixed`: campaign selector shown under native default after title fix; selected OotF via menu; New Game reached Intro control; natural44-second timer fired exit without DRAKAN_LEVELCHANGE/START_LEVEL. **Not accepted as playable startup:** after transition gui14->-1 reports gameplay0 and inspected village.png shows close/occluded camera against Rynn. OotF agent investigating actual arrival/camera state. Earlier private redirected loads did not reveal this.

Co-op HUD incorrectly named selected NPCs Rynn in direct-start sessions because UI used lobby Status.character while bodies used PlayerState.character. UI now uses latest pose character during Playing (self uses selected). Requires next live verification.

Validated implementation checkpoints:

- Primary dist/DrakanPC refreshed successfully through canonical builder, with original settings/cards preserved and ootf-levels plus drakan-character-motion active. No user game process was terminated. Same existing WorkDir reused throughout.
- New multiplayer full-body motions pass13 live acceptance checks across57/60-joint skeleton families: real distinct cartwheels and jump/air/landing, local/remote mapping agreement, save teardown/recreation. In-place root-motion fix visually validated. Armed split animation remains under investigation (NPC instances allocate only one channel); melee hits and actual pain playback are not yet accepted.
- Bow live checks prove LMB consumes ammo before release, release adds no shot, RMB release retains sight(FOV40) and second tap leaves sight(FOV80), strafe firing works. Enemy damage observed. Original data reveals standard arrow damage10 vs native5, still being implemented; no1:1 completion claim.
- Real RV -> Wartok walk-in test succeeded through native save-required prompt, load and control; setup used a private relocated start with opening scene disabled, no host-fired exit. Arrival camera remains obstructed.
- Background harnesses explicitly disable pause-unfocused now; normal startup waits on actual control after cinematic. Latest normal Intro handover still waits on loading14 and is under investigation with RAM dumps.
- Unrelated concurrent renderer edits caused transient API compilation failures. One integration fix permits configuring a newly captured SceneModels before publishing as const; later external API changes completed and subsequent canonical build passed. Other sessions remain active by user direction.

Normal-control run ended at360-second safety limit without scripted quit; cannot count as startup PASS. Follow-up RAM evidence in another normal run shows actual player/camera movement through RV cinematic, so stale GUI/input telemetry alone is not adequate; agent comparing completion state.

Harness clarification: sync play N requires N consecutive gameplay frames; it is not a timeout in seconds. Native-look telemetry can be absent during cinematic camera control. Actual player/camera RAM and unconditional input are used to distinguish the states.

Further original-source evidence: RV opening comprises61.19+52.85+90.76sec (~205sec). Final resume event names Wrynn; name-only player detection missed the controlled-actor identity after Intro carryover. Converter identity fix/regression is underway; previous short observation windows cannot establish a permanent freeze.

Identity-corrected campaign pack audit: RV final scene1.3 and Volcanic9.6c/9.1 Wrynn actors now mode1 (controlled player), where they were mode0 (placed generic actor). 29 sequence/campaign tests and typecheck pass. Long normal startup proof underway; no premature handoff claim.

Bow next checkpoint:64 focused checks pass; local-player-owned standard-arrow collision adjusts individual impact5 -> 10 while retaining native multipliers/armor and leavingNPC/sharedclass data untouched. Native-vs-modern control live tests both work; hostile comparison after the damage build remains pending. Multiplayer allocating torso channel through native guest trampoline to enable independent armed animation.

Consolidated combat build/deploy passed: native torso-channel allocation, live-node channel mapping, corrected own cosmetic body filtering in PvP, shared standard-arrow damage policy, and HUD snapshot character source. Primary play folder updated without stopping user processes; genuine hit/armed/audio proof on new build underway.

Cinematic long proof did not restore control within test window. Camera-key comparison proves normal source-time movement, but~130sec inter-scene gap between RV1.1 and1.2a. Identity-based finalresume fix is necessary but not sufficient evidence of complete startup. Agent investigating sequence chaining rather than globally rescaling or forcibly skipping.

Bow hostile12 live proof: three selector queries acquired real targets84/83/84, target4da restored0; three impact messages raw5 -> 10 (no repeated10 -> 20 onresume). Same enemy83 secondshotHP40 -> 32 vs baseline40 -> 36, demonstrating damage10 with native armor. Ammo200 -> 199 -> 198 -> 197. Clean scripted quit. Remaining original gravity/lifetime/cadence/jump behavior not declared1:1.

Multiplayer latest live checkpoint: NPCs allocate2nativechannels safely. Genuine (noninjected) arrowclaim hit remoteplayer2 damage10; server80 at explicit8x testscale; victimHP20; hurtLocal80 and soundinstance50807 voice45 (Nichols male sample), WAVcaptured. Test did not finish cleanly due native-vs-modern inventory recipe mismatch; not counted as clean armed/melee acceptance.

Clean armedmotion all14checksPASS with MonkBoss/Ranwulf: real WoodClub attachment, nativechannels2 -> 3, independent source run/upperattack20672/20686, cleanexits and save/recreation. Malevoice waveform matched originalNPCS0807 at33.5099sec with normalizedcorrelation0.98246 following genuinehit (no injection). Standardflight gravity0/lifetime15 checkpoint72tests passes, primary build/deploy passed; liveflight next.

Cinematic speech candidate separate/unpromoted:343 imported descriptor/sample pairs,370 mapped actions across16sequences, referencesaudited, but9allocatorwarnings and4oversizedAlwarrenclips remain. Production pack not replaced by incompletecandidate. Flight live hook confirmed once on realarrow with cachegravity0/life15; subsequentnockedarrow retained1/7.5 shareddefaults. Longerflight verification pending.

Native sequence analysis: firstheaderword is genuinelyflags (nativeOR/mask), notduration; no speculative duration rewrite. Delay localized to nextsequence preparation (elapsed=-0.01667/ready0 before starts).
Bow flight15 live: localstandardarrow constantvelocity(-10261.799,6732.690,601.140) over0.25 -> 3.3sec, >37kunittravel, gravity0/lifelimit15; nockedarrowdefaults1/7.5 unchanged. Field17c is cachedlifelimit (notremainingcountdown); naming corrected. Nativeacceleration300 -> PC240 exactbaselinecheckpoint74tests passes, buildingstagingwhileprimaryusergameisopen.

Matched180-unit melee fixture: Rynn -> Rynn nativecontacts/acceptedhits; Monk -> Rynn223callbacks38claims12accepted; Rynn -> Monk0contacts. VictimNPC eligibility isolated; hiddenattackerpose hypothesis rejected, no render change. NativeSSDfirstwordflags verified; nextsequence readinesspending isolated. Motionpack excluded frommatched OOTF tests, so no globalmotionpackcost blame.


## Committed scope correction

The user clarified that projectile velocity was never the issue: aiming and shooting while running and jumping are the acceptance target. Experimental arrow damage, gravity, acceleration and lifetime overrides described above were removed before commit d28a8b8. Those earlier measurements describe discarded experiments, not the committed behavior. Native projectile properties remain unchanged. Airborne firing, NPC melee victim eligibility and uninterrupted OotF startup/campaign progression are still unresolved.

Implementation checkpoints were committed separately for save editor artwork/catalog, bow input/targeting, campaign harness evidence, formats converter/retargeting and multiplayer motion/voices. The external renderer/language work was excluded through selective staging.
