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

## PPSA99745 - Inter screen-typeface test

- Goal: replace Roboto's remaining lowercase `r` shape while preserving exact
  bitmap coverage, native 1920x1080 placement, and the verified layout.
- Changes:
  - uses official Inter 4.1 Regular and SemiBold, a face designed for
    computer-screen readability and a tall mixed-case x-height;
  - maps emphasized UI text to weight 600 instead of synthetic weight 700;
  - retains PPSA99744's exact unscaled glyph-quad blitter and changes no layout
    dimensions;
  - writes `/download0/PPSA99745-ui.bmp` for native comparison.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99745-native/PPSA99745-ui.bmp`, SHA-256
  `230381D5B9EB2825E703DC2F5547C12861CCAEB2F3BB64ABC8A210BDCAFB625E`.
- FFPFSC: 13,828,096 bytes, SHA-256
  `FE2EF8D061A91B77DB888336DFF14E8831AF03F89886B91F56D491EC3469BF5E`.
- Result: native and Chiaki frames show a clean lowercase `r`, even baseline,
  complete lower rows, and less blocky emphasis. Inter's wider metadata wraps
  in the selected-station panel, making its lower edge touch the player strip;
  PPSA99746 will correct only that width-dependent layout regression.

## PPSA99746 - pixel-perfect Inter layout

- Goal: retain PPSA99745's verified Inter glyph rendering while restoring clear
  separation between the selected-station panel and player strip.
- Changes:
  - widens only the selected-station panel from 360 to 390 pixels so its fixed
    metadata remains on one line and shortens the panel by one text line;
  - retains all font sizes, line heights, card dimensions, and exact glyph-quad
    blitting from PPSA99745;
  - writes `/download0/PPSA99746-ui.bmp` for final native layout inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99746-native/PPSA99746-ui.bmp`, SHA-256
  `91300C240930D47B33A927A670C82CC1708DE71DCAE4FA869490CFEA6D51FEB1`.
- FFPFSC: 13,828,096 bytes, SHA-256
  `972D9E984988EEFCD529F633CDA8E3A2C2A2FCDA7C952166102FE7137B98EFA0`.
- Result: the lossless frame has clean `r` shoulders, even baselines, complete
  lower rows, consistent Regular/SemiBold weight, one-line selected metadata,
  and clear separation between every panel, player strip, and footer.

## PPSA99747 - production pixel-perfect UI

- Goal: package the hardware-verified PPSA99746 renderer, Inter typography, and
  layout without diagnostic filesystem activity.
- Changes:
  - keeps the exact glyph-quad blitter, Inter Regular/SemiBold assets, RCSS, and
    RmlUi markup verified in PPSA99746;
  - removes the one-time `/download0` BMP capture from the frame loop.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. Chiaki exited before the shared PS5
  lock was released.
- Evidence: `PPSA99747-20260823-150200-result.json`, klog, and running/after-close
  screenshots; lifecycle classification is `entered-eboot`.
- FFPFSC: 13,828,096 bytes, SHA-256
  `BAF31D4F72643A7210BA26263E1E09784D7EB8F169BBC8ACE194019947B6D959`.
- Result: the production build preserves PPSA99746's inspected native frame:
  clean lowercase `r` contours, aligned baselines, complete bottom glyph rows,
  consistent Regular/SemiBold text, and unclipped controls. The Chiaki capture
  also shows the same typography and layout under the PS5 system overlay.

## PPSA99748 - native PlayStation SST typography

- Goal: replace the remaining malformed Inter diagonals and curves (`K`, `9`,
  and `/`) with the native PlayStation UI face, while retaining exact glyph
  bitmap coverage and the established layout.
- Changes:
  - loads `/preinst/common/font/SST-Light.otf` and `SST-Bold.otf` directly from
    the console through RmlUi's existing FreeType engine;
  - uses the `SST` family for all document and button text;
  - retains the exact unscaled glyph-quad blitter and Noto Emoji fallback;
  - writes `/download0/PPSA99748-ui.bmp` for lossless 1920x1080 inspection.
- Packaging note: the Sony font files remain on the console and are not copied
  into or redistributed with the application.
- Outcome: entered `eboot` and remained stable, but displayed the deliberate
  red initialization-failure frame. It then closed cleanly and released its
  runtime layers and Chiaki before the shared PS5 lock was released.
- Evidence: `PPSA99748-20260823-152303-result.json`, klog, and running/after-close
  screenshots; lifecycle classification is `entered-eboot`. The retrieved
  `download0.dat` contains no BMP because the render loop was never entered.
- FFPFSC: 13,828,096 bytes, SHA-256
  `EFD6CD58DA80020C7635433279FDACBEACE7FA5A3C3682F176BE5722B1B206F9`.
- Result: FTP can read both SST files, but the native application sandbox cannot
  open `/preinst/common/font`; `LoadFonts()` fails before document creation.
  Direct system-font loading is rejected. The title ID will not be reused.

