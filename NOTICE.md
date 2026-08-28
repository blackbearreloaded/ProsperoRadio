# Notices

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The project’s PS5 ELF converter and FSELF writer are independently authored
GPL-3.0-or-later code. SharpProspero was a useful public format reference during
development but is not fetched, copied, linked, or required by the build.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

## PS5 Radio application dependencies

The checked-in `vendor/` snapshot contains application-facing headers, static
archives, and source needed by the PS5 Radio build:

- [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2), Copyright 1997-2025
  Sam Lantinga and contributors, is distributed under the zlib license retained
  in `vendor/ps5/sdl/include/SDL2/SDL_copying.h`.
- [RmlUi 6.2](https://github.com/mikke89/RmlUi/tree/6.2), Copyright CodePoint
  Ltd, Shift Technology Ltd, the RmlUi Team, and contributors, is distributed
  under the MIT license retained in `vendor/ps5/rmlui/LICENSE.txt`.
- [FreeType 2.13.2](https://freetype.org/), Copyright the FreeType Project
  authors, is distributed under the FreeType Project License retained in
  `vendor/ps5/freetype/LICENSE.txt`.
- [stb_vorbis](https://github.com/nothings/stb), by Sean Barrett, is MIT
  licensed; its pinned single-file source and licence are retained in
  `vendor/stb/`.
- [dr_flac](https://github.com/mackron/dr_libs), by David Reid, is MIT-0
  licensed; its pinned single-file source and licence are retained in
  `vendor/dr_flac/`.

The static C++ runtime inputs and public PS5 import stubs originate from the
open-source PS5 Payload SDK. The project retains only public headers, static
link inputs, and import metadata. No proprietary Sony SDK library, firmware
module, encryption key, or extracted game file is present. The temporary
link-only facades under `tooling/native/ps5_radio_import_stub_*.cpp` contain only
the application-imported C symbol declarations; they do not implement or copy
PS5 decoder or system-module code.

## SQLite catalogue storage

PS5 Radio declares the public [PS5 PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo)
SQLite 3.46.1 port. The build verifies and extracts it into ignored `.deps/`;
the application links its static archive but does not commit PacBrew's binary.
SQLite is dedicated to the public domain.

## Radio Browser, fonts, and artwork

Station metadata and stream URLs are supplied by the free and open
[Radio Browser](https://www.radio-browser.info/) community service. Radio
Browser does not host the individual radio streams; stations remain responsible
for their stream content and branding.

The bitmap UI fonts are generated from Montserrat and extended with Noto Sans,
DejaVu Sans, Noto Sans Thai, and Source Han Sans SC glyph masks. Montserrat and
the Noto/Source Han faces are under the SIL Open Font License 1.1; complete
licence texts accompany `assets/ui/fonts/lvgl-bitmap/` and its multilingual
pages. The `.rta` atlas container is a project-authored lossless alpha encoding.

PS5 Radio launcher artwork and controller prompts used OpenAI image-generation
assistance, then received project-specific review and adaptation. The prompts
are visual aids, not official Sony artwork. Play/stop symbols are independently
authored geometric assets.

No proprietary runtime module, encryption key, or game file is included.
