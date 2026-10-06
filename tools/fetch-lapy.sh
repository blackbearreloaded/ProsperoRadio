#!/usr/bin/env bash
# ProsperoRadio - Pinned upstream Lapy helper sources and their build inputs.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches into .deps what tools/build-lapy-helper.py builds from: the pinned
# PS5-Lapy-JB-Daemon commit, the PS5 Payload SDK release that commit is written
# for, and the single-header logging client its build includes. The pins are
# the ones ProsperoEden validated.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps="$root/.deps"
mkdir -p "$deps"

lapy_commit=c3bdfe3a399366d8eacfc580f20b19fd03b16ca3
lapy_url=https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon.git
lapy="$deps/PS5-Lapy-JB-Daemon-${lapy_commit:0:7}"
if [[ $(git -C "$lapy" rev-parse HEAD 2>/dev/null || true) != "$lapy_commit" ]]; then
    printf '==> [lapy] Fetching PS5-Lapy-JB-Daemon %s\n' "${lapy_commit:0:7}" >&2
    rm -rf -- "$lapy"
    git init --quiet "$lapy"
    git -C "$lapy" remote add origin "$lapy_url"
    git -C "$lapy" fetch --quiet --depth 1 origin "$lapy_commit"
    git -C "$lapy" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
fi
[[ $(git -C "$lapy" rev-parse HEAD) == "$lapy_commit" ]] || {
    echo "PS5-Lapy-JB-Daemon checkout is not the pinned commit" >&2
    exit 2
}

# The pinned helper uses v0.42's 32-byte credential attributes and firmware layouts.
sdk_sha256=8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da
sdk_url=https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip
sdk="$deps/lapy-ps5-payload-sdk-v0.42"
if [[ ! -x $sdk/bin/prospero-clang ]]; then
    printf '==> [lapy] Downloading PS5 Payload SDK v0.42\n' >&2
    archive="$deps/lapy-ps5-payload-sdk-v0.42.zip"
    curl -fL --retry 3 -o "$archive.part" "$sdk_url"
    mv -- "$archive.part" "$archive"
    sha256sum --check --status <<<"$sdk_sha256  $archive" || {
        echo "PS5 Payload SDK v0.42 archive checksum mismatch" >&2
        exit 2
    }
    rm -rf -- "$sdk" "$sdk.tmp"
    mkdir -p "$sdk.tmp"
    unzip -q "$archive" -d "$sdk.tmp"
    # The archive holds one top-level folder.
    mv -- "$sdk.tmp"/*/ "$sdk"
    rm -rf -- "$sdk.tmp" "$archive"
    chmod +x "$sdk"/bin/* 2>/dev/null || true
fi
[[ -x $sdk/bin/prospero-clang ]] || { echo "PS5 Payload SDK v0.42 is incomplete" >&2; exit 2; }

log_sha256=394af67d0f8b60b3335deb53396e52855ea2daa50ca914a456ea7663f48900c6
log_url=https://raw.githubusercontent.com/mpereiraesaa/ps5-agc-gears/1ae1f9182abd2770c131b97419034fb85173c2dc/native/ps5log/ps5log.h
log="$deps/lapy-ps5log-1ae1f918"
if ! sha256sum --check --status <<<"$log_sha256  $log/ps5log.h" 2>/dev/null; then
    printf '==> [lapy] Downloading ps5log.h\n' >&2
    mkdir -p "$log"
    curl -fL --retry 3 -o "$log/ps5log.h.part" "$log_url"
    sha256sum --check --status <<<"$log_sha256  $log/ps5log.h.part" || {
        echo "ps5log.h checksum mismatch" >&2
        exit 2
    }
    mv -- "$log/ps5log.h.part" "$log/ps5log.h"
fi
printf '%s\n' "$lapy"
