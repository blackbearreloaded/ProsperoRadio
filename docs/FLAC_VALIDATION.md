# FLAC validation

PSRadio decodes native-container FLAC and Ogg-encapsulated FLAC with a bounded CPU
fallback. The hardware-first firmware investigation found no callable native
FLAC decoder: the referenced `libSceAudiodecCpuFlac.prx` plug-in is CPU-based,
absent from the inspected firmware set, and internal module `0x80000053`
returned `0x805a1000` when a game-category probe attempted to load it. No AJM
or recovered AvPlayer FLAC branch was found.

## Decoder and bounds

The adapter in `src/flac_decoder.cpp` uses `dr_flac` from upstream commit
`b55a0d9a30b91ad8901f89ecf05f76a33186c185`. The pinned decoder and license
SHA-256 digests are:

```text
D947F54784467160D30DCA540542BF92CED94965703E5DEEB9E82DB2EC5E0C02  dr_flac.h
DD1C647E6F767F8FF4B2DFAE0FED314726600A01E0CF1EF556AFDDD5FA96FF15  LICENSE.txt
```

Standard file I/O is disabled. The decoder reads from the active cancellable
HTTP request and uses only forward seeks, which are implemented by draining
the same callback. The production limits are:

- strict opening with a 1 MiB read ceiling;
- a 1 MiB aggregate decoder-allocation ceiling;
- one or two channels at 8 to 192 kHz;
- source blocks no larger than 8,192 PCM frames;
- signed-16 output in chunks no larger than 4,096 frames;
- CRC validation retained;
- channel normalization, 48 kHz resampling, and the shared eight-second PCM
  queue after decoding.

Stop and station switching use the shared HTTP abort and PCM-discard path. A
reconnect constructs a new decoder rather than retaining state across HTTP
responses.

Reconnect accounting is based on actual AudioOut progress rather than decoder
initialization alone. Three consecutive failures are allowed with 250, 500,
and 1,000 ms backoff. Once a stream has produced AudioOut successfully and the
attempt has remained active for 30 seconds, a later transport failure renews
that budget. This prevents an otherwise healthy long-running station from
eventually exhausting one lifetime retry counter while keeping persistent
failure bounded.

## Host checks

`tools/flac_decoder_check.cpp` validates a deterministic native FLAC fixture with
normal reads and 257-byte split input. It checks exact stream metadata, nonzero
PCM, complete sample count, allocation bounds, truncation, an invalid
signature, an oversized source-block declaration, and a metadata scan that
exceeds the 1 MiB opening limit.

The same test decodes a deterministic 48 kHz mono Ogg-FLAC fixture in 131-byte
chunks and verifies all 12,000 samples. The encoded Ogg-FLAC bytes have SHA-256
`88B1DA09CD4360364A805F0E59E48483F6E644913147A932B29D03D1757DE1DA`.
The normal check and an AddressSanitizer/UndefinedBehaviorSanitizer build both
pass.

## PS5 evidence

The production path was tested on firmware 6.02 with ShadowMountPlus using
disposable Game-category identities. These are evidence identities, not the
release identity `PPSA99001`.

### Native-container FLAC

`PPSA99686` played Radio Paradise Serenity from a direct FLAC response at
44.1 kHz stereo. A managed controller cycle stopped it and switched to the AAC
control station, which reached 24 kHz mono playback. Both title runs closed
normally and released their runtime layers.

The tested package SHA-256 was
`C3DA62074321A1B6F6DC1DA54153B65A307709680A190612BEB2F1DB0AF0730C`;
its `eboot.bin` SHA-256 was
`0556B44040560205EBF3BC5FE077575F224C992E380D991DB884F043F9DD8C88`.

### Ogg-FLAC

`PPSA99687` played Radio Browser station
`fcbd7124-cde1-4d70-ab58-5dd85f9d3c3e` (`Royalty Free Music 24/7 - Ogg
FLAC - Lossless Stereo`) at 44.1 kHz stereo. An exact-image rerun stopped the
station and switched to the AAC control at 24 kHz mono without a crash or
delayed title teardown.

The tested package SHA-256 was
`FE0C20A7F6C48122523995535365489808C1701EFC81724895E762748C8BD52F`;
its `eboot.bin` SHA-256 was
`368B94ACF2F9C12BEE4ACB1ECB1EFD53E106A064E9BB32DC22BF0AD4A2321893`.

The reconnect-budget correction was validated with diagnostic Game-category
image `PPSA99694`. App-owned LAN telemetry recorded one transition through
buffering to `state=3`, 44.1 kHz, two channels. No error or rebuffer transition
followed during more than eleven minutes of continuous execution. The managed
cycle then stopped the exact title; ShadowMount recorded normal sandbox removal
and runtime-layer release at `02:37:45`.

The sustained-run package SHA-256 was
`0AE68413734D9B87A55B910FD812027F49A4DC556B8205B548C8F130E9A58EE1`;
its `eboot.bin` SHA-256 was
`076A00128F1F7B5E5C4157C23719FBFA507F12C4ADB75F886F3853BA48B55298`.

## Supported boundary

This evidence establishes native and Ogg-FLAC playback, cancellation, AAC
switching, sustained runtime stability, and clean loader lifecycle. The final
PS5 matrix added FLAC to twelve rapid cross-codec switches and ended with a
clean 44.1 kHz stereo FLAC state. Host checks cover malformed, truncated, and
bounded-opening failures. Per-codec reconnect and network-underrun injection
remain useful extended coverage rather than missing decoder or container paths.
See [audio release validation](AUDIO_RELEASE_VALIDATION.md).
