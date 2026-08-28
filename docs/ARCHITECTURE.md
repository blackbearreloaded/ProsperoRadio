# Architecture

PS5 Radio is a C++20 application. RmlUi owns document layout, SDL2 owns
presentation, and PS5 platform services provide networking, input, text entry,
compressed-audio decoding, and PCM output. Narrow declarations isolate the
platform ABI and vendored decoder boundaries.

## Runtime overview

```text
DualSense / IME
      |
      v
radio_input.cpp + radio_ime.cpp
      |
      v
radio_app.cpp <----> RmlUi document and RCSS
      |
      v
radio_service.cpp
  |        |        |
  |        |        +--> /download0 SQLite catalog and atomic favorites
  |        +-----------> AAC / MP3 / Opus -> native decoders ---+
  |                       Ogg Vorbis -> bounded CPU decoder ------+-> PCM -> AudioOut
  |                       FLAC / Ogg-FLAC -> bounded CPU decoder --+
  +--------------------> Radio Browser over native HTTP/TLS
```

The UI thread never performs catalog downloads or stream decoding. It polls a
small synchronized service status object and updates the existing document
elements when state changes.

## Application entry and rendering

[`src/main.cpp`](../src/main.cpp) owns application lifetime and the 1920 x 1080
render loop. It supplies RmlUi with three platform adapters:

- `AppSystemInterface` supplies elapsed time and logging.
- `AppFileInterface` resolves packaged assets through `/app0` on PS5.
- `SdlRenderInterface` translates RmlUi geometry and textures to SDL2's software
  renderer and presents the resulting window surface.

The software path is intentional. It avoids a runtime OpenGL dependency and
keeps behavior consistent with the SDL backend already validated on the target.
Ordinary RmlUi geometry uses `SDL_RenderGeometry`; bitmap-font atlas quads take
a pixel-aligned copy path to prevent filtering and subpixel resampling.

Each multilingual `.rta` page expands to a 16 MiB RGBA atlas. The texture
loader allocates that buffer through SDL's mmap-backed allocator, converts it
in place after creating the SDL texture, and retains the same buffer for exact
pixel compositing. It does not keep separate decoded BGRA and RGBA copies.
This matters on the target because the small libc heap is not suitable for
repeated full-atlas allocations even when the process still has ample virtual
memory.

## RmlUi document and application state

[`assets/ui/main.rml`](../assets/ui/main.rml) contains the complete document and
[`assets/ui/styles/app.rcss`](../assets/ui/styles/app.rcss) defines the fixed television
layout. The document contains stable element IDs for station cards, tabs,
paging, selected-station details, now-playing state, search, and credits.

[`src/radio_app.cpp`](../src/radio_app.cpp) is the controller between native
state and those elements. It:

- builds the current filtered station index;
- pages four visible station cards at a time;
- tracks spatial focus for the main screen and search overlay;
- maps controller actions to view, filter, favorite, refresh, and playback
  operations;
- updates labels, classes, icons, and equalizer state without replacing the
  document.

Keeping the RML structure stable makes focus and rendering behavior predictable
and avoids rebuilding large DOM fragments during live playback.

## Text rendering

PS5 Radio registers [`BitmapFontEngine`](../src/bitmap_font_engine.cpp) as RmlUi's
font backend. The checked-in BMFont metadata and exact alpha atlases under
`assets/ui/fonts/lvgl-bitmap/` are build inputs and ship with the application.

The base Montserrat faces cover interface text at 20, 24, 28, 32, 36, 40, and
48 px. Extended 20, 24, 28, and 32 px pages contain 17,854 additional glyphs
per size. The custom `.rta` format stores 4-bit alpha pages losslessly using a
small zero-run/literal encoding and is decoded by the SDL texture loader.

[`src/radio_text.cpp`](../src/radio_text.cpp) provides the minimum layout support
needed beyond RmlUi's left-to-right bitmap interface: Arabic and Persian
presentation-form selection plus visual ordering for right-to-left metadata.
Font sources and licenses are documented in [`NOTICE.md`](../NOTICE.md).

