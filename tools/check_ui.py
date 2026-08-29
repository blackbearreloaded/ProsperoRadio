#!/usr/bin/env python3
# ProsperoRadio - RmlUi asset validation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the shipped RmlUi document, bitmap fonts, and icon assets."""

from __future__ import annotations

import json
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


CARD_FIELDS = ("card", "art", "badge", "name", "meta", "rank", "favorite", "live")
FIXED_IDS = {
    "heading", "subtitle", "page-prev", "page-prev-icon", "page-prev-label",
    "page-label", "page-next", "page-next-icon", "page-next-label", "page-thumb",
    "discover-panel", "detail-art", "detail-badge", "detail-name", "detail-meta",
    "detail-codec", "detail-metric", "detail-status-dot", "detail-status",
    "play-button", "play-icon", "play-label", "now-art", "now-badge", "now-name",
    "now-meta", "now-state", "credit-button", "connection-dot", "connection-label",
    "search-overlay", "search-query", "search-query-label", "search-reset", "search-apply",
    "credits-overlay", "credits-close", "brand-mark", "brand-name", "brand-version",
}


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    ui = repo / "assets" / "ui"
    root = ET.parse(ui / "main.rml").getroot()
    ids = [value for element in root.iter() if (value := element.get("id"))]
    assert len(ids) == len(set(ids)), "RML ids must be unique"

    stylesheet = (ui / "styles" / "app.rcss").read_text(encoding="utf-8")
    supported_font_sizes = {20, 24, 28, 32, 36, 40, 48}
    used_font_sizes = {int(size) for size in re.findall(r"font-size:\s*([0-9]+)px", stylesheet)}
    assert used_font_sizes <= supported_font_sizes, (
        f"unsupported bitmap font sizes: {sorted(used_font_sizes - supported_font_sizes)}"
    )

    required = set(FIXED_IDS)
    required.update(f"tab-{index}" for index in range(5))
    required.update(f"discover-{index}" for index in range(3))
    required.update(f"discover-label-{index}" for index in range(3))
    required.update(f"filter-{index}" for index in range(4))
    required.update(f"filter-label-{index}" for index in range(4))
    required.update(f"eq-{index}" for index in range(5))
    for field in CARD_FIELDS:
        required.update(f"{field}-{index}" for index in range(4))
    assert required <= set(ids), f"missing RML ids: {sorted(required - set(ids))}"

    project = json.loads((repo / "sce_sys" / "param.json").read_text(encoding="utf-8"))
    title = project["localizedParameters"][project["localizedParameters"]["defaultLanguage"]]["titleName"]
    assert title == "ProsperoRadio"
    assert re.fullmatch(r"[0-9]{2}\.[0-9]{3}\.[0-9]{3}", project["contentVersion"])
    assert re.fullmatch(r"[0-9]{2}\.[0-9]{2}", project["masterVersion"])
    assert project["titleId"] == "PPSA99001"
    assert project["applicationCategoryType"] == 65536
    assert project["contentBadgeType"] == 2
    assert "gameIntent" not in project
    brand_mark = root.find(".//*[@id='brand-mark']")
    assert (brand_mark.get("width"), brand_mark.get("height")) == ("56", "56")
    assert root.find(".//*[@id='brand-name']").text == "ProsperoRadio"
    assert root.find(".//*[@id='brand-version']").text == "v{{PROSPERO_RADIO_VERSION}}"
    for source in ("src/radio_app.cpp", "src/radio_input.cpp", "src/radio_ime.cpp",
                   "src/radio_service.cpp", "src/radio_text.cpp"):
        assert (repo / source).is_file()

    multilingual = ui / "fonts" / "lvgl-bitmap" / "multilingual"
    expected_pages = {20: 2, 24: 3, 28: 4, 32: 5}
    for size, page_count in expected_pages.items():
        font = ET.parse(multilingual / f"Radio-{size}.fnt").getroot()
        pages = font.findall("./pages/page")
        glyphs = font.findall("./chars/char")
        assert len(pages) == page_count and len(glyphs) == 17854
        for page in pages:
            data = (multilingual / page.get("file")).read_bytes()
            width, height, pixels, payload = struct.unpack_from("<HHII", data, 4)
            assert data[:4] == b"RTA1" and (width, height) == (2048, 2048)
            assert pixels == width * height and payload == len(data) - 16
    for license_name in ("DejaVuSans-LICENSE.txt", "NotoSans-OFL.txt",
                         "SourceHanSansSC-OFL.txt"):
        assert (multilingual / "licenses" / license_name).stat().st_size > 4000

    for name, dimensions in {
        "cross": (40, 40), "circle": (40, 40), "square": (40, 40),
        "triangle": (40, 40), "options": (40, 40), "search": (40, 40),
        "prospero-radio": (64, 64), "play": (24, 24), "stop": (24, 24),
        "page-up": (18, 18), "page-down": (18, 18),
    }.items():
        data = (ui / "icons" / f"{name}.tga").read_bytes()
        header = struct.unpack("<BBBHHBHHHHBB", data[:18])
        assert header[2] == 2 and header[8:12] == (*dimensions, 32, 0x28)
        assert len(data) == 18 + dimensions[0] * dimensions[1] * 4

    css = (ui / "styles" / "app.rcss").read_text(encoding="utf-8")
    assert "left: 240px;" in css and "width: 1440px;" in css
    assert "#brand-name" in css and "#brand-version" in css
    assert css.count("left: 140px;") >= 2
    assert "top: 52px;" in css
    assert "#search-query.focused" in css and "#search-apply.focused" in css
    assert "#credit-button.focused" in css and "#play-button.focused" in css
    play_icon = root.find(".//*[@id='play-icon']")
    playback_sources = {image.get("src") for image in play_icon.findall("img")}
    assert playback_sources == {"icons/play.tga", "icons/stop.tga"}
    assert root.find(".//*[@id='page-prev-icon']").get("src") == "icons/page-up.tga"
    assert root.find(".//*[@id='page-next-icon']").get("src") == "icons/page-down.tga"
    assert root.find(".//*[@id='search-query-label']") is not None
    print(f"validated {len(required)} RML ids, 71,416 extended glyphs, and 11 exact TGA assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
