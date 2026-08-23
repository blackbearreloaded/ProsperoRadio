# Rendering investigation

This log records each PS5 UI milestone, its exact title ID, the evidence captured
by `workspace/docs/investigation-loop`, and the decision made from that evidence.
Every tested build uses a new title ID and is committed before deployment.

## PPSA99724 - RmlUi compatibility baseline

- Commit: `5654d43`
- Outcome: entered `eboot`; closed cleanly.
- Evidence: `PPSA99724-20260823-103702-running.png`
- Changes:
  - routed RmlUi 6 through `RenderInterfaceCompatibility` so SDL receives
    straight-alpha colors and textures;
  - fixed scissor updates while clipping is enabled;
  - removed unsupported RCSS (`box-shadow`, browser border styles,
    percentage radii, outlines, and radial gradients);
  - bundled Noto Sans regular and bold.
- Result: layout, borders, dark player strip, and text returned. Small text and
  Unicode control glyphs remained coarse or malformed in SDL's software
  `RenderGeometry` path.

## PPSA99725 - safe labels and readable metadata

- Commit: `71563b6`
- Outcome: entered `eboot`; closed cleanly.
- Evidence: `PPSA99725-20260823-105107-running.png`
- Changes:
  - replaced fragile emoji and geometric Unicode controls with ASCII labels;
  - wrapped station status text in child spans;
  - enlarged small text and replaced tiny rounded status dots with squares.
- Result: card metadata became readable and tiny geometry stopped breaking.
  Direct text inside flex containers was blank, revealing a separate RmlUi
  layout compatibility issue.

## PPSA99726 - stable software-renderer UI

- Commit: `d68ed41`
- Outcome: entered `eboot`; closed cleanly.
- Evidence: `PPSA99726-20260823-105432-running.png`
- Changes:
  - replaced direct-text flex containers with block layout and explicit line
    heights;
  - wrapped remaining anonymous flex text nodes.
- Result: all labels and controls render, including station badges, PLAY,
  PREV/NEXT, controller hints, LIVE, and Connected. This is the stable fallback
  baseline. Small glyphs are still visibly coarse because the PS5 SDL software
  renderer quantizes RmlUi's floating-point geometry and texture coordinates.

## Investigation-loop improvement

Chiaki successfully decoded 1920x1080 video while the cycle reported that no
video frame was ready. Its Qt child window, rather than the top-level process
window, owned Windows foreground focus. The loop now records that mismatch as
an evidence warning and still captures the restored Chiaki rectangle. This made
the launch/capture/close workflow self-sufficient without weakening PS5 lock
ownership or runtime-release checks.

## PPSA99727 - first OSMesa/OpenGL renderer probe

- Commit: `57d0ac9`
- Outcome: entered `eboot`, remained alive, and closed cleanly, but presented a
  black frame.
- Evidence: `PPSA99727-20260823-110336-running.png` and its klog.
- Runtime source: pacbrew-repo v0.39 `ps5-payload-dev.tar.gz`.
- Runtime: `libOSMesa.so.8`, 92,405,336 bytes, SHA-256
  `128D2AB20B980B96DECB1F20A9ED57D10CC922605259666B63EFA52963920F0A`.
- Runtime dependencies: `libkernel_web.sprx` and
  `libSceLibcInternal.sprx` only.
- FFPFSC: 53,608,448 bytes, SHA-256
  `301ED4BB24F804260E46DFAA4434EBB3789E677E5A551B11E93EBAB2DE2584AA`.
- Changes:
  - replaced SDL software `RenderGeometry` with an OpenGL 2 compatibility
    renderer backed by SDL's PS5 OSMesa context;
  - resolves all OpenGL entry points through `SDL_GL_GetProcAddress`, leaving
    no direct GL imports in `eboot.bin`;
  - uses straight-alpha blending through RmlUi's compatibility adapter,
    linear font-atlas sampling, orthographic 1920x1080 projection, and proper
    top-left scissor conversion;
  - added hash-validated app-root runtime-file staging to the build.
- Goal: preserve anti-aliased font atlas sampling and rounded geometry without
  SDL software `RenderGeometry` quantization.