## Radio Browser and persistence

[`src/radio_service.cpp`](../src/radio_service.cpp) uses native PS5 network, SSL, and
HTTP services. A background worker discovers Radio Browser mirrors, rotates to
the next mirror after a request failure, and scans the healthy server-ordered
catalog in 10,000-row pages. The parser admits AAC, AAC+, MP3, OGG, Opus,
Vorbis, and FLAC records locally, avoiding overlapping codec-specific scans.
Discover submits the current text, country code, tag, language, and minimum
bitrate to the advanced search endpoint. Only the latest pending query is
retained while a sync is active.

The 10,000-row transient batch and 16 MiB response buffer use the app's
mmap-backed SDL allocator, keeping them independent of the small process heap
while SQLite preserves an unrestricted disk-backed catalog. Country, tag, and
language choices come from Radio Browser's live facet endpoints. Full
synchronization writes a fresh disposable staging database and commits each
downloaded page in bounded 256-row transactions. The staging database is
integrity-checked and then atomically promoted. A failed or
interrupted sync therefore cannot modify the last known-good live catalog.
Automatic full refresh is limited to once per 12 hours; Options always requests
a manual refresh.

[`src/radio_catalog_store.cpp`](../src/radio_catalog_store.cpp) owns the SQLite
schema and prepared statements. Catalog state and the canonical favorite file
remain under the only writable runtime mount, `/download0`:

```text
/download0/radio-browser.sqlite3          live catalog
/download0/radio-browser-next.sqlite3     disposable synchronization stage
/download0/radio-browser-favorites.bin    canonical favorites
/download0/radio-browser-favorites.tmp    atomic favorite-write temporary
```

SQLite uses an in-memory rollback journal and memory-only temporary storage
because the current PacBrew static library and static-link profile do not safely
support the secondary rollback-file and `fdatasync` paths. Before opening a
database, the service gives SQLite a dedicated 64 MiB, mmap-backed global page
pool; each connection may use up to 32 MiB from that pool. If external-page-cache
configuration is unavailable, connections fall back to a 256 KiB cache. Small
256-row transactions bound rollback-journal allocations independently of the
page pool. Those relaxed durability settings apply only while creating
disposable server-derived state. Startup validates the live database with
`quick_check`; an invalid catalog is rebuilt while the staging design protects
the previous live file from an interrupted refresh. Favorites use a separate
versioned file written by temporary-file rename, then are mirrored into a newly
promoted catalog. The old fixed-size binary catalog and favorite formats are
imported once when present.

RmlUi never receives a full-catalog snapshot. The main thread asks SQLite for
the total matching count and one four-record page using indexed equality/range
filters, deterministic text matching, ordering, `LIMIT`, and `OFFSET`. Text
matching uses the connection-local `contains_nocase` SQL function registered by
the app. This avoids the PacBrew SQLite archive's built-in `LIKE` callback,
whose function pointer is unsuitable for the current static PS5 link profile.
Only that page and a stable
copy of the playing station are materialized in RAM, so catalog growth does not
increase UI geometry or station-object memory. Popular, Trending, Top rated,
Favorites, and Discover use the same bounded query path. There is no
480-station runtime ceiling.
Radio Browser's broader `OGG` label is normalized when URL or name hints identify
Opus or Ogg-FLAC; other Ogg records are signature-probed at playback. HLS records
are admitted only for AAC or AAC+, and custom station input remains outside
scope.

Catalog synchronization runs on a dedicated native thread with a 4 MiB stack.
This accommodates the facet snapshot plus the nested native HTTP and SQLite
calls without relying on the PS5's much smaller default worker-thread stack.

### PS5 catalog validation

The production `PPSA99001` package with SHA-256
`8D223DFDA4988B75A67D8DB353A544AF41A8CA1659140FCB840317173EE49589`
completed a clean first sync and cached restart on August 27, 2026. The promoted
database contained 56,248 stations in one sync generation: 39,287 MP3, 8,637
AAC+, 7,725 AAC, 351 generic OGG, 178 FLAC/Ogg-FLAC, and 70 explicitly
identified Opus records. Both `quick_check` and `integrity_check` returned
`ok`; `catalog_last_error` was zero, all four browsing indexes were present,
and the staged file had been removed after promotion. Facet storage contained
240 countries, 512 genres/tags, and 512 languages.

