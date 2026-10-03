<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoRadio icon">
</p>

<h1 align="center">ProsperoRadio</h1>

<p align="center">
  <strong>A native Internet-radio application for PlayStation 5 homebrew</strong><br>
  Browse and search the Radio Browser catalogue with resilient disk-backed
  caching, native PS5 audio playback, and a controller-first OpenGL interface.
</p>

<p align="center">
  <a href="https://github.com/blackbearreloaded/ProsperoRadio/actions/workflows/tooling.yml"><img src="https://github.com/blackbearreloaded/ProsperoRadio/actions/workflows/tooling.yml/badge.svg" alt="Build"></a>
  <a href="https://github.com/blackbearreloaded/ProsperoRadio/releases/latest"><img src="https://img.shields.io/github/v/release/blackbearreloaded/ProsperoRadio?display_name=tag" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="GPL-3.0-or-later"></a>
</p>

## Highlights

- Browse more than 56,000 supported stations in a validated Radio Browser
  sync; the live total changes as the public catalogue evolves.
- Explore 240 countries and hundreds of genres and languages with server-side
  paging, search, and on-demand filters.
- Play AAC/AAC+, MP3, and Opus through native PS5 decoder paths, with validated
  Vorbis, FLAC, and Ogg-FLAC fallbacks.
- Resolve audio-only AAC HLS, M3U/PLS playlists, and ICY metadata while
  recovering cleanly from malformed or interrupted streams.
- Keep the catalogue and favourites fast and persistent in SQLite under
  `/data/prosperoradio`, with atomic refreshes and Radio Browser mirror failover.
- Use a controller-first OpenGL interface built on
  [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui):
  a Now Playing view with a visualizer, a letter rail on every station list,
  sound controls (volume, bass, treble, balance), PS5 text input, and station
  names in every script.

<p align="center">
  <img src="sce_sys/background-source.png" alt="ProsperoRadio artwork">
</p>

## Project foundations

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).**
> ProsperoRadio preserves the template's modern C++20 structure, `.hpp` interfaces,
> reproducible runtime, FSELF tooling, tests, deployment flow, and release
> automation.

