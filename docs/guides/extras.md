# HD textures, video & subtitles

Build textures and movies from your own disc. Supply matching local subtitle files for narration in The Ancients' Gates.

## HD textures

### Generate HD textures with the Windows builder

In Setup, select **Generate HD textures after the build**. From the builder folder, you can also run
`Build-Drakan.cmd -HdTextures on`. Generation runs after the game build and writes to `textures` in the play folder.
Run the same command to resume generation and keep the textures already made.

Setup uses its bundled Python. The ZIP builder downloads NumPy and Pillow when needed for standard 64-bit Windows
CPython 3.11 through 3.14. The v0.1.36 ZIP builder downloads only the Python 3.12 packages. If a ZIP build
reports a NumPy or Pillow error, use Setup with your existing builder and play folders. See
[Switch from the ZIP to Setup](install.md#update-with-setup-or-switch-from-the-zip-to-setup).

If the builder says **Drakan is ready to play**, launch the game even if HD generation stopped. The game uses
the existing replacement files where available and original textures for the rest. Check `build-log.txt` in the
builder folder for the error before retrying. To skip generation on future runs, run `Build-Drakan.cmd -HdTextures auto`.

### Generate HD textures from source

The Windows renderer supports replacement textures as well as live texture upscaling. To generate the AI pack from a source checkout, follow the [HD texture reference](../reference/hd-textures.md). The workflow needs NumPy, Pillow and a configured `realesrgan-ncnn-vulkan` upscaler with its models.

```powershell
python -I analysis/scripts/hd_pack.py extract --iso "D:/MyDiscs/Drakan.iso"
python -I analysis/scripts/hd_pack.py upscale
```

Extraction writes to `build/hdpack`; upscaling writes replacements to `dist/DrakanPC/textures` and can be resumed.

Turn **HD texture replacement** on or off in PC Settings. Rebuilds keep the pack. Linux and browser builds can use it too.

The same build step also fits every upscaled image to its original (colour, grain, soft transparency) and writes material maps (normal, roughness, height) into `textures/materials`. **Material detail** in PC Settings uses them on the Direct3D 11 renderer; the maps are ordinary PNG files named by the same hash as the textures, so other tools can read them too.

<!-- media:hd-compare -->

## Upscaled movies

Replaces the intro, outro and logo with **4× upscaled frames** generated from your ISO (Windows). Needs FFmpeg with IPU decoding and Real-ESRGAN. The normal build does not create it.

From the repository root:

```powershell
python analysis/scripts/fmv_hd_pack.py list --iso "D:/MyDiscs/Drakan.iso"
python analysis/scripts/fmv_hd_pack.py build --iso "D:/MyDiscs/Drakan.iso"
```

Use the script's `--help` for explicit FFmpeg, upscaler and output paths. Its default output is `dist/DrakanPC/movies`, which the game builder preserves. See the [FMV reference](../reference/fmv.md) for frame matching, decoding and synchronization details.

The Windows runtime enables the replacement path when a compatible pack is present. To use original movies, close the game and set this in `drakan.ini`:

```ini
[video]
hd_movies = 0
```

Set `hd_movies = 1` to enable it again. Missing HD frames fall back to the original video.

<!-- media:movie-compare -->

![A dragon illustration in the locally upscaled intro movie](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-movie.jpg)

## Dialogue versus narration subtitles

**Gameplay dialogue:** the original game already supplies subtitles. Use **Options → Audio → Show Subtitles** (or the corresponding game-sound setting in the modern UI).

**Intro and ending narration:** the PC port adds an SRT subtitle overlay because the PS2 narration has no subtitle text. Matching files must exist locally. For English, place your local files in:

```text
local/subtitles/intro_english.srt
local/subtitles/outro_english.srt
```

The builder copies these into the playable `subtitles` folder and preserves them across rebuilds. For the other PAL languages, replace `english` with `german`, `french`, `italian` or `spanish`. Use timings from the matching recordings, since dubbed narration can differ in length.

Enable **Narration subtitles** in PC Settings and ensure the game's **Show Subtitles** option is enabled. Subtitle size can follow the window or use an explicit scale. The [subtitle reference](../reference/subtitles.md) contains implementation and timing notes; its earlier proposal sections are historical.

**Order of the Flame scene captions.** Setup automatically uses the bundled English or German track that matches your Order of the Flame installation. To transcribe your own copy, choose **Generate subtitles from my recordings** or run `Build-Drakan.cmd -OotfSubtitles generate`. You can also place `ootf-subtitles.json` in the install folder.

![Intro narration with the added subtitle overlay](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/narration-subtitles.jpg)

## DLSS and ray tracing

<!-- media:dlaa-compare -->

The installer offers an optional NVIDIA DLSS download, subject to NVIDIA's licence and compatible RTX hardware. A source build can use a local SDK as described in [Build from source](../../BUILDING.md#optional-local-nvidia-build). DXR water and shadows require compatible hardware and a build with the required shader tools.

Use [Graphics & PC settings](settings.md) to turn enhancements on or off. Advanced Windows renderer features do not automatically carry over to Linux or browser builds.
