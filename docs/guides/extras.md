# HD textures, video & subtitles

These additions are optional. Textures and movies are generated from your disc, and narration subtitles are locally supplied. None of these game-derived packs are distributed by this site.

## HD textures

The Windows renderer supports replacement textures as well as live texture upscaling. To generate the AI pack from a source checkout, follow the [HD texture reference](../reference/hd-textures.md). The workflow needs NumPy, Pillow and a configured `realesrgan-ncnn-vulkan` upscaler with its models.

```powershell
python -I analysis/scripts/hd_pack.py extract --iso "D:/MyDiscs/Drakan.iso"
python -I analysis/scripts/hd_pack.py upscale
```

Extraction writes source images under `build/hdpack`; upscaling writes hash-named replacements under `dist/DrakanPC/textures`. Upscaling is resumable. Time and output can vary with hardware and drivers.

Enable **HD texture replacement** in PC Settings to use the pack; disable it to use native textures. Existing texture packs are preserved by the canonical game builder. Linux and browser can reuse the shared pack through their supported replacement path, but are not at full Windows renderer parity.

![Side-by-side texture comparison, original on left and HD replacements on right](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-textures.jpg)

## Upscaled movies

The Windows HD movie implementation replaces the intro, outro and logo with **4× AI-upscaled frames** generated from your ISO. It requires FFmpeg with IPU decoding and a Real-ESRGAN installation. This is a separate, potentially lengthy processing step; the normal installer build does not automatically create the movie pack.

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

Set `hd_movies = 1` to enable it again. Missing HD frames fall back to the original video. Upscaling does not add new animation frames or change the original movie cadence.

![A dragon illustration in the locally upscaled intro movie](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-movie.jpg)

## Dialogue versus narration subtitles

**Gameplay dialogue:** the original game already supplies subtitles. Use **Options → Audio → Show Subtitles** (or the corresponding game-sound setting in the modern UI).

**Intro and ending narration:** the PC port adds an SRT subtitle overlay because the PS2 narration has no subtitle text. Matching files must exist locally. For English, place your local files in:

```text
local/subtitles/intro_english.srt
local/subtitles/outro_english.srt
```

The canonical builder copies these into the playable `subtitles` folder and preserves existing subtitles across rebuilds. Other languages use the same `<intro|outro>_<language>.srt` naming pattern. Missing translations are not generated automatically.

Enable **Narration subtitles** in PC Settings and ensure the game's **Show Subtitles** option is enabled. Subtitle size can follow the window or use an explicit scale. The [subtitle reference](../reference/subtitles.md) contains implementation and timing notes; its earlier proposal sections are historical.

![Intro narration with the added subtitle overlay](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/narration-subtitles.jpg)

## DLSS and ray tracing

The installer offers an optional NVIDIA DLSS download, subject to NVIDIA's licence and compatible RTX hardware. A source build can use a local SDK as described in [Build from source](../../BUILDING.md#optional-local-nvidia-build). DXR water and shadows require compatible hardware and a build with the required shader tools.

Use [Graphics & PC settings](settings.md) to turn enhancements on or off. Advanced Windows renderer features do not automatically carry over to Linux or browser builds.
