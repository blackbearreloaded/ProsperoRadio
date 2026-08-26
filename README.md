> **Disclaimer:** This is an AI-assisted project developed using OpenAI Codex.

<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="PSRadio icon">
</p>

<h1 align="center">PSRadio</h1>

<p align="center">
  <strong>A controller-first internet radio application for PlayStation 5 homebrew</strong><br>
  Browse, search, favorite, and play Radio Browser stations in a native RmlUi interface.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-PlayStation%205-003791?logo=playstation&amp;logoColor=white" alt="PlayStation 5">
  <img src="https://img.shields.io/badge/UI-RmlUi%20%2B%20SDL2-70E1DC" alt="RmlUi and SDL2">
  <img src="https://img.shields.io/badge/audio-AAC%20%2B%20Opus-5DDFA4" alt="AAC and Opus audio">
  <a href="https://github.com/blackbearreloaded/psradio/releases/latest"><img src="https://img.shields.io/github/v/release/blackbearreloaded/psradio?display_name=tag&amp;sort=semver&amp;label=latest%20release" alt="Latest release"></a>
  <a href="https://github.com/blackbearreloaded/psradio/actions/workflows/build.yml"><img src="https://github.com/blackbearreloaded/psradio/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="GPL-3.0-or-later"></a>
</p>