The folder candidate from commit `54c4c95` (FSELF SHA-256
`6614A5D1A9D52FBF65EA96A54C63376106677979396CABED110F6FAE36542938`)
completed the controller-driven filtered-query regression on PS5 on August 27,
2026. Genre-only, genre plus language, country plus genre plus language plus
minimum bitrate, United Kingdom-only, filtered next-page navigation, reset, and
four rapid apply cycles all rendered results without a fatal signal. The
formerly blank 2,192-station `GB` facet rendered as `United Kingdom`; the title
then closed with its runtime layers released.

## Audio pipeline

Playback runs on a separate native thread:

```text
resolved station URL
  -> bounded M3U / PLS indirection when returned by Radio Browser
  -> direct native HTTP read with advertised ICY metadata stripped,
     or audio-only HLS
       master/media playlist -> lowest-bandwidth AAC variant
       live MPEG-TS segments -> PAT/PMT/PES -> ADTS AAC bytes
  -> codec dispatch
       AAC  -> ADTS synchronization -> native AAC decoder
       MP3  -> MPEG frame synchronization -> native MP3 decoder
       Opus -> incremental Ogg demux -> native libSceOpusDec
                                      -> bounded CELT decoder fallback on -502
       Vorbis -> bounded stb_vorbis push-data CPU decoder
       FLAC / Ogg-FLAC -> bounded dr_flac callback CPU decoder
  -> channel conversion and 48 kHz resampling when required
  -> two-second decoded PCM ring
  -> dedicated PS5 AudioOut consumer
```

The producer primes one second of decoded audio before initial playback and
half a second after an underrun. Network reads and decoding therefore continue
independently of AudioOut's synchronous pacing, while the bounded two-second
capacity prevents unlimited latency or memory growth.

An HLS discontinuity drains the old PCM sink, recreates the native AAC decoder,
and clears partial transport/framing state before the next segment. This lets
sample rate, channel count, and AAC profile change without reusing stale output
geometry.

Stop and station switching set the shared cancellation state and call
`sceHttpAbortRequest` for the active playback request. This releases a playback
thread blocked in connection setup, request transmission, or an HTTP read
instead of waiting for the network timeout. Stop and failed-stream teardown
discard queued PCM immediately. Unexpected live-stream failures receive two
bounded reconnect attempts with cancellation-aware backoff. Decoder, network,
and output failures are reported as service state rather than terminating the
UI.

The Ogg parser is project-owned, allocation-free state with bounded packet
storage. It validates full-page CRCs, stream structure, Opus headers, packet
limits, page sequence, continuation, and chained serial transitions before
compressed packets reach the platform decoder. Opus audio packets are bounded
at 61,440 bytes; an orphan continuation at a valid live join is discarded
before later complete packets are dispatched. Opus TOC configurations 16-31
are routed to the general decoder first. A CELT packet rejected with native
result `-502` is retried once with `libSceOpusCeltDec`; decoder failover keeps
the existing PCM sink alive and does not reapply the logical stream's pre-skip.
AAC, MP3, Opus, Vorbis, and FLAC are currently advertised; planned codec and delivery
work is tracked in [`ROADMAP.md`](../ROADMAP.md). The completed hardware-first
review found no callable native Vorbis or FLAC path on the current firmware
baseline. Vorbis therefore uses the validated bounded `stb_vorbis` CPU path,
while native and Ogg-encapsulated FLAC use bounded CPU decoding through
`dr_flac`. See [`VORBIS_VALIDATION.md`](VORBIS_VALIDATION.md) and
[`FLAC_VALIDATION.md`](FLAC_VALIDATION.md). HLS is a transport layer above the existing native AAC
decoder rather than another codec implementation. Its bounded subset supports
relative master/media URLs, live media sequences, discontinuities, and MPEG-TS
with ADTS AAC. It rejects encryption, byte ranges, fMP4/CMAF, low-latency parts,
alternate renditions, video variants, and non-AAC elementary streams. See
[`CODEC_INVESTIGATION.md`](CODEC_INVESTIGATION.md) for evidence and scope.

