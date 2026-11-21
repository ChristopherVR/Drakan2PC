"""Check for a Linux game update, then verify and rebuild a release from the local disc."""

import filecmp
import hashlib
import json
import os
import re
import select
import shutil
import ssl
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

RELEASES = "https://github.com/ChristopherVR/Drakan2PC/releases"
ASSET = "DrakanPC-Builder.zip"
TAG = re.compile(r"v(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})")
ASSET_HOSTS = {
    "release-assets.githubusercontent.com",
    "objects.githubusercontent.com",
    "github-releases.githubusercontent.com",
}
MAX_ARCHIVE = 1024**3
MAX_EXTRACTED = 2 * MAX_ARCHIVE
CHUNK = 1024**2
PLAY = Path(__file__).resolve().parent


def release_number(tag):
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError("Expected a stable vX.Y.Z release tag.")
    return tuple(int(part) for part in match.groups())


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        return None


class AssetRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        url = urllib.parse.urlsplit(newurl)
        if (
            url.scheme != "https"
            or url.hostname not in ASSET_HOSTS
            or url.port not in (None, 443)
            or url.username is not None
            or url.password is not None
            or url.fragment
        ):
            raise ValueError("GitHub returned an unexpected download location.")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def request(url, redirect, method="GET", *, timeout=30):
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ssl.create_default_context()), redirect)
    return opener.open(urllib.request.Request(url, headers={"User-Agent": "DrakanPC"}, method=method), timeout=timeout)


def latest_release(*, timeout=30):
    try:
        with request(f"{RELEASES}/latest", NoRedirect(), "HEAD", timeout=timeout):
            raise ValueError("GitHub did not return a release location.")
    except urllib.error.HTTPError as response:
        with response:
            location = response.headers.get("Location", "")
            prefix = f"{RELEASES}/tag/"
            if response.code != 302 or not location.startswith(prefix):
                raise ValueError("GitHub did not return the expected release location.") from response
    tag = location.removeprefix(prefix)
    release_number(tag)
    return tag


def expected_digest(checksums):
    matches = [re.fullmatch(r"([0-9a-fA-F]{64})[ \t]+\*?" + re.escape(ASSET), line) for line in checksums.splitlines()]
    digests = [match[1].lower() for match in matches if match]
    if len(digests) != 1:
        raise ValueError("The release did not publish an unambiguous builder ZIP checksum.")
    return digests[0]


def download_archive(tag, archive):
    base = f"{RELEASES}/download/{tag}"
    redirect = AssetRedirect()
    with request(f"{base}/SHA256SUMS.txt", redirect) as response:
        checksums = response.read(65537)
    if len(checksums) > 65536:
        raise ValueError("The release checksum file is too large.")
    expected = expected_digest(checksums.decode("ascii"))
    digest = hashlib.sha256()
    size = 0
    deadline = time.monotonic() + 1800
    with request(f"{base}/{ASSET}", redirect) as response, archive.open("xb") as output:
        while chunk := response.read(CHUNK):
            size += len(chunk)
            if size > MAX_ARCHIVE or time.monotonic() > deadline:
                raise ValueError("The release download exceeded its size or time limit.")
            digest.update(chunk)
            output.write(chunk)
    if digest.hexdigest() != expected:
        raise ValueError("The downloaded builder ZIP did not match the release checksum.")


def extract_builder(archive, output, tag):
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > 20_000 or sum(entry.file_size for entry in entries) > MAX_EXTRACTED:
            raise ValueError("The builder ZIP expands beyond its size limit.")
        paths = set()
        root = None
        for entry in entries:
            name = PurePosixPath(entry.filename)
            mode = stat.S_IFMT(entry.external_attr >> 16)
            if (
                name.is_absolute()
                or not name.parts
                or ".." in name.parts
                or "\\" in entry.filename
                or mode not in (0, stat.S_IFREG, stat.S_IFDIR)
                or (root is not None and root != name.parts[0])
                or name in paths
            ):
                raise ValueError(f"Unsafe builder ZIP member: {entry.filename!r}")
            root = name.parts[0]
            paths.add(name)
            if len(name.parts) == 1 and not entry.is_dir():
                raise ValueError("The builder ZIP must contain one top-level folder.")
            target = output.joinpath(*name.parts[1:])
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as data, target.open("xb") as file:
                    shutil.copyfileobj(data, file, CHUNK)
    if read_tag(output) != tag or not (output / "scripts/buildtool/bootstrap_linux.py").is_file():
        raise ValueError("The builder ZIP does not contain the requested Linux release.")


