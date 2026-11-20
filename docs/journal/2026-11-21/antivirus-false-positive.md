# Antivirus detections of DrakanPC-Builder.zip (issue #14)

Report: Windows Defender removed `DrakanPC-Builder.zip` on download as a severe trojan. The reporter's VirusTotal
screenshot is the v0.1.31 zip (SHA-256 `02d06590...05d2e`, matching that release's `SHA256SUMS.txt`): 3 of 58 engines,
Avast and AVG `Other:Malware-gen [Trj]`, WithSecure `Trojan.TR/Malware`. Defender is not among the VirusTotal hits.

## What is in the zip

2623 entries, no PE files at all: C++/TS/Python sources, 27 `.ps1`, a few `.cmd`, JS, and 120 MB of PNG art. So every
hit is a heuristic on scripts or on the archive as a whole, not on a binary.

Local Windows Defender (signatures 1.463.389.0, updated 2026-11-21) finds nothing in the zip or in the extracted tree.
The reporter's removal on download is most likely a cloud-delivered machine-learning verdict on a fresh, unsigned,
low-reputation file (those `!ml` style detections do not reproduce in an offline scan and often clear as the file gains
reputation or after a false-positive submission).

## Why heuristics fire

The kit has the shape generic "trojan downloader" rules look for:

- `Build-Drakan.cmd` at the archive root runs `pwsh -NoProfile -ExecutionPolicy Bypass -File ...`.
- `Build-Drakan.ps1` `Invoke-WebRequest`s an `.exe` (Microsoft's `vs_BuildTools.exe`) into `%TEMP%` and
  `Start-Process`es it (it does check the Authenticode signature first).
- `extras.py` builds an `Invoke-WebRequest` PowerShell command line; developer harness scripts in `scripts/harness/`
  and `scripts/tests/` start processes with `-WindowStyle Hidden`. None of these are needed by players.
- Nothing is code-signed, and every release is a new hash with no reputation.

This was not verified per file (no VirusTotal key locally), so which file each engine keys on is inferred.

## What changed

- `release.yml` and `editor-release.yml` scan the files with Windows Defender before upload
  (`scripts/buildtool/Scan-ReleaseDefender.ps1`) and fail on a threat.
- `release-scan.yml` posts VirusTotal verdicts and report links into the release notes (`VT_API_KEY` secret).
- README "Antivirus warnings" explains the false positive and how to verify a download.

## Options not taken yet

- Leave `scripts/harness/`, `scripts/perf/`, `scripts/tests/` and `analysis/` out of the player kit (needs a check that
  the `runtime-check` gate and the build do not read them).
- Install the Build Tools with `winget install Microsoft.VisualStudio.2022.BuildTools --override ...` instead of
  downloading and starting the bootstrapper ourselves.
- Authenticode-sign the Setup and scripts (removes most of the reputation problem; needs a certificate).
- Submit the release files as false positives to Microsoft (wdsi/filesubmission), Avast/AVG and WithSecure.
