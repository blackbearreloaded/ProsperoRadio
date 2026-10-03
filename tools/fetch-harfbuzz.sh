#!/usr/bin/env bash
# ProsperoRadio - Pinned HarfBuzz source download.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Downloads and verifies the pinned HarfBuzz release into .deps and prints its
# directory. The text engine compiles it from its single-file source.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=12.3.2
archive_sha256=6f6db164359a2da5a84ef826615b448b33e6306067ad829d85d5b0bf936f1bb8
url="https://github.com/harfbuzz/harfbuzz/releases/download/$version/harfbuzz-$version.tar.xz"

cache="$root/.deps"
prefix="$cache/harfbuzz-$version"
archive="$cache/harfbuzz-$version.tar.xz"
mkdir -p "$cache"

if [[ ! -f $prefix/src/harfbuzz.cc || ! -f $prefix/COPYING ]]; then
    printf '==> [harfbuzz] Downloading HarfBuzz %s\n' "$version" >&2
    curl -fL --retry 3 -o "$archive.part" "$url"
    mv -- "$archive.part" "$archive"
    sha256sum --check --status <<<"$archive_sha256  $archive" || {
        echo "HarfBuzz archive checksum mismatch" >&2
        exit 2
    }
    rm -rf -- "$prefix"
    tar -xJf "$archive" -C "$cache" "harfbuzz-$version/src" "harfbuzz-$version/COPYING"
    rm -f -- "$archive"
fi
printf '%s\n' "$prefix"
