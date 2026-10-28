#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
# The launch server only needs the system Python standard library.
exec python3 ./serve_web.py --open "$@"
