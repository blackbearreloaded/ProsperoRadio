# PS5 Radio

[![Build](https://github.com/blackbearreloaded/ps5-radio/actions/workflows/tooling.yml/badge.svg)](https://github.com/blackbearreloaded/ps5-radio/actions/workflows/tooling.yml)
[![Latest release](https://img.shields.io/github/v/release/blackbearreloaded/ps5-radio?display_name=tag)](https://github.com/blackbearreloaded/ps5-radio/releases/latest)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

PS5 Radio is a native PlayStation 5 Internet-radio application. It browses and
searches the public Radio Browser catalogue, keeps a resilient disk-backed
catalogue on the console, and plays supported live streams through the PS5
audio stack. The fixed, controller-first interface is built with RmlUi HTML and
RCSS, SDL2, and deterministic bitmap fonts.

## Project foundations

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).**
> PS5 Radio preserves the template's modern C++20 structure, `.hpp` interfaces,
> reproducible runtime, FSELF tooling, tests, deployment flow, and release
> automation.

> [!IMPORTANT]
> **Audio work is documented in [PS5 Audio Decoding Research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).**
> The companion repository records the hardware-first decoder investigation,
> reverse-engineering notes, native API probes, codec boundaries, and device
> validation that informed PS5 Radio's audio implementation.

| Identity | Value |
| --- | --- |
| Shell title | `PS5 Radio` |
| Title ID | `PPSA99001` |
| Category | Media |
| Current release version | `01.000.002` |
| Release-version source | [`sce_sys/param.json`](sce_sys/param.json) |
| Writable data | `/download0` only |

## Features

- Browse Popular, Trending, Top rated, Favorites, and Discover views.
- Search live Radio Browser data by text, country, genre, language, and
  bitrate, with mirror failover and server-side paging.
- Keep a large SQLite catalogue and favourites on `/download0`; a failed sync
  leaves the last verified catalogue untouched.
- Navigate entirely with the DualSense D-pad or left analogue stick; the PS5
  IME handles text entry.
- Render packaged, multilingual bitmap fonts deterministically rather than
  depending on platform font rasterisation.
- Play AAC/AAC+ and MP3 through native PS5 decoding; play Opus through the
  native Opus/CELT decoder route; play Vorbis and FLAC/Ogg-FLAC with bounded
  CPU decoders.
- Resolve bounded M3U/PLS indirection, strip ICY metadata, and support the
  audio-only AAC HLS subset.

App-specific codec limits and validation are documented in
[Architecture](docs/ARCHITECTURE.md),
[Codec investigation](docs/CODEC_INVESTIGATION.md), and
[Roadmap](ROADMAP.md). The complete reusable research record lives in
[PS5 Audio Decoding Research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).

## Requirements

Build from Linux, WSL, or a Linux CI runner. On Ubuntu, Debian, or WSL:

```bash
sudo apt update
sudo apt install curl git make pkg-config python3 python3-venv tar unzip wget \
  clang-18 clang-format-18 clang-tidy-18 lld-18 libsqlite3-dev
```

The production `.ffpfsc` target uses the pinned MkPFS bootstrapper. The
repository downloads, verifies, and caches the public PS5 Payload SDK, zlib,
PacBrew's SQLite port, GoogleTest, and packaging tools below ignored `.deps/`.
No proprietary SDK, system module, key, or game asset is included or fetched.

Run a read-only prerequisite check before building:

```bash
make doctor
```

See [Getting started](docs/GETTING_STARTED.md) and
[Native tooling](docs/NATIVE_TOOLING.md) for build-environment detail.

## Build

`sce_sys/param.json` is the only identity and release-version source. Do not
change `PPSA99001` when updating PS5 Radio: changing it produces a separate PS5
title rather than an update.

```bash
# Production release image (also assembles the complete title folder).
make ffpfsc

# Faster folder-only build for development deployment.
make
```

Outputs are written to:

```text
dist/PPSA99001/           complete title folder
dist/PPSA99001.ffpfsc     compressed package
```

An optional local UFS2 `.ffpkg` target remains available for development; it
is intentionally excluded from CI and GitHub Releases. See
[Package formats](docs/FFPKG.md).

The app is a **Media** category title. Stage the whole folder, not `eboot.bin`
alone. For a local development loop against an already-running FTP service:

```bash
make deploy PS5_HOST=192.168.4.30 DEPLOY_FORMAT=folder
```

> [!NOTE]
> The first launch can take a while while PS5 Radio downloads, validates, and
> caches the Radio Browser catalogue. Keep the console online and leave the app
> open until the database finishes loading; later launches use the local cache.

The deployer writes only title-scoped paths below `/data/homebrew`. It uploads
each file through a temporary name, then publishes `eboot.bin` and
`sce_sys/param.json` last. For console protocol and evidence requirements, see
[Deployment](docs/DEPLOYMENT.md) and [Testing](docs/TESTING.md).

## Test and quality gates

```bash
make test            # C++ unit tests, UI/metadata tests, 16 codec/catalogue checks
make lint            # formatting, static analysis, metadata, and shell checks
make check           # lint + all host tests + complete folder build
make ffpfsc          # production folder + FFPFSC image
```

The test suite runs entirely on the host and never contacts a console. It
covers RML/UI asset consistency, catalogue persistence, mirror/query parsing,
AAC timing, MP3 framing, ICY metadata, PCM/retry behaviour, Ogg/Opus,
Vorbis, FLAC, HLS/MPEG-TS, controller input, and Arabic/RTL text ordering.
The PS5-only boundary is documented in [Testing](docs/TESTING.md).

GitHub Actions runs linting, every host test, deterministic runtime
reproduction, and an FFPFSC build. When an exact `contentVersion` tag is
pushed, the workflow publishes only the verified `.ffpfsc` image and its
SHA-256 checksum.

## Source layout

```text
src/main.cpp                  SDL2/RmlUi application lifetime and renderer bridge
src/radio_app.cpp             C++20 controller and focus/state transitions
src/radio_text.cpp            C++20 UTF-8 visual-order helper
src/*.hpp                     Private C++ application interfaces
include/*.hpp                 Public codec, catalogue, input, and service interfaces
assets/ui/                    RML, RCSS, font atlases, and icons mounted at /app0/assets/ui
vendor/                       Checked-in RmlUi, SDL2, FreeType, decoder, and PS5 SDK inputs
tools/build.sh                Template-native compile/link/FSELF/folder assembler
tooling/native/               Template-owned native ELF and FSELF tooling
tests/                        GoogleTest and Python integration regressions
sce_sys/param.json            Shell metadata, title identity, and release version
docs/                         Architecture, testing, codec, build, and deployment documentation
```

PS5 Radio is a C++20 application throughout. Repository-owned interfaces use the
boilerplate's `.hpp` convention; portable codec, demux, persistence, input, and
service modules are independently testable C++ translation units. Vendored
single-file decoders retain their upstream filenames and are compiled through
small C++ adapters. See [Template port notes](docs/TEMPLATE_PORT.md).

## Versioning and releases

`contentVersion` in [`sce_sys/param.json`](sce_sys/param.json) drives the
packaged metadata, top-bar UI version, Git tag, and GitHub Release name. It
uses PS5's exact `NN.NNN.NNN` format without a `v` prefix.

```bash
# After updating sce_sys/param.json and passing the local gates.
git tag 01.000.003
git push origin main 01.000.003
```

The workflow rejects a mismatched tag. Full field meanings, Game/Media
metadata, and the import-linking configuration are in
[Configuration](docs/CONFIGURATION.md).

## Credits and licences

PS5 Radio uses Radio Browser for station metadata and URLs; Radio Browser does
not host the individual station streams. The project also uses or references
the PS5 Payload SDK, PacBrew SQLite, LLVM/Clang/lld, RmlUi, SDL2, FreeType,
GoogleTest, stb_vorbis, dr_flac, MkPFS, and UFS2Tool. Their provenance and
licences—including the public PS5 import metadata and template tooling—are
listed in [NOTICE.md](NOTICE.md).

PS5 Radio is GPL-3.0-or-later. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md),
and [Contributing](CONTRIBUTING.md).

PlayStation and PS5 are trademarks of Sony Interactive Entertainment. This
project is independent and is not affiliated with or endorsed by Sony.

This project was developed with assistance from OpenAI Codex, including some
original interface artwork. Project maintainers reviewed and validated the
resulting code, tests, documentation, dependencies, and generated assets.
