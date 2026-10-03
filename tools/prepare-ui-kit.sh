#!/usr/bin/env bash
# ProsperoRadio - Pinned ps5-homebrew-ui download and staging.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches the pinned commit of the interface kit into .deps/ui-kit and stages
# the sources the app compiles (ui-kit/kit-files.txt), with the app's
# overrides and patches laid over them, under .deps/ui-kit/stage. Prints the
# staged directory. UI_KIT_DIR uses a local kit checkout instead.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
commit=327024dda0911bd1d03bf8beec80ba735b0977ea
url=https://github.com/blackbearreloaded/ps5-homebrew-ui.git

cache="$root/.deps/ui-kit"
stage="$cache/stage"
mkdir -p "$cache"

if [[ -n ${UI_KIT_DIR:-} ]]; then
    kit=$(realpath -e -- "$UI_KIT_DIR")
    identity="local $kit $(git -C "$kit" rev-parse HEAD 2>/dev/null || echo unknown) $(date +%s)"
else
    kit="$cache/checkout"
    if [[ $(git -C "$kit" rev-parse HEAD 2>/dev/null || true) != "$commit" ]]; then
        printf '==> [ui-kit] Fetching ps5-homebrew-ui %s\n' "${commit:0:7}" >&2
        rm -rf -- "$kit"
        git init --quiet "$kit"
        git -C "$kit" remote add origin "$url"
        git -C "$kit" fetch --quiet --depth 1 origin "$commit"
        git -C "$kit" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    fi
    [[ $(git -C "$kit" rev-parse HEAD) == "$commit" ]] || {
        echo "ps5-homebrew-ui checkout is not the pinned commit" >&2
        exit 2
    }
    identity="pinned $commit"
fi

# Staged again only when the kit or what is laid over it changed.
stamp=$( { printf '%s\n' "$identity"; cd "$root/ui-kit" && find . -type f -print0 | sort -z |
    xargs -0 sha256sum; } | sha256sum | cut -d' ' -f1)
if [[ ! -f $stage/.stamp || $(< "$stage/.stamp") != "$stamp" ]]; then
    rm -rf -- "$stage.tmp"
    mkdir -p "$stage.tmp/src" "$stage.tmp/assets/fonts"
    while IFS= read -r relative; do
        [[ -n $relative ]] || continue
        [[ $relative =~ ^[A-Za-z0-9_./-]+$ && $relative != *..* && -f $kit/src/$relative ]] || {
            echo "ui-kit/kit-files.txt names a file the kit does not have: $relative" >&2
            exit 2
        }
        mkdir -p "$stage.tmp/src/$(dirname "$relative")"
        cp -- "$kit/src/$relative" "$stage.tmp/src/$relative"
    done < "$root/ui-kit/kit-files.txt"
    cp -a -- "$root/ui-kit/overrides/." "$stage.tmp/src/"
    for patch in "$root"/ui-kit/patches/*.patch; do
        patch --quiet --directory "$stage.tmp/src" --strip 1 --no-backup-if-mismatch < "$patch"
    done
    for font in inter-regular inter-semibold montserrat-medium dejavu-sans-mono; do
        cp -- "$kit/assets/fonts/$font.huifont" "$stage.tmp/assets/fonts/"
    done
    cp -- "$kit/assets/fonts/Inter-LICENSE.txt" "$kit/assets/fonts/Montserrat-LICENSE.txt" \
        "$kit/assets/fonts/DejaVu-LICENSE.txt" "$stage.tmp/assets/fonts/"
    printf '%s\n' "$stamp" > "$stage.tmp/.stamp"
    rm -rf -- "$stage"
    mv -- "$stage.tmp" "$stage"
fi
printf '%s\n' "$stage"