## PPSA99749 - matched Noto Sans screen typography

- Goal: correct Inter's glyph-specific `K`, `9`, `/`, and lower-edge shapes
  using a redistributable, statically hinted family with matched metrics.
- Changes:
  - pairs the existing official Noto Sans 2.008 Regular with the matching
    official 2.008 SemiBold instead of the older mismatched Bold asset;
  - uses 1.4 line boxes for proportional text to provide explicit lower-edge
    clearance without reducing font sizes;
  - retains RmlUi's grayscale FreeType rasterizer, compatibility conversion to
    straight alpha, and exact unscaled SDL glyph-quad blitter;
  - writes `/download0/PPSA99749-ui.bmp` for lossless 1920x1080 inspection.
- Asset hashes:
  - Regular: `B85C38ECEA8A7CFB39C24E395A4007474FA5A4FC864F6EE33309EB4948D232D5`;
  - SemiBold: `87A8B90ECE1E89746B544E4E086F85A3710E41485A8078F9BE874837DFAD45D5`.
- Distribution: both Noto Sans faces and `NotoSans-OFL.txt` are bundled with
  the application. No PlayStation system font is opened or copied at runtime.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99749-native/PPSA99749-ui.bmp`, SHA-256
  `DC8B22AEF4C84E0AF3D1C889033123C146402E512C831DD9C72F644D3AC5E64A`;
  `PPSA99749-20260823-152938-running.png`, SHA-256
  `1784D89F73F11D17962E7A311C2ABC3DBCB19EC3C4E7C2A621D10A620DC6DC60`.
- FFPFSC: 14,155,776 bytes, SHA-256
  `22CE5F68BE754E2276F50FBF98F47794D01FD6A2C2C9F1CBDEFB1071E3DD0414`.
- Result: inspection of the exact 1920x1080 framebuffer and 4x
  nearest-neighbor crops confirms conventional, complete `K`, `9`, `/`, and
  `r` contours. Every line has empty rows below its glyph coverage; there is
  no lower-edge clipping. Regular and SemiBold baselines are aligned, and all
  controls remain inside their borders without overlap or wrapping regressions.

## PPSA99750 - production distributable Noto UI

- Goal: package the hardware-verified PPSA99749 renderer, Noto typography, and
  layout without diagnostic framebuffer writes.
- Changes:
  - keeps the exact glyph-quad blitter, matched Noto Sans 2.008 Regular and
    SemiBold assets, 1.4 proportional line boxes, RCSS, and RmlUi markup
    verified in PPSA99749;
  - bundles the font files and SIL Open Font License with the application;
  - does not load, copy, or depend on PlayStation system fonts;
  - removes the one-time `/download0` BMP capture from the frame loop.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. Chiaki exited before the shared PS5
  lock was released.
- Evidence: `PPSA99750-20260823-153528-result.json`, klog, and running/after-close
  screenshots; lifecycle classification is `entered-eboot`. The running frame
  preserves the inspected PPSA99749 typography and layout.
- FFPFSC: 14,155,776 bytes, SHA-256
  `CC638561515F11447D827EE8D68D63D431C164C7E3FD62F3A5BD320B1DE12D64`.
- Result: production open-font typography milestone complete, with no runtime
  dependence on proprietary or console-installed fonts.

## PPSA99751 - simplified large-type browse screen

- Goal: remove the residual lower-edge clipping by correcting the dense card
  layout rather than changing the verified Noto family again.
- Diagnosis: each old 136-pixel card provided 104 pixels of inner height after
  padding and borders, while its three proportional text lines and gaps needed
  about 112 pixels. The centered `.station-copy` overflowed its box and its
  descendants clipped the excess. The absolutely positioned `SAVED` label did
  not share that constraint, explaining why it appeared complete.
- Changes:
  - reduces each station card from three text lines to two;
  - uses 184-pixel cards, integer 48/44-pixel line boxes, larger SemiBold text,
    and visible vertical overflow for the text container;
  - displays four stations and one spacious selection panel;
  - removes the introductory subtitle, bitrate/codec rows, duplicate player
    strip, and secondary selected-station actions;
  - replaces text controller abbreviations with CSS-native controller symbols;
  - writes `/download0/PPSA99751-ui.bmp` for exact-frame inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99751-native/PPSA99751-ui.bmp`, SHA-256
  `9490ABE064EE1DFADF65DFDBB3ABB2276723D9DB8563D571AC4D5143E3670A4D`;
  running screenshot SHA-256
  `96F54ECC4A85181D375EC33196D29881C866238F2E6F6C38FB54B4357A04BE67`.
- FFPFSC: 14,155,776 bytes, SHA-256
  `D5203366075AB055639D597C9717D8DF008B04305CF9CEA1CD2070C6FC1C204C`.
