# Roadmap

PS5 Radio's implemented playback baseline includes the complete RmlUi interface,
Radio Browser catalog and search, persistent cache and favorites, native input
and IME, AAC, MP3, Ogg Opus, Ogg Vorbis, native-container FLAC, and Ogg-FLAC
playback through PS5 AudioOut. Hardware probing confirms native HE-AAC SBR
reconstruction. The public decoder exposes HE-AAC v2 Parametric Stereo as mono,
so the release path uses the timing-safe AAC core and duplicates normalized
output to stereo instead of claiming unavailable native PS stereo.

New stream formats are enabled in Radio Browser queries only after bounded host
checks plus on-device playback, stop, station switching, sustained playback,
and clean lifecycle checks. The current release also passed fault-injected
reconnect, underrun, malformed Ogg, HLS discontinuity, and rapid-switch tests.
This keeps known unsupported codec categories out of the catalog while keeping
edge-case claims evidence-based. Radio Browser's ambiguous generic `OGG`
records remain visible and are signature-probed at playback; an unknown Ogg
payload fails before it reaches a mismatched decoder.

## Hardware-first decoder policy

Every new codec begins with an investigation of the PS5's existing decoder
libraries. Prefer a callable platform decoder when it can be used reliably by
a homebrew application. Bundle a software decoder only when the native path is
absent, inaccessible, or fails the target validation matrix.

The investigation gate for each codec is:

1. Inventory relevant system libraries, exports, SDK bindings, and existing
   IDA databases.
2. Establish whether the path reaches the device-backed AJM service, a CPU
   decoder module, or another implementation.
3. Build the smallest target probe that decodes representative frames to PCM.
4. Record runtime behavior, firmware context, and failure modes before choosing
   the production decoder.

Use the term **hardware/firmware audio offload** unless runtime evidence proves
a more specific hardware implementation.

The investigation is complete for the current firmware baseline.
`libSceAudiodec` plus AJM remains the native AAC and MP3 path, while
`libSceOpusDec` / `libSceOpusCeltDec` plus AJMI remains the native Opus path.
No callable native Vorbis or FLAC route was found. The complete evidence and
selected software fallbacks are recorded in
[`docs/CODEC_INVESTIGATION.md`](docs/CODEC_INVESTIGATION.md).

## 1. MP3 playback

- [x] Add MP3 codec detection to the existing compressed-audio-to-PCM path.
- [x] Preserve the existing native AAC implementation in the shared stream loop.
- [x] Validate the native `libSceAudiodec` MP3 path (`codec 0x0002`) and derive
  sample rate and channel count from MPEG frame headers.
- [x] Retain the native path without adding a software MP3 dependency.
- [x] Strip advertised ICY metadata from direct streams when a server ignores
  `Icy-MetaData: 0`, without exposing metadata bytes to codec framing.
- [ ] Validate representative sample rates, channel layouts, and malformed
  streams on PS5 before the next release.

The first live hardware proof decoded a 48 kHz stereo frame into 4,608 bytes of
signed-16 PCM and reached AudioOut. Broader stream-shape and switching tests
remain before the next release.

## 2. Ogg Opus and Ogg Vorbis

- [x] Add bounded incremental Ogg demuxing for live Opus radio streams,
  including continued packets and chained logical streams.
- [x] Validate `libSceOpusDec` on PS5 with a known 20 ms, 48 kHz stereo packet.
- [x] Integrate native Opus decoding, pre-skip handling, and the existing
  AudioOut sink into the production player.
- [x] Confirm the production integration launches on PS5 and admits explicitly
  identified Opus stations from Radio Browser's broader OGG category.
- [x] Validate Ogg page CRCs before dispatch and apply Opus output gain,
  pre-skip, and end-granule trimming for container-spec compliance.
- [x] Accept bounded Opus packets through 61,440 bytes and discard an orphan
  continued packet when joining an Icecast stream mid-broadcast.
- [x] Confirm audible playback against live Radio Browser Opus stations.
- [x] Validate the decoded-PCM queue and direct Opus-to-AAC and AAC-to-Opus
  station switches on PS5 without a runtime crash.
- [x] Validate immediate stop from buffered AAC playback.
- [x] Validate a ten-minute sustained Opus session and clean stop without a
  runtime crash.
- [x] Validate the dedicated `libSceOpusCeltDec` codec-16 path with live
  CELT-only WALM packets and route TOC configurations 16-31 to it.
- [x] Add cancellation-aware request handling, bounded live-stream reconnects,
  immediate PCM discard on stop/error, and one alternate native decoder retry
  for CELT packets that return `-502`.
- [x] Validate live Opus stop and station switching on PS5; the measured stop
  completed in 67 ms before a second stream reached 48 kHz stereo playback.
- [x] Validate induced reconnect and underrun behavior with persisted on-device
  telemetry; both resumed or remained responsive without a crash.
- [ ] Capture a naturally occurring live `-502` CELT fallback.
- [x] Investigate AvPlayer, AJM helpers, and the firmware library inventory for
  a callable native Vorbis path; none was found.
- [x] Select `stb_vorbis` as the smallest redistributable software candidate.
- [x] Vendor a pinned `stb_vorbis` revision and retain its MIT license.
- [x] Integrate bounded push-data decoding and normalize decoded channels and
  sample rates for the current output path.