The absence of a crash proves the app and runtime mount survived, but the first
probe did not expose which initialization stage returned early because ordinary
stderr was not present in the captured klog.

## PPSA99728 - OSMesa stage diagnostics

- Commit: `a29f41a`
- Outcome: crashed immediately with signal 11 before creating `runtime.log`.
- Evidence: `PPSA99728-20260823-110653-klog.log`.
- Changes:
  - uses lazy relocation for the OSMesa preload, matching SDL's own loader;
  - records each loader, SDL, OpenGL, and RmlUi stage through
    `sceKernelDebugOutText` and `/data/homebrew/PPSA99728/runtime.log`;
  - presents a purple OpenGL diagnostic frame before RmlUi initialization.
- Interpretation: a black frame with a runtime log identifies an early loader
  or SDL failure; purple identifies successful GL presentation but later RmlUi
  failure; the complete UI proves the renderer path.

Because `PPSA99727` remained alive with the same renderer and the first new
operation in `PPSA99728` was `sceKernelDebugOutText`, the debug import is the
isolated crash regression.

## PPSA99729 - file-only OSMesa diagnostics

- Commit: `9eda50c`
- Outcome: signal 11 immediately after `eboot`, before a frame or file log.
- Evidence: `PPSA99729-20260823-111034-klog.log`.
- Changes:
  - removed `sceKernelDebugOutText` entirely;
  - writes stage diagnostics to `/app0/runtime.log` before each risky loader,
    SDL, OpenGL, and RmlUi operation;
  - retains lazy OSMesa relocation and the purple GL diagnostic frame.
- Result: `/app0` is not a writable diagnostic location. The backtrace is a
  null function call from early `main`; `libOSMesa.so.8` is absent from the
  loaded-library list, so execution did not reach a successful preload.

## PPSA99730 - direct OSMesa visual probe

- Commit: `7be64c1`
- Outcome: entered `eboot`, remained alive, and closed cleanly.
- Evidence: `PPSA99730-20260823-111811-running.png`.
- Changes:
  - removed runtime file logging;
  - starts from the proven SDL software surface and encodes every OSMesa loader,
    symbol, context, surface-binding, and OpenGL-clear stage as a distinct solid
    color;
  - calls OSMesa directly with `/app0/libOSMesa.so.8`, independent of SDL's
    hardcoded basename-only OSMesa loader.
- Result: the screenshot reached the absolute-path `dlopen` failure color.
  Pacbrew's raw shared object is incompatible with this native app loader, so
  dynamic OSMesa is removed from subsequent packages.

## PPSA99731 - linearly sampled font atlases

- Commit: `6aaea3c`
- Outcome: entered `eboot`, displayed the complete UI, remained alive, and
  closed cleanly.
- Evidence: `PPSA99731-20260823-113046-running.png`.
- Changes:
  - restored the stable PPSA99726 SDL software renderer;
  - forces `SDL_ScaleModeLinear` on every generated RmlUi texture and requests
    linear render scaling globally;
  - removes the 92 MB OSMesa runtime from the staged app.
- Goal: preserve the proven native renderer while smoothing glyph alpha edges
  and texture-coordinate quantization at small sizes.
- Result: glyph edges are smooth and solid, all controls render correctly, and
  the complete UI is stable. Remaining work is typography scale and spacing,
  not renderer correctness.

## PPSA99732 - TV typography and layout polish

- Commit: `23627bb`
- Outcome: entered `eboot`, displayed the complete UI, remained alive, and
  closed cleanly.
- Evidence: `PPSA99732-20260823-113558-running.png`.
- Changes:
  - keeps Noto Sans regular/bold and raises every secondary label to at least
    17-20 px;
  - increases muted-text contrast and gives headings safe accent-glyph line
    height;
  - adds overflow protection for real Radio Browser station names and metadata;
  - compacts the selected-station panel so its actions clear the player strip.
- Result: labels and metadata fit without overlap at the forced 640x480 host
  capture scale. A lossless native frame is needed for final 1920x1080 review.

## PPSA99733 - lossless native UI capture

- Commit: `7d1f805`
- Outcome: shell launch rejected with `0x80a40087` on two attempts before
  `eboot`; runtime mount released cleanly both times.