- Result: the lossless frame and 4x card crop show complete lower glyph rows
  with substantial clearance. Typography and layout are accepted. RmlUi did
  not preserve rotated child strokes in the footer: Cross and Triangle became
  horizontal bars, while percentage border radii left Circle and Search
  square. PPSA99752 will use only axis-aligned blocks and pixel radii for those
  icons.

## PPSA99752 - software-renderer-safe controller icons

- Goal: preserve PPSA99751's accepted typography while making every visual
  control hint recognizable in RmlUi's PS5 software-rendering path.
- Changes:
  - replaces rotated Cross and Triangle strokes with integer-positioned,
    axis-aligned color blocks;
  - replaces percentage radii on Circle and Search with fixed pixel radii;
  - keeps Square and Options as integer-positioned borders and bars;
  - keeps Play as the verified CSS border triangle;
  - uses no Unicode controller glyphs, icon fonts, external textures, SVG
    plugin, or generated bitmap dependencies;
  - writes `/download0/PPSA99752-ui.bmp` for exact-frame inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence: `PPSA99752-native/PPSA99752-ui.bmp`, SHA-256
  `04C89B96DF1313AACB070608CEDDCF6CAB9C1D554ECEA710F353F8238E493998`;
  running screenshot SHA-256
  `B3E2097899591DB5A8FF58643B8E479DC4F137B2ACAE70870C19766B538A1104`.
- FFPFSC: 14,155,776 bytes, SHA-256
  `0CAF0B0734C64F5CE9FEE961AC815BF65E72FE7826A5B19E5B7AF9939BABBCA2`.
- Result: the native 1920x1080 frame shows complete, unclipped large-type text
  and distinct Cross, Circle, Square, Triangle, Options, Search, Save, and Play
  symbols. The simplified screen has no overlapping or malformed controls.

## PPSA99753 - production simplified large-type UI

- Goal: package the hardware-verified PPSA99752 screen without diagnostic
  filesystem activity.
- Changes:
  - keeps the two-line station cards, integer line boxes, larger Noto Sans
    SemiBold typography, simplified information hierarchy, and axis-aligned
    controller symbols verified in PPSA99752;
  - removes the one-time `/download0` framebuffer capture.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. Chiaki exited before the shared PS5
  lock was released.
- Evidence: `PPSA99753-20260823-160010-result.json`, klog, and running/after-close
  screenshots; lifecycle classification is `entered-eboot`. Running screenshot
  SHA-256:
  `FCFEAEE16AC71797CE03D18487A78D4ACBF53D235F19E799DB21F601DDAA15A2`.
- FFPFSC: 14,155,776 bytes, SHA-256
  `BB4E30B389FA3DDA0398FED34C9C6BFDBFFFA1E8DA832AEB2480D7A675D50900`.
- Result: production redesign milestone complete. The page uses fewer, larger
  strings with explicit vertical clearance and controller-native iconography;
  it has no proprietary font dependency and no text-based PS5 button labels.

## PPSA99754 - Source Sans and generated controller assets

- Goal: remove the remaining malformed Noto Sans glyph shapes reported in
  `NTS Radio` and replace the block-built footer controls with polished bitmap
  symbols.
- Typography diagnosis: the lossless PPSA99753 layout already had substantial
  vertical clearance. The residual uneven `N`, `S`, `a`, `r`, and slash shapes
  were therefore font-outline/raster results, not CSS descendants clipping one
  another.
- Changes:
  - replaces Noto Sans with Adobe Source Sans 3 v3.052 static Regular and
    Semibold OTF/CFF1 faces under the SIL Open Font License 1.1;
  - preserves the verified integer font sizes, integer line boxes, two-line
    cards, and exact glyph-quad renderer;
  - replaces all five footer controller constructions with ImageGen-derived
    Cross, Circle, Square, Triangle, and Options masks;
  - exports each runtime icon as a straight-alpha, uncompressed 36 x 36 TGA and
    renders it 1:1 with nearest sampling;
  - adds a minimal TGA loader to the existing SDL renderer, without SDL_image
    or another image dependency;
  - writes `/download0/PPSA99754-ui.bmp` for lossless inspection.
- Font provenance:
  - upstream: `https://github.com/adobe-fonts/source-sans`, tag `3.052R`;
  - Regular SHA-256:
    `08DF266400933D3178D081A45F94A08814C3E55B4B7DD2E0FF69CB1329F13AB6`;
  - Semibold SHA-256:
    `36F1CD2C344AA3109E64429BCE41A41E4B2B923BEB2DB19B8DBF9BB56F05A4F9`.
- Image generation provenance: `ui/icons/README.md` retains the complete prompt,
  generated source mask, transparent masters, runtime palette, and conversion
  description.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock. A second brief locked operation fetched
  the download image, then released the lock again.
