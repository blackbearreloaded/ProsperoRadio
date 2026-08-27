# Architecture

PSRadio is a native C/C++ application with a deliberately small runtime. RmlUi
owns document layout, SDL2 owns presentation, and PS5 platform services provide
networking, input, text entry, compressed-audio decoding, and PCM output.

## Runtime overview

```text
DualSense / IME
      |
      v
radio_input.c + radio_ime.c
      |
      v
radio_app.cpp <----> RmlUi document and RCSS
      |
      v
radio_service.c
  |        |        |
  |        |        +--> /download0 cache and favorites
  |        +-----------> AAC / MP3 / Opus -> native decoders ---+
  |                       Ogg Vorbis -> bounded CPU decoder ------+-> PCM -> AudioOut
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

## RmlUi document and application state

[`ui/main.rml`](../ui/main.rml) contains the complete document and
[`ui/styles/app.rcss`](../ui/styles/app.rcss) defines the fixed television
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

PSRadio registers [`BitmapFontEngine`](../src/bitmap_font_engine.cpp) as RmlUi's
font backend. The checked-in BMFont metadata and exact alpha atlases under
`ui/fonts/lvgl-bitmap/` are build inputs and ship with the application.

The base Montserrat faces cover interface text at 20, 24, 28, 32, 36, 40, and
48 px. Extended 20, 24, 28, and 32 px pages contain 17,854 additional glyphs
per size. The custom `.rta` format stores 4-bit alpha pages losslessly using a
small zero-run/literal encoding and is decoded by the SDL texture loader.

[`src/radio_text.cpp`](../src/radio_text.cpp) provides the minimum layout support
needed beyond RmlUi's left-to-right bitmap interface: Arabic and Persian
presentation-form selection plus visual ordering for right-to-left metadata.
Font sources and licenses are documented in [`NOTICE.md`](../NOTICE.md).

## Radio Browser and persistence

[`src/radio_service.c`](../src/radio_service.c) uses native PS5 network, SSL, and
HTTP services. Nine bounded Radio Browser feeds are merged into one station
catalog. AAC, MP3, Opus, and Vorbis variants are requested for each ranking:

- popular by click count;
- trending by click trend;
- top rated by vote count.

The queries use `hidebroken=true`. Radio Browser currently reports Opus streams
under its broader `OGG` codec label, so PSRadio admits only OGG entries whose
resolved URL explicitly identifies Opus and normalizes their displayed codec to
`OPUS` or `VORBIS`. AAC, MP3, and normalized Ogg entries are parsed into the fixed-size
`radio_station_t` model, merged by station UUID, ranked across codecs, and
capped at 480 stations. Catalog refresh runs on a background thread and
publishes changes under an SDL mutex.

Radio Browser entries flagged as HLS are admitted only when their catalog codec
is AAC. PSRadio intentionally has no custom station or playlist input; every
playable URL originates in the fetched or cached Radio Browser catalog.

The latest usable catalog and favorite UUIDs are stored atomically under
`/download0`:

```text
/download0/radio-browser-cache.bin
/download0/radio-browser-favorites.bin
```

Temporary files are written and renamed so an interrupted write does not
replace the last complete state. The app can browse a valid cached catalog when
Radio Browser is temporarily unavailable.

## Audio pipeline

Playback runs on a separate native thread:

```text
resolved station URL
  -> bounded M3U / PLS indirection when returned by Radio Browser
  -> direct native HTTP read, or audio-only HLS
       master/media playlist -> lowest-bandwidth AAC variant
       live MPEG-TS segments -> PAT/PMT/PES -> ADTS AAC bytes
  -> codec dispatch
       AAC  -> ADTS synchronization -> native AAC decoder
       MP3  -> MPEG frame synchronization -> native MP3 decoder
       Opus -> incremental Ogg demux -> native libSceOpusDec
                                      -> bounded CELT decoder fallback on -502
       Vorbis -> bounded stb_vorbis push-data CPU decoder
  -> channel conversion and 48 kHz resampling when required
  -> two-second decoded PCM ring
  -> dedicated PS5 AudioOut consumer
