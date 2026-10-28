# NVIDIA DLSS (optional)

No NVIDIA files are part of this repository or of the builder packages. DLSS is opt-in:

- `python scripts/buildtool/fetch_dlss_sdk.py --accept-nvidia-license` (or `Build-LocalGame.ps1 -AcceptNvidiaDlssLicense`,
  `Build-Drakan.ps1 -EnableDlss`, or the installer's DLSS box) downloads the parts of NVIDIA's public DLSS SDK the build
  needs from [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) into `tools/dlss/dlss_sdk`, after you accept the
  [NVIDIA RTX SDKs License](https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt). Tag and SHA-256 are pinned in
  `scripts/buildtool/dlss.lock.json`.
- `Build-LocalGame.ps1` then builds the DLSS module and copies the SDK's `nvngx_dlss.dll` into your play folder
  (`local/nvidia/dlss/nvngx_dlss.dll` overrides it).
- DLSS only runs on NVIDIA GPUs; the PC settings show the NVIDIA DLSS credit whenever it is available.

Without the SDK the game builds and runs normally, without DLSS.
