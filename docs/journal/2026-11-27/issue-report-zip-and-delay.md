# Report an Issue: slow dialog and a zip Windows cannot extract

Report: Report an Issue took a few seconds before its first Windows dialog appeared, and the report zip could not be
extracted.

## Zip

`createZip` (`ps2recomp/ps2xRuntime/src/lib/drakan_issue.cpp`) ran `tar -a -cf <zip> -C <staging> .`, so every entry was
stored as `./session.log`, `./saves/...` plus a `./` entry. The zip itself is valid (Python and 7-Zip read it), but the
Windows zip folder lists the user's 23:04 report as 0 items, so Extract All produced nothing. tar now gets the staging
folder's top-level names instead of `.`. Check: the same 57 files re-zipped that way list as `saves, gameplay.png,
session.log` in `Shell.Application` and `CopyHere` extracts all 57.

## Delay

The report thread flushed the session log (`drakan_log::flush`, waits up to 1.5 s for its marker), copied it and read
the system details (DXGI adapters) before the first message box. The collection now runs in a `std::async` beside the
first question (the log is still captured as of the click) and logs `[issue] log and system details collected in N ms`.
The first box only waits for it when the player answers Yes.

Not verified in game: the message boxes cannot be driven by the input script, so the faster first dialog was not timed
in a real session. The build starts and runs (`build/harness/issue-smoke`).
