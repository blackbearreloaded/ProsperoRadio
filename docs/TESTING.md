# Testing

PS5 Radio separates deterministic host regressions from behaviour that only a
real PS5 can prove. Host tests never contact a console or a public Radio
Browser server.

## Commands

| Command | Scope |
| --- | --- |
| make test-deps | Fetch and verify the pinned, host-only GoogleTest source. |
| make test-unit | Compile and run the C++20 GoogleTest suite. |
| make test-integration | Run tool/UI Python tests and every host codec/catalogue regression. |
| make test | Run the complete host suite. |
| make check | Run linting, all host tests, and a full title-folder build. |
| make ffpfsc | Build the production folder and compressed FFPFSC image. |
| make ffpkg | Build the optional local UFS2 image; CI does not publish it. |

The host suite requires clang-18, clang++-18, and libsqlite3-dev.
GoogleTest is fetched into ignored .deps/test/ and is never included in a PS5
executable or package.

## Unit tests

The GoogleTest suite covers the C++ contracts that are portable and quick to
exercise:

- UTF-8 text stays unchanged for left-to-right scripts.
- Arabic/Persian presentation selection and RTL visual ordering remain stable.
- PS5 Opus import manifests expose their SONAME and required symbols to the
  native template converter.

Add a focused GoogleTest case for C++ state transformations, bounds handling,
or parser behaviour that does not need a PS5. Repository-owned interfaces use
the boilerplate's `.hpp` convention.

## Integration and codec regressions

Python discovery runs tests/test_*.py. It checks deployment dry-runs, metadata
initialization, and the RML/UI asset contract.

tools/run-radio-checks.sh compiles and runs 16 small host-native checks:

- AAC timing and MP3 frame geometry.
- ICY metadata, PCM queue, reconnect retry, and controller mapping.
- M3U/PLS resolution, HLS parsing, and MPEG-TS AAC extraction.
- Ogg page/stream validation, Opus packet semantics, and PCM handling.
- Incremental Vorbis and FLAC/Ogg-FLAC decoding.
- Radio Browser JSON/mirror/query parsing and SQLite catalogue persistence.

These checks use production C sources directly with strict warnings. They
exercise failure and truncation paths as well as normal data.

## PS5 smoke test

The following cannot be established on a host:

- RmlUi/SDL presentation, bitmap atlas sampling, and TV safe-area layout.
- Native AudioOut timing and the AAC/MP3/Opus decoder paths.
- Network/SSL/HTTP implementation, IME, controller hardware, and HLS live
  playback.
- Loader/FSELF compatibility, title lifecycle, and /download0 persistence.

For a release candidate:

1. Build from a clean commit and record the artifact SHA-256 and
   contentVersion.
2. Follow the repository's current console coordination protocol before
   connecting to shared hardware.
3. Deploy the whole dist/PPSA99001/ folder for a development test, or the
   matching FFPFSC for package installation.
4. Launch the Game-category title, verify browsing, search, favourites,
   playback/stop/switch, one non-ASCII station name, and a cached restart.
5. Capture the result and relevant logs; close the title and any Remote Play
   client.
6. Record console firmware, loader, exact artifact, and result in the relevant
   validation document.

Do not claim hardware support from passing host tests alone. See
[Deployment](DEPLOYMENT.md), [Architecture](ARCHITECTURE.md), and the
project's PS5 homebrew-development protocol for the console procedure.

The 2026-08-28 migration smoke test froze commit `9b710fb`, atomically deployed
the complete `PPSA99001` folder, and kept `/download0` intact. The title
rendered through RmlUi/SDL, loaded the existing 29,578-station cache, completed
a full Radio Browser refresh to 56,273 supported stations in about 100 seconds,
showed `Database ready`, and closed without a title crash. The count is an
observation of that live catalogue, not a fixed product limit.

## Before review or release

Run:

    make lint
    make test
    make ffpfsc

A release tag must exactly equal contentVersion in sce_sys/param.json, for
example 01.000.003. GitHub Actions repeats the same host gates and publishes
only the verified FFPFSC image and its SHA-256 checksum.