- Evidence:
  - `PPSA99754-native/PPSA99754-ui.bmp`, SHA-256
    `56C9E864C583BF0F4555FB2C9B6B58A62EDEF093B980454F2A2917011D1AC678`;
  - `PPSA99754-20260823-161631-running.png`, SHA-256
    `576C6D97B16B08C297FF986A541721A785817DF8FD41E745B35CEFA500E8397F`;
  - exact native crops: `brand.png`, `nts-card.png`, `selected.png`, and
    `controller-icons.png` under `PPSA99754-native`.
- FFPFSC: 15,007,744 bytes, SHA-256
  `D992B9779E91E1D7B4E3059A6F92AE3C2D0F0095CA81C3A2B94DDBF454358410`.
- Result: the native framebuffer shows complete lower glyph rows, a consistent
  baseline, clean Source Sans letterforms in every reported problem string,
  and smooth, recognizable controller symbols with no scaling artifacts.

## PPSA99755 - production polished UI

- Goal: package the hardware-verified PPSA99754 rendering without diagnostic
  framebuffer writes.
- Changes:
  - keeps the byte-equivalent Source Sans 3 faces, RCSS geometry, RmlUi markup,
    exact glyph-quad renderer, generated 36 x 36 TGA assets, and straight-alpha
    SDL texture path verified in PPSA99754;
  - removes the one-time `/download0` BMP capture;
  - increments the title ID and displayed title name to PPSA99755.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. Chiaki exited before the shared PS5
  lock was released.
- Evidence: `PPSA99755-20260823-162140-result.json`, klog, and running/after-close
  screenshots. Running screenshot SHA-256:
  `CA414F01F27C6867B9E4B422C88A00DDD88698B2C59D2AA1B1D39D345929CD53`.
- FFPFSC: 15,007,744 bytes, SHA-256
  `D9CD10CBF29CC368F81267C128023933750844DA504A001488E5CE43D7D15448`.
- Result: production typography-and-icon milestone complete. The release frame
  visibly matches the lossless PPSA99754 proof while performing no diagnostic
  filesystem write in the render loop.

## PPSA99756 - plain Liberation Sans review build

- Goal: replace Source Sans 3's remaining humanist character with a deliberately
  neutral, conventional typeface while preserving the accepted layout and
  generated controller icons.
- Changes:
  - uses Liberation Sans 2.1.5, an Arial-metric-compatible static TrueType
    family with conventional `a`, `r`, `t`, `N`, `S`, numeral, and punctuation
    forms;
  - replaces the global SemiBold style with Regular 400 for metadata, footer,
    and inactive filters;
  - uses the real Bold 700 face only for headings, station names, artwork
    labels, active filters, and primary actions; no synthetic weight is used;
  - changes no dimensions, line boxes, RmlUi markup, renderer code, colors, or
    controller assets;
  - writes `/download0/PPSA99756-ui.bmp` for lossless inspection.
- Font provenance:
  - upstream: `https://github.com/liberationfonts/liberation-fonts`, release
    `2.1.5`;
  - license: SIL Open Font License 1.1, bundled as
    `ui/fonts/LiberationSans-OFL.txt`;
  - Regular SHA-256:
    `76D04C18EA243F426B7DE1F3AD208E927008F961DC5945E5AAD352D0DFDE8EE8`;
  - Bold SHA-256:
    `788ABEE4C806D660E8AEE46689DD8540CD4BB98DA03DCC9D171CE3EFD99A9173`.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock. A second brief locked operation fetched
  the download image and released the lock immediately.
- Evidence:
  - `PPSA99756-native/PPSA99756-ui.bmp`, SHA-256
    `01C3C105117AF3839D83943DFC836811CDF45BFC0B9D4058A4FF4D8535D701A1`;
  - `PPSA99756-20260823-163517-running.png`, SHA-256
    `08E6F5AB1277CC6ACDF229A984D897ABE861A3FE0736CBEFF06E17E5DA85CF04`;
  - exact native crops: `brand.png`, `nts-card.png`, `selected.png`, and
    `controller-icons.png` under `PPSA99756-native`.
- FFPFSC: 15,466,496 bytes, SHA-256
  `64E458D96D993C4BBFBC346359DFB18F785616D736E17F9BE4C6808FE35E4F4C`.
- Result: the native frame has a visibly plainer typography system, clean
  conventional glyphs in every previously reported string, intact baselines,
  and a stronger regular-versus-bold hierarchy. PPSA99756 remains the installed
  review build pending live-TV acceptance before creating a production title.

## PPSA99757 - isolate native hinting

- Goal: determine whether the remaining malformed `K`, `P`, `9`, slash, and
  apparent lower-edge clipping originates in the selected font, RmlUi layout,
  SDL compositing, or FreeType rasterization.
- Diagnosis:
  - RmlUi loads each glyph with `FT_LOAD_COLOR`, which enables the font's native
    TrueType hinter by default;
  - the generated glyph atlas, integer RmlUi quads, straight-alpha texture
    conversion, 1:1 SDL blit, and 1920 x 1080 presentation path preserve every
    selected source row and column;
  - the lossless PPSA99756 framebuffer contains the reported contour defects
    before Remote Play scaling, ruling out Chiaki and the television as their
    source.
