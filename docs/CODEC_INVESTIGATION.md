# Codec investigation

This document records PS5 Radio's hardware-first codec investigation. It
separates decoder availability from
container and delivery support so the application does not infer a usable
codec path from a library name or a station's metadata.

The complete reusable research record—including reverse-engineering notes,
native API probes, recovered interfaces, and device evidence—is published in
[PS5 Audio Decoding Research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).
This document keeps the PS5 Radio-specific decisions and integration boundaries.

## Decision summary

| Format | Native investigation | Current decision |
| --- | --- | --- |
| AAC / HE-AAC | AAC through `libSceAudiodec` reaches the device-backed AJM service. A dedicated HE-AAC v2 probe confirms 48 kHz SBR reconstruction, but every valid public configuration exposes Parametric Stereo as mono. | Keep native AAC. Use the timing-safe AAC-core fallback and stereo normalization for HE-AAC v2; do not claim unavailable native PS stereo. |
| MP3 | Public codec `0x0002` reaches AJM and is hardware-validated in PS5 Radio. | Keep the native decoder. |
| Ogg Opus | `libSceOpusDec` and `libSceOpusCeltDec` reach AJMI/AJM and are hardware-validated in PS5 Radio. | Keep the native dual-decoder path. |
| Ogg Vorbis | No Vorbis branch exists in the recovered AvPlayer decoder factory. The inspected AJM Vorbis helper prepares headers but exposes no PCM decode job. | Use the bundled, bounded `stb_vorbis` CPU decoder; live playback, stop, AAC switching, and an eleven-minute PS5 run are validated. |
| FLAC | The firmware references an internal CPU FLAC plug-in, but it is absent from the inspected module inventory and cannot be loaded by the application. No AJM or AvPlayer FLAC decode branch was found. | Use the bundled, bounded `dr_flac` CPU decoder for native and Ogg-encapsulated FLAC; host and PS5 playback/lifecycle validation pass. |
| HLS | HLS is segmented delivery, not an audio codec. The bounded playlist and MPEG-TS/AAC path now passes host checks and PS5 lifecycle tests. | Keep unencrypted, audio-only MPEG-TS HLS carrying AAC; reject unsupported HLS shapes explicitly. |

Ogg Vorbis and FLAC are enabled after their bundled software paths passed host
and PS5 validation. Radio Browser HLS/AAC records are enabled after passing live
playback, switching, stop, restart, playlist reload, and a forced hardware
discontinuity. The HE-AAC and release stress evidence is recorded in
[`AUDIO_RELEASE_VALIDATION.md`](AUDIO_RELEASE_VALIDATION.md).

### HE-AAC SBR/PS probe

Disposable game-category probe `PPSA99697` decoded a 24-frame HE-AAC v2 ADTS
fixture through `libSceAudiodec`. With HE enabled, the public 24-byte AAC
configuration produced nonzero 48 kHz mono PCM with energy above 12 kHz,
confirming SBR reconstruction. Disabling HE produced the 24 kHz mono AAC core.

The public and recovered extended configurations were then varied across word
size, AAC configuration, channel count, and the extra AvPlayer field. Every
valid HE configuration still reported one decoded channel. The AvPlayer-style
16-bit word-size configuration was rejected by the public API. Static analysis
of AvPlayer confirmed its 28-byte internal parameter shape, but did not expose
a callable public path that returns Parametric Stereo as two channels.

This is a measured platform boundary, not a container-parser failure. PS5 Radio
retains native AAC offload, correct timing, and bounded stereo normalization;
it does not add a software HE-AAC decoder solely to synthesize PS stereo.

## Firmware and test boundary

The runtime probes below used firmware 6.02 with ShadowMountPlus and game
category packages. Disposable probe identities are included so a result can be
matched to retained development-protocol evidence. They are not application
release identities.

### FLAC module probe

Static inspection found that `libSceAudiodecCpu.sprx` references
`libSceAudiodecCpuFlac.prx` through internal sysmodule ID `0x80000053`. The
referenced implementation is a CPU plug-in, not evidence of AJM-backed offload,
and the plug-in itself was absent from the inspected firmware library set.

