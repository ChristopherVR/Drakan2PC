"""Two-instance host-departure regression (issue 1): the host quits to title from the modern pause menu, the guest
must be disconnected (connection-lost message, automatic return to the title) and then be able to host again.

Reserve the live game slot first. Runs the deployed dist/DrakanPC/drakan.exe twice with private settings, cards and
logs under build/harness/<name>. Dry run unless --run:

    python scripts/harness/mp_host_leave.py --name host-leave-1 --run

Inspect b/client_loss_error.png (Join Game page with the loss reason) and b/client_rehost_lobby.png.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import netplay_menu_two_instances as menu  # noqa: E402

EXE = ROOT / "dist/DrakanPC/drakan.exe"

QUIT_FLOW = """7000 shot host_playing
9000 key tap ESC
11000 shot host_pause
12000 key tap DOWN
12300 key tap DOWN
12600 key tap DOWN
12900 key tap DOWN
13200 key tap ENTER
14500 shot host_quit_page
15500 key tap ENTER
0 sync screen 8 3
3000 shot host_title_after_quit
10000 quit
"""

GUEST_FLOW = (
    "6000 shot client_playing\n21000 shot client_loss_error\n22000 key tap ESC\n"
    + "".join(f"{t} key tap DOWN\n" for t in (23000, 23250, 23500, 23750))
    + "24000 key tap ENTER\n"
    + "".join(f"{t} key tap DOWN\n" for t in range(25000, 27000, 250))
    + "27250 key tap ENTER\n31000 shot client_rehost_lobby"
)


def recipes() -> dict[str, str]:
    host = menu.recipe_a_text(0, QUIT_FLOW, 18000).rsplit("18000 quit", 1)[0]
    host = host.replace("22000 shot", "59000 shot").replace("22500 key", "60000 key")
    host = host.replace("23000 key", "60500 key").replace("23500 shot", "61000 shot")
    guest = menu.recipe_b_text(GUEST_FLOW, 36000).splitlines()
    for i, line in enumerate(guest):
        if "sync play" in line:
            break
        stamp, _, rest = line.partition(" ")
        if stamp.isdigit() and int(stamp) > 0:
            guest[i] = f"{int(stamp) + 20000} {rest}"
    return {"a": host, "b": "\n".join(guest) + "\n"}


def checks(out: Path, codes: dict[str, int | None]) -> dict[str, bool]:
    a = (out / "a/drakan.log").read_text(errors="replace")
    b = (out / "b/drakan.log").read_text(errors="replace")
    return {
        "both_exit_0": all(c == 0 for c in codes.values()),
        "both_playing": "client_playing" in b and "host_playing" in a,
        "host_quit_confirmed": "quit to title: box Yes" in a,
        "guest_got_disconnect": "connection lost" in b,
        "guest_auto_title": "auto return to the title: Quit to Title queued" in b,
        "guest_can_rehost": (out / "b/client_rehost_lobby.png").exists(),
        "no_stall": not any(m in a + b for m in ("missing guest branch target", "silent-input stall")),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--name", required=True)
    ap.add_argument("--port", type=int, default=47985)
    ap.add_argument("--run", action="store_true")
    args = ap.parse_args()
    out = (ROOT / "build/harness" / args.name).resolve()
    if out.parent != (ROOT / "build/harness").resolve() or out.exists():
        ap.error("name must identify a new output directory inside build/harness")
    if not EXE.is_file():
        ap.error("deployed dist executable required")
    menu.PORT[:] = [args.port]
    texts = recipes()
    for role, text in texts.items():
        (out / role).mkdir(parents=True)
        (out / role / "recipe.txt").write_text(text, newline="\n")
    if not args.run:
        print("prepared recipes only; pass --run to launch")
        return 0
    procs = {}
    for role in "ab":
        folder = out / role
        env = menu.game_env(
            folder, EXE.parent, folder / "recipe.txt", 240, "HostLeave" if role == "a" else "GuestLeave"
        )
        ini = folder / "drakan.ini"
        ini.write_text(
            "[general]\nquit_exits = 0\ntitle_menu = modern\npause_menu = modern\nsave_menu = modern\n"
            "inventory_screen = modern\n[video]\nrenderer = d3d11\n"
        )
        env.update(
            DRAKAN_INI=str(ini),
            DRAKAN_GAME="d2",
            DRAKAN_LANG="english",
            PS2X_GS="d3d11",
            DRAKAN_PAUSE_UNFOCUSED="0",
            DRAKAN_PAUSE_MENU="modern",
            DRAKAN_ISO=str(ROOT.parent / "Drakan - The Ancients' Gates.iso"),
            DRAKAN_INPUT_INI=str(folder / "drakan_input.ini"),
            DRAKAN_MP_FRAG_LIMIT="0",
            DRAKAN_MP_TIME_LIMIT_SEC="0",
        )
        procs[role] = menu.start(EXE, env, folder)
    codes = {role: proc.wait() for role, proc in procs.items()}
    result = {"exit_codes": codes, "checks": checks(out, codes)}
    result["passed"] = all(result["checks"].values())
    (out / "summary.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