- Experiment:
  - changes only the FreeType load flags to
    `FT_LOAD_COLOR | FT_LOAD_NO_HINTING`;
  - preserves Liberation Sans 2.1.5, all font sizes, weights, line boxes,
    markup, UI geometry, renderer code, colors, and image assets;
  - stores the reproducible RmlUi delta as
    `vendor/ps5/rmlui/patches/0001-disable-font-hinting.patch`;
  - rebuilt `librmlui.a` SHA-256:
    `66D71B9FCA92DE9703C9BC90EC9740CFABDF16772EE080E8E04413A3597CD85D`.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki before
  releasing the shared PS5 lock. The capture was fetched during a second brief
  locked operation.
- Evidence:
  - `PPSA99757-native/PPSA99757-ui.bmp`, SHA-256
    `C422B35DED8E7806DE579978D9AACC612C3382E2862F9951A760095C3C7307A9`;
  - `PPSA99757-20260823-165213-running.png`, SHA-256
    `8FB70D774F10A334218D6BD95E582AFA21516F2BC7B006EFA038CAAA2F5CDC32`;
  - exact and nearest-neighbor 6x crops: `kexp-render.png` and
    `kexp-render-6x.png` under `PPSA99757-native`.
- FFPFSC: 15,466,496 bytes, SHA-256
  `AB9FD055D387D7A0CF2F1379AE3BE718540E70A8ED207EA30F9666375AC2D17A`.
- Result: removing native hinting eliminates the irregularly snapped contours
  and proves that the defect is rasterizer configuration, not the font or CSS.
  Fully unhinted text is smoother but slightly too soft for the final TV UI.

## PPSA99758 - light auto-hinted production typography

- Goal: retain PPSA99757's corrected glyph shapes while recovering crisp,
  consistent screen-text stems.
- Changes:
  - loads glyphs with
    `FT_LOAD_COLOR | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT`;
  - changes no font file, size, weight, line box, markup, UI geometry, renderer
    code, color, or image asset;
  - applies the reproducible follow-up patch
    `vendor/ps5/rmlui/patches/0002-use-light-autohint.patch` after the
    PPSA99757 diagnostic patch;
  - rebuilt `librmlui.a` SHA-256:
    `876BE332FA588DFA8B253813AA0D373DA352391BC7A886FD08074BD0711701DB`.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki before
  releasing the shared PS5 lock. A second locked capture fetch waited for
  another agent to release the console, then acquired and released the lock
  normally.
- Evidence:
  - `PPSA99758-native/PPSA99758-ui.bmp`, SHA-256
    `23DFA5AD5BFC4CC3FF3AD0921A76355A61C987FC2B7B8D16F3EFC324FA6D53A6`;
  - `PPSA99758-20260823-170112-running.png`, SHA-256
    `2095EB05F02F8D0DA5979E274CE855A56F4186CCD47AAC0C2495AF33EDE20931`;
  - exact native crops: `brand.png`, `nts-card.png`, `selected-copy.png`,
    `footer.png`, and `kexp-render.png` under `PPSA99758-native`;
  - nearest-neighbor 6x comparison crop: `kexp-render-6x.png`.
- FFPFSC: 15,466,496 bytes, SHA-256
  `8CD7EC8877E4A0DAA92C07B23148E3392A5292CB28C7D25567B14712A7D8B99B`.
- Result: the lossless PS5 framebuffer shows complete lower glyph rows,
  consistent baselines, crisp stems, and stable contours for every reported
  problem class: `K`, `P`, `9`, `N`, `S`, lowercase `a`, `r`, `t`, slash, and
  footer labels. PPSA99758 is the installed renderer-review build.

## PPSA99759 - exact LVGL Montserrat source

- Goal: remove font-family differences from the comparison with the precise
  LVGL reference application.
- Changes:
  - replaces Liberation Sans with the byte-identical Montserrat Medium TTF used
    to generate LVGL's built-in fonts;
  - uses one real 500-weight face without synthetic bolding;
  - aligns the app's active text sizes with LVGL's compiled 20, 24, 28, 32, 36,
    40, and 48-pixel size set while preserving the simplified RmlUi layout;
  - keeps light auto-hinting, the exact glyph-quad renderer, 1:1 nearest SDL
    copies, and the existing open-source controller assets;
  - writes `/download0/PPSA99759-ui.bmp` for lossless inspection.
- Font provenance:
  - Montserrat Medium SHA-256:
    `421F26B23E2BE6B98373D32ACD3CB2897B154D4BF0A77D26534CE476E4CBED53`;
  - SIL Open Font License SHA-256:
    `C376DD6B498C6886FE5A43DB3C48CF83EF05CAC6846EC4010E535D48E43A29B0`.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence:
  - `PPSA99759-native/PPSA99759-ui.bmp`, SHA-256
    `46D60CC51726F0466D4FA1DE3E6B34B47C7DD842A88E07E9436EEEBD0A4865DA`;
  - `PPSA99759-20260823-171946-running.png`, SHA-256
    `D3B480B0921C51DFAED5E9E740565BE4BE1969008ED84752240597D9FBA4C536`.