- Changes:
  - saves the first complete 1920x1080 SDL surface to
    `/download0/PPSA99733-ui.bmp`;
  - reduces the temporary download-data reservation to 32 MB so the backing
    UFS2 image remains quick to retrieve and inspect.
- Goal: validate exact PS5 renderer output independently of Chiaki window and
  host-desktop scaling.
- Result: the 32 MB download-data reservation is not accepted by this native
  launch path. No app code ran and no BMP was written.

## PPSA99734 - proven download mount and native capture

- Commit: `00c258a`
- Outcome: entered `eboot`, wrote the native frame, remained alive, and closed
  cleanly.
- Evidence: `PPSA99734-native/PPSA99734-ui.bmp` extracted from the title's
  `/user/download/PPSA99734/download0.dat` UFS2 image.
- Changes:
  - restores the hardware-proven 256 MB `/download0` reservation;
  - writes `/download0/PPSA99734-ui.bmp` after the first complete UI frame.
- Result: the lossless 1920x1080 frame proves smooth, readable Noto Sans and
  correct layout. Rounded interactive borders still show SDL software-raster
  seams; square panel borders do not.

## PPSA99735 - seam-free controls

- Commit: `c838014`
- Outcome: entered `eboot`, wrote the native frame, remained alive, and closed
  cleanly.
- Evidence: `PPSA99735-native/PPSA99735-ui.bmp`, SHA-256
  `FAC9F8874D8106E99390A60548D25B126D8B4934FA82820B055ADF7C320B763B`.
- Changes:
  - sets card and interactive-control radii to zero while preserving the same
    spacing, color hierarchy, focus borders, and typography;
  - writes `/download0/PPSA99735-ui.bmp` for lossless verification.
- Result: the 1920x1080 native frame has clean control edges, no button seams,
  smooth font rendering, readable secondary labels, and no overlap or clipping.

## PPSA99736 - production UI handoff

- Commit: `6094aed`
- Outcome: entered `eboot`, remained alive through observation, and closed
  cleanly with runtime release confirmed.
- Evidence: `PPSA99736-20260823-115820-result.json` and klog; visual output is
  the same renderer/RCSS path as the lossless PPSA99735 native frame.
- FFPFSC: 12,451,840 bytes, SHA-256
  `F151019926233464D1B8161670428DDBBA5CBE34D2BD8E07CCCD406B4077EB7F`.
- Changes:
  - keeps the byte-equivalent verified rendering and RCSS paths;
  - removes the one-time native BMP capture from the frame loop.
- Result: production UI milestone complete.

## PPSA99737 - matched font metrics

- Goal: remove the one-to-two-pixel baseline clipping visible across regular
  and bold strings on the physical PS5.
- Diagnosis:
  - the bundled Noto Sans regular and bold files report different internal
    releases;
  - their 1362-unit line spacing exceeds the 1000-unit em square, making the
    existing 1.3 line boxes slightly shorter than the face's native metrics;
  - the bundled LatoLatin regular and bold files are a matched release and use
    consistent 2400-unit line spacing over a 2000-unit em square.
- Changes:
  - makes the matched LatoLatin regular/bold pair the primary UI family;
  - retains DejaVu Sans and Noto Emoji as fallback coverage;
  - makes every proportional text line at least 1.3 high, with 1.25 retained
    only for the oversized 43 px page heading;
  - writes `/download0/PPSA99737-ui.bmp` for exact 1920x1080 verification.
- Outcome: entered `eboot`, remained stable, closed cleanly, and released its
  runtime layers. The first attempt never launched because the obsolete klog
  default at port 40972 refused the connection; the byte-identical retry used
  the console's active klog endpoint at port 3232.
- Evidence: `PPSA99737-native/PPSA99737-ui.bmp`, SHA-256
  `84A9187F06E1B85515E05BC6E8D7D93D254C43D72243C18E5C5711F558433493`.
- FFPFSC: 12,451,840 bytes, SHA-256
  `0BEA2DCCA87F5FDEE240BCF6FF7931A29E9B52305BED0912BDC9E24941CAE44C`.