## Input and text entry

[`src/radio_input.cpp`](../src/radio_input.cpp) translates native DualSense state
into edge-triggered application keys. The D-pad and left analog stick share
navigation behavior; the stick has a dead zone and controlled repeat timing.

[`src/radio_ime.cpp`](../src/radio_ime.cpp) opens the native PS5 on-screen keyboard
for station and filter text. The UI remains controller-only and requires no
mouse or physical keyboard.

## Template build boundary

The project is built directly from a clone of
[`ps5-native-app-boilerplate`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
Clang 18 and lld build the C++20 application; the template-owned native C++
toolchain then validates PS5 imports and writes the development FSELF. `make`,
`make test`, `make lint`, `make ffpfsc`, and `make deploy` are the primary
entry points. `.NET` is needed only for the optional local UFS2 `.ffpkg` tool;
CI and GitHub Releases build only FFPFSC.

RmlUi requires RTTI in its static library, so PS5 Radio adds `-frtti` after the
template's conservative C++ flags. Exceptions remain disabled. The template
runtime owns the global C++ allocation bridge; PS5 Radio gives SDL its own
tracked allocator rather than defining a second global `new`/`delete` bridge.

The dependency, import-facade, and licensing boundaries are described in
[Template port notes](TEMPLATE_PORT.md), [`NOTICE.md`](../NOTICE.md), and
[Native tooling](NATIVE_TOOLING.md).

## Regression checks

The repository keeps small deterministic checks for the parts that do not need
PS5 hardware:

- `tools/check_ui.py`: RML IDs, atlas geometry, licenses, and TGA assets;
- `tools/aac_timing_check.cpp`: AAC timing and resampling calculations;
- `tools/mp3_header_check.cpp`: MPEG version, rate, channel, and frame geometry;
- `tools/icy_metadata_check.cpp`: response-header parsing, split metadata blocks,
  truncation, and read-error propagation;
- `tools/ogg_opus_check.cpp`: split-input Ogg pages, packet continuation,
  chained streams, and malformed-input rejection;
- `tools/vorbis_decoder_check.cpp`: incremental Vorbis decoding and bounded
  malformed/no-progress behavior;
- `tools/flac_decoder_check.cpp`: native and Ogg-FLAC decoding, split input,
  truncation, malformed metadata, and memory/read ceilings;
- `tools/radio_input_check.cpp`: controller edge, dead-zone, and repeat behavior;
- `tools/radio_playlist_check.cpp`: M3U/PLS detection, URL resolution, bounds,
  scheme rejection, and HLS separation;
- `tools/radio_hls_check.cpp`: audio-only master/media parsing, variant selection,
  live media/discontinuity sequences, and unsupported-feature rejection;
- `tools/radio_ts_aac_check.cpp`: split MPEG-TS input, PAT/PMT discovery, PES
  stripping, ADTS extraction, and optional live-segment probing;
- `tools/radio_service_json_check.cpp`: raw and escaped UTF-8 metadata, AAC+,
  query encoding, mirror validation, pagination, and facet parsing;
- `tools/radio_catalog_store_check.cpp`: file-backed SQLite journal mode,
  close/reopen persistence, station, favorite, facet, metadata, ordering,
  update, and pruning behavior;
- `tests/test_radio_text.cpp`: UTF-8 shaping and right-to-left ordering;
- `tests/test_import_stubs.cpp`: PS5 import-manifest SONAME and symbol parsing;
- `tests/test_ui_assets.py`: packaged RML, atlas, icon, and metadata consistency;
- `tools/run-radio-checks.sh`: the host-native codec and SQLite check runner.

Networking, native decoder behavior, AudioOut, IME, and loader lifecycle still
require testing on the intended console environment.
