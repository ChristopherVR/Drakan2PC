#!/bin/sh
# Run from the play folder so native saves and relative asset paths stay together.
CDPATH= cd -- "$(dirname -- "$0")" || exit 1
exec ./drakan "$@"
