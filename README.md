<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoRadio icon">
</p>

<h1 align="center">ProsperoRadio</h1>

<p align="center">
  <strong>A native Internet-radio application for PlayStation 5 homebrew</strong><br>
  Browse and search the Radio Browser catalogue with resilient disk-backed
  caching, native PS5 audio playback, and a controller-first RmlUi interface.
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
  `/download0`, with atomic refreshes and Radio Browser mirror failover.
- Use a controller-first RmlUi interface with DualSense and left-stick
  navigation, PS5 text input, and multilingual station names.

<p align="center">
  <img src="sce_sys/pic1.png" alt="ProsperoRadio artwork">
</p>

## Project foundations

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).**
> ProsperoRadio preserves the template's modern C++20 structure, `.hpp` interfaces,
> reproducible runtime, FSELF tooling, tests, deployment flow, and release
> automation.

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

## Credits, third-party software, and licences

ProsperoRadio acknowledges the open-source projects and public services that made
the application possible:

- **Platform and packaging:** [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (GPL-3.0-or-later), [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)
  v0.42, [PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) v0.40.2,
  [SharpProspero](https://github.com/SvenGDK/SharpProspero) as a public format
  reference, [MkPFS](https://github.com/PSBrew/MkPFS), and
  [UFS2Tool](https://github.com/SvenGDK/UFS2Tool).
- **Application stack:** [Radio Browser](https://www.radio-browser.info/),
  [RmlUi 6.2](https://github.com/mikke89/RmlUi/tree/6.2) (MIT),
  [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2),
  [FreeType 2.13.2](https://freetype.org/),
  [SQLite 3.46.1](https://sqlite.org/) (public domain),
  [zlib 1.3.2](https://github.com/madler/zlib),
  [stb_vorbis](https://github.com/nothings/stb) (MIT), and
  [dr_flac](https://github.com/mackron/dr_libs) (MIT-0).
- **Tooling and type assets:** [LLVM](https://github.com/llvm/llvm-project),
  [GoogleTest 1.17.0](https://github.com/google/googletest),
  [DirectXTex](https://github.com/microsoft/DirectXTex),
  [LVGL](https://github.com/lvgl/lvgl),
  [Montserrat](https://github.com/JulietaUla/Montserrat),
  [Noto fonts](https://github.com/notofonts),
  [DejaVu fonts](https://dejavu-fonts.github.io/), and
  [Source Han Sans](https://github.com/adobe-fonts/source-han-sans).

Radio Browser supplies station metadata and URLs but does not host individual
station streams. The SDK, zlib, GoogleTest, MkPFS, and UFS2Tool are verified
build inputs kept below ignored `.deps/`; they are not distributed in the
release package. SharpProspero is neither fetched nor linked. Checked-in SDL2,
RmlUi, FreeType, stb_vorbis, and dr_flac files retain their upstream licence
texts below `vendor/`. Complete font licences accompany
`assets/ui/fonts/lvgl-bitmap/`.

The runtime shim, ELF converter, and FSELF writer are independently authored
GPL-3.0-or-later code. No proprietary Sony SDK, firmware module, encryption
key, or extracted game asset is included. The maintainer-supplied launcher
artwork and selection audio are distributed under the project licence.

ProsperoRadio is GPL-3.0-or-later. See [LICENSE](LICENSE) and
[Contributing](CONTRIBUTING.md).

PlayStation and PS5 are trademarks of Sony Interactive Entertainment. This
project is independent and is not affiliated with or endorsed by Sony.

This project was developed with assistance from OpenAI Codex, including some
original interface artwork. Project maintainers reviewed and validated the
resulting code, tests, documentation, dependencies, and generated assets.
