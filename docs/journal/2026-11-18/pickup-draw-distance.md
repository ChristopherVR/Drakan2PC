# Pickup draw distance

In the Order of the Flame conversion, small pickups could disappear a few thousand world units from the camera.
The Wartok Canyons health potions store a draw distance of 5,796 units. The native renderer uses that value for
both distance culling and fading.

The PAL runtime now scales the draw distance of weapons, usable items and power-ups by at least four in both
render calls. Pickups bypass the prop taper. The level's view distance still caps the visibility
test. The hooks restore the stored value before returning, so the engine continues to use it for object activity.
See [model detail](../../reference/graphics.md#10-model-detail-lod-and-draw-distance-factors).

## Verification

Started `OotF 03 - Wartok Canyons` with a fresh test card, `DRAKAN_DRAW_DISTANCE=1`, and
`DRAKAN_MODEL_DETAIL_LOG=1`. The input recipe placed Arokh at `(95000, 12600, 65000)` with yaw `1.898`.
At that pose, two health potions were about 21,200 units from the camera. The original build reported no
extended draws for the pair. The updated build reported two for model `0x31765`, with the nearest at 21,191 units.
The stored distance read from guest RAM was 5,796.33 units in both builds. From a clear view near the canyon
ruins, the rebuilt game drew a red potion at 7,427 units in `build/harness/pickup-canyon-sightline/potion2.png`.
The native Surdana smoke run also loaded, rendered gameplay and exited cleanly.

The before and after logs are in `build/harness/pickup-pot-far-stock` and `build/harness/pickup-pot-far-fixed`.
The input recipe and comparison script are in `build/item-draw-distance`.
