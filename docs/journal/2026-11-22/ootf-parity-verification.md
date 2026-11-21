# OOTF multiplayer parity verification

Converted the supplied retail installation and compared the exported arena definitions against the retail LVL and LFL
files. All ten maps retain their group and rotation order. The comparison matched 87 launch positions and headings
and 182 pickup records with their respawn delays. Two clients joined and reached their assigned spawn on each map.
Each of the four Air Only maps placed both players on dragons.

The Master of the Dragon maps import the red launch pad heading and the retail dragon and poison breath class values.
The three maps exported 300 dragon health, 4,500 world units per second in flight, 4,000 in climb, 40 damage, a
500 ms reload and 2.5 energy per second after applying the recharge multiplier. On Island of Death, both clients
received the unclaimed dragon's pursuit and breath events, and the two players each took 40-point poison hits in the
hostility run. Claiming the crystal stopped the attacks. A separate two-client run transferred the dragon to the other
player on a lethal hit at its last position. The network test verifies that damage to the dragon leaves the rider's
health intact and that killing the dragon after the rider dismounts still transfers ownership. The port implements
the chase and targeting behavior with the authored tuning.

Hill of Fire's Fire Summon created the native ring on both clients. Its display was hidden on collection and drawn
again after the 30-second respawn. Invisibility and invulnerability were exercised on two Relic clients in the
[previous run](../2026-11-21/ootf-multiplayer-parity.md#pickup-displays-automatic-effects-and-the-hostile-dragon).

The campaign speed potion halved the collector's world tick while doubling its animation rate. Arena QuickSpeed
now scales the local actor's horizontal root motion through native collision and keeps the world clock unchanged.
A controlled two-client check moved one pickup onto an open path in a private Hill of Fire fixture. The same input
and 56 samples from the same point and heading covered 3,899 horizontal units during the five-second effect and
1,951 after expiry. The private fixture was used only for measurement. The audit used the regular converted pack.
