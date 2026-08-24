# LVGL-to-RmlUi port

## Functional baseline: PPSA99768

PPSA99768 ports the completed PSRadio application from the sibling
`ps5-radio-lvgl` repository at commit `74f5ca8` while keeping this repository's
hardware-verified RmlUi/SDL text path.

### Reused platform functionality

- Radio Browser API discovery, metadata, AAC stream selection, station cache,
  and refresh behavior;
- favorites persisted under `/download0`;
- native HTTP, AAC decoding, HE-AAC fallback, resampling, and AudioOut;
- native `libScePad` event draining and native PS5 IME search input;
- Popular, Trending, Top rated, Favorites, and Discover station views;
- local query, country, genre, language, and bitrate filtering;
- playback switching, pending-station handling, status, and Radio Browser
  attribution.

The platform-neutral service is compiled from `src/radio_service.c`. Small
RmlUi-specific adapters in `src/radio_input.c` and `src/radio_ime.c` expose the
same native facilities without introducing an LVGL runtime dependency.

### RmlUi implementation

`src/radio_app.cpp` owns application state and updates the fixed 1920 x 1080
document in `ui/main.rml`. `ui/styles/app.rcss` translates the final LVGL
geometry to integer RmlUi boxes. Four station cards, paging, selected-station
details, the Discover filters, search and credits overlays, and the persistent
now-playing rail are all represented by stable document IDs checked by
`tools/check_rml_port.py`.

The port deliberately preserves `src/bitmap_font_engine.cpp`, the retained CPU
font-atlas surface, and the SDL surface blitter. The Montserrat bitmap assets
are generated from the exact LVGL 4-bit font data, so the RmlUi text does not
return to the sampled texture path that caused malformed glyphs in earlier
builds.

### Controls

- D-pad: move through stations, filters, playback, and attribution.
- Cross: play/stop the selected station or activate the focused control.
- Square: add or remove the selected station from Favorites.
- Triangle: open search and filters.
- L1/R1: move between the five main views.
- Options: refresh Radio Browser data.
- Circle: close an overlay or return to Popular.

### Local validation

Run these checks before packaging:

```powershell
python tools/check_rml_port.py
python tools/generate_lvgl_bitmap_fonts.py --check
python tools/generate_radio_bitmap_fonts.py --check
wsl g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/radio_text_check.cpp src/radio_text.cpp -o /tmp/radio-text-check
wsl /tmp/radio-text-check
wsl clang -std=c11 -Wall -Wextra -Werror -Iinclude tools/aac_timing_check.c -o /tmp/aac-timing-check
wsl /tmp/aac-timing-check
./build.ps1 -Dotnet 'C:\Program Files\dotnet\dotnet.exe' -OutputFormat Ffpfsc
```

The bitmap-font check's expected combined SHA-256 is
`c1e9b41232afd6720e04ea14c972caaea20cd839149766ada95aecd4e15ee677`.

### Multilingual parity: PPSA99770

PPSA99770 extends the dynamic 20, 24, 28, and 32 px faces with the exact
17,854 multilingual glyph masks per size used by the completed LVGL app. A
host exporter reads the compiled LVGL font descriptors, packs the masks into
14 deterministic 2048 x 2048 pages, and writes a lossless 4-bit `.rta` atlas
format. RmlUi merges those pages into the existing Montserrat faces, retains
all original ASCII metrics and kerning, and loads only atlas pages referenced
by text that is actually rendered. The decoded pixels continue through the
same retained SDL surface and unsampled CPU blit path.

`src/radio_text.cpp` supplies the text-layout behavior that RmlUi's bitmap
font interface does not provide itself: common Arabic and Persian letters are
converted to connected Unicode presentation forms, and Arabic/Hebrew runs are
placed in visual order while embedded Latin text and numbers remain forward.
Host regressions cover unchanged ASCII, Cyrillic, and Chinese strings as well
as Hebrew ordering and a connected Arabic lam-alef word.

## PPSA99768 hardware baseline

The first complete RmlUi port entered `eboot`, loaded live Radio Browser data,
remained stable through observation, and closed cleanly in the automated PS5
cycle. Evidence is retained by the shared investigation loop as
`PPSA99768-20260824-000459-result.json`, its klog, running screenshot, and
after-close screenshot. The captured frame confirmed the fixed card, detail,
paging, tab, and now-playing geometry on hardware.

## PPSA99769 interaction correction

PPSA99769 addresses the first hands-on review of the complete port:

- equalizer bars keep a fixed lower edge and grow upward;
- the play/stop action uses the approved generated Cross image in a dark icon
  well instead of a stepped text-like shape;
- explicit ID-specific focus styles make the query, all filters, Reset, Show
  results, Play, attribution, and credits-close controls unmistakable;
- the search panel is centered and simplified to a query, symmetric 2 x 2
  filter grid, and centered action row;
- Left or Right from Reset/Show results moves between the two actions, and
  Cross on Show results applies the search;
- Right or Down from Play reaches `Data by Radio Browser`, with a visible focus
  border;
- either analog stick now mirrors D-pad navigation with a 50% dead zone,
  dominant-axis selection, a 350 ms initial repeat delay, and 110 ms repeat.
