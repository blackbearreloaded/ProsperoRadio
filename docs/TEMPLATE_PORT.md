# Template port notes

PSRadio is layered on a clean clone of
[`ps5-native-app-boilerplate`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
at upstream revision `a15ab71d1a5ba6d37c6af28f65bb51520a588005`. The
boilerplate remains responsible for application startup, runtime-shim
reproduction, ELF/FSELF validation, output assembly, package creation,
FTP deployment, formatting, linting, host-test bootstrap, CI, and releases.

The PSRadio layer supplies the RmlUi/SDL2 UI, Radio Browser service, SQLite
catalogue, controller/IME flow, codecs, and its presentation assets. This
separation keeps application changes independent of the native format tooling.

## Source-language boundary

The application/controller and renderer adapter are C++20:

- `src/main.cpp` owns lifetime, RmlUi/SDL adapters, and the presentation loop.
- `src/radio_app.cpp` owns view, focus, filtering, and user interactions.
- `src/radio_text.cpp` provides UTF-8 visual ordering for bitmap fonts.
- C++ interfaces use the boilerplate-style `.hpp` extension.

The proven decoder, transport, SQLite, input, and IME modules are C++20
translation units with `.hpp` interfaces. Their procedural APIs remain narrow
because that keeps decoder and platform boundaries independently testable.
Vendored single-file decoders remain in upstream form behind C++ adapters.

Do not add another global `new`/`delete` implementation: the template's
`tooling/native/app_cpp_runtime.cpp` owns that bridge. SDL uses the
application's tracked allocator through `SDL_SetMemoryFunctions`.

## Build inputs

The root [Makefile](../Makefile) declares the PSRadio-specific build inputs:

| Input | Reason |
| --- | --- |
| `APP_INCLUDE_PATHS` | App interfaces plus SDL2 and RmlUi headers. |
| `APP_STATIC_ARCHIVES` | SDL2, RmlUi, FreeType, and the required C++ runtime archives. |
| `PACBREW_STATIC_ARCHIVES=lib/libsqlite3.a` | Disk-backed Radio Browser catalogue. |
| `APP_IMPORT_STUBS` | Public Opus and Opus-CELT PS5 import manifests. |
| `APP_CXXFLAGS=-frtti` | RmlUi static-library type information. |

The public PS5 Payload SDK supplies ordinary platform imports such as
`libSceSysmodule`. The historical PS5-format Opus manifest archives contain
PS5 metadata rather than ordinary ELF archive members, so lld cannot use them
directly. `tools/build.sh` keeps those original manifests as the source of
truth for the template's import validator and creates temporary, build-only
C++ ABI facades with the matching SONAME. The normal linker uses the facade to
record the module edge; the native converter validates every imported symbol
against the original manifest.

AAC/MP3 decoding and the PS5 IME require `libSceAudiodec` and
`libSceCommonDialog`, which are not exposed as public SDK link archives in the
current payload SDK. Their minimal build-only facades are explicit in
`tooling/native/psradio_import_stub_*.cpp`; they declare only the C ABI
symbols PSRadio actually imports. They contain no decoder implementation,
firmware code, or copied Sony library.

The application symbol map deliberately hides static C++ definitions from the
dynamic export table. This prevents RmlUi/libc++ RTTI internals from being
mistaken for application exports by the native converter while preserving the
undefined PS5 imports it must validate.

## Packaged assets and version substitution

All RmlUi assets live under `assets/ui/`, so the packaged application reads
them from `/app0/assets/ui/`. During assembly, `tools/build.sh` replaces the
single `{{PSRADIO_VERSION}}` placeholder in the copied RML document with
`contentVersion` from `sce_sys/param.json`. The source document retains the
placeholder; no second version file exists.

The title is always `PPSA99001`, with a Game category. Keep the title ID
stable for updates. See [Configuration](CONFIGURATION.md).

## Verification

Run the complete host gate from a clean checkout:

```bash
make check
make packages
```

`make test` includes GoogleTest checks for C++ text/import parsing, Python
checks for tools and UI metadata, and the 16 retained C codec/catalogue
regressions. `make app` compiles and validates the native FSELF before
assembling `dist/PPSA99001/`. `make packages` additionally creates FFPKG
and FFPFSC outputs.

The port's final hardware evidence is intentionally separate from these
repeatable host gates. Follow [Testing](TESTING.md) for the exact console
smoke-test protocol and record the title, version, artifact digest, loader,
firmware, and result.
