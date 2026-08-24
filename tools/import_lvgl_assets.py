#!/usr/bin/env python3
"""Import the approved PSRadio launcher and controller artwork."""

from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path

from PIL import Image


ICONS = ("cross", "circle", "square", "triangle", "options", "search")
LAUNCHER_ASSETS = ("icon0.png", "pic0.dds", "pic1.dds", "snd0.at9")


def write_tga(source: Path, destination: Path, size: tuple[int, int]) -> None:
    image = Image.open(source).convert("RGBA").resize(size, Image.Resampling.LANCZOS)
    rgba = image.tobytes()
    bgra = bytearray(len(rgba))
    for offset in range(0, len(rgba), 4):
        bgra[offset : offset + 4] = (
            rgba[offset + 2],
            rgba[offset + 1],
            rgba[offset],
            rgba[offset + 3],
        )
    header = struct.pack(
        "<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0,
        image.width, image.height, 32, 0x28
    )
    destination.write_bytes(header + bgra)


def main() -> None:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=repo.parent / "ps5-radio-lvgl",
        help="completed LVGL PSRadio repository",
    )
    args = parser.parse_args()
    source = args.source.resolve()

    icon_output = repo / "ui" / "icons"
    for name in ICONS:
        write_tga(source / "assets" / "icons" / f"{name}.png",
                  icon_output / f"{name}.tga", (40, 40))
    write_tga(source / "sce_sys" / "icon0.png",
              icon_output / "psradio.tga", (64, 64))
    for name in LAUNCHER_ASSETS:
        shutil.copyfile(source / "sce_sys" / name, repo / "sce_sys" / name)


if __name__ == "__main__":
    main()
