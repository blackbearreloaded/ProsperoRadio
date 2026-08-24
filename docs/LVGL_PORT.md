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

The production build compiled 340 objects with 7,637 defined symbols, 289
imports, and zero unresolved symbols. Its 32,768,000-byte FFPFSC has SHA-256
`1904C2CE204559F247E95AACAAD3C78AD54CBC864F10260E8C3525F8BE3C7C63`;
MkPFS reported data CRC32 `0x45405EB3`, manifest SHA-256
`3f6ea47bb44a1c1db2f72b239fb4aebe2a8316ace232cc11ce3dc791c0ea17f2`,
and zero warnings or errors.

The shared investigation loop deployed PPSA99770, entered `eboot`, captured a
fully populated live Radio Browser frame, observed the title for 20 seconds,
closed it cleanly, confirmed runtime release, stopped its managed Chiaki
process, and released the console lock. Evidence is retained as
`PPSA99770-20260824-004033-*`. Klog contains the expected native executable
entry and no app fatal signal; the recorded evidence notes only the explicitly
skipped Chiaki readiness gate and internal-window foreground fallback.

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

## PPSA99771 search and playback polish

PPSA99771 keeps a persistent styled child inside the search-query button so
runtime text updates no longer replace its centered 28 px label with the
button's small top-left fallback text. Filter labels and both search actions
use explicit centered 28 px line boxes. Navigation now reads only the left
analog stick; the D-pad remains unchanged. The station action uses smaller,
project-authored 24 px play and stop TGA symbols and switches them with the
existing playback-state class instead of presenting Cross as a media icon.

The 32,768,000-byte FFPFSC has SHA-256
`E164211DE169B53CDD86EAD6E482E644AF81CC048D7A6638EE84CB239176ED77`;
MkPFS reported data CRC32 `0xC838E490`, manifest SHA-256
`d849674e70c0bf5ca7467a2124910bbd112616c28d36e86c56dc53d547e29a82`,
and zero warnings or errors. ShadowMount ignored two directory-only staging
attempts, so the identical image was uploaded as
`/data/homebrew/PPSA99771.ffpfsc`, matching the project's deployment protocol.
That image registered immediately, entered `eboot`, rendered the smaller play
symbol in the captured live catalog frame, remained stable for 20 seconds,
closed cleanly, and released its runtime layers and console lock. Evidence is
retained as `PPSA99771-20260824-010614-*`; the only evidence note is the
explicitly skipped Chiaki readiness gate.

## PPSA99772 compact playback symbols

PPSA99772 removes the 36 px dark icon well from the station action. The
project-authored play and stop shapes now render directly on the cyan button
as clean 18 px dark glyphs, preserving the existing playback-state switch and
leaving the controller footer's Cross prompt unchanged. The paging rail also
matches the LVGL layout with a compact up chevron immediately before Previous
page and a down chevron after the right-anchored Next page label. Unavailable
actions hide both their text and icon.

The 32,768,000-byte FFPFSC has SHA-256
`2EFB4180160819DD213DE295A2F876C31C92D897832E0E41D6F66116BABB7C16`;
MkPFS reported data CRC32 `0xD1DAFC23`, manifest SHA-256
`0ec97f2214706cebb79642ae56234680472cea4dba81fe69c497c0c487143211`,
and zero warnings or errors. Its eboot SHA-256 is
`518E1AD7CC635597DD521EF9D16B07577BE1427F1CB36B66C6F06607F9B29B26`
and its libc SHA-256 is
`CD961EE6ED3D08117459B0FE70D86FE322672EBE0103678EE7C3F15AF7E00504`.
The image was deployed as `/data/homebrew/PPSA99772.ffpfsc`.

The shared investigation loop entered `eboot`, sent two Down inputs to reach
page 2, and captured both paging chevrons with Next page at the right edge. The
title remained stable for 20 seconds, closed cleanly, left no managed Chiaki
process, and released the console lock. Evidence is retained as
`PPSA99772-20260824-012244-*` with no evidence warnings. An earlier identical
run, `PPSA99772-20260824-012046-*`, also entered `eboot` but its screenshot
caught Chiaki's loading spinner because video readiness was explicitly
skipped; it is transport evidence rather than the visual acceptance frame.

## PPSA99773 balanced playback symbols

PPSA99773 increases the unboxed play and stop runtime assets from 18 x 18 to
24 x 24 pixels and expands their shapes within that canvas. The paging
chevrons and the rest of the PPSA99772 layout remain unchanged.

PPSA99773 was retired before an app launch after a partial directory-staging
attempt produced a duplicate-title error and contaminated its ShadowMount
registration. PPSA99774 was built but not deployed. At the operator's request,
PPSA99600 moves the identical visual change to a clean 600-series title range;
future builds increment from PPSA99601. No UI or runtime behavior changed
between these artifacts.
