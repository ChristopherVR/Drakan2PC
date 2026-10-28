#!/usr/bin/env python3
"""Focused regression checks for stability verdicts and generated input scripts.

Run: python scripts/harness/stability/stab_check_tools.py [--bash path/to/bash]
Uses temporary synthetic logs; does not launch the game or modify saved runs.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bash", default="C:/Program Files/Git/bin/bash.exe" if sys.platform == "win32" else "bash")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[3]
    verdict = root / "scripts/harness/stability/stab_verdict.sh"
    normal = "[gsr] exit: frames=123\nGame thread returned\n"
    cases = [
        ("completed", normal, 0, "script quit", "OK"),
        ("zero frames", "[gsr] exit: frames=0\n", 0, "script quit", "NOPROGRESS"),
        ("unfinished", normal, 0, "", "INCOMPLETE"),
        ("missing input", normal, 0, None, "INCOMPLETE"),
        ("exception", "Error during program execution\n" + normal, 0, "script quit", "DEAD_EXC"),
        ("crash", "[crash] fatal\n" + normal, 3, "", "CRASH"),
        ("crash after unterminated log line", "other log[crash] fatal\n" + normal, 0, "script quit", "CRASH"),
        ("ring flush", "[trace-ring] #123\n" + normal, 0, "", "DEAD"),
        ("timeout", normal, 124, "", "HANG"),
        ("nonzero exit", normal, 7, "", "EXIT7"),
        ("no shutdown", "", 0, "script quit", "NOEXIT"),
        (
            "later missing target",
            "[guest-branch:missing-target] target=0x0\n[guest-branch:missing-target] target=0x1234\n" + normal,
            0,
            "script quit",
            "MISSING",
        ),
    ]
    with tempfile.TemporaryDirectory(prefix="stab-check-") as tmp:
        log, inp = pathlib.Path(tmp) / "case.log", pathlib.Path(tmp) / "input.log"
        for name, contents, rc, input_contents, expected in cases:
            log.write_text(contents)
            if input_contents is None:
                inp.unlink(missing_ok=True)
            else:
                inp.write_text(input_contents)
            result = subprocess.run(
                [args.bash, str(verdict), str(log), str(rc), str(inp)], capture_output=True, text=True, check=True
            )
            actual = result.stdout.split()[0]
            assert actual == expected, (name, expected, result.stdout, result.stderr)
        log.unlink()
        result = subprocess.run([args.bash, str(verdict), str(log)], capture_output=True, text=True, check=True)
        assert result.stdout.startswith("NOLOG "), result.stdout
    for scenario in "abcde":
        for seed in (1, 2, 17):
            result = subprocess.run(
                [sys.executable, str(root / "scripts/harness/stability/stab_genscript.py"), scenario, str(seed)],
                capture_output=True,
                text=True,
                check=True,
            )
            lines = result.stdout.splitlines()
            assert lines[-1].endswith(" quit"), (scenario, seed)
            frame = 0
            for line in lines:
                number, command = line.split(" ", 1)
                number = int(number)
                if command.startswith("sync "):
                    frame = 0
                else:
                    assert number >= frame, (scenario, seed, line, frame)
                    frame = number
    print("PASS: 13 verdict fixtures; 15 scenario/seed scripts end in quit with ordered frames")


if __name__ == "__main__":
    main()
