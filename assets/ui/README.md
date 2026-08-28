# PSRadio interface assets

This directory contains the complete RmlUi interface shipped with PSRadio.

- `main.rml` defines the browse screen, station cards, selected-station panel,
  now-playing rail, search overlay, and credits overlay.
- `styles/app.rcss` defines the fixed 1920 x 1080 television layout and all
  focused, selected, playing, loading, and disabled states.
- `fonts/lvgl-bitmap/` contains the deterministic runtime bitmap faces and
  multilingual atlas pages.
- `icons/` contains controller prompts, paging chevrons, the PSRadio mark, and
  playback symbols.

Native state and controller behavior are implemented in `src/radio_app.cpp`.
The document performs no networking, persistence, or audio work. See
[`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) for the complete runtime flow.
