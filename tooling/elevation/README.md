# Filesystem access (sandbox elevation)

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
`examples/sandbox-elevation`, as ProsperoEden carries it (GPL-3.0-or-later): `elevation.hpp`,
`protocol.hpp`, `elevation.cpp` (app side), `helper/` (the elfldr helper) and `validate-helper.py`.

ProsperoRadio changes: the helper's `target_title_id` is `PPSA99001`. The build makes the helper
with the PS5 payload SDK and ships it as `sandbox-elevator.elf` beside `eboot.bin`;
`radio_storage_init()` requests `Capability::filesystem` once at startup, and with it the app
keeps everything it writes under `/data/prosperoradio`. Without an elfldr on the console the
request fails and the app keeps its sandbox paths (`/app0`, `/download0`).
