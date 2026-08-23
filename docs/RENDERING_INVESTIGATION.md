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

## Next milestone

`PPSA99727` moves RmlUi rendering to the OpenGL 2 interface backed by SDL's PS5
OSMesa context. The goal is to preserve anti-aliased font atlas sampling and
rounded geometry without the SDL software `RenderGeometry` quantization.
