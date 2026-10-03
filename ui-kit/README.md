# The interface kit

ProsperoRadio's interface is built on
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui). The
kit is not kept in this repository: `tools/prepare-ui-kit.sh` fetches the
commit pinned in that script during the build and stages what the app uses
under `.deps/ui-kit/stage`.

| Here | What it is |
| --- | --- |
| `kit-files.txt` | The kit sources the app compiles, relative to the kit's `src/` |
| `overrides/` | Files laid over the kit's: the text engine from ProsperoEden (the console's own fonts, HarfBuzz shaping, right-to-left ordering), because station names come in every script and the kit's baked fonts are ASCII |
| `patches/` | Patches applied to the staged kit sources (the upload of the font atlas rows the text engine drew) |

To build against a kit checkout of your own, set `UI_KIT_DIR` to it. To move
to a newer kit, change the commit in `tools/prepare-ui-kit.sh`, rebuild, and
run the console scripts in `tests/console`.
