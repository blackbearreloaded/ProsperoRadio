# Native Opus validation

PSRadio uses the console-provided `libSceOpusDec` and `libSceOpusCeltDec`
decoders. Packets begin on the general decoder. A CELT-only packet rejected with
native result `-502` receives one bounded retry through the dedicated CELT
decoder; SILK and hybrid packets remain on the general decoder. The application
packages generated import metadata only; it does not distribute Sony runtime
modules or a decoder implementation.

## Validation record

| Date | Firmware | Title | Result |
| --- | --- | --- | --- |
| 2026-08-25 | 6.02 | `PPSA99609` probe | A known 20 ms, 48 kHz stereo Opus packet decoded to the expected 960 frames (3,840 signed-16 PCM bytes) with nonzero energy. |
| 2026-08-25 | 6.02 | `PPSA99612` integration | The production PSRadio eboot entered normally, rendered `v0.2.0`, refreshed six catalog feeds, admitted 13 explicitly identified Opus stations, and closed with runtime layers released. |
| 2026-08-25 | 6.02 | `PPSA99614` Icecast fix | The corrected eboot entered normally, rendered the AAC/Opus-ready UI, and closed with runtime layers released. Its SHA-256 was `C7F00EB0654B07CE2E8CF4E7490AD3A355E50568BD60E3DF5FEB10B71C9A273F`. |
| 2026-08-25 | 6.02 | `PPSA99615` live playback | Multiple Radio Browser Opus stations produced audible output. Cyrillic station metadata also rendered correctly with the extended bitmap atlas. Brief recurring gaps and delayed stops exposed the need for independent PCM buffering and HTTP-read cancellation. |
| 2026-08-25 | 6.02 | `PPSA99616` buffering integration | The eboot with a two-second PCM ring, one-second startup reserve, and active HTTP-request cancellation entered normally, rendered the full UI, closed normally, and released its runtime layers. Audible stop/switch and sustained-playback validation remains pending. Eboot SHA-256: `5BA6981020F408069D4FF16055A0340A5AF360F15F73AE7A9110F09E39BA548B`. |
| 2026-08-26 | 6.02 | `PPSA99623` buffered AAC regression | The corrected buffered sink played an AAC station at 24 kHz mono for the full observation window without a fatal signal. |
| 2026-08-26 | 6.02 | `PPSA99624` Opus variant check | `WALM 2 HD Opus` reached the native decoder but returned signed error `-502` (`fffffe0a`) without crashing. The failure is stream-specific; the known-good Opus control below succeeded with the same eboot. |
| 2026-08-26 | 6.02 | `PPSA99625` buffered Opus regression | Deutschlandfunk OPUS 24k played at 48 kHz mono for the full 50-second observation window without a fatal signal. |
| 2026-08-26 | 6.02 | `PPSA99627` codec transition | The app moved from playing Deutschlandfunk Opus at 48 kHz mono directly to Smooth Radio AAC at 44.1 kHz stereo without a crash. The explicit stop action was not observed in this automation run and remains a separate validation item. |
| 2026-08-26 | 6.02 | `PPSA99628` immediate stop | Cross stopped buffered AAC playback within the first one-second observation point. The UI remained in `Ready to play` at 5 and 15 seconds, and no fatal signal was logged. |
| 2026-08-26 | 6.02 | `PPSA99629` reverse codec transition | The app moved directly from KIIS FM AAC at 24 kHz mono to WALM Old Time Radio Opus at 48 kHz mono without a crash. This WALM stream succeeded; the separate WALM 2 HD variant remains the reproducible `-502` case. |
| 2026-08-26 | 6.02 | `PPSA99630` ten-minute soak | Deutschlandfunk Opus remained in `Playing` at the start and the 2, 4, 6, and 8-minute checkpoints, then accepted a clean stop after the ten-minute interval. The app produced no fatal signal. Desktop focus changes invalidated the transient screen-difference sampler, so this run proves sustained runtime stability but not gap-free audible output. |
| 2026-08-26 | 6.02 | `PPSA99631` repeated error recovery | Three consecutive attempts to play WALM Old Time Radio Opus returned native result `-502` without crashing the app. The same station played successfully in `PPSA99629`, so the failure follows changing live stream data rather than a permanently unsupported station. Attempted AAC lifecycle captures were inconclusive because the automated stop inputs were not observed. |
| 2026-08-26 | 6.02 | `PPSA99646` isolated CELT path | After removing an unsafe diagnostic `sceKernelDebugOutText` call from the disposable harness, the dedicated codec-16 decoder loaded, initialized, created, and decoded live WALM CELT packets continuously. PCM was produced immediately and the title remained alive for the full observation window. |
| 2026-08-26 | 6.02 | `PPSA99648` production routing | The production-style dual decoder selected CELT from the packet TOC, completed every codec-16 setup stage, decoded the first packet, produced PCM, and remained crash-free for the observation window using the checked-in, hash-pinned import stub. |
| 2026-08-26 | 6.02 | `PPSA99649` AAC regression | The same dual-decoder eboot auto-played an AAC station, opened AudioOut, produced decoded PCM, and remained crash-free for the observation window. |
| 2026-08-26 | 6.02 | `PPSA99652` lifecycle validation | The hardened player reached 48 kHz stereo Opus output, stopped the first live station in 67 ms, switched to a second Opus station, and persisted `stage=passed`. The Game-category title remained stable for the 50-second observation, closed normally, and released its runtime layers. Eboot SHA-256: `07FDA2070F7E0893A2F5DA34DF500CA27855793460E4DEA2782E8B48E1C94292`. |
| 2026-08-27 | 6.02 | `PPSA99695` cross-format matrix | Current Radio Browser station `64b357e7-d9e9-4cb6-99b5-4cb6cef785cc` reached native Opus playback at 48 kHz mono after HLS/AAC and MP3 had each played and stopped. Opus then returned to stopped state before the Vorbis case. |

