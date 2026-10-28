// Dual wielding: with [general] dual_wield = 1 (Gameplay > Combat > Dual wielding), using a second one-handed weapon
// while Rynn holds one puts it in her left hand; her strikes then alternate hands (every second strike is the native
// swing mirrored onto the left arm) and the left blade is live in the attack windows. Native model and limits:
// docs/reference/combat-controls.md "Dual wielding". All hooks run on the EE game thread.
#pragma once

class PS2Runtime;

namespace drakan_dual_wield {
// Reads [general] dual_wield (env DRAKAN_DUAL_WIELD) and installs the hooks. A disabled option also puts away an
// off-hand weapon restored by loading a save.
void install(PS2Runtime& runtime);
// Live switch from the settings overlay. Disabling queues native off-hand unequip on the next EE update.
void setEnabled(bool on);
} // namespace drakan_dual_wield
