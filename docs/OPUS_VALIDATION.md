# Native Opus validation

PSRadio uses the console-provided `libSceOpusDec` decoder. The application
packages generated import metadata only; it does not distribute the Sony
runtime module or a decoder implementation.

## Validation record

| Date | Firmware | Title | Result |
| --- | --- | --- | --- |
| 2026-08-25 | 6.02 | `PPSA99609` probe | A known 20 ms, 48 kHz stereo Opus packet decoded to the expected 960 frames (3,840 signed-16 PCM bytes) with nonzero energy. |
| 2026-08-25 | 6.02 | `PPSA99612` integration | The production PSRadio eboot entered normally, rendered `v0.2.0`, refreshed six catalog feeds, admitted 13 explicitly identified Opus stations, and closed with runtime layers released. |

The integrated eboot in the second run had SHA-256
`6E6D84ED90678DC72D1B59A4B780570939E4A3A4BC9E6BD65ACEDD27D3AF0666`.
Its staged metadata used `applicationCategoryType: 0`, matching the production
game/application category so the UI remains available through Remote Play.

The packet probe source and reverse-engineering notes are maintained in the
[PS5 hardware audio decoding investigation](https://github.com/blackbearreloaded/ps5-hardware-audio-decoding).

## Remaining release gate

Before publishing Opus as a completed playback format, validate live audible
playback, stop, AAC-to-Opus and Opus-to-AAC switching, reconnect behavior, and a
long-running stream on the target console. Ogg CRC, output-gain, and final
granule trimming work remains tracked in the project roadmap.
