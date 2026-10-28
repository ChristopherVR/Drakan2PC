#!/usr/bin/env bash
set -euo pipefail
if ! command -v python3 >/dev/null 2>&1; then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo 'Install Python 3 to bootstrap this Linux build.' >&2
        exit 1
    fi
    privilege=()
    if (( EUID != 0 )); then privilege=(sudo); fi
    "${privilege[@]}" apt-get update
    "${privilege[@]}" apt-get install -y python3 ca-certificates
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$script_dir/bootstrap_linux.py" "$@"