`PPSA99653` called `sceSysmoduleLoadModuleInternal(0x80000053)` and remained
stable for the observation window. The receipt was:

```text
PS5_RADIO_FLAC_NATIVE module=0x80000053 load=-2141581312 load_hex=0x805a1000 unload=0
```

The module therefore was not loadable from the tested application context.
Together with the missing binary and the lack of an AJM/AvPlayer FLAC branch,
this closes the native FLAC route for the current firmware baseline.

### AvPlayer source probes

Recovered AvPlayer structures and allocator callbacks were sufficient for
`sceAvPlayerInit` to succeed, but `sceAvPlayerAddSource` rejected every local
control before any stream or audio frame became available:

| Probe | Source | Result |
| --- | --- | --- |
| `PPSA99654` | Vorbis/WebM | `0x806a0003` at `stage=source` |
| `PPSA99655` | Opus/WebM control | `0x806a0003` at `stage=source` |
| `PPSA99656` | AAC/M4A positive control | `0x806a0003` at `stage=source` |

Because the AAC positive control failed at the same entry point, these probes
do **not** prove that AvPlayer lacks Vorbis support. They prove only that this
recovered AddSource path is not usable as a codec discriminator on the tested
firmware. The native Vorbis conclusion instead rests on the static decoder
factory, which contains explicit AAC hardware/software, Opus hardware, AC-3
hardware, and E-AC-3 software branches but no Vorbis branch, plus the absence
of a callable PCM decode path in the inspected Vorbis helper.

## Selected software fallbacks

The Ogg Vorbis and FLAC fallbacks are bundled at immutable upstream revisions
with their licenses and provenance. Both paths stay behind the same bounded
playback worker and PCM queue used by the native codecs.

### Ogg Vorbis: `stb_vorbis`

