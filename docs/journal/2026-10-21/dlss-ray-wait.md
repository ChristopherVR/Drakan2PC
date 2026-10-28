# Rejected deferred ray completion wait experiment — 21 October 2026

The ray renderer previously waited on the CPU immediately after every D3D12 submission, before handing its output back to D3D11. Shared producer/consumer GPU fences already order that handoff. The candidate defers CPU completion until the next render call, before upload buffers, descriptors, acceleration structures or the command allocator can be reused. Retired resources remain alive until that completion; renderer destruction drains outstanding work. `GSR_RAY_DEFER_WAIT=0` restores the immediate-wait path for comparison.

This follows the resource-lifetime requirement described by [Microsoft's fence-based resource management documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management) and the [command allocator reset contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset). It does not remove GPU dependencies or promise that downstream neural evaluation will never wait for ray work.

Six focused tests pass with hardware ray validation and cache-reference comparison enabled. Added tests queue 32 camera/input changes without intervening readbacks, compare final output exactly, and destroy a renderer immediately after submission. D3D12 debug messages are checked after completed work and at teardown. Movement/performance and deployment evidence are retained under `build/dlss-ray-wait-20261021`.

The candidate preserves the deployed world-triangle cache and frozen native runtime. It replaces the ray object; the backend profiler object is identical to the deployed one. Full-game performance and installed smoke results must be read from the evidence receipts, rather than inferred from submission timing alone. Original coarse terrain, approximate offscreen sky and whole-game coverage limitations remain.

Pond, river and camp movement passed finite SR/neural output, exact presentation and independent model motion. Every logged ray frame had native lights: 349 pond, 327 river, 349 camp. The river run ended after 327 ray epochs; no missing-light frame or ray bypass was logged. All three paths recorded zero original-packing mismatches, and save hashes were unchanged.

## Rejected after performance validation

The paired runs recorded no competing game/compiler processes but measured 25.48 FPS for deployed cache build `1ca391ce...` and 20.95 FPS for deferred-wait candidate `4eb832d4...` (17.8% lower). The final candidate windows fell to 16–20 FPS. Although observed CPU ray-wait time dropped close to zero, end-to-end performance failed acceptance. The experiment was not deployed; `ray_water.cpp` was restored from the pre-experiment snapshot. The installed cache build and its passing smoke receipt remain the release. Hardware queue/teardown regressions remain as useful tests. These measurements do not establish the precise cause of the slowdown.

Later system sampling exposed substantial Java/Node/Android build CPU load that the game/compiler-only overlap probe missed. Therefore the observed lower FPS cannot be attributed solely to deferred waiting. The experiment remains rejected because a performance benefit has not been established; it must not be presented as a proven intrinsic regression.
