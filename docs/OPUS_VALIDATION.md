# Native Opus validation

PSRadio uses the console-provided `libSceOpusDec` decoder. The application
packages generated import metadata only; it does not distribute the Sony
runtime module or a decoder implementation.

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
between validated tags and the first complete audio packet. Continued packets,
additional discontinuities, and gaps after audio starts remain errors.

The sanitizer-enabled host regression suite passed captures from
Deutschlandfunk, Nightwave Plaza, and Dance Wave. It also verifies rejection of
a second pre-audio jump, a mismatched continued page, and a missing page after
audio starts, plus reset behavior for a chained logical stream.

The packet probe source and reverse-engineering notes are maintained in the
[PS5 hardware audio decoding investigation](https://github.com/blackbearreloaded/ps5-hardware-audio-decoding).

## Remaining release gate

Before publishing Opus as a completed playback format, validate reconnect
behavior and a long-running stream on the target console. The two-second queue,
immediate AAC stop, and transitions in both codec directions now have
crash-free device evidence. Ogg CRC, output-gain, final granule trimming, and
the WALM `-502` packet variant remain tracked in the project roadmap.