- Result: the lossless native frame shows intact lower strokes and descenders
  in regular and bold text throughout the top bar, cards, controls, player,
  and footer. No text overlaps or layout regressions are visible.

## PPSA99738 - production typography handoff

- Goal: package the hardware-verified LatoLatin typography and safe line boxes
  without diagnostic writes in the runtime frame loop.
- Changes:
  - keeps the byte-equivalent PPSA99737 font loading, RCSS, and SDL rendering
    paths;
  - removes the one-time `/download0` BMP capture.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. The managed Chiaki window closed;
  one windowless local helper was stopped explicitly before releasing the PS5
  lock.
- Evidence: `PPSA99738-20260823-134847-result.json` and klog. The only evidence
  warnings concern the intentionally skipped host-video readiness check and
  Chiaki's internal foreground window handle; hardware lifecycle classification
  is `entered-eboot`.
- FFPFSC: 12,451,840 bytes, SHA-256
  `BE4A9408054408D93FA9F71696ED94AECCAF92EAF147FAAD347BD99702F804F9`.
- Result: production typography milestone complete.

## PPSA99739 - neutral small-text glyph test

- Goal: replace LatoLatin's deliberately asymmetric lowercase `t` and compact
  x-height with more conventional, evenly hinted small-screen glyphs.
- Changes:
  - makes a matched DejaVu Sans 2.37 regular/bold pair the primary UI family;
  - preserves the verified safe line boxes and Noto Emoji fallback;
  - writes `/download0/PPSA99739-ui.bmp` for exact 1920x1080 inspection.
- Outcome: entered `eboot`, remained stable, closed cleanly, and released its
  runtime layers.
- Evidence: `PPSA99739-native/PPSA99739-ui.bmp`, SHA-256
  `6D51505708A4A3E4BDECF0C29769C8C8EDFF2D027E4D11AC58157713E1CDB09D`.
- FFPFSC: 12,845,056 bytes, SHA-256
  `361686F903F16AB257A9159C4CF7F95B712D0525AE43B5088D3600AFF17AE82F`.
- Result: the native frame shows an even lowercase baseline and a conventional
  `t` stem/crossbar. The wider face still fits every card, button, player field,
  and footer label without clipping, overlap, or unintended wrapping.

## PPSA99740 - production neutral typography

- Goal: package the hardware-verified DejaVu Sans rendering without diagnostic
  writes in the runtime frame loop.
- Changes:
  - keeps the byte-equivalent PPSA99739 font loading, RCSS, and SDL rendering
    paths;
  - removes the one-time `/download0` BMP capture.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. The residual windowless local
  Chiaki helper was stopped before releasing the PS5 lock.
- Evidence: `PPSA99740-20260823-135543-result.json` and klog; lifecycle
  classification is `entered-eboot`.
- FFPFSC: 12,845,056 bytes, SHA-256
  `29E8AD2BF21BC7AC4D570AF74827CE3C16342240043C171E64E71DD336DDA40E`.
- Result: production neutral-typography milestone complete.

## PPSA99741 - hinted Roboto TV-scale typography

- Goal: eliminate the hard, apparently cropped lower stems that remain visible
  at 18-25 px on a television after multiple matched font-family tests.
- Diagnosis: RmlUi 6.2 uses FreeType's hinted bitmap rasterizer, and SDL 2.30.12
  software rendering copies each glyph quad at exact pixel size. At the former
  small sizes, snapped lower stems have no soft edge and read as clipped even
  when the bitmap and line box are complete.
- Changes:
  - uses the matched, statically hinted Roboto 2.0 regular/bold pair;
  - raises small UI text by roughly 12-16% and provides 1.35-1.4 line boxes;
  - proportionally increases the top bar, cards, selected-station panel, player
    strip, and footer while preserving the existing layout;
  - writes `/download0/PPSA99741-ui.bmp` for exact 1920x1080 inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers.
- Evidence: `PPSA99741-native/PPSA99741-ui.bmp`, SHA-256
  `57CE986B2746A3783FA061D8FFA1AB14CA9DCF72EBBDEA114FE53FF7451454D0`.
