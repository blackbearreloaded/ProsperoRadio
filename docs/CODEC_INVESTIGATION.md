# Codec investigation

This document records PSRadio's hardware-first codec investigation. It
separates decoder availability from
container and delivery support so the application does not infer a usable
codec path from a library name or a station's metadata.

## Decision summary

| Format | Native investigation | Current decision |
| --- | --- | --- |
| AAC / HE-AAC | `libSceAudiodec` reaches the device-backed AJM service and is hardware-validated in PSRadio. | Keep the native decoder. |
| MP3 | Public codec `0x0002` reaches AJM and is hardware-validated in PSRadio. | Keep the native decoder. |
| Ogg Opus | `libSceOpusDec` and `libSceOpusCeltDec` reach AJMI/AJM and are hardware-validated in PSRadio. | Keep the native dual-decoder path. |
| Ogg Vorbis | No Vorbis branch exists in the recovered AvPlayer decoder factory. The inspected AJM Vorbis helper prepares headers but exposes no PCM decode job. | Use the bundled, bounded `stb_vorbis` CPU decoder; live playback, stop, and AAC switching are validated. |
| FLAC | The firmware references an internal CPU FLAC plug-in, but it is absent from the inspected module inventory and cannot be loaded by the application. No AJM or AvPlayer FLAC decode branch was found. | Use a small redistributable software decoder; `dr_flac` is the selected candidate. |
| HLS | HLS is segmented delivery, not an audio codec. The bounded playlist and MPEG-TS/AAC path now passes host checks and PS5 lifecycle tests. | Keep unencrypted, audio-only MPEG-TS HLS carrying AAC; reject unsupported HLS shapes explicitly. |

Ogg Vorbis is enabled after its bundled software path passed host and PS5
validation. FLAC remains disabled until its software path passes the same
gates. Radio Browser HLS/AAC records are enabled after passing live
playback, switching, stop, restart, and playlist-reload checks. Audible HE-AAC
fallback fidelity and a live discontinuity remain release gates.

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
PSRADIO_FLAC_NATIVE module=0x80000053 load=-2141581312 load_hex=0x805a1000 unload=0
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

The Ogg Vorbis fallback is bundled at an immutable upstream revision with its
license and provenance. FLAC remains the selected but not yet bundled fallback.
Both paths stay behind the same bounded playback worker and PCM queue used by
the native codecs.

### Ogg Vorbis: `stb_vorbis`

[`stb_vorbis`](https://github.com/nothings/stb/blob/master/stb_vorbis.c) is a
single-file decoder available under public-domain or MIT terms. Its push-data
API accepts caller-buffered Ogg bytes, reports exactly how many bytes it
consumed, and supports resynchronization after discontinuous input. This maps
directly to PSRadio's cancellable network producer without adding libogg or a
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
individual frame. PSRadio therefore enforces its own memory ceiling and fails
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

Implementation constraints:

- bridge its read callback to the active cancellable HTTP request;
- use strict open when STREAMINFO is present and bounded relaxed open only for
  a mid-stream start;
- reject unsupported channel counts, sample rates, and unreasonable block
  sizes before allocating output buffers;
- keep CRC validation enabled;
- normalize to the existing signed-16, 48 kHz stereo output contract;
- measure CPU use, compressed-buffer peaks, and stop latency on PS5 before the
  catalog advertises FLAC.

The decoder is CPU-based. Lossless streams also have materially higher network
and PCM throughput than the current compressed formats, so device validation is
a release gate rather than a formality.

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
PSRadio needs only a small adaptation that accepts an audio-only PMT with one
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
demuxed output for the captured segment. For `mp4a.40.29` manifests, PSRadio
also propagates the declared stereo intent into the existing native-decoder
workaround. The stable path decodes the 24 kHz mono AAC core and normalizes it
to the 48 kHz stereo AudioOut contract instead of enabling the native mode that
produced slow-motion playback. See [HLS/AAC validation](HLS_VALIDATION.md).

## Validation gates

Each new path must pass all of the following before its codec or delivery type
is included in Radio Browser queries:

1. Deterministic host tests for framing, split input, malformed data, bounded
   memory, cancellation, and no-progress behavior.
2. A clean game-category launch on the target PS5.
3. Audible playback for representative mono/stereo and sample-rate variants.
4. Stop while connecting, while blocked in a read, and with queued PCM.
5. Direct switching to and from AAC and Opus without a crash.
6. Reconnect, underrun recovery, and at least ten minutes of sustained playback.
7. Persisted development-protocol evidence containing the decoder, output rate,
   channel count, and terminal state.

The preferred implementation order is simple M3U/PLS resolution, HLS/AAC,
Ogg Vorbis, and finally FLAC. This adds the broadest station coverage while
introducing only one new risk boundary at a time.