The shared `PPSA99695` matrix image used production audio source commit
`4283866`. Its package SHA-256 was
`525062C378A304A365BDC90D65E44204B2F0A087EA60C9AE434AD6092AAE07D0`;
its `eboot.bin` SHA-256 was
`83B5E2FF6CD6A8FB975E8672860081DC5A9BA9D8AAA459712C3880BE652A50EE`.

Do not use `PPSA99640` through `PPSA99645` as decoder-crash evidence. Their
disposable telemetry harness called `sceKernelDebugOutText` immediately after
selecting the stream; that optional debug path jumped through a null target in
`libkernel` before decoder-stage telemetry. Removing the call in `PPSA99646`
eliminated the crash and exposed the successful CELT lifecycle above.

The integrated eboot in the second run had SHA-256
`6E6D84ED90678DC72D1B59A4B780570939E4A3A4BC9E6BD65ACEDD27D3AF0666`.
Its staged metadata used `applicationCategoryType: 0`, matching the production
game/application category so the UI remains available through Remote Play.

The 2026-08-26 regressions used eboot SHA-256
`751DD3C99B971CC3E912D871E432B704467A80344CF61E26E45F098DAFCA9ED5`.
The preceding buffered build crashed at `RIP=0` when SDL created the output
thread with a non-null debug name. The PS5 SDL backend forwarded that name to
an unavailable optional `pthread_set_name_np` import. Creating the same worker
with a null name preserves the queue and cancellation design without making
the null call.

## Icecast live-sequence compatibility

Initial live tests returned `ffffffffb`, the signed parser result `-5`
(`OGG_OPUS_ERR_SEQUENCE`). Icecast supplied fresh `OpusHead` and `OpusTags`
pages with sequence numbers 0 and 1, then joined the current live audio page at
a much higher sequence number. The parser now allows exactly one discontinuity
between validated tags and the first audio page. If that page begins with an
orphan continued packet, its incomplete bytes are discarded through the first
terminating lace and subsequent complete packets are decoded. Additional
discontinuities and gaps after audio starts remain errors.

The sanitizer-enabled host regression suite passed captures from
Deutschlandfunk, Nightwave Plaza, and Dance Wave. It verifies full-page CRC
before packet dispatch, 61,440-byte packet acceptance, 61,441-byte rejection,
orphan-continuation discard, a second pre-audio jump rejection, gaps after
audio starts, chained logical streams, output gain, pre-skip, and final-granule
trimming.

The packet probe source and reverse-engineering notes are maintained in the
[PS5 hardware audio decoding investigation](https://github.com/blackbearreloaded/ps5-hardware-audio-decoding).

## Release status

The fault-injected PS5 matrix now covers a deliberate Opus underrun, bounded
reconnect behavior in the shared player, malformed Ogg failure, and rapid
cross-codec switching without a crash or hang. The two-second queue, 67 ms Opus
stop, transitions in both codec directions, a ten-minute session, dedicated
CELT decoding, and the AAC regression also have device evidence. Ogg CRC,
output gain, pre-skip, end trimming, bounded padded packets, and live-join
continuation handling have deterministic host coverage. A naturally occurring
live `-502` CELT fallback remains useful field coverage, not a release blocker.

See [audio release validation](AUDIO_RELEASE_VALIDATION.md) for the final matrix.
