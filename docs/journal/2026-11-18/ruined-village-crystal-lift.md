# Ruined Village lift landing after Heron's Crystal

Rynn could ride the lift and stand on the upper cave landing. On the diagonal route, saving could make her fall
through the map even after five seconds standing still. Opening and closing the pause menu held her position.
The reported quicksave reproduced the fall on load. It also reproduced at the original 30 Hz world rate.

The recorded character is correctly located just above the upper floor. The native world reinsertion in save
and restore uses the global lookup at `0x14b6b8`, caller `0x149ab4`. It files the same position on layer 346,
the generated push-puzzle support more than 15,000 world units below. Ordinary actor movement starts the
lookup from its previous layer and keeps the upper floor.

The global reinsertion result now uses the upper landing where its bounds cover the query and the known lower
push floor was chosen. It checks the authored layer IDs and heights so the rule remains scoped to this terrain.
The native lookup and its correction run without a guest scheduling yield, including on a cold Load Game from
the title.
The lower collision is still available to Rynn and the push block in the tomb. Changing visibility order failed
to correct the global search. Removing the lower collision did correct it, but that removed solid triangles from
the floor of the lower room.

A native ride, diagonal walk-off, quicksave and quickload held Rynn on the upper landing. A copy of the
reported quicksave then loaded and walked along that landing with its save bytes intact. Manual Save Game
held the upper landing too. Quicksaving while still on the lift kept the shaft layer. The push block slid
onto plate 992 and restored there through a quicksave and quickload. The refile trace, terrain probes and
recipes are under `build/remaining-fixes` and `build/rv-lift-20261118-194511`.
