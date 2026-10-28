"""Read-only named SPU voice / mixed-WAV evidence; never a listening verdict.

Run combat_encounter_run.ps1 with -AudioDump -SoundLog 3 and
DRAKAN_HLE_LOG=100000, then pass its evidence directory and --sound-root
pointing to privately extracted SDBs. Only the KON SPU frame counter is used
to locate WAV samples: input, sound-manager and host clocks differ.
"""

import argparse
import array
import json
import math
import pathlib
import re
import struct
import sys
import wave


def report(directory, sound_root, name_pattern):
    log = (directory / "stderr.log").read_text(errors="replace")
    with wave.open(str(directory / "audio.wav")) as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (2, 2, 48000):
            raise ValueError("Expected 48 kHz stereo PCM16 DRAKAN_AUDIO_DUMP")
        frames = wav.getnframes()
        samples = array.array("h", wav.readframes(frames))
    if sys.byteorder != "little":
        samples.byteswap()
    if len(samples) != frames * 2:
        raise ValueError("Truncated WAV payload")

    def window(start, end):
        lo, hi = max(0, start), min(frames, end)
        if hi <= lo:
            return {"frames": 0, "rms": None, "peak": None}
        values = samples[lo * 2 : hi * 2]
        return {
            "frames": hi - lo,
            "rms": math.sqrt(sum(v * v for v in values) / len(values)) / 32768,
            "peak": max(abs(v) for v in values) / 32768,
        }

    cache = {}
    missing_sdbs = set()

    def sample_name(resource, number):
        if sound_root is None:
            return None
        if resource not in cache:
            path = (sound_root / resource.replace("\\", "/")).resolve()
            if not path.is_relative_to(sound_root.resolve()):
                raise ValueError("SDB resource escapes sound root")
            names = {}
            if path.is_file():
                data = path.read_bytes()
                magic, version, index, count = struct.unpack_from("<4sHII", data)
                if magic != b"SRSC" or index + count * 14 > len(data):
                    raise ValueError(f"Invalid SDB index: {resource}")
                for i in range(count):
                    kind, num, sub, offset, size = struct.unpack_from("<HHHII", data, index + i * 14)
                    if kind == 0x302:
                        if size < 2 or offset + size > index:
                            raise ValueError(f"Invalid SDB descriptor: {resource}/{num}")
                        length = struct.unpack_from("<H", data, offset)[0]
                        if length > size - 2:
                            raise ValueError(f"Invalid SDB name: {resource}/{num}")
                        names[num] = data[offset + 2 : offset + 2 + length].decode("latin1").rstrip("\0")
            else:
                missing_sdbs.add(resource)
            cache[resource] = names
        return cache[resource].get(number)

    # Preserve log order, including multiple interleaved records on one line.
    events = re.compile(
        r"\[hle\] resource: (?P<resource>\S+?):? 0304/(?P<sample>\d+)/-1 \+0 len \d+ -> type1 dest (?P<dest>[0-9a-f]+)"
        r"|\[spu (?P<time>[0-9.]+)\]\s+KON c(?P<core>\d)\.v(?P<voice>\d+) ssa=(?P<ssa>[0-9a-f]+) .*?vol=(?P<left>[0-9a-f]+)/(?P<right>[0-9a-f]+) .*? f=(?P<frame>\d+)"
        r"|\[spu [0-9.]+\] setParam reg=0(?P<channel>[01]) core(?P<vcore>\d) voice(?P<vvoice>\d+) = 0x(?P<volume>[0-9a-f]+)"
        r"|\[spu [0-9.]+\] KOFF core(?P<offcore>\d) mask=(?P<mask>[0-9a-f]+)"
    )
    uploads, active, voices = {}, {}, []
    for match in events.finditer(log):
        e = match.groupdict()
        if e["resource"] is not None:
            uploads[int(e["dest"], 16)] = (e["resource"], int(e["sample"]))
        elif e["core"] is not None:
            key = (int(e["core"]), int(e["voice"]))
            active.pop(key, None)
            address = int(e["ssa"], 16)
            if address == 0x5000:  # Park/silence voice, not an attack.
                continue
            resource, number = uploads.get(address, (None, None))
            name = sample_name(resource, number) if resource else None
            frame = int(e["frame"])
            voice = {
                "voice": f"c{key[0]}.v{key[1]}",
                "spu_frame": frame,
                "wav_seconds": frame / 48000,
                "ssa": hex(address),
                "resource": resource,
                "sample": number,
                "name": name,
                "max_volume_word_lr": [int(e["left"], 16), int(e["right"], 16)],
                "volume_writes": 0,
                "mix_before": window(frame - 14400, frame),
                "mix_after": window(frame + 2400, frame + 16800),
            }
            voices.append(voice)
            active[key] = voice
        elif e["channel"] is not None:
            voice = active.get((int(e["vcore"]), int(e["vvoice"])))
            if voice is not None:
                channel = int(e["channel"])
                voice["max_volume_word_lr"][channel] = max(voice["max_volume_word_lr"][channel], int(e["volume"], 16))
                voice["volume_writes"] += 1
        else:
            core, mask = int(e["offcore"]), int(e["mask"], 16)
            for key in list(active):
                if key[0] == core and mask & (1 << key[1]):
                    del active[key]
    if not voices:
        raise ValueError("No non-park KON records; enable SoundLog 3")
    selected = [v for v in voices if name_pattern.search(v["name"] or "")]
    return {
        "wav_frames": frames,
        "wav_seconds": frames / 48000,
        "wasapi_output_observed": "[audio] WASAPI shared:" in log,
        "nonpark_keyons": len(voices),
        "named_keyons": sum(v["name"] is not None for v in voices),
        "missing_sdbs": sorted(missing_sdbs),
        "selected_voices": selected,
        "limit": "Volume words and mixed WAV windows establish output evidence, not isolated attack audibility or subjective parity. Missing names are unavailable, not silent voices.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--sound-root", type=pathlib.Path)
    parser.add_argument("--names", default=r"Swish|Rynn Attack|Bow|Arrow|Block|Parry")
    args = parser.parse_args()
    try:
        result = report(args.directory, args.sound_root, re.compile(args.names, re.IGNORECASE))
    except (OSError, ValueError, struct.error, wave.Error) as error:
        print(json.dumps({"available": False, "error": str(error)}))
        raise SystemExit(1) from error
    print(json.dumps(result, indent=2, allow_nan=False))
