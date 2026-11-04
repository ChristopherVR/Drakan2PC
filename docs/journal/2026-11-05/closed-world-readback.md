# Closed-world guest readback

The transition-menu review identified an independently reproducible deferred
resolve defect. Modern world rendering keeps its pending work in `neuralState`,
while target readback only checked the older `wstate` upscaler path. A guest
screen grab after EndWorld but before a HUD draw or display latch therefore
read native world RGB without the pending ray pass.

The offscreen hardware `world_readback` regression constructs a world with
visible offscreen diffuse transport. Its immediate post-EndWorld VRAM read
failed equality against the same scene latched before readback
(`build/rt-world-readback-before.log`). This establishes an image-ownership bug;
it does not establish that the observed level-exit prompt took this exact path.
Live prompt logs separately showed RT remaining active.

Both native backends now finish deferred modern work before consuming its
closed target through readback. The guard requires a pending EndWorld, no
active engine world, no late native-water submission, and an unresolved modern
target. It clears the pending marker before resolving, matching the display/HUD
boundary while preventing reentrant consumption. Active world and native-water
reads retain their original ordering.

The Release D3D11 hardware regression passes in 3.24 seconds after the fix
(`build/rt-world-readback-after.log`; build receipt
`build/rt-world-readback-after-build.log`). It checks the latch-first reference,
post-EndWorld readback, active-world and late-water negative controls, and exact
native-image preservation with effects disabled. The focused build/test does
not compile or execute the WebGPU parity path; that requires the broader
integration checks. No gameplay-artifact resolution claim follows from this
synthetic test alone.