PSRadio is a native C/C++ application for compatible PS5 homebrew environments.
It queries the community-run [Radio Browser](https://www.radio-browser.info/)
service, keeps a local station cache and favorites list, and decodes supported
AAC and Ogg Opus streams through the console's native audio facilities.

The television interface is authored in RML and RCSS. SDL2 provides the native
window and software presentation path, while a custom RmlUi bitmap-font backend
preserves crisp, deterministic text rendering at 1920 x 1080.

PSRadio is built on
[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
which provides the reproducible native build, host-side tooling, clean-room
runtime shim, and package pipeline. PSRadio extends that foundation with its
radio client, controller-first interface, media stack, and bundled dependencies.

> [!IMPORTANT]
> PSRadio does not run on an unmodified retail console. It is intended for
> consoles you own with an already configured, compatible homebrew loader.
> Firmware and loader behavior varies; no exploit or console-modification
> procedure is included here.

## Features

- Popular, Trending, Top rated, Favorites, and Discover station views.
- Cached catalog search with country, genre, language, and bitrate filters.
- DualSense-friendly navigation using the D-pad or left analog stick.
- Native PS5 on-screen keyboard for text search.
- AAC, HE-AAC, and Ogg Opus playback through native PS5 decoders and AudioOut.
- Persistent favorites and cached catalog data under `/download0`.
- Responsive play, stop, station switching, paging, and refresh actions.
- RmlUi overlays, fixed television safe area, and a persistent now-playing rail.
- Deterministic multilingual bitmap atlases covering extended Latin, Greek,
  Cyrillic, Arabic/Persian, Hebrew, Devanagari, Thai, common Chinese characters,
  and Japanese kana at metadata sizes.
- Folder, UFS2 `.ffpkg`, and compressed `.ffpfsc` build outputs.

## Current status

The complete browse, search, favorite, cache, and AAC playback loop has been
validated on PS5 hardware with ShadowMountPlus. Ogg Opus playback is also
hardware-validated: CELT-mode packets use the console's dedicated CELT decoder,
while SILK and hybrid modes retain the general native Opus decoder. The release
display name is **PSRadio** and its stable application identity is `PPSA99001`.

Catalog requests advertise AAC and explicitly identified Ogg Opus stations.
Radio Browser's generic OGG records are not exposed unless their resolved URL
identifies Opus. MP3, Ogg Vorbis, FLAC, and HLS delivery remain gated by the
hardware-first work in the [roadmap](ROADMAP.md).

## Requirements

Builds run from PowerShell on Windows and invoke Clang inside WSL. The generated
PS5 application contains no .NET or managed runtime.

| Requirement | Purpose |
| --- | --- |
| Windows PowerShell 5.1 or newer | Build orchestration and asset validation |
| WSL with `/usr/bin/clang-18` and `/usr/bin/llvm-config-18` | C11/C++20 cross-compilation |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) at `/opt/ps5-payload-sdk` in WSL | PS5 headers and compiler target support |
| [Git for Windows](https://git-scm.com/download/win) | Fetching the pinned SharpProspero build dependency |
| [.NET SDK 10](https://dotnet.microsoft.com/download/dotnet/10.0) | Host-side linker and FSELF tooling |
| Python 3.9 or newer | Required only for compressed `.ffpfsc` output |

The repository includes the PS5 SDL2, RmlUi, FreeType, C++ runtime, and stub
artifacts used by the validated build. The first build downloads a pinned
[SharpProspero](https://github.com/SvenGDK/SharpProspero) revision into the
ignored `.deps/` cache and applies the tracked compatibility patch.

## Build from source

Open PowerShell in the repository root and verify the host first:

```powershell
./tools/doctor.ps1
```

Build the default directory form:

```powershell
./build.ps1
```

Or request a mountable filesystem image:

```powershell
./build.ps1 -OutputFormat Ffpkg
./build.ps1 -OutputFormat Ffpfsc
./build.ps1 -OutputFormat All
```

Successful builds are written under `dist/`:

| Command | Output |
| --- | --- |
| `./build.ps1` | `dist/PPSA99001/` |
| `./build.ps1 -OutputFormat Ffpkg` | App folder plus `dist/PPSA99001.ffpkg` |
| `./build.ps1 -OutputFormat Ffpfsc` | App folder plus `dist/PPSA99001.ffpfsc` |
| `./build.ps1 -OutputFormat All` | App folder and both image formats |

If `dotnet` or Python is installed outside `PATH`, pass the executable directly:

```powershell
./build.ps1 -Dotnet C:\path\to\dotnet.exe
./build.ps1 -OutputFormat Ffpfsc -Python C:\path\to\python.exe
```

The [Build workflow](.github/workflows/build.yml) verifies prerequisites, lints
the UI and metadata, compiles all project-owned C and C++ with warnings treated
as errors, and runs the host regressions on every push to `main` and every pull
request. It then builds the app folder and verified `.ffpfsc` image and keeps
both in a downloadable GitHub Actions artifact for 14 days. The workflow can
also be run manually from the repository's **Actions** tab.

Version tags publish permanent GitHub Releases automatically. The release
contains the verified `.ffpfsc`, a ZIP of the directory build, generated
release notes, and `SHA256SUMS`. Before tagging, set `version` in `project.json`
and advance `contentVersion`. The packaged top bar displays `version`, the PS5
Information screen displays `contentVersion`, and CI rejects a mismatched tag:

```powershell
$version = (Get-Content project.json -Raw | ConvertFrom-Json).version
git tag -a "v$version" -m "PSRadio v$version"
git push origin "v$version"
```

Only tags beginning with `v` publish a release; ordinary branch builds remain
downloadable Actions artifacts and do not create release entries.

For a clean machine walkthrough, SDK setup, identity customization, and the
expected output tree, read [Getting started](docs/GETTING_STARTED.md).

## Deploy

Stage exactly one complete build output using a loader that supports it. For
directory deployment, copy the entire `dist/PPSA99001/` directory; uploading
only `eboot.bin` is insufficient. Filesystem-image users can deploy the
matching `.ffpkg` or `.ffpfsc` instead.

Detailed safety notes and smoke-test expectations are in
[Deployment](docs/DEPLOYMENT.md). PSRadio does not configure the console,
install a loader, or transfer files automatically.

## Controls

| Input | Action |
| --- | --- |
| D-pad / left analog stick | Move focus; Left/Right changes a focused filter |
| Cross | Activate, play, stop, or open text entry |
| Circle | Close an overlay or return to Popular |
| Square | Add or remove the selected station from Favorites |
| Triangle | Open search and filters |
| L1 / R1 | Change the main station view |
| Options | Refresh Radio Browser data |

## Repository layout

```text
project.json                 App identity and explicit build inputs
build.ps1                    Windows/WSL build orchestrator
include/                     Native application interfaces
src/main.cpp                 SDL, RmlUi, renderer, and application lifecycle
src/radio_app.cpp            UI state, navigation, search, and station actions
src/radio_service.c          Radio Browser, persistence, audio streams, and AudioOut
src/ogg_opus.c               Incremental Ogg Opus demuxing
src/opus_decoder.c           Native PS5 general-Opus/CELT decoder adapter
src/radio_input.c            Native controller adapter
src/radio_ime.c              Native on-screen keyboard adapter
src/radio_text.cpp           Arabic shaping and visual RTL ordering
ui/main.rml                  Complete application document
ui/styles/app.rcss           Fixed 1920 x 1080 television layout
ui/fonts/lvgl-bitmap/        Deterministic runtime font atlases
ui/icons/                    Controller and playback assets
sce_sys/                     Launcher metadata, artwork, and selection audio
runtime/libc.prx             Reproducible clean-room loader shim
tooling/                     Host-side C# build tools
tools/                       Validation, asset, and dependency scripts
vendor/ps5/                  Pinned PS5 dependency snapshot
```

Generated `.deps/`, `build/`, and `dist/` directories are intentionally ignored
and can be removed at any time.

## Documentation

| Document | Purpose |
| --- | --- |
| [Getting started](docs/GETTING_STARTED.md) | Clean-machine prerequisites and first build |
| [Architecture](docs/ARCHITECTURE.md) | Runtime components, rendering, data flow, and persistence |
| [Project configuration](docs/CONFIGURATION.md) | `project.json`, source lists, identity, and runtime modules |
| [Build output formats](docs/FFPKG.md) | Folder, `.ffpkg`, and `.ffpfsc` artifacts |
| [Deployment](docs/DEPLOYMENT.md) | Staging and PS5 smoke-test expectations |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common host, build, launcher, and runtime failures |
| [Presentation assets](docs/PRESENTATION_ASSETS.md) | Launcher icon, background, and selection audio |
| [C# build tooling](docs/CSHARP_TOOLING.md) | SharpProspero integration and host tools |
| [Runtime shim](docs/RUNTIME_SHIM.md) | Clean-room `libc.prx` scope and reproduction |
| [Platform constraints](docs/PLATFORM_NOTES.md) | Loader, filesystem, and presentation boundaries |
| [Native Opus validation](docs/OPUS_VALIDATION.md) | Hardware decoder evidence and remaining device gates |
| [Roadmap](ROADMAP.md) | Planned codec and streaming support |
| [Contributing](CONTRIBUTING.md) | Development workflow and validation checklist |
| [Notices](NOTICE.md) | Dependencies, fonts, artwork, and attribution |

## Development checks

Run the lightweight repository checks before a full PS5 build:

```powershell
python tools/check_ui.py
wsl --exec clang-18 -std=c11 -Wall -Wextra -Werror -Iinclude tools/aac_timing_check.c -o /tmp/aac-timing-check
wsl /tmp/aac-timing-check
wsl --exec clang-18 -std=c11 -Wall -Wextra -Werror -Iinclude tools/radio_input_check.c -o /tmp/radio-input-check
wsl /tmp/radio-input-check
wsl --exec clang-18 -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections -Iinclude -Ivendor/ps5/sdl/include/SDL2 tools/radio_service_json_check.c -o /tmp/radio-service-json-check
wsl /tmp/radio-service-json-check
wsl --exec clang++-18 -std=c++20 -Wall -Wextra -Werror -Isrc tools/radio_text_check.cpp src/radio_text.cpp -o /tmp/radio-text-check
wsl /tmp/radio-text-check
```

Then run `./tools/doctor.ps1` and `./build.ps1`. Hardware-specific changes
should also be tested on the intended firmware and loader before a release.

## Updating the boilerplate foundation

PSRadio records the boilerplate commit it was derived from in Git history, so
future foundation changes can be reviewed and merged normally instead of copied
file by file. Configure the read-only upstream remote once:

```bash
git remote add boilerplate git@github.com:blackbearreloaded/ps5-native-app-boilerplate.git
git config remote.boilerplate.tagOpt --no-tags
git remote set-url --push boilerplate DISABLED
```

Then review and merge updates:

```bash
git fetch --no-tags boilerplate main
git log --oneline HEAD..boilerplate/main
git merge boilerplate/main
```

The repositories have independent `v*` release tags, so boilerplate tags are
intentionally not fetched. Resolve any application-specific conflicts, run the
development checks above, and complete a full build before pushing the merge.

## Acknowledgements

PSRadio exists thanks to the maintainers and contributors behind these
open-source projects and public services:

- **PS5 platform and runtime:**
  [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) provides the public
  target headers, compiler wrappers, runtime archives, and import stubs used by
  the native build.
- **Interface and rendering:**
  [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) provides presentation and
  synchronization, [RmlUi](https://github.com/mikke89/RmlUi) provides the RML
  and RCSS document engine, and [FreeType](https://freetype.org/) underpins
  RmlUi's font facilities.
- **Linking and FSELF tooling:**
  [SharpProspero](https://github.com/SvenGDK/SharpProspero), by SvenGDK and its
  contributors, provides the host-side ELF linker, import catalog, and FSELF
  reader/writer extended by this project's tracked compatibility patch.
- **Filesystem-image packaging:**
  [UFS2Tool](https://github.com/SvenGDK/UFS2Tool) creates optional `.ffpkg`
  images, with the command profile informed by
  [PSFFPKG](https://github.com/sinajet/PSFFPKG). [MkPFS](https://github.com/PSBrew/MkPFS)
  creates and verifies compressed `.ffpfsc` images.
- **Station catalog:**
  the volunteer-run [Radio Browser](https://www.radio-browser.info/) service
  supplies station discovery, metadata, and stream URLs. Individual stations
  host and control their own streams and content.
- **Typography:**
  [LVGL](https://github.com/lvgl/lvgl) supplied the deterministic Montserrat
  bitmap-font source and the reference rendering used to resolve PS5 texture
  sampling differences. Extended metadata coverage uses glyphs from
  [Montserrat](https://github.com/JulietaUla/Montserrat),
  [Noto](https://github.com/notofonts/noto-fonts),
  [DejaVu Sans](https://dejavu-fonts.github.io/), and
  [Source Han Sans](https://github.com/adobe-fonts/source-han-sans).
- **Host toolchain:**
  [LLVM/Clang](https://llvm.org/), [PowerShell](https://github.com/PowerShell/PowerShell),
  [WSL](https://learn.microsoft.com/windows/wsl/),
  [.NET](https://dotnet.microsoft.com/), [Python](https://www.python.org/), and
  [Git](https://git-scm.com/) power the reproducible build, dependency, and
  validation scripts. [GitHub Actions](https://github.com/features/actions)
  runs the clean-machine build. [DirectXTex](https://github.com/microsoft/DirectXTex)
  and [FFmpeg](https://ffmpeg.org/) are supported for optional presentation
  image and audio preparation.
- **Project assets and development:**
  controller artwork was created with OpenAI image-generation assistance, and
  implementation and investigation were assisted by OpenAI Codex. Every
  generated asset and code change was reviewed and validated by the maintainer.

## License

PSRadio code is distributed under [GPL-3.0-or-later](LICENSE). Bundled and
fetched components retain their respective licenses. Versions, pinned
revisions, copyright notices, redistribution terms, font licenses, and asset
provenance for distributed and on-demand project components are recorded in
[NOTICE.md](NOTICE.md).

PlayStation and PS5 are trademarks of Sony Interactive Entertainment. PSRadio
is an independent homebrew project and is not affiliated with or endorsed by
Sony Interactive Entertainment.