- FFPFSC: 15,597,568 bytes, SHA-256
  `CD97082AAB14E6E345BEFF00A04460E2CE656AD17D89409FB90DCA8A958FD60C`.
- Result: the typeface and metrics now match the LVGL design and look visibly
  closer, but the native frame retains harder, more binary contours than the
  LVGL pre-rasterized glyphs. This isolates the remaining difference to glyph
  mask generation rather than font selection, CSS spacing, clipping, Remote
  Play, or the final PS5 presentation surface.

## PPSA99760 - LVGL-style 4-bit coverage diagnostic

- Goal: determine whether LVGL's 16-level antialiasing coverage alone explains
  the remaining contour difference.
- Changes:
  - quantizes every RmlUi grayscale glyph pixel to `0, 17, ..., 255` after
    FreeType light auto-hinting and before atlas construction;
  - records the one-hunk reproducible change as
    `vendor/ps5/rmlui/patches/0003-quantize-grayscale-coverage.patch`;
  - keeps the exact Montserrat source, fixed integer sizes, CSS geometry,
    texture format, blend mode, and 1:1 SDL copy path unchanged;
  - rebuilt `librmlui.a` SHA-256:
    `1A8AE54B5F2A486FBAFAE3169EDC9141DCA6F881224883A5B9D571F549B325C2`;
  - writes `/download0/PPSA99760-ui.bmp` for lossless inspection.
- Outcome: entered `eboot`, wrote the native frame, remained stable through
  observation, closed cleanly, and released its runtime layers and Chiaki
  before releasing the shared PS5 lock.
- Evidence:
  - `PPSA99760-native/PPSA99760-ui.bmp`, SHA-256
    `22A87D8F9518AB3F148544988BC66BFC028EEF5BB11A2E402A942BE92027E3E2`;
  - `PPSA99760-20260823-172822-running.png`, SHA-256
    `B4A099420FC6FFBD432E38AFC68E767A191ACE046B5080DB9B80DE1845171350`;
  - lifecycle result: `entered-eboot`, eboot SHA-256
    `CC2E53119310B3D9426FFCFA813EED812EECB3561A4691F6794C3804DA563F35`.
- FFPFSC: 15,597,568 bytes, SHA-256
  `DD7A5CFFAF06F1DDA472A1A1EEE93DBF3C71C071707382C87C6128E4D6B779DD`.
- Result: 4-bit quantization does not materially improve the visible glyph
  contours. LVGL's advantage comes from its build-time rasterized mask shapes,
  not merely the number of alpha levels. The next implementation will keep
  RmlUi for HTML/CSS layout but use those exact LVGL bitmap masks through
  RmlUi's supported custom bitmap-font interface.

## PPSA99761 - exact LVGL bitmap-font engine

- Commit: `022ea3d`
- Goal: render the exact masks used by LVGL while retaining RmlUi for HTML/RCSS
  layout.
- Changes:
  - adds a custom RmlUi bitmap-font engine derived from RmlUi's supported sample
    interface;
  - generates 20, 24, 28, 32, 36, 40, and 48-pixel BMFont atlases directly
    from LVGL's built-in 4-bit Montserrat C data;
  - preserves LVGL glyph dimensions, offsets, integer advances, class kerning,
    and all 16 alpha levels;
  - adds `tools/generate_lvgl_bitmap_fonts.py --check`, whose 16 generated files
    have combined SHA-256
    `c1e9b41232afd6720e04ea14c972caaea20cd839149766ada95aecd4e15ee677`.
- Outcome: entered `eboot`, wrote the native frame, remained stable, closed
  cleanly, and released its runtime layers and Chiaki before the PS5 lock was
  released.
- Evidence:
  - `PPSA99761-native/PPSA99761-ui.bmp`, SHA-256
    `2DABFA54059DAFF461170B461F53D96CD9B22E8CAF680DA546A6E05EB67E1325`;
  - exact source-atlas reconstruction matches the LVGL preview with zero mask
    or alpha differences;
  - nearest-neighbor comparisons under `PPSA99761-native` show the native RmlUi
    output still differs from that source mask.
- FFPFSC: 15,794,176 bytes, SHA-256
  `D4DE55FD1CF06997EF034B9BE78FEE6031CA4C05835EF42A6BA7E64917AF02C6`.
- Result: font selection, hinting, and mask generation are no longer variables.
  The remaining defect occurs after the exact atlas enters the SDL rendering
  path.

## PPSA99762 - textured-quad dispatch diagnostic

- Commit: `559c8e9`
- Goal: determine why exact LVGL atlas texels were not preserved by the final
  textured-quad path.
