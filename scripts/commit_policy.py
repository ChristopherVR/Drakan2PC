"""Enforce the commit message policy that cliff.toml turns into release notes.

Subject format (Conventional Commits with a required component scope):

    <type>(<scope>[,<scope>...])[!]: <Imperative subject>

    types   feat fix perf revert            listed in release notes
            refactor docs test build ci chore style   kept out of release notes
    scopes  game         the PC port (runtime, renderer, audio, input, multiplayer, installer, builder kit)
            editor       the Drakan Editor (level / mod editor)
            save-editor  the Drakan Save Editor
            repo         ships in none of them (CI, repository tooling, research notes)

Examples: "fix(game): Fix VIF1 readback for CHCR.DIR=0", "feat(editor,save-editor): Read German disc images".
The subject starts with a capital letter, has no trailing period, stays within 100 characters, and no message uses
an em-dash. Merge commits and fixup!/squash! commits (local autosquash) are allowed.

Usage:
    python scripts/commit_policy.py --message-file .git/COMMIT_EDITMSG   # commit-msg hook (.githooks/commit-msg)
    python scripts/commit_policy.py [--range A..B]                       # history check; default POLICY_BASE..HEAD
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Last commit before the policy; it and its ancestors are legacy and keep their free-form subjects.
POLICY_BASE = "8ae96142c1d9542fae6cbd733506320af7ac0d54"

RELEASE_TYPES = ("feat", "fix", "perf", "revert")
OTHER_TYPES = ("refactor", "docs", "test", "build", "ci", "chore", "style")
SCOPES = ("game", "editor", "save-editor", "repo")
MAX_SUBJECT = 100

HEADER = re.compile(r"^(?P<type>[a-z]+)\((?P<scopes>[^)]*)\)(?P<breaking>!)?: (?P<subject>.+)$")
EXEMPT = re.compile(r"^(Merge |fixup! |squash! |amend! )")


def check_message(message: str) -> list[str]:
    """Return the policy violations of one commit message (empty when it complies)."""
    lines = [line for line in message.splitlines() if not line.startswith("#")]
    while lines and not lines[0].strip():
        lines.pop(0)
    if not lines:
        return ["empty commit message"]
    header = lines[0].rstrip()
    errors = []
    if "—" in message:
        errors.append("use a comma, colon or parentheses instead of an em-dash")
    if EXEMPT.match(header):
        return errors
    match = HEADER.match(header)
    if not match:
        usage = "'<type>(<scope>): <Subject>', e.g. 'fix(game): Fix VIF1 readback'"
        return [*errors, f"subject {header!r} must be {usage}; scopes: {', '.join(SCOPES)}"]
    kind, subject = match["type"], match["subject"]
    if kind not in RELEASE_TYPES + OTHER_TYPES:
        errors.append(f"unknown type {kind!r}; use one of {', '.join(RELEASE_TYPES + OTHER_TYPES)}")
    scopes = match["scopes"].split(",")
    unknown = [scope for scope in scopes if scope not in SCOPES]
    if unknown:
        errors.append(f"unknown scope {', '.join(map(repr, unknown))}; use one or more of {', '.join(SCOPES)}")
    elif len(set(scopes)) != len(scopes):
        errors.append("a scope is repeated")
    elif "repo" in scopes and len(scopes) > 1:
        errors.append("'repo' means none of the shipped components, so it cannot be combined with them")
    if not subject[0].isupper() and not subject[0].isdigit():
        errors.append("start the subject with a capital letter (imperative: 'Fix ...', 'Add ...')")
    if subject.endswith("."):
        errors.append("drop the trailing period from the subject")
    if len(header) > MAX_SUBJECT:
        errors.append(f"subject line is {len(header)} characters; keep it within {MAX_SUBJECT}")
    if len(lines) > 1 and lines[1].strip():
        errors.append("leave a blank line between the subject and the body")
    return errors


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8")


def check_range(revision_range: str) -> int:
    commits = git("rev-list", "--reverse", revision_range).split()
    failures = 0
    for commit in commits:
        message = git("log", "-1", "--format=%B", commit)
        errors = check_message(message)
        if errors:
            failures += 1
            print(f"{commit[:10]} {message.splitlines()[0] if message else ''}", file=sys.stderr)
            for error in errors:
                print(f"    {error}", file=sys.stderr)
    if failures:
        print(
            f"{failures} of {len(commits)} commits break the commit policy (scripts/commit_policy.py).", file=sys.stderr
        )
        return 1
    print(f"Commit policy: {len(commits)} commits in {revision_range} comply.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--message-file", type=Path, help="check one message (the commit-msg hook passes its file)")
    group.add_argument("--range", default=f"{POLICY_BASE}..HEAD", help="revision range to check (default: %(default)s)")
    args = parser.parse_args()
    if args.message_file:
        errors = check_message(args.message_file.read_text(encoding="utf-8"))
        if errors:
            print("Commit rejected by the commit policy (scripts/commit_policy.py):", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        return 0
    return check_range(args.range)


if __name__ == "__main__":
    raise SystemExit(main())
