# Third-party notices

## Credits and acknowledgements

ProsperoRadio acknowledges the open-source projects and public services that made
the application possible:

- **Platform and packaging:** [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (GPL-3.0-or-later), [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)
  v0.42, [PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) v0.40.2,
  [SharpProspero](https://github.com/SvenGDK/SharpProspero) by SvenGDK
  (GPL-3.0), from which the ELF converter and FSELF writer in `tooling/native/`
  are derived, and
  [UFS2Tool](https://github.com/SvenGDK/UFS2Tool).
- **Filesystem access:** [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon)
  (MIT), created by ArkSama, in the cooperative owned-root form of
  [ProsperoRadio's compatibility fork](https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon), based on
  [mpereiraesaa's fork](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon), and its
  [contributors](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/graphs/contributors).
  The package carries the helper upstream's own build produces for this title
  (`lapy.elf`, `lapy-manifest.json`) and Lapy's licence (`licenses/Lapy-MIT.txt`);
  ProsperoRadio's client of it (`src/elevation/`) comes from ProsperoEden.
- **Self-update:** the update check, the self-update engine and its helper
  (`third_party/update_check/`, `third_party/self_update_helper/`) are the
  PS5 Native App Boilerplate's, with ProsperoEden's changes to the helper; the
  helper unpacks releases with [miniz 3.0.2](https://github.com/richgel999/miniz)
  (MIT, `third_party/miniz/`, licence in the package as `licenses/miniz-MIT.txt`).
- **Application stack:** [Radio Browser](https://www.radio-browser.info/),
  [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)
  (GPL-3.0-or-later) on the [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK,
  [HarfBuzz 12.3.2](https://github.com/harfbuzz/harfbuzz) (MIT),
  [stb_truetype](https://github.com/nothings/stb) (MIT),
  [libcurl 8.18.0](https://curl.se/) (curl licence) with
  [OpenSSL 3.5.2](https://www.openssl.org/) (Apache-2.0),
  [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) (John Törnblom's [PS5 port](https://github.com/ps5-payload-dev/SDL)),
  [SQLite 3.46.1](https://sqlite.org/) (public domain),
  [zlib 1.3.2](https://github.com/madler/zlib),
  [stb_vorbis](https://github.com/nothings/stb) (MIT), and
  [dr_flac](https://github.com/mackron/dr_libs) (MIT-0).
- **Tooling and type assets:** [LLVM](https://github.com/llvm/llvm-project),
  [GoogleTest 1.17.0](https://github.com/google/googletest),
  [DirectXTex](https://github.com/microsoft/DirectXTex),
  [Inter](https://github.com/rsms/inter) (OFL-1.1),
  [Montserrat](https://github.com/JulietaUla/Montserrat) (OFL-1.1), and
  [DejaVu fonts](https://dejavu-fonts.github.io/).

Radio Browser supplies station metadata and URLs but does not host individual
station streams. The SDK, zlib, GoogleTest, UFS2Tool, the interface
kit, the ps5-opengl SDK, HarfBuzz, libcurl, OpenSSL, and SQLite are verified
build inputs fetched below ignored `.deps/`; of these the interface kit, the
OpenGL runtime, HarfBuzz, libcurl, OpenSSL, SQLite, and zlib are linked into
the release package. Checked-in SDL2, stb, and dr_flac files retain their
upstream licence texts below `vendor/`. The update check under
`third_party/update_check/` comes from the PS5 Native App Boilerplate. The
text engine under `ui-kit/overrides/` and the Lapy client under
`src/elevation/` come from ProsperoEden (GPL-3.0-or-later, same author). The
licences of the interface's baked fonts ship beside them in `assets/fonts/`
of the package.

The maintainer-supplied launcher artwork is part of the
project and is covered by its licence.
