# Native MP3 validation

PSRadio decodes MP3 with the console-provided `libSceAudiodec` codec type
`0x0002`. The application loads the public AudioDec sysmodule (`0x0088`) and
uses the same `sceAudiodecInitLibrary`, `CreateDecoder`, `Decode`,
`DeleteDecoder`, and `TermLibrary` lifecycle already used for AAC. No software
MP3 decoder or Sony runtime module is packaged with the application.

Static analysis maps public MP3 codec `2` to internal AJM codec `0`; decoder
creation makes an AJM instance and each synchronous decode submits an AJM batch
job. This supports the description **AJM-backed hardware/firmware audio
offload**, without claiming which physical processor executes every operation.

The native MP3 stream-info block does not report sample rate or channel count.
[`mp3_header.h`](../include/mp3_header.h) therefore validates MPEG-1, MPEG-2,
and MPEG-2.5 Layer III headers and derives rate, channels, frame length, and
samples per channel before PCM enters the existing resampler and AudioOut
queue.

## Validation record

| Date | Firmware | Title | Result |
| --- | --- | --- | --- |
| 2026-08-26 | 6.02 | `PPSA99650` | The one-feed auto-play build entered eboot, remained alive for 60 seconds, closed normally, and released its runtime layers. Chiaki's GPU-surface capture was stale, so this run proves stability but not decoded PCM. |
| 2026-08-26 | 6.02 | `PPSA99651` | The same probe wrote `PSRADIO_MP3 stage=playing result=0 rate=48000 channels=2 bytes=4608` into title-local storage. The runner observed the title for 60 seconds, closed it normally, and confirmed runtime release. |
| 2026-08-27 | 6.02 | `PPSA99695` | The production stream path played current Radio Browser station `d1a54d2e-623e-4970-ab11-35f7b56c5ec3` at 48 kHz stereo, returned to stopped state, and proceeded to native Opus without a crash. App-owned LAN telemetry labeled every state with its codec and UUID. |

The shared `PPSA99695` matrix image used production audio source commit
`4283866`. Its package SHA-256 was
`525062C378A304A365BDC90D65E44204B2F0A087EA60C9AE434AD6092AAE07D0`;
its `eboot.bin` SHA-256 was
`83B5E2FF6CD6A8FB975E8672860081DC5A9BA9D8AAA459712C3880BE652A50EE`.

The `PPSA99651` receipt was recovered from its `/download0` image after the
title closed. Its 4,608 output bytes equal one maximum-size MP3 frame of 1,152
signed-16 stereo samples. The recovered data image SHA-256 was
`10E232358AF328065837B4DFCD042512CB6F1ACE59F5FC8DBF79E8A226D21D5A`.

## Host coverage

`tools/mp3_header_check.c` covers MPEG-1, MPEG-2, and MPEG-2.5 headers,
44.1/24/11.025 kHz rates, mono and stereo, padded frames, prefixed metadata,
and reserved version/layer rejection. `tools/icy_metadata_check.c` covers
case-insensitive `icy-metaint` parsing, split and zero-length metadata blocks,
truncation, read errors, and codec sync bytes embedded in discarded metadata.
The wrapper is transport-scoped, so any direct Radio Browser response that
advertises `icy-metaint` is stripped before AAC, MP3, Ogg, or FLAC framing.
`tools/radio_service_json_check.c` confirms that a healthy non-HLS Radio Browser
MP3 entry enters the catalog.
The production compile also fixes the recovered 64-bit ABI sizes at 24-byte AU
and PCM descriptors, a 32-byte control block, an 8-byte MP3 parameter block,
and a 20-byte MP3 result block. Runtime decoding rejects inconsistent consumed
or produced lengths before PCM reaches AudioOut.

## Remaining release gates

- Exercise mono and additional sample rates on hardware.
- Expand direct AAC/MP3/Opus switching beyond the validated matrix sequence.
- Confirm bounded stop latency during connect, read, buffering, and playback.
- Reject or recover from malformed frames without losing UI responsiveness.
