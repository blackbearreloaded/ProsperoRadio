# Roadmap

PSRadio's current release baseline includes the complete RmlUi interface,
Radio Browser catalog and search, persistent cache and favorites, native input
and IME, and AAC/HE-AAC playback through PS5 AudioOut.

New stream formats are enabled in Radio Browser queries only after they pass
on-device playback, stop, station-switching, reconnect, and error-recovery
checks. This prevents the catalog from advertising stations that the packaged
player cannot use.

## 1. MP3 playback

- Add codec detection and a shared compressed-audio-to-PCM boundary.
- Preserve the existing native AAC implementation behind that boundary.
- Integrate a redistributable open-source MP3 decoder and frame synchronizer.
- Handle ICY metadata without disrupting MP3 frame alignment.
- Validate representative sample rates, channel layouts, and malformed streams
  on PS5 before exposing AAC and MP3 stations together.

MP3 is the next playback milestone because it can reuse the existing continuous
HTTP, buffering, PCM conversion, and AudioOut paths.

## 2. Ogg Vorbis and Ogg Opus

- Add the minimal Ogg demuxing required by live radio streams.
- Integrate redistributable Vorbis and Opus decoders.
- Normalize decoded channels and sample rates for the current output path.
- Validate malformed pages, chained streams, reconnects, long-running playback,
  and rapid station switching.

Vorbis and Opus share an Ogg-container milestone but remain independently
gated capabilities.

## 3. FLAC

- Add a small redistributable FLAC decoder.
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
- Add release automation once the public repository and release naming scheme
  are stable.

## Definition of done for a new format

- All bundled decoder dependencies permit redistribution and are recorded in
  `NOTICE.md`.
- The packaged build advertises only the codecs it enables.
- Controller input remains responsive during network and decoder activity.
- Unsupported containers and codec variants fail without crashing.
- Host checks cover deterministic parsing or framing behavior.
- PS5 hardware results are documented in the release notes before publishing.
