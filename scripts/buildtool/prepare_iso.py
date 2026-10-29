#!/usr/bin/env python3
"""Extract only the supported boot ELF from a user-owned ISO9660 disc image.

The disc region (PAL SCES_500.06 or USA SCUS_971.28) is chosen from SYSTEM.CNF BOOT2 and the ELF must match the
profile in regions.json byte for byte (SHA-256).
"""

import argparse
import hashlib
import json
import re
import struct
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
SECTOR = 2048


class Iso:
    def __init__(self, path):
        self.file = path.open("rb")
        self.length = path.stat().st_size
        for sector in range(16, 64):
            pvd = self.read(sector * SECTOR, SECTOR)
            if pvd[1:6] != b"CD001" or pvd[6] != 1:
                raise ValueError("Not a 2048-byte-sector ISO9660 image")
            if pvd[0] == 1:
                if struct.unpack_from("<H", pvd, 128)[0] != SECTOR:
                    raise ValueError("Unsupported ISO logical sector size")
                self.root = self.record(pvd[156 : 156 + pvd[156]])
                return
            if pvd[0] == 255:
                break
        raise ValueError("No primary volume descriptor")

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.length:
            raise ValueError("ISO extent outside file")
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError("Truncated ISO read")
        return data

    @staticmethod
    def record(data):
        if len(data) < 34 or 33 + data[32] > len(data):
            raise ValueError("Invalid ISO directory record")
        if data[25] & 128 or data[26] or data[27] or data[1]:
            raise ValueError("Multi-extent, interleaved or extended-attribute ISO file unsupported")
        lba, size = struct.unpack_from("<I", data, 2)[0], struct.unpack_from("<I", data, 10)[0]
        if lba != struct.unpack_from(">I", data, 6)[0] or size != struct.unpack_from(">I", data, 14)[0]:
            raise ValueError("ISO endian fields disagree")
        name = data[33 : 33 + data[32]].decode("ascii").split(";", 1)[0].upper()
        return name, lba, size, bool(data[25] & 2)

    def lookup(self, path):
        node = self.root
        for part in path.replace("\\", "/").split("/"):
            if not part or part in (".", "..") or not node[3]:
                raise ValueError("Invalid ISO boot path")
            if node[2] > 16 * 1024 * 1024:
                raise ValueError("ISO directory too large")
            directory = self.read(node[1] * SECTOR, node[2])
            offset, found = 0, None
            while offset < len(directory):
                size = directory[offset]
                if size == 0:
                    offset = (offset // SECTOR + 1) * SECTOR
                    continue
                if offset + size > len(directory) or offset % SECTOR + size > SECTOR:
                    raise ValueError("Truncated ISO directory entry")
                record = self.record(directory[offset : offset + size])
                if record[0] == part.upper():
                    if found is not None:
                        raise ValueError("Ambiguous ISO filename")
                    found = record
                offset += size
            if found is None:
                raise ValueError(f"ISO file not found: {part}")
            node = found
        return node

    def file_bytes(self, path, maximum):
        node = self.lookup(path)
        if node[3] or node[2] > maximum:
            raise ValueError("Expected a bounded regular ISO file")
        return self.read(node[1] * SECTOR, node[2])


def load_regions():
    return json.loads((HERE / "regions.json").read_text(encoding="utf-8"))


def region_profile(region=None):
    regions = load_regions()
    region = region or regions["default"]
    if region not in regions["regions"]:
        raise ValueError(f"Unknown disc region {region!r}")
    return region, regions["regions"][region]


def region_for_boot(boot_name):
    """The region whose boot executable SYSTEM.CNF names, or None."""
    for region, profile in load_regions()["regions"].items():
        if profile["boot"] == boot_name.strip().upper():
            return region, profile
    return None


def configure(template, elf, generated, repo, profile=None):
    profile = profile or region_profile()[1]
    text = template.read_text(encoding="utf-8-sig")
    parsed = tomllib.loads(text)
    general = parsed.get("general", {})
    if not isinstance(general.get("stubs"), list) or not general.get("functions_per_file"):
        raise ValueError("Supply the validated Drakan config template, including stub bindings and batching")
    values = {
        "input": elf.as_posix(),
        "output": generated.as_posix(),
        "functions_csv": (repo / profile["functions_csv"]).as_posix(),
        "jump_tables_csv": (repo / profile["jump_tables_csv"]).as_posix(),
        "ghidra_output": "",
    }
    for key in ("functions_csv", "jump_tables_csv"):
        if not Path(values[key]).is_file():
            raise ValueError(f"Missing required metadata: {values[key]}")
    match = re.search(r"(?ms)^\[general\][^\n]*\n(.*?)(?=^\[|\Z)", text)
    if not match:
        raise ValueError("Config has no [general] section")
    body = match[1]
    for key, value in values.items():
        line = key + " = " + json.dumps(value)
        pattern = rf"(?m)^\s*{key}\s*=.*$"
        if re.search(pattern, body):
            body = re.sub(pattern, lambda _: line, body)
        else:
            body = line + "\n" + body
    result = text[: match.start(1)] + body + text[match.end(1) :]
    tomllib.loads(result)
    return result


def region_template(template, repo, profile):
    """The default template (configs/drakan.toml, PAL) follows the disc region; an explicit other file is kept."""
    if template is not None and Path(template).resolve() == (repo / "configs/drakan.toml").resolve():
        return repo / profile["config"]
    return template


def detect_region(iso):
    """(region, profile) of the executable SYSTEM.CNF boots; ValueError for any other disc."""
    cnf = iso.file_bytes("SYSTEM.CNF", 16384).decode("ascii")
    boot = re.search(r"(?im)^\s*BOOT2\s*=\s*cdrom0:[\\/]+([^;\r\n]+)(?:;\d+)?\s*$", cnf)
    found = region_for_boot(boot[1]) if boot else None
    if not found:
        supported = ", ".join(p["label"] for p in load_regions()["regions"].values())
        raise ValueError(f"Unsupported disc: SYSTEM.CNF does not boot a verified executable ({supported})")
    return found


def refresh_config(target, template, repo, iso_region=None):
    """Rewrite config.toml from the template, leaving the file untouched when nothing changed."""
    manifest = target / "local-input.json"
    if not manifest.is_file():
        raise ValueError(f"{target} was not prepared by a full build; run one first")
    region, profile = region_profile(json.loads(manifest.read_text(encoding="utf-8")).get("region"))
    if iso_region is not None and iso_region != region:
        raise ValueError(
            f"{target} holds a {profile['label']} build but the ISO is {region_profile(iso_region)[1]['label']}; "
            "build the other region into a fresh WorkDir"
        )
    if not (target / profile["boot"]).is_file():
        raise ValueError(f"{target} was not prepared by a full build; run one first")
    if template is None:
        raise ValueError("--refresh-config needs --config-template")
    template = region_template(template, repo, profile)
    config = configure(template, target / profile["boot"], target / "generated", repo, profile)
    path = target / "config.toml"
    if path.is_file() and path.read_text(encoding="utf-8") == config:
        return False
    path.write_text(config, encoding="utf-8")
    print(f"Updated {path}")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--output-directory", type=Path)
    ap.add_argument("--config-template", type=Path)
    ap.add_argument("--repo-root", type=Path, default=HERE.parents[1])
    ap.add_argument(
        "--refresh-config",
        action="store_true",
        help="re-render config.toml in an existing output directory (incremental builds); nothing else is touched",
    )
    ap.add_argument("--detect", action="store_true", help="print the disc region id (pal / usa) and exit")
    args = ap.parse_args()
    if args.detect:
        iso = Iso(args.iso.resolve())
        try:
            print(detect_region(iso)[0])
        finally:
            iso.file.close()
        return
    if args.output_directory is None:
        ap.error("--output-directory is required")
    target = args.output_directory.resolve()
    if args.refresh_config:
        iso = Iso(args.iso.resolve())
        try:
            iso_region = detect_region(iso)[0]
        finally:
            iso.file.close()
        refresh_config(target, args.config_template, args.repo_root.resolve(), iso_region)
        return
    if target.exists():
        raise ValueError("Output directory already exists; choose a fresh private build directory")
    iso = Iso(args.iso.resolve())
    try:
        region, profile = detect_region(iso)
        data = iso.file_bytes(profile["boot"], 64 * 1024 * 1024)
    finally:
        iso.file.close()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 8:
        raise ValueError("Boot file is not a little-endian ELF32 MIPS executable")
    if struct.unpack_from("<I", data, 24)[0] != 0x100008:
        raise ValueError("Unexpected ELF entry point")
    sha = hashlib.sha256(data).hexdigest()
    if sha != profile["elf_sha256"]:
        raise ValueError(f"ELF hash differs from the verified {profile['boot']} revision; no output was written")
    template = region_template(args.config_template, args.repo_root.resolve(), profile)
    config = (
        configure(template, target / profile["boot"], target / "generated", args.repo_root.resolve(), profile)
        if template
        else None
    )
    target.mkdir(parents=True)
    (target / profile["boot"]).write_bytes(data)
    if config is not None:
        (target / "config.toml").write_text(config, encoding="utf-8")
    manifest = {
        "region": region,
        "boot": profile["boot"],
        "serial": profile["serial"],
        "elf_sha256": sha,
        "elf_size": len(data),
        "private_local_output": True,
        "config_created": config is not None,
    }
    (target / "local-input.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared private local input: {target} ({profile['label']}; no disc assets copied)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, UnicodeError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