```

The producer primes one second of decoded audio before initial playback and
half a second after an underrun. Network reads and decoding therefore continue
independently of AudioOut's synchronous pacing, while the bounded two-second
capacity prevents unlimited latency or memory growth.

Stop and station switching set the shared cancellation state and call
`sceHttpAbortRequest` for the active playback request. This releases a playback
thread blocked in connection setup, request transmission, or an HTTP read
instead of waiting for the network timeout. Stop and failed-stream teardown
discard queued PCM immediately. Unexpected live-stream failures receive two
bounded reconnect attempts with cancellation-aware backoff. Decoder, network,
and output failures are reported as service state rather than terminating the
UI.

The Ogg parser is project-owned, allocation-free state with bounded packet
storage. It validates stream structure, Opus headers, packet limits, page
sequence, continuation, and chained serial transitions before compressed
packets reach the platform decoder. Opus TOC configurations 16-31 are routed to
the general decoder first. A CELT packet rejected with native result `-502` is
retried once with `libSceOpusCeltDec`; decoder failover keeps the existing PCM
sink alive and does not reapply the logical stream's pre-skip.
AAC, MP3, Opus, and Vorbis are currently advertised; planned codec and delivery
work is tracked in [`ROADMAP.md`](../ROADMAP.md). The completed hardware-first
review found no callable native Vorbis or FLAC path on the current firmware
baseline. Vorbis therefore uses the validated bounded `stb_vorbis` CPU path;
FLAC's selected future path is bounded CPU decoding through `dr_flac`. See
[`VORBIS_VALIDATION.md`](VORBIS_VALIDATION.md). HLS is a transport layer above the existing native AAC
decoder rather than another codec implementation. Its bounded subset supports
relative master/media URLs, live media sequences, discontinuities, and MPEG-TS
with ADTS AAC. It rejects encryption, byte ranges, fMP4/CMAF, low-latency parts,
alternate renditions, video variants, and non-AAC elementary streams. See
[`CODEC_INVESTIGATION.md`](CODEC_INVESTIGATION.md) for evidence and scope.

## Input and text entry

[`src/radio_input.c`](../src/radio_input.c) translates native DualSense state
into edge-triggered application keys. The D-pad and left analog stick share
navigation behavior; the stick has a dead zone and controlled repeat timing.

[`src/radio_ime.c`](../src/radio_ime.c) opens the native PS5 on-screen keyboard
for station and filter text. The UI remains controller-only and requires no
mouse or physical keyboard.

## Host build boundary

The runtime is native. .NET is used only by the Windows build to drive the
pinned SharpProspero linker and FSELF writer. Clang 18 runs inside WSL using
the PS5 Payload SDK target. `build.ps1` validates all declared inputs, hashes
the clean-room runtime shim, builds the ELF/FSELF, and assembles the selected
output under `dist/`.

The dependency and licensing boundary is described in
[`NOTICE.md`](../NOTICE.md); build details are in
[`GETTING_STARTED.md`](GETTING_STARTED.md) and
[`CSHARP_TOOLING.md`](CSHARP_TOOLING.md).

## Regression checks

The repository keeps small deterministic checks for the parts that do not need
PS5 hardware:

- `tools/check_ui.py`: RML IDs, atlas geometry, licenses, and TGA assets;
- `tools/aac_timing_check.c`: AAC timing and resampling calculations;
- `tools/mp3_header_check.c`: MPEG version, rate, channel, and frame geometry;
- `tools/ogg_opus_check.c`: split-input Ogg pages, packet continuation,
  chained streams, and malformed-input rejection;
- `tools/radio_input_check.c`: controller edge, dead-zone, and repeat behavior;
- `tools/radio_playlist_check.c`: M3U/PLS detection, URL resolution, bounds,
  scheme rejection, and HLS separation;
- `tools/radio_hls_check.c`: audio-only master/media parsing, variant selection,
  live sequences, discontinuities, and unsupported-feature rejection;
- `tools/radio_ts_aac_check.c`: split MPEG-TS input, PAT/PMT discovery, PES
  stripping, ADTS extraction, and optional live-segment probing;
- `tools/radio_service_json_check.c`: raw and escaped UTF-8 catalog metadata;
- `tools/radio_text_check.cpp`: UTF-8 shaping and right-to-left ordering;
- `tools/inspect.ps1`: ELF/FSELF structure and required imports.

Networking, native decoder behavior, AudioOut, IME, and loader lifecycle still
require testing on the intended console environment.