> [!IMPORTANT]
> **The interface is built on [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui).**
> The kit is a build dependency, not part of this repository: the build
> fetches a pinned commit of it, the pinned
> [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK and the
> pinned HarfBuzz source into `.deps/`. See [`ui-kit/README.md`](ui-kit/README.md).

> [!IMPORTANT]
> **Audio work is documented in [PS5 Audio Decoding Research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).**
> The companion repository records the hardware-first decoder investigation,
> reverse-engineering notes, native API probes, codec boundaries, and device
> validation that informed ProsperoRadio's audio implementation.

| Identity | Value |
| --- | --- |
| Shell title | `ProsperoRadio` |
| Title ID | `PPSA99001` |
| Category | Media |
| Current release version | `01.000.005` |
| Release-version source | [`sce_sys/param.json`](sce_sys/param.json) |
| Development version on `main` | `01.000.010` (not released) |
| Writable data | `/data/prosperoradio` |

## Features

- Browse Popular, Trending, Top rated, Favorites, and Discover views.
- Search live Radio Browser data by text, country, genre, language, and
  bitrate, with mirror failover and server-side paging.
- Keep a large SQLite catalogue and favourites on `/data/prosperoradio`; a failed sync
  leaves the last verified catalogue untouched.
- Navigate entirely with the DualSense D-pad or left analogue stick; the PS5
  IME handles text entry.
- Draw station names in every script (Arabic, Cyrillic, Chinese, Persian and
  more) with the console's own fonts, HarfBuzz shaping and right-to-left
  ordering.
- Say once per launch when [homebrew.page](https://homebrew.page) lists a
  newer release.
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
sudo apt install curl git make pkg-config python3 python3-venv tar unzip wget zip \
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
change `PPSA99001` when updating ProsperoRadio: changing it produces a separate PS5
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

GitHub Releases also provide `PPSA99001.zip`, a ZIP of the complete
`PPSA99001/` folder for direct directory deployment.

An optional local UFS2 `.ffpkg` target remains available for development; it
is intentionally excluded from CI and GitHub Releases. See
[Package formats](docs/FFPKG.md).

The app is a **Media** category title. Stage the whole folder, not `eboot.bin`
alone. For a local development loop against an already-running FTP service:

```bash
make deploy PS5_HOST=192.168.4.30 DEPLOY_FORMAT=folder
```

> [!NOTE]
> The first launch can take a while while ProsperoRadio downloads, validates, and
> caches the Radio Browser catalogue. Keep the console online and leave the app
> open until the database finishes loading; later launches use the local cache.

## Updating

1. Fully close ProsperoRadio.
2. From the
   [latest release](https://github.com/blackbearreloaded/ProsperoRadio/releases/latest),
   download either `PPSA99001.ffpfsc` or `PPSA99001.zip`.
3. Deploy one format over FTP and wait for the transfer to finish:

   - **FFPFSC:** replace `/data/homebrew/PPSA99001.ffpfsc` with the downloaded
     image.
   - **ZIP:** extract it locally, then upload the entire `PPSA99001/` folder
     so its destination is `/data/homebrew/PPSA99001/`. Do not upload the ZIP
     file itself.

4. Do not keep the folder and FFPFSC image under `/data/homebrew` at the same
   time. Restart ShadowMountPlus cleanly or restart the PS5.
5. Start the approved services normally, wait for ShadowMountPlus to
   rediscover `PPSA99001`, then launch ProsperoRadio and confirm the version
   shown below the app name.

Do not relaunch immediately after replacing the app: ShadowMountPlus may
still have the previous folder or `.ffpfsc` mounted. Keeping title ID `PPSA99001`
preserves the catalogue and favourites under `/download0`; `/app0` comes from
the replacement app, and Shell presentation metadata may remain cached.

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
pushed, the workflow archives the same complete app folder, verifies both
release files, and publishes the `.ffpfsc`, folder `.zip`, and their shared
`SHA256SUMS` file.

## Source layout

```text
src/main.cpp                  Application lifetime, renderer, fonts, and the frame loop
src/app/                      The interface: screens, session state, platform seam
src/radio_http_curl.cpp       The service's HTTP calls on libcurl
src/elevation/                Filesystem access outside the sandbox
ui-kit/                       What the app takes from ps5-homebrew-ui and lays over it
src/radio_text.cpp            C++20 UTF-8 visual-order helper
src/*.hpp                     Private C++ application interfaces
include/*.hpp                 Public codec, catalogue, input, and service interfaces
vendor/                       Checked-in SDL2, decoder, stb, and PS5 SDK inputs
third_party/update_check/     The update check of the PS5 Native App Boilerplate
tests/console/                Scripted console runs for tools/console-run.py
tools/build.sh                Template-native compile/link/FSELF/folder assembler
tooling/native/               Template-owned native ELF and FSELF tooling
tests/                        GoogleTest and Python integration regressions
sce_sys/param.json            Shell metadata, title identity, and release version
docs/                         Architecture, testing, codec, build, and deployment documentation
```

ProsperoRadio is a C++20 application throughout. Repository-owned interfaces use the
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
git tag 01.000.005
git push origin main 01.000.005
```

The workflow rejects a mismatched tag. Full field meanings, Game/Media
metadata, and the import-linking configuration are in
[Configuration](docs/CONFIGURATION.md).

<!-- bbr-footer:start -->
<!-- Generated by ps5-homebrew-dev-protocol/scripts/readme-footer. Edit the template there, not here. -->

## Credits

Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom (ps5-payload-dev).
Third-party components, authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Copyright © 2026 BlackBearReloaded. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party components keep their own licenses. Binary releases are built from the tagged source in this repository.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc. This project is not affiliated with or endorsed by Radio Browser.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.
<!-- bbr-footer:end -->
