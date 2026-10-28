# HD textures, video & subtitles

All optional. Textures and movies are generated from your own disc; narration subtitles are files you supply. None of them are distributed here.

## HD textures

The Windows renderer supports replacement textures as well as live texture upscaling. To generate the AI pack from a source checkout, follow the [HD texture reference](../reference/hd-textures.md). The workflow needs NumPy, Pillow and a configured `realesrgan-ncnn-vulkan` upscaler with its models.

```powershell
python -I analysis/scripts/hd_pack.py extract --iso "D:/MyDiscs/Drakan.iso"
python -I analysis/scripts/hd_pack.py upscale
```

Extraction writes to `build/hdpack`; upscaling writes replacements to `dist/DrakanPC/textures` and can be resumed.

Turn **HD texture replacement** on or off in PC Settings. Rebuilds keep the pack. Linux and browser builds can use it too.

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
