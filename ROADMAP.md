# Roadmap

PSRadio's current release baseline includes the complete RmlUi interface,
Radio Browser catalog and search, persistent cache and favorites, native input
and IME, AAC/HE-AAC playback, and an integrated Ogg Opus path through PS5
AudioOut.

New stream formats are enabled in Radio Browser queries only after they pass
on-device playback, stop, station-switching, reconnect, and error-recovery
checks. This prevents the catalog from advertising stations that the packaged
player cannot use.

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

Current candidates are `libSceAudiodec` plus AJM for AAC and MP3,
`libSceOpusDec` / `libSceOpusCeltDec` plus AJMI for Opus, and AvPlayer for
container-managed tracks. No dedicated Vorbis or FLAC decoder library has been
identified in the current firmware library inventory.

## 1. MP3 playback

- Add codec detection and a shared compressed-audio-to-PCM boundary.
- Preserve the existing native AAC implementation behind that boundary.
- Validate the native `libSceAudiodec` MP3 path (`codec 0x0002`) and derive
  sample rate and channel count from MPEG frame headers.
- Use a redistributable open-source MP3 decoder only if the native path fails
  the hardware-first investigation gate.
- Handle ICY metadata without disrupting MP3 frame alignment.
- Validate representative sample rates, channel layouts, and malformed streams
  on PS5 before exposing AAC and MP3 stations together.

MP3 is the next playback milestone because it can reuse the existing continuous
HTTP, buffering, PCM conversion, and AudioOut paths.

## 2. Ogg Opus and Ogg Vorbis

- [x] Add bounded incremental Ogg demuxing for live Opus radio streams,
  including continued packets and chained logical streams.
- [x] Validate `libSceOpusDec` on PS5 with a known 20 ms, 48 kHz stereo packet.
- [x] Integrate native Opus decoding, pre-skip handling, and the existing
  AudioOut sink into the production player.
- [x] Confirm the production integration launches on PS5 and admits explicitly
  identified Opus stations from Radio Browser's broader OGG category.
- [ ] Validate Ogg page CRCs before dispatch and apply Opus output gain and
  end-granule trimming for full container-spec compliance.
- [x] Confirm audible playback against live Radio Browser Opus stations.
- [x] Validate the decoded-PCM queue and a direct Opus-to-AAC station switch on
  PS5 without a runtime crash.
- [x] Validate immediate stop from buffered AAC playback.
- [ ] Validate rapid AAC-to-Opus switching, reconnect behavior, sustained
  playback, and the native decoder's `-502` response to the WALM Opus stream
  variant.
- Investigate AvPlayer and the firmware library inventory for a callable
  Vorbis path.
- Integrate redistributable software decoders only for formats without a
  validated native route.
- Normalize decoded channels and sample rates for the current output path.
- Validate malformed pages, chained streams, reconnects, long-running playback,
  and rapid station switching.

The Opus packet-to-PCM probe produced the expected 960 stereo frames (3,840
signed-16 PCM bytes) with nonzero output. Vorbis remains a separately gated
capability and will not be advertised until its own decoder path is validated.

## 3. FLAC

- Investigate AvPlayer and the firmware library inventory for a callable FLAC
  path before selecting a decoder.
- Add a small redistributable FLAC decoder only if no usable native route is
  found.
- Size compressed and PCM buffers for higher-bitrate lossless streams.
- Measure memory use, underrun behavior, and sustained playback on PS5.
- Expose FLAC stations only when the runtime cost remains acceptable.

## 4. HLS and playlist delivery

HLS is a delivery protocol rather than a codec and is tracked separately.

- Parse master and media playlists, fetch segments, and follow live updates.
- Support only codecs already validated by the continuous-stream player.
- Handle cancellation, variant selection, discontinuities, retries, and stale
  segments.
- Resolve simple M3U and PLS playlists when Radio Browser does not provide a
  usable direct stream URL.

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
