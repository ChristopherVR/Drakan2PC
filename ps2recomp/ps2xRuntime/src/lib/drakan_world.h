// Drakan: read-only world introspection (runtime/world, docs/reference/world-model.md). Every frame the overlay is open
// the EE thread copies the guest's actor registry into a world::Snapshot (plain memory reads, no guest calls); the F1
// overlay draws it as a "World" window (class / type filter, distance to the player, details) with a "Dump JSON"
// button. Opt in with the environment variable DRAKAN_WORLD=1 (DRAKAN_WORLD=2 also samples with the overlay closed,
// DRAKAN_WORLD_DUMP_FRAME=N writes one dump at the Nth sample: harness runs). Nothing is ever written to the guest.
#pragma once
#include <cstdint>

namespace drakan_world {

void configure(); // env DRAKAN_WORLD=1; resolves the region's addresses
bool enabled();
// EE thread, once per frame (after the pad update). Cheap early out unless enabled and the overlay is open.
void sample(const uint8_t* rdram, bool overlayOpen);
// UI thread, inside the overlay's ImGui frame while the F1 menu is open (SettingsOverlay::setMenuExtra).
void drawPanel();

} // namespace drakan_world
