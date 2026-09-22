#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
build_dir="${1:-builddir}"
if [[ -f "$build_dir/meson-private/coredata.dat" ]]; then
    meson setup --reconfigure "$build_dir"
else
    meson setup "$build_dir"
fi
meson compile -C "$build_dir"
meson test -C "$build_dir" --print-errorlogs
meson install -C "$build_dir"