def read_tag(folder):
    tag = (folder / "DrakanPC-release.txt").read_text(encoding="ascii").strip()
    release_number(tag)
    return tag


def builder_release_status(folder):
    try:
        installed = read_tag(folder)
    except (OSError, ValueError):
        return None
    try:
        latest = latest_release(timeout=3)
    except (OSError, ValueError):
        return {"state": "offline", "installed": installed, "latest": None, "url": None}
    installed_number, published_number = release_number(installed), release_number(latest)
    if installed_number < published_number:
        state = "behind"
    elif installed_number > published_number:
        state = "ahead"
    else:
        state = "current"
    return {"state": state, "installed": installed, "latest": latest, "url": f"{RELEASES}/tag/{latest}"}


def game_exit_handle(play, pid):
    handle = os.pidfd_open(pid)
    try:
        if not (play / "drakan").samefile(f"/proc/{pid}/exe"):
            raise ValueError("The requesting process is not running this installation.")
        return handle
    except BaseException:
        os.close(handle)
        raise


def wait_for_game(handle):
    watcher = select.poll()
    watcher.register(handle, select.POLLIN)
    if not watcher.poll(60_000):
        raise RuntimeError("The game did not close. Close the game before starting another update.")


def managed_folder(play):
    location = play.with_name(f"{play.name}.drakan-update")
    location.mkdir(mode=0o700, exist_ok=True)
    if location.is_symlink() or not location.is_dir():
        raise ValueError("The Linux update folder must be an ordinary directory.")
    owner = location / "play-path.txt"
    if not owner.exists() and not any(location.iterdir()):
        try:
            with owner.open("x", encoding="utf-8") as file:
                file.write(str(play))
        except FileExistsError:
            pass
    if owner.is_symlink() or owner.read_text(encoding="utf-8") != str(play):
        raise ValueError("This Linux update folder belongs to another installation.")
    return location


def preserve_timestamps(candidate, current):
    for fresh in candidate.rglob("*"):
        old = current / fresh.relative_to(candidate)
        if fresh.is_file() and old.is_file() and not old.is_symlink() and filecmp.cmp(old, fresh, shallow=False):
            stamp = old.stat()
            os.utime(fresh, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))


def replace_sources(candidate, current, previous):
    if current.is_symlink() or (current.exists() and not current.is_dir()):
        raise ValueError("The Linux builder must be an ordinary directory.")
    if current.exists():
        preserve_timestamps(candidate, current)
        current.rename(previous)
    try:
        candidate.rename(current)
    except OSError:
        if previous.exists():
            previous.rename(current)
        raise


def work_folder(managed, old_elf, scratch):
    prior = Path(old_elf).parent
    if prior.parent == managed and prior.name.startswith("work-") and (prior / "local-input.json").is_file():
        return prior
    work = managed / "work"
    if work.exists() and not (work / "local-input.json").is_file():
        work = managed / f"work-{scratch.name}"
    return work


