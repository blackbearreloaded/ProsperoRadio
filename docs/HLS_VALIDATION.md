# HLS/AAC validation

ProsperoRadio supports a deliberately bounded HLS subset for AAC stations returned
by Radio Browser. HLS is handled as delivery: playlists and MPEG-TS are parsed
locally, while complete ADTS frames continue through the existing native
`libSceAudiodec` / AJM AAC path and PS5 AudioOut sink.

## Supported boundary

- master and media playlists with relative URL resolution;
- lowest-bandwidth audio-only AAC variant selection;
- live media sequences, reloads, stale-segment suppression, retries, and
  inline or discontinuity-sequence resets;
- MPEG-TS PAT, PMT, PES, and ADTS extraction;
- cancellation during requests and playlist waits;
- unencrypted, whole-segment MPEG-TS only.

ProsperoRadio rejects encryption, byte ranges, fMP4/CMAF, low-latency partial
segments, alternate rendition groups, video variants, multi-program transport
streams, and non-AAC HLS. The application accepts no custom station or playlist
input; every HLS URL originates in the fetched or cached Radio Browser catalog.

## Decoder failure and transport fix

The first hardware run (`PPSA99659`) reached the native decoder but returned
`0x807f0000`. IDA analysis of `libSceAudiodec` identified this as the generic
AudioDec API failure used when the underlying AJM sideband reports a codec
error; the lower-level result is stored in the AAC result structure.

A captured RTL MPEG-TS segment exposed the transport defect. The stream repeats
its PMT while an AAC PES is in progress. ProsperoRadio reset PES assembly on every
valid PMT, even when the announced AAC PID was unchanged, and therefore dropped
the tail of an AAC frame. The parser now resets PES state only when the AAC PID
actually changes. A host regression inserts a repeated PMT inside a multi-packet
PES, and the captured segment's extracted ADTS output is byte-identical to
FFmpeg's output:

```text
b24ad0b0f398e2e73e2b296c4b3e5f1c15a72471d5c36dd7753e530226d64fc8
```

## HE-AAC timing

The validated RTL master advertises `mp4a.40.29`, while its ADTS core is 24 kHz
mono AAC. HLS initially discarded the manifest's stereo intent, bypassing an
existing native HE-AAC workaround and producing slow-motion audio on PS5.

Variant selection now records two source channels for exact `mp4a.40.29`
manifests. This activates the stable decoder configuration: decode the 24 kHz
mono AAC core, then normalize it through the shared 48 kHz stereo output path.
A dedicated hardware probe subsequently confirmed native 48 kHz SBR output but
also showed that every valid public configuration exposes HE-AAC v2 Parametric
Stereo as mono. The fallback therefore preserves correct timing and the PS5
stereo output contract without claiming unavailable native PS stereo.

## Hardware lifecycle evidence

All runs used firmware 6.02, game-category app folders, ShadowMountPlus, klog on
TCP 3232, Chiaki-ng observation, and the exclusive investigation lock.

| Title | Result |
| --- | --- |
| `PPSA99660` | Corrected TS demux entered sustained HLS playback and switched HLS -> MP3 -> HLS without a crash; exposed slow native HE timing. |
| `PPSA99661` | Manifest channel intent selected the stable 24 kHz mono core path; HLS -> MP3 -> HLS switching completed normally. |
| `PPSA99663` | HLS played for 40 seconds across playlist reloads, stopped, restarted, and remained in `Playing` state for the final capture; normal title close released runtime layers. |
| `PPSA99695` | Current RTL HLS UUID `042d3140-227c-4fac-9387-4903b692d5f2` reached 24 kHz mono AAC-core playback, stopped, and later accepted a direct switch from 44.1 kHz stereo Vorbis before returning to stopped state. |

The shared `PPSA99695` matrix image used production audio source commit
`4283866`. Its package SHA-256 was
`525062C378A304A365BDC90D65E44204B2F0A087EA60C9AE434AD6092AAE07D0`;
its `eboot.bin` SHA-256 was
`83B5E2FF6CD6A8FB975E8672860081DC5A9BA9D8AAA459712C3880BE652A50EE`.

The final run produced no AudioDec failure, fatal signal, app-crash marker, or
loader error. Synthetic host cases cover inline discontinuities and retained
discontinuity-sequence values. A later fault-injected PS5 run exercised a
discontinuity from 44.1 kHz stereo to 48 kHz mono and reopened the decoder and
sink with the new geometry without a crash.

At an inline discontinuity or a forced live-edge jump, the reader now drains
the old PCM sink, recreates the AAC decoder, clears partial ADTS framing, and
starts the next segment with fresh sample-rate and channel geometry. A change
to the playlist's base `EXT-X-DISCONTINUITY-SEQUENCE` alone does not trigger a
second reset because it can simply mean an already-consumed boundary slid out
of the live window.

## Retained boundary

- native HE-AAC SBR is available, but public Parametric Stereo output is mono;
- unsupported HLS forms remain explicit failures;
- naturally occurring live discontinuities remain useful field coverage in
  addition to the deterministic fault-injected hardware test.