- Changes:
  - adds bounded diagnostics for each strict 1:1 quad acceptance or rejection
    branch;
  - changes no font asset, glyph metric, layout rule, texture data, or renderer
    decision.
- Outcome: entered `eboot`, remained stable, closed cleanly, and released its
  runtime layers and Chiaki before the PS5 lock was released.
- Evidence: `PPSA99762-20260823-175454-result.json`; eboot SHA-256
  `0755AD003B7B0B5D4C7E8258FB1A9B3146A07C15A9F17D546FDF60DAEF0BABED`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `62D9D1990E168151518379C836ED6A1C91AF43DE67218ABACF70DF4F83C6F520`.
- Result: app `stderr` is not forwarded into the port-3232 kernel log, so this
  diagnostic channel cannot classify renderer-internal branches and was
  removed in the next build.

## PPSA99763 - generic indexed-quad copy control

- Commit: `a4e74e6`
- Goal: remove the strict contiguous-vertex assumption from the 1:1 texture
  copy path and test whether RmlUi's final mesh batching caused fallback to
  `SDL_RenderGeometry`.
- Changes:
  - recognizes each canonical two-triangle quad from its six indices, including
    non-contiguous vertex storage;
  - validates the complete batch before drawing and copies accepted 1:1 quads
    with nearest sampling;
  - removes the ineffective `stderr` diagnostics.
- Outcome: entered `eboot`, wrote the native frame, remained stable, closed
  cleanly, and released its runtime layers and Chiaki before the PS5 lock was
  released. Lock state was free and the local Chiaki process count was zero
  after the cycle.
- Evidence:
  - `PPSA99763-native/PPSA99763-ui.bmp`, SHA-256
    `2DABFA54059DAFF461170B461F53D96CD9B22E8CAF680DA546A6E05EB67E1325`;
  - `PPSA99763-20260823-180222-result.json`, lifecycle result `entered-eboot`;
  - eboot SHA-256
    `0C0C31AF0182F8173FF0985E554FF1315DD963F100D9B88D4131696FF3A621A9`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `A7EE4DE0EBDB5FDB51393E03CAE802DE6A21B258F41CDBCD3BFA5ABC358A79AB`.
- Result: the native BMP is byte-identical to PPSA99761, disproving mesh
  batching as the cause. Pixel inspection of the 32-pixel `K` finds the exact
  first atlas rows followed by skipped/compressed interior rows: the PS5 SDL
  textured-copy sampler does not preserve a nominal 1:1 atlas rectangle. The
  next experiment will composite the retained atlas texels directly into the
  software framebuffer, bypassing texture-coordinate sampling entirely.

## PPSA99764 - retained-atlas surface blit

- Commit: `e950588`
- Goal: bypass SDL texture-coordinate sampling for LVGL font atlases by keeping
  their decoded pixels and copying 1:1 source rectangles with SDL's unscaled
  CPU surface blitter.
- Changes:
  - wraps each RmlUi texture handle with its SDL texture, dimensions, and the
    retained straight-alpha source pixels;
  - creates a CPU surface for paths identified as LVGL bitmap atlases;
  - flushes pending renderer work, applies the active RmlUi clip, and uses
    `SDL_BlitSurface` for validated 1:1 font quads;
  - leaves generated textures, icons, and non-axis-aligned geometry on their
    existing renderer paths.
- Outcome: entered `eboot`, wrote the native frame, remained stable, closed
  cleanly, and released its runtime layers and Chiaki before the PS5 lock was
  released.
- Evidence:
  - `PPSA99764-native/PPSA99764-ui.bmp`, SHA-256
    `2480B99FD2E61EE3F109C8DED2DF80EF581863E1EF84934DD1C109F8D633DBDC`;
  - `PPSA99764-20260823-181246-result.json`, lifecycle result `entered-eboot`;
  - eboot SHA-256
    `B564C043F327F97131AB9F5D1C4BA56B6E2C817DB360BAD40D695281BDB3C02E`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `735D2EA8C5E4B6D09916C043D3C871A57D82F7365042E9803A6185C8116149A2`.
- Result: compared with PPSA99763, only 872 pixels change and no channel changes
  by more than two values. Those differences are confined to white artwork
  labels and are consistent with blend rounding. The 32-pixel `K` support map
  remains unchanged, with 18 extra and 8 missing pixels against the LVGL atlas.
  The next diagnostic will force all loaded TGA textures through this path and
  record the actual source/destination rectangles to `/download0`.

## PPSA99765 - forced-blit path trace

- Commit: `5eb1348`
- Goal: prove which rendered strings reach the CPU surface blitter and capture
  their actual atlas and destination rectangles.
- Changes:
  - enables retained CPU surfaces for every loaded TGA;
  - writes at most 32 accepted 1:1 batches to
    `/download0/PPSA99765-render.log`;
  - records source path, texture size, mesh size, copy count, and the first
    source and destination rectangle for each batch.