def display_progress(log):
    terminal = shutil.which("x-terminal-emulator")
    if terminal and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        try:
            subprocess.Popen(
                [terminal, "-e", "tail", f"--pid={os.getpid()}", "-n", "+1", "-f", str(log)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
        except OSError:
            pass


def notify(message):
    notifier = shutil.which("notify-send")
    if notifier and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        try:
            subprocess.run(
                [notifier, "Drakan PC update", message],
                check=False,
                timeout=5,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass


def install(play, tag, pid, ready):
    import fcntl  # Linux only. Keeping the import local lets Python tests import the archive helpers on Windows.

    old_tag = read_tag(play)
    if release_number(tag) <= release_number(old_tag):
        raise ValueError("The requested release is not newer than the installed game.")
    handle = game_exit_handle(play, pid)
    log_path = play.with_name(f"{play.name}.update.log")
    try:
        manager = managed_folder(play)
        with (manager / "update.lock").open("a") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise ValueError("Another update is already running for this installation.") from error
            log_fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
            if not stat.S_ISREG(os.fstat(log_fd).st_mode):
                os.close(log_fd)
                raise ValueError("The Linux update log must be an ordinary file.")
            os.ftruncate(log_fd, 0)
            with (
                os.fdopen(log_fd, "w", encoding="utf-8", buffering=1) as log,
                tempfile.TemporaryDirectory(dir=manager) as temporary,
            ):
                scratch = Path(temporary)
                candidate = scratch / "candidate"
                archive = scratch / ASSET
                try:
                    plan = json.loads((play / "drakan-update.json").read_text(encoding="utf-8"))
                    iso = Path(plan["iso"])
                    old_elf = Path(plan["elf"])
                    if not iso.is_absolute() or not iso.is_file():
                        raise ValueError(f"The saved disc image is unavailable: {iso}")
                    if not old_elf.is_absolute():
                        raise ValueError("The previous local build path is invalid.")
                    jobs = plan["jobs"]
                    if type(jobs) is not int or not 1 <= jobs <= 16:
                        raise ValueError("The saved build job count is invalid.")
                    print(f"Downloading Drakan PC {tag}…", file=log)
                    download_archive(tag, archive)
                    extract_builder(archive, candidate, tag)
                    archive.unlink()
                    print("Download verified. Waiting for the game to close…", file=log)
                    ready()
                    display_progress(log_path)
                    wait_for_game(handle)
                    source = manager / "source"
                    replace_sources(candidate, source, scratch / "old-source")
                    command = [
                        sys.executable,
                        source / "scripts/buildtool/bootstrap_linux.py",
                        iso,
                        "--work-dir",
                        work_folder(manager, old_elf, scratch),
                        "--play-dir",
                        play,
                        "--jobs",
                        str(jobs),
                        "--tool-cache",
                        manager / "tools",
                        "--installed-release",
                        old_tag,
                    ]
                    if plan.get("texture_dir"):
                        command += ["--texture-dir", plan["texture_dir"]]
                    print("Rebuilding from your local disc image…", file=log)
                    subprocess.run(
                        command, cwd=source, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, check=True
                    )
                    if read_tag(play) != tag:
                        raise RuntimeError("The local build finished without publishing the requested release.")
                    print(f"Drakan PC {tag} is ready. Open the game as usual.", file=log)
                    notify(f"Drakan PC {tag} is ready. Open the game as usual.")
                except (
                    OSError,
                    ValueError,
                    RuntimeError,
                    subprocess.SubprocessError,
                    zipfile.BadZipFile,
                    KeyError,
                    TypeError,
                ) as error:
                    print(f"Update failed: {error}", file=log)
                    notify(f"The rebuild did not finish. For details, see {log_path}")
                    raise
    finally:
        os.close(handle)


def main(args):
    usage = "Usage: drakan-update.py check | vX.Y.Z game-pid"
    if args in (["-h"], ["--help"]):
        print(usage)
        return 0
    try:
        if args == ["check"]:
            print(latest_release(), flush=True)
        elif len(args) == 2 and args[1].isdigit() and int(args[1]) > 1:
            install(PLAY, args[0], int(args[1]), lambda: print("READY", flush=True))
        else:
            raise ValueError(usage)
    except (
        OSError,
        ValueError,
        RuntimeError,
        subprocess.SubprocessError,
        zipfile.BadZipFile,
        KeyError,
        TypeError,
    ) as error:
        print(f"Drakan update: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
