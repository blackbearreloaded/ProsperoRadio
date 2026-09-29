# Third-party notices

## Credits and acknowledgements

ProsperoRadio acknowledges the open-source projects and public services that made
the application possible:

- **Platform and packaging:** [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (GPL-3.0-or-later), [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)
  v0.42, [PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) v0.40.2,
  [SharpProspero](https://github.com/SvenGDK/SharpProspero) by SvenGDK
  (GPL-3.0), from which the ELF converter and FSELF writer in `tooling/native/`
  are derived, [MkPFS](https://github.com/PSBrew/MkPFS), and
  [UFS2Tool](https://github.com/SvenGDK/UFS2Tool).
- **Application stack:** [Radio Browser](https://www.radio-browser.info/),
  [RmlUi 6.2](https://github.com/mikke89/RmlUi/tree/6.2) (MIT),
  [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) (John Törnblom's [PS5 port](https://github.com/ps5-payload-dev/SDL)),
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
release package. Checked-in SDL2,
RmlUi, FreeType, stb_vorbis, and dr_flac files retain their upstream licence
texts below `vendor/`. Complete font licences accompany
`assets/ui/fonts/lvgl-bitmap/`.

The maintainer-supplied launcher artwork and selection audio are part of the
project and are covered by its licence.