- Outcome: entered `eboot`, wrote the native frame and trace, remained stable,
  closed cleanly, and released its runtime layers and Chiaki before the PS5
  lock was released.
- Evidence:
  - `PPSA99765-native/PPSA99765-ui.bmp`, SHA-256
    `AF7B1181A4F4D9255C186343F1C2A40439243D57C652755490D0D505AD578B4B`;
  - `PPSA99765-native/PPSA99765-render.log`;
  - `PPSA99765-20260823-181916-result.json`, lifecycle result `entered-eboot`;
  - eboot SHA-256
    `1BEF4863A5E33ACFF78422E96EF7ED9852410816A944F345F5D6418C95217571`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `4FF3FB54192068BCBEA88662DC90978A665865EADDB553C86CE51EB821C6A881`.
- Result: accepted trace entries include `Jazz24`, one-word filters, artwork
  labels, and controller icons, but exclude `KEXP 90.3 FM`, headings, metadata,
  and other strings containing spaces. The bitmap engine emitted a 0 x 0 quad
  for each space glyph; one degenerate quad made the all-or-fallback batch
  validator reject the complete string. The fix is to preserve the space
  advance and kerning while omitting only its empty geometry.

## PPSA99766 - pixel-exact LVGL text rendering

- Commit: `0c648cf`
- Goal: keep whitespace metrics and kerning while allowing every visible glyph
  in a string to use the exact retained-atlas surface blitter.
- Changes:
  - omits geometry only for zero-area glyphs such as spaces;
  - still applies their kerning, advance, and CSS letter spacing;
  - restores CPU surfaces to LVGL font atlases only and removes the bounded
    runtime trace;
  - preserves RmlUi HTML/RCSS layout, SDL icons, and all non-font geometry.
- Outcome: entered `eboot`, wrote the native frame, remained stable, closed
  cleanly, and released its runtime layers and Chiaki before the PS5 lock was
  released. Final lock state was free and the local Chiaki process count was
  zero.
- Evidence:
  - `PPSA99766-native/PPSA99766-ui.bmp`, SHA-256
    `AE797A4096DCDA55A3D79146BFE48A2C898FDD42A1036D75E9A61C273CDA438C`;
  - `PPSA99766-20260823-182218-result.json`, lifecycle result `entered-eboot`;
  - eboot SHA-256
    `B40A9F748D3A2F71C79080558836CDB68713D743454374862E8044DF66F224D9`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `8F642F6E0807752462B34DB6D5F6B3B319866ABC98E9D1B91510EE4A2D8E6E15`.
- Exact pixel audit: reconstructed masks from the generated FNT/TGA assets were
  compared with the native framebuffer after inverting the known foreground
  and background blend palette. `RADIO BROWSER`, `Popular stations`, both
  `KEXP 90.3 FM` sizes, `Seattle - Alternative`, `NTS Radio`,
  `London - Electronic`, and the selected metadata all report
  `support_diff=0`, `alpha_diff=0`, and `alpha_abs=0`.
- Result: RmlUi now places the same integer-advanced, class-kerned glyph masks
  as LVGL, and SDL copies every coverage texel without texture sampling. The
  previously malformed `K`, `P`, `9`, slash, `N`, `S`, lowercase letters, and
  lower rows are byte-exact to their LVGL source masks.

## PPSA99767 - production pixel-exact UI

- Commit: `71756b6`
- Goal: package the PPSA99766 rendering path without diagnostic framebuffer
  writes.
- Changes:
  - keeps the exact generated LVGL Montserrat assets, bitmap-font engine,
    whitespace metrics, class kerning, retained-atlas CPU blitter, RmlUi
    HTML/RCSS layout, and controller icon renderer verified in PPSA99766;
  - removes the one-time `/download0` BMP save from the frame loop;
  - increments the title ID and displayed title name to PPSA99767.
- Outcome: entered `eboot`, remained stable through observation, closed
  cleanly, and released its runtime layers. The PS5 lock was released and the
  local Chiaki process count was zero after the cycle.
- Evidence:
  - `PPSA99767-20260823-182918-result.json`, lifecycle result `entered-eboot`;
  - `PPSA99767-20260823-182918-running.png`, SHA-256
    `9F938DAF7C7F082759E5C68010609C271491ADDDBC03CFF1170113BD025B2835`;
  - eboot SHA-256
    `5CD1F80144374DFD7E364FFDF82DBBDD9E7FC7BBFC50DF68605CC69869CB0D25`.
- FFPFSC: 15,794,176 bytes, SHA-256
  `A4AB6B71165AE4CE9F849C87BDE8D37988588F5C87A0D4952ED044D3C83B118D`.
- Result: production pixel-exact UI milestone complete. Its renderer, font
  assets, metrics, and layout are byte-equivalent to the zero-difference
  PPSA99766 proof; only the diagnostic BMP write was removed.
