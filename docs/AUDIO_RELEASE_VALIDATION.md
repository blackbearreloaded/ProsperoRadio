# Audio release validation

This document records the final fault-injected audio checks for the 0.2.0
release. It complements the per-codec documents with cross-codec behavior and
platform limitations observed on a PS5 running firmware 6.02.

## Decoder boundary

| Format | Release path | Device result |
| --- | --- | --- |
| AAC / HE-AAC | Native `libSceAudiodec` / AJM | AAC and SBR decode in firmware. HE-AAC v2 PS is exposed as mono, so the stable core path is normalized to stereo. |
| MP3 | Native `libSceAudiodec` / AJM | 48 kHz stereo playback passed. |
| Ogg Opus | Native `libSceOpusDec` and `libSceOpusCeltDec` | 48 kHz playback, gain, pre-skip, end trim, CRC, and lifecycle checks passed. |
| Ogg Vorbis | Bounded `stb_vorbis` CPU fallback | 44.1 kHz stereo, linked streams, and lifecycle checks passed. |
| FLAC / Ogg-FLAC | Bounded `dr_flac` CPU fallback | 44.1 kHz stereo and lifecycle checks passed. |
| HLS/AAC | Bounded playlist/TS transport plus native AAC | Reload, retry, discontinuity, and output-geometry checks passed. |

## HE-AAC probe

Game-category diagnostic `PPSA99697` used a self-contained, 24-frame HE-AAC v2
ADTS fixture. The fixture SHA-256 was
`5C014D49884015B0A36A00F703224DD2E4B3F2086F72A29FE8CFD9E9410C7C05`.

| Configuration | Result |
| --- | --- |
| Public 24-byte AAC parameters, HE enabled | 24 frames, 48 kHz, one channel, SBR energy above 12 kHz |
| Public 28-byte extended parameters, HE enabled | 24 frames, 48 kHz, one channel |
| Recovered AvPlayer-style 16-bit word size | Rejected by the public decoder API |
| Public parameters, HE disabled | 24 kHz mono AAC core |

The probe confirms SBR reconstruction and rejects a stronger claim: no valid
public configuration returned the HE-AAC v2 Parametric Stereo source as two
channels. PSRadio keeps the hardware AAC path and safely normalizes mono output
for the PS5 stereo sink.

## Fault-injected PS5 matrix

Game-category diagnostic `PPSA99698` was built from the production audio source
with only its title identity, deterministic catalog, and test telemetry changed.
Its package SHA-256 was
`FA61303180EB5018A330B23703163FC1A67242781E979A117100F4F1D76E65FE`.

The console fetched deterministic streams from a local HTTP server while input
was driven through an owned Chiaki-ng session. The matrix completed 92 telemetry
events and 16 status reads:

| Scenario | Observed result |
| --- | --- |
| AAC reconnect | Three forced disconnects; each playback attempt reached 48 kHz stereo output. |
| Opus underrun | Delayed delivery reached and retained 48 kHz stereo playback without a hang. |
| Malformed Ogg Opus | Three bounded retries, then playback error `-12`; no crash or UI lock. |
| HLS discontinuity | Output changed from 44.1 kHz stereo to 48 kHz mono across three cycles with fresh decoder geometry. |
| ICY metadata | Metadata blocks were stripped and AAC reached 48 kHz stereo output. |
| Chained Vorbis | Both logical streams decoded; playback reached 44.1 kHz stereo. |
| MP3 | Playback reached 48 kHz stereo. |
| FLAC | Playback reached 44.1 kHz stereo and the final state was cleanly stopped. |
| Rapid switching | Twelve switches over three AAC/Opus/HLS/Vorbis/MP3/FLAC cycles completed without a hang or crash. |

ShadowMountPlus recorded a normal title start and stop with runtime-layer
release. Klog contained no title crash, fatal signal, or loader error. Host
ASan, UBSan, parser, framing, container, metadata, discontinuity, and bounded
failure checks passed against the same production source before this run.

## Exact production smoke

The final Game-category image was deployed as `/data/homebrew/PPSA99001.ffpfsc`
and verified by downloading it back from the console:

| Artifact | SHA-256 |
| --- | --- |
| `PPSA99001.ffpfsc` | `9CAEB2F691F6E12AE742203F7F9A411E63A32B5C520D7EA2B99FC0593BF3B115` |
| Packaged `eboot.bin` | `3A8C27632D54477E81A59B9F8B52DEBD2E40C6EAB6B7200C69BA180584331F4D` |
| Packaged `libc.prx` | `AF5DBB1C778135F63DAF07F225F84FB948B07034D6D0CD2E393528510F2236B4` |

The production title displayed `v0.2.0`, fetched the live Radio Browser
catalog, played the first station, switched to the second and fourth stations,
and visibly reached MP3 playback at 48 kHz stereo. The bounded cycle then
closed `PPSA99001`; ShadowMountPlus recorded the title stop, sandbox removal,
runtime-layer release, and both image unmounts. All three declared console
services remained reachable after cleanup, and the owned Chiaki process exited.

## Release conclusion

All formats advertised by PSRadio have a working PS5 path and bounded failure
behavior. The known fidelity boundary is HE-AAC v2 Parametric Stereo: SBR is
firmware-decoded, but the public API returns mono. This limitation is documented
and does not affect timing, stability, or the stereo AudioOut contract.
