# Issue 1 multiplayer list: re-verification on the deployed build

Goal: reproduce each multiplayer item of issue 1 with two real instances on the current `dist/DrakanPC/drakan.exe`
(SHA-256 `604aef63...`, built 2026-11-06 17:18, which contains the 2026-11-05 fixes). No production code changed:
none of the seven items reproduced. All runs held `build/harness/game.lock`, used private settings, cards and logs, and
both processes exited 0 with no missing guest branch target.

| Item | Run (under `build/harness/`) | Result |
| --- | --- | --- |
| (a) pickup freeze | `mp-verify-city-blade`, `mp-verify-city-street` (`arena_pickup.py --city-sites ... --interaction use`) | Host and client pick up the City of the Damned Blade and Mana pickups at the authored sites with native Use, server accepts both players, each peer removes the other's copy, pickups come back, game keeps running, clean quit. This closes the "authored City coordinates not certified" gap of the 2026-11-05 note. |
| (b) Surdana cutscene | `mp-verify-sp-mp` | Menu-hosted Surdana Meadow reaches Playing with the arena HUD and free movement (5032 units walked); no campaign cutscene. |
| (c) Inquisitor W/S | `mp-verify-inq` (`arena_movement.py --joined --join-from-new-game --arena 2`) | W and S translate host 3755 / 1866 units and client 2380 / 1909 units with zero yaw change. |
| (d) SP, title, MP freeze | `mp-verify-sp-mp` | New Game, Quit to Title, then Host Surdana: loads and plays, no freeze. |
| (e) host leaves | `mp-verify-host-leave` (new `mp_host_leave.py`) | Host Quit to Title; guest receives the disconnect, returns to title automatically, Join Game shows "Connection to the server lost", and the guest can host again. |
| (f) lag / teleport | `mp-verify-motion` (`remote_motion_check.py`, Archer vs Sevoth, 65 +/- 35 ms, 8% loss) | No receiver or sender step over 500 units on either side; 385 datagrams dropped by the relay. |
| (g) low poly | same run, `a/near.png` | The nearby remote player renders with the full mesh (native high-detail request within 4000 units). |

## Harness

`scripts/harness/mp_host_leave.py` replaces the untracked ad hoc script used on 2026-11-05 for the host departure
run. It prepares both recipes, launches the two instances, and checks both exit codes, host quit confirmation, guest
disconnect and automatic title return, and that the guest reached the re-host lobby. Screenshots to inspect:
`b/client_loss_error.png`, `b/client_rehost_lobby.png`.

## Not covered

Abrupt process loss (falls back to the 10 second receive timeout), a WAN race for pickups, other roster characters'
LOD beyond the Archer and Sevoth pair, and the reporter's exact settings and hardware.
