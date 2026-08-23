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

- Changes:
  - saves the first complete 1920x1080 SDL surface to
    `/download0/PPSA99733-ui.bmp`;
  - reduces the temporary download-data reservation to 32 MB so the backing
    UFS2 image remains quick to retrieve and inspect.
- Goal: validate exact PS5 renderer output independently of Chiaki window and
  host-desktop scaling.
