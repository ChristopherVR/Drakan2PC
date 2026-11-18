# Ruined Village cooking spit and camp lighting

The 20:35:40 quicksave loads with an empty space between the posts above the fire. The converted level places the
`Meat` switch at the correct position and marks it visible. The save keeps that switch as a separate instance record
with visibility cleared. Most of the other props at the camp still load from the level's static placements, so the
older fix for the zone culling toggler left the spit hidden.

The normal save reader repairs visibility in memory for the affected Ruined Village saves from the two known OotF
builds. It resolves the saved slot for object 701 and checks the world-common database and class 2416 before setting
bit 0 of the record's flags at offset 8. This preserves the transform, activation state and other flags. It also keeps
the slot table and the original save file intact. A synthetic test covers an already visible record, a different class
and an absent object.

The OotF switch schema uses the label `Switch Type` for two fields. The first chooses the activation mode and the
second chooses rotation or translation. The converter now copies the authored Crank mode into the first field.
In a controlled run from the 20:38 copy, the switch with the old converted defaults took the first E press and stayed
at rest after the next two. The corrected class completed another turn after each subsequent E press. Both runs used
the same approach and timing with room for each turn to finish. The rebuilt game also restored the spit from the
20:35:40 quicksave through the regular load path. Two successful uses each turned it and returned it to the same rest
position. The quicksave's SHA-256 stayed the same throughout the run.

The warm light at the fire was already present in a guest memory capture. Its red, green and blue channels were
2.8471, 2.4706 and 0.9647. The model light upload can pack a channel up to 2.0. The retail packer masks off
overflow bits, which lowered red and green on models that used the full light strength. The runtime saturates the
channels at the model light upload call. Terrain lighting continues to use its own light values.

With the installed fix, the nearby posts, small crate and Wartoks pick up warmer light in the same saved camera view.
The large crate to the right retains a purple tint at the same strength with HD textures on or off. Saturating the
separate ambient colour upload had no effect on that crate, so the fix only changes the proven model light upload.

Reproduction images and the live switch trace are in the local harness runs `rv-spit-exact-base`,
`rv-spit-exact-fixed`, `rv-spit-cycle-base`, and `rv-spit-cycle-crank`. The test runner uses copies of the card.