- [x] Validate live 44.1 kHz stereo playback, stop, and direct switching back
  to AAC on PS5.
- [x] Reject CRC-invalid, truncated, and inconsistent Ogg pages and decode PCM
  from both logical streams in a chained Vorbis host regression.
- [x] Validate induced reconnects and rapid repeated cross-codec station
  switching on PS5.

The Opus packet-to-PCM probe produced the expected 960 stereo frames (3,840
signed-16 PCM bytes) with nonzero output. Ogg Vorbis is enabled through the
bounded `stb_vorbis` CPU fallback after passing incremental host checks and live
PS5 playback, stop, AAC-switch, and sustained-playback validation. Extended
container-compliance and fault-injection checks remain tracked separately from
basic format support.

## 3. FLAC

- [x] Investigate AvPlayer, AJM, and the firmware library inventory for a
  callable FLAC path. The only discovered plug-in is CPU-based, absent from
  the inspected firmware set, and failed its runtime module-load probe.
- [x] Select `dr_flac` as the smallest redistributable software candidate.
- [x] Vendor a pinned `dr_flac` revision and retain its MIT-0 license.
- [x] Integrate cancellable callback reads and signed-16 PCM output.
- [x] Bound decoder allocations and opening reads to 1 MiB, source blocks to
  8,192 frames, and PCM reads to 4,096 frames.
- [x] Validate native FLAC and Ogg-FLAC playback, stop, and AAC switching on
  PS5.
- [x] Validate a ten-minute sustained Ogg-FLAC session on PS5.
- [x] Expose FLAC stations after the bounded runtime passed device validation.
- [x] Add induced reconnect, underrun, malformed-input, and rapid-switch
  robustness evidence through the shared stress matrix and host regressions.

`dr_flac` is the bounded CPU fallback because firmware 6.02 exposes no callable
native FLAC decoder to this application. Host checks cover native and
Ogg-encapsulated FLAC, split reads, truncation, invalid signatures, oversized
blocks, and bounded opening scans. See
[`docs/FLAC_VALIDATION.md`](docs/FLAC_VALIDATION.md).

## 4. HLS and playlist delivery

HLS is a delivery protocol rather than a codec and is tracked separately.

- [x] Identify reusable bounded master/media playlist and MPEG-TS/AAC components
  in the related PS5 IPTV implementation.
- [x] Adapt the TS parser to accept audio-only PMTs and submit ADTS AAC frames to
  PS5 Radio's native decoder.
- [x] Fetch segments and follow live updates while supporting only codecs
  already validated by the continuous-stream player.
- [x] Handle cancellation, variant selection, discontinuities, retries, and stale
  segments.
- [x] Resolve bounded M3U and PLS playlists when Radio Browser does not provide a
  usable direct stream URL.
- [x] Reject encrypted, byte-range, fMP4/CMAF, low-latency, and non-AAC variants
  explicitly in the first release.
- [x] Validate the parser against a current audio-only HLS entry and MPEG-TS
  segment returned by the Radio Browser catalog.
- [x] Validate live HLS/AAC playback, stop, restart, station switching, and
  playlist reloads on PS5 hardware.
- [x] Probe HE-AAC v2 on hardware: native SBR reaches 48 kHz, while public
  configurations expose Parametric Stereo as mono; retain the stable core
  fallback and stereo normalization.
- [x] Hardware-exercise an HLS discontinuity that changes output from 44.1 kHz
  stereo to 48 kHz mono without a crash or stale decoder geometry.

## 5. Large Radio Browser catalog

- [x] Replace the 480-record binary cache with a dynamically sized SQLite
  catalog, while importing existing favorites and cached stations once.
- [x] Fetch healthy supported codecs through bounded server-side pagination,
  including AAC+ as a distinct catalog format.
- [x] Fetch country, genre, and language facets and issue filtered advanced
  searches on demand.
- [x] Discover Radio Browser mirrors and rotate requests after failures.
- [x] Preserve a usable cache across partial, empty, interrupted, or failed
  full synchronization.
- [x] Validate a 56,248-station first sync, atomic promotion, cached restart,
  database integrity, and clean teardown on PS5 hardware.
- [x] Validate mixed filtered search, reset, and filtered paging responsiveness
  with controller-driven PS5 acceptance tests.
- [ ] Validate manual refresh responsiveness with a controller-driven PS5
  acceptance test.

## Later improvements

- Improve stream-title and ICY metadata presentation.
- Add bounded favicon fetching and caching after memory and network limits are
  defined.
- Add user-facing volume controls if the verified AudioOut path supports them
  consistently.
- Expand localization while retaining deterministic television rendering.
- Reduce packaged font-atlas size without reducing currently supported scripts.

## Definition of done for a new format

- All bundled decoder dependencies permit redistribution and are recorded in
  `NOTICE.md`.
- The hardware-first investigation result and selected decoder path are
  documented, including whether decoding is offloaded or CPU-based.
- The packaged build advertises only the codecs it enables.
- Controller input remains responsive during network and decoder activity.
- Unsupported containers and codec variants fail without crashing.
- Host checks cover deterministic parsing or framing behavior.
- PS5 hardware results are documented in the release notes before publishing.
