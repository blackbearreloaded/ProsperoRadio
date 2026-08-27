# HLS/AAC validation

PSRadio supports a deliberately bounded HLS subset for AAC stations returned
by Radio Browser. HLS is handled as delivery: playlists and MPEG-TS are parsed
locally, while complete ADTS frames continue through the existing native
`libSceAudiodec` / AJM AAC path and PS5 AudioOut sink.

## Supported boundary

- master and media playlists with relative URL resolution;
- lowest-bandwidth audio-only AAC variant selection;
- live media sequences, reloads, stale-segment suppression, retries, and
  discontinuity resets;
- MPEG-TS PAT, PMT, PES, and ADTS extraction;
- cancellation during requests and playlist waits;
- unencrypted, whole-segment MPEG-TS only.

PSRadio rejects encryption, byte ranges, fMP4/CMAF, low-latency partial
segments, alternate rendition groups, video variants, multi-program transport
streams, and non-AAC HLS. The application accepts no custom station or playlist
input; every HLS URL originates in the fetched or cached Radio Browser catalog.

## Decoder failure and transport fix

The first hardware run (`PPSA99659`) reached the native decoder but returned
`0x807f0000`. IDA analysis of `libSceAudiodec` identified this as the generic
AudioDec API failure used when the underlying AJM sideband reports a codec
error; the lower-level result is stored in the AAC result structure.

A captured RTL MPEG-TS segment exposed the transport defect. The stream repeats
its PMT while an AAC PES is in progress. PSRadio reset PES assembly on every
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
manifests. This activates the existing stable decoder configuration: decode the
24 kHz mono AAC core, then normalize it through the shared 48 kHz stereo output
path. This prioritizes correct timing and stability; operator confirmation of
final audible HE-AAC fidelity remains required before release.

## Hardware lifecycle evidence

All runs used firmware 6.02, game-category app folders, ShadowMountPlus, klog on
TCP 3232, Chiaki-ng observation, and the exclusive investigation lock.

| Title | Result |
| --- | --- |
| `PPSA99660` | Corrected TS demux entered sustained HLS playback and switched HLS -> MP3 -> HLS without a crash; exposed slow native HE timing. |
| `PPSA99661` | Manifest channel intent selected the stable 24 kHz mono core path; HLS -> MP3 -> HLS switching completed normally. |
| `PPSA99663` | HLS played for 40 seconds across playlist reloads, stopped, restarted, and remained in `Playing` state for the final capture; normal title close released runtime layers. |

The final run produced no AudioDec failure, fatal signal, app-crash marker, or
loader error. A synthetic host case covers discontinuity reset behavior, but a
live discontinuity has not yet been captured on hardware.

## Remaining release gates

- operator-confirm normal speed and acceptable fidelity for the HE-AAC core
  fallback;
- hardware-exercise a live discontinuity;
- retain the explicit rejection behavior for unsupported HLS forms.