[`stb_vorbis`](https://github.com/nothings/stb/blob/master/stb_vorbis.c) is a
single-file decoder available under public-domain or MIT terms. Its push-data
API accepts caller-buffered Ogg bytes, reports exactly how many bytes it
consumed, and supports resynchronization after discontinuous input. This maps
directly to PS5 Radio's cancellable network producer without adding libogg or a
new I/O abstraction.

Implementation constraints:

- retain a bounded compressed-input window and reject a stream that exceeds it;
- initialize only after complete Vorbis headers are available;
- convert the decoder's planar float output to interleaved signed-16 PCM;
- reuse the current channel normalization, 48 kHz resampler, and PCM queue;
- recreate the decoder for a chained logical stream or reconnect;
- fuzz truncated headers, malformed pages, and no-progress input on the host.

The implementation keeps a 256 KiB compressed-input window, limits each live
network read to 16 KiB, accepts one or two channels from 8 to 192 kHz, and caps
decoded output at 8,192 frames per channel. It converts planar float samples to
interleaved signed-16 PCM before the shared channel normalizer, resampler, and
queue. The decoder is recreated after a reconnect. See
[`VORBIS_VALIDATION.md`](VORBIS_VALIDATION.md) for host and PS5 evidence.

The upstream API notes that the Vorbis specification does not bound an
individual frame. PS5 Radio therefore enforces its own memory ceiling and fails
the station cleanly instead of growing buffers indefinitely.

[Tremor](https://xiph.org/vorbis/) remains a valid BSD-licensed, integer-only
reference alternative. It is aimed at embedded systems without efficient
floating point and would add a larger multi-file integration; it offers no
clear advantage for the PS5's x86-64 CPU. The reference `libvorbis` decoder is
also mature but adds libogg and a broader API surface. Neither is the initial
choice.

### FLAC: `dr_flac`

[`dr_flac`](https://github.com/mackron/dr_libs/blob/master/dr_flac.h) is a
single-file decoder offered under public-domain or MIT-0 terms. It supports
native and Ogg-encapsulated FLAC, custom read/seek/tell callbacks, incremental
PCM reads, signed-16 output, and a relaxed open mode intended for broadcast or
internet-radio streams that begin without a header.

The production adapter uses strict opening, keeps CRC validation enabled, and
bridges reads to the active cancellable HTTP request. It accepts one or two
channels at 8 to 192 kHz, rejects source blocks above 8,192 frames, emits at
most 4,096 signed-16 frames per call, and caps both decoder allocations and
opening reads at 1 MiB. Seeking is forward-only and drains through the same
bounded callback; standard file I/O is disabled. The shared sink performs
channel normalization, 48 kHz resampling, queueing, cancellation, and output.

Deterministic host checks cover native and Ogg-encapsulated FLAC, small split
reads, truncation, invalid signatures, oversized source blocks, and a 1 MiB
opening-scan ceiling. PS5 tests cover native FLAC and Ogg-FLAC playback, stop,
AAC switching, and a ten-minute sustained Ogg-FLAC session. See
[`FLAC_VALIDATION.md`](FLAC_VALIDATION.md).

The decoder is CPU-based. Lossless streams have materially higher network and
PCM throughput than the native compressed formats, so its explicit memory and
stream-shape ceilings remain part of the supported boundary.

## HLS/AAC implementation boundary

[RFC 8216](https://www.rfc-editor.org/rfc/rfc8216.html) defines HLS as playlists
and sequential media segments. It does not imply a new audio decoder. The
smallest useful first implementation can reuse the existing PS5 IPTV client's
bounded components for:

- master and media playlists;
- relative URL resolution and variant selection;
- media-sequence deduplication and live playlist reload;
- discontinuity handling and cancellation-aware retries;
- MPEG-TS PAT, PMT, PES, and ADTS extraction.

That transport parser currently requires a video PID and accepts optional AAC.
PS5 Radio needs only a small adaptation that accepts an audio-only PMT with one
AAC ADTS stream and submits complete ADTS frames to the already validated
native AAC decoder.

The first slice rejects encryption, byte ranges, fMP4/CMAF, low-latency
partial segments, alternate rendition groups, multi-program transport streams,
and non-AAC audio. Those features can be added independently after the simple
MPEG-TS/AAC path is stable. Simple non-HLS M3U and PLS indirection should be
resolved before HLS because it requires no segment demuxer.

The implemented parser preserves PES state when a repeated PMT announces the
same AAC PID. Resetting that state at every PMT had truncated an AAC access unit
in a current Radio Browser segment and surfaced only as generic AudioDec error
`0x807f0000`. After the fix, the extracted ADTS bytes are identical to FFmpeg's
demuxed output for the captured segment. For `mp4a.40.29` manifests, PS5 Radio
also propagates the declared stereo intent into the existing native-decoder
workaround. The stable path decodes the 24 kHz mono AAC core and normalizes it
to the 48 kHz stereo AudioOut contract instead of enabling the native mode that
produced slow-motion playback. See [HLS/AAC validation](HLS_VALIDATION.md).

## Validation gates

A format is included in Radio Browser queries only after these core gates pass:

1. Redistribution terms and immutable decoder provenance are retained.
2. Deterministic host tests cover valid split input, malformed/truncated data,
   bounded memory, cancellation or no-progress behavior, and expected PCM.
3. A game-category build plays the format on the target PS5 and records output
   sample rate and channel count.
4. Stop, switching to the AAC control, and clean title teardown succeed without
   a runtime crash.
5. A sustained playback run completes for the new decoder path.

Representative sample-rate/channel variants and naturally occurring CELT
fallbacks remain useful coverage. Fault-injected reconnect, underrun, malformed
Ogg, HLS discontinuity, ICY metadata, and rapid cross-codec switching now have
persisted PS5 evidence in
[`AUDIO_RELEASE_VALIDATION.md`](AUDIO_RELEASE_VALIDATION.md).

The shared reconnect policy permits three consecutive failures with bounded
250, 500, and 1,000 ms backoff. An attempt is considered stable only after it
has produced real AudioOut data and remained active for 30 seconds; a later
transport failure then renews the consecutive-failure budget. Host checks cover
the retry state machine, and the sustained Ogg-FLAC device run validates the
long-lived behavior.

The preferred implementation order is simple M3U/PLS resolution, HLS/AAC,
Ogg Vorbis, and finally FLAC. This adds the broadest station coverage while
introducing only one new risk boundary at a time.
