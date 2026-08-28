# Getting started

This repository is PSRadio on top of a clean clone of the native-app
boilerplate. Build it from Linux, WSL, or Linux CI; do not use a proprietary
SDK checkout or copy runtime libraries into the repository.

## 1. Install host tools

On Ubuntu, Debian, or WSL:

    sudo apt update
    sudo apt install curl git make pkg-config python3 python3-venv tar unzip wget \
      clang-18 clang-format-18 clang-tidy-18 lld-18 libsqlite3-dev

Install .NET SDK 8 or newer only when building an FFPKG package. The FFPFSC
target uses the pinned MkPFS bootstrapper instead.

## 2. Clone and inspect

    git clone git@github.com:blackbearreloaded/psradio.git
    cd psradio
    make doctor

The doctor command is read-only. It reports missing requirements without
downloading or modifying a global SDK installation.

## 3. Build

    make

The first build verifies and caches the public PS5 Payload SDK, zlib, the
PacBrew SQLite port, and the clean-room runtime inputs below ignored .deps/.
It then creates:

    dist/PPSA99001/
      eboot.bin
      sce_module/libc.prx
      sce_sys/param.json
      assets/ui/

The title is PSRadio, title ID PPSA99001, and a Game-category application. The
category is intentional for Remote Play/Chiaki-ng audio behaviour.

## 4. Run host checks

    make test
    make lint
    make check

The host checks do not need a PS5 or public Radio Browser network access.
They cover the application text/UI contracts, 16 codec/catalogue regressions,
metadata, shell deployment dry runs, formatting, and static analysis.

## 5. Build release forms

    make packages

This produces:

    dist/PPSA99001/           development title folder
    dist/PPSA99001.ffpkg      UFS2 package
    dist/PPSA99001.ffpfsc     compressed package

The full title folder is the preferred development artifact. Never deploy only
eboot.bin.

## 6. Deploy a development folder

With an already-running console FTP service:

    make deploy PS5_HOST=192.168.4.30 DEPLOY_FORMAT=folder

The deployment tool sends only this title's files below /data/homebrew and
publishes critical files last. It does not launch the title or modify Shell
registration. Use the current project console protocol for launch, logs,
screenshots, Remote Play cleanup, and any shared-console coordination.

## Version and title identity

sce_sys/param.json is the single version source. Its contentVersion appears in
the packaged metadata, PSRadio top bar, Git tag, and GitHub Release. Use the
exact PS5 format NN.NNN.NNN, with no v prefix.

Keep PPSA99001 stable for PSRadio updates. Increment contentVersion before
creating a release tag:

    git tag 01.000.003
    git push origin main 01.000.003

GitHub Actions rejects a tag that does not exactly match contentVersion and
publishes the verified title folder archive, FFPKG, FFPFSC, runtime companion,
and checksums.

For source architecture, dependencies, and PS5-only validation, continue with
[Architecture](ARCHITECTURE.md), [Template port notes](TEMPLATE_PORT.md),
[Testing](TESTING.md), and [Deployment](DEPLOYMENT.md).
