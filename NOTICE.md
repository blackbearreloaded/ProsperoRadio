# Notices

## SharpProspero build dependency

SharpProspero source is not distributed in this repository. During setup,
`tools/setup-tooling.ps1` fetches
[SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero), originally by
SvenGDK, at commit `e36e610fa5b4be23ad38b9c8429f11f11750cc0c` and applies
`tooling/patches/sharpprospero-native-app.patch`. The generated checkout is
ignored under `.deps/SharpProspero`.

The fetched source and the compatibility delta remain subject to
SharpProspero's upstream GPL-3.0 terms.

## Optional UFS2Tool build dependency

When `-OutputFormat Ffpkg` (or its legacy `-Ffpkg` alias) is requested,
`tools/setup-ffpkg-tooling.ps1` fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache. No UFS2Tool source or binary is distributed here.
The fetched dependency remains subject to its upstream BSD-2-Clause license.

The optional command profile follows the public procedure documented by
[sinajet/PSFFPKG](https://github.com/sinajet/PSFFPKG). PSFFPKG is not fetched,
copied, or distributed by this repository.

## Optional MkPFS build dependency

When `.ffpfsc` output is requested, `tools/setup-mkpfs-tooling.ps1` fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies in `.deps/MkPFS/.venv`. No MkPFS
source or binary is distributed here. The fetched dependency remains subject
to its upstream GPL-3.0 license; its Python dependencies retain their own
licenses.

## Independently authored runtime shim

`runtime/libc.prx` and `tooling/MinimalLibcBuilder` are independently authored
for this project and licensed under GPL-3.0-or-later. The module contains only
minimal loader-compatibility stubs and semantic metadata. It contains no Sony
runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Radio Browser

Station discovery, metadata, and stream URLs are provided by the free and open
[Radio Browser](https://www.radio-browser.info/) community service. Radio
Browser does not host the radio streams. Individual stations remain
responsible for their streams, branding, and content.

## Fonts and interface artwork

The active bitmap faces are deterministically generated from LVGL's built-in
Montserrat Medium data. Montserrat is Copyright 2011 The Montserrat Project
Authors and is distributed under the SIL Open Font License 1.1; the complete
license accompanies the generated files in `ui/fonts/lvgl-bitmap/OFL.txt`.

The dynamic 20, 24, 28, and 32 px faces are extended with exact 4-bit masks
exported from the completed LVGL application's compiled radio fonts. Those
glyphs originate from Noto Sans, DejaVu Sans, Noto Sans Thai, and Source Han
Sans SC. Their complete OFL and license texts accompany the paged atlas files
under `ui/fonts/lvgl-bitmap/multilingual/licenses/`. The `.rta` container is a
lossless zero-run/literal encoding authored for this project; it does not
resample or modify the source masks.

PSRadio launcher artwork and controller prompts were generated for this
project with OpenAI image-generation assistance and reviewed and adapted by
the maintainer. The generated controller images are visual control prompts,
not official Sony artwork. The play and stop symbols are independently
authored geometric vector assets with deterministic TGA runtime exports.

## Original selection audio

The selection track `sce_sys/snd0.at9` is an original project asset supplied by
BlackBearReloaded, Copyright (C) 2026 BlackBearReloaded, and distributed under
GPL-3.0-or-later. The track is titled `Night Drive`. Its distributable ATRAC9
master is a 15-second whole-track loop at 48 kHz stereo and 192 kb/s. The
measured integrated loudness of the final encoded excerpt is approximately
-28.6 LUFS. The lossy source and uncompressed intermediate are intentionally
not included.

No Sony runtime module, SDK binary, encryption key, or game file is included.
