# Ogg Vorbis validation

PSRadio uses a bounded CPU fallback for Ogg Vorbis because the hardware-first
firmware investigation found no callable native PCM decoder. The vendored
`stb_vorbis` revision and license are recorded in [`NOTICE.md`](../NOTICE.md)
and [`vendor/stb/README.md`](../vendor/stb/README.md).

## Runtime design

The decoder is isolated behind `src/vorbis_decoder.c` and uses the upstream
push-data API over the active cancellable HTTP request. Its fixed ceilings are:

- 256 KiB compressed-input storage;
- 16 KiB maximum live network read;
- one or two channels at 8 to 192 kHz;
- 8,192 decoded frames per channel per call;
- planar float to interleaved signed-16 conversion before the shared channel
  normalizer, 48 kHz resampler, and two-second PCM queue.

Stop and station switching use the existing HTTP abort and queue-discard path.
A reconnect recreates the decoder instead of carrying codec state across the
new source response.

The PS5 target does not provide all libc/libm imports referenced by the
upstream translation unit. The wrapper supplies equivalent target-safe hooks:
`sin` and `cos` replace `sincos`, while `scalbn` implements the required
base-two exponent scaling without an unresolved `ldexp` import.

## Host checks

`tools/vorbis_decoder_check.c` decodes a deterministic Ogg Vorbis fixture with
normal input and with 257-byte incremental chunks. It verifies stream metadata,
nonzero signed-16 output, exact input progress, and bounded no-progress
handling. The check also passes under AddressSanitizer and UndefinedBehaviorSanitizer.

## PS5 evidence

Disposable game-category candidate `PPSA99685`, built from commit
`2c5c868ae59e9f5a25fe92e58e524e1ccce904a1`, validated the production path on
firmware 6.02 with ShadowMountPlus:

- Radio 1 Bulgaria reached `Playing | 44100 Hz | 2 ch` with active PCM output;
- stop completed without a hang or runtime crash;
- the same exact image switched to AAC station 102.7 KIIS FM and reached
  `Playing | 24000 Hz | 1 ch`;
- both managed cycles closed the exact title and released the runtime layers.

The tested package SHA-256 was
`B045562C1395D3EE52F65F790B88CCCEECA3B971305174029431E1978DCF7F0C`; its
`eboot.bin` SHA-256 was
`4CA6BE890BE15D87228F5AC1516D2FC74BFDBEEE467FC896F43BA9FAC50AFBE1`.

Diagnostic matrix image `PPSA99695` independently replayed the same station at
44.1 kHz stereo after stopping HLS/AAC, MP3, and native Opus streams. App-owned
LAN telemetry then recorded a direct Vorbis-to-HLS/AAC switch through a normal
stopped transition, followed by 24 kHz mono AAC-core playback. The final AAC
stop returned to `state=0`. This matrix image used production audio source
commit `4283866`; its package SHA-256 was
`525062C378A304A365BDC90D65E44204B2F0A087EA60C9AE434AD6092AAE07D0`,
and its `eboot.bin` SHA-256 was
`83B5E2FF6CD6A8FB975E8672860081DC5A9BA9D8AAA459712C3880BE652A50EE`.

Dedicated soak image `PPSA99696` used current Radio Browser station
`648b2abd-92a0-11e9-a605-52543be04c81` (`Dance Wave!`). A host probe first
identified the live payload as Vorbis. On PS5, app-owned LAN telemetry entered
`state=3` at 44.1 kHz stereo and recorded no later rebuffer or error transition
before the managed title close roughly 11 minutes 17 seconds later. ShadowMount
then recorded the game stop at `03:12:51`, sandbox removal, and runtime-layer
release at `03:12:52`.

The sustained-run package SHA-256 was
`B8403E6936CC5DDB99133C1174A29EFFBD8EE81E02FE03D5EDC1B83CFAF23132`;
its `eboot.bin` SHA-256 was
`4A2B9F47B12085BD2140B3ADBF9FC01DCA021AB19DC341CDA2CBDE34CAC240A2`.

This establishes supported-format playback and lifecycle behavior. Additional
malformed-page coverage, chained streams, induced reconnects, rapid repeated
switching, and deliberate live-stream faults remain robustness and
container-compliance work rather than a missing Vorbis decoder path.