- FFPFSC: 13,434,880 bytes, SHA-256
  `66AD755C4EC92FFDBDA66691A3E1CEF2885F5AA12A7042E270DCA1C9B8AA4E53`.
- Result: the lossless frame confirms smooth, conventional glyph shapes and
  complete lower stems in the top bar, cards, controls, and player. The larger
  scale exposed a separate layout regression: the selected-station actions
  extend behind the player strip, and the third card row has insufficient
  clearance above it. PPSA99742 will retain this typography while compacting
  only the surrounding panel and strip geometry.

## PPSA99742 - TV-scale layout clearance

- Goal: retain PPSA99741's hardware-verified Roboto rendering and larger text
  while restoring deliberate space around the fixed player strip.
- Changes:
  - widens and compacts the selected-station panel so metadata stays on one
    line and both actions remain above the player strip;
  - reduces card chrome by six pixels without changing card text sizes;
  - lowers the player strip by six pixels while retaining a ten-pixel gap above
    the footer;
  - writes `/download0/PPSA99742-ui.bmp` for exact 1920x1080 inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers. Chiaki exited
  before the shared PS5 lock was released.
- Evidence: `PPSA99742-native/PPSA99742-ui.bmp`, SHA-256
  `E993BACABB848049DCAA82FCB17E4235063EBB40127C3B566E6AA8BC4FFD659E`.
- FFPFSC: 13,434,880 bytes, SHA-256
  `79E177BA2B22A938B104376C7BAB6A520148A35FDE76A18470212CC2ECBEDF8E`.
- Result: the native 1920x1080 frame has complete lower stems and consistent
  baselines throughout. All six cards, both selected-station actions, the
  player strip, and the footer fit with clear separation and no clipping,
  overlap, malformed controls, or unintended wrapping.

## PPSA99743 - production polished UI

- Goal: package the hardware-verified PPSA99742 typography and layout without
  diagnostic writes in the runtime frame loop.
- Changes:
  - keeps the byte-equivalent Roboto font loading, RmlUi markup, RCSS, and SDL
    rendering paths verified in PPSA99742;
  - removes the one-time `/download0` BMP capture.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. Chiaki exited before the shared PS5
  lock was released.
- Evidence: `PPSA99743-20260823-142235-result.json` and klog; lifecycle
  classification is `entered-eboot`.
- FFPFSC: 13,434,880 bytes, SHA-256
  `AED5BD1E8BB00CA79553D1AC5A64543BF81FA561D213C732EC888FE6D29DE22C`.
- Result: production polished-UI milestone complete. Its rendering path is
  equivalent to the lossless PPSA99742 frame, with no diagnostic filesystem
  activity in the frame loop.

## PPSA99744 - exact glyph-quad blitting

- Goal: eliminate the malformed lowercase `r` shoulders and subtly eroded
  bottom rows still visible on the physical TV.
- Diagnosis: RmlUi generates every font glyph as an integer-positioned,
  axis-aligned quad, but SDL's software `RenderGeometry` path splits each quad
  into two fixed-point textured triangles. That path previously produced
  visible seams in other UI geometry and does not preserve exact glyph bitmap
  coverage.
- Changes:
  - detects RmlUi's canonical four-vertex/six-index textured quads;
  - sends only unscaled, axis-aligned quads through SDL's exact rectangle blit;
  - retains `SDL_RenderGeometry` as the fallback for all other geometry;
  - uses nearest sampling and writes `/download0/PPSA99744-ui.bmp` for native
    1920x1080 comparison.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99744-native/PPSA99744-ui.bmp`, SHA-256
  `C553199B849314471245D700A406E004D1043C8EEFEFE6863B9BC7952C637BCE`.
- FFPFSC: 13,434,880 bytes, SHA-256
  `6748D8D02DF7CE0EAF3B28A037A7905FCA69494E0253D7538670E2CBEA146CD8`.
- Result: exact blitting changes glyph-edge pixels and removes the triangle
  path as a source of nondeterminism. The enlarged comparison still preserves
  Roboto's objectionable lowercase `r` shoulder, proving that remaining shape
  is in the hinted glyph rather than its quad rasterization.
