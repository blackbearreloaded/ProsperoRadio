#!/usr/bin/env python3
"""Small regression check for the RmlUi PSRadio integration contract."""

from __future__ import annotations

import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


CARD_FIELDS = ("card", "art", "badge", "name", "meta", "rank", "favorite", "live")
FIXED_IDS = {
    "heading", "subtitle", "page-prev", "page-label", "page-next", "page-thumb",
    "discover-panel", "detail-art", "detail-badge", "detail-name", "detail-meta",
    "detail-codec", "detail-metric", "detail-status-dot", "detail-status",
    "play-button", "play-icon", "play-label", "now-art", "now-badge", "now-name",
    "now-meta", "now-state", "credit-button", "connection-dot", "connection-label",
    "search-overlay", "search-query", "search-reset", "search-apply",
    "credits-overlay", "credits-close", "brand-mark",
}


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = ET.parse(repo / "ui" / "main.rml").getroot()
    ids = [value for element in root.iter() if (value := element.get("id"))]
    assert len(ids) == len(set(ids)), "RML ids must be unique"

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

    project = json.loads((repo / "project.json").read_text(encoding="utf-8"))
    assert project["titleId"] == "PPSA99768"
    for source in ("src/radio_app.cpp", "src/radio_input.c", "src/radio_ime.c",
                   "src/radio_service.c"):
        assert source in project["sources"]

    for name, dimensions in {
        "cross": (40, 40), "circle": (40, 40), "square": (40, 40),
        "triangle": (40, 40), "options": (40, 40), "search": (40, 40),
        "psradio": (64, 64),
    }.items():
        data = (repo / "ui" / "icons" / f"{name}.tga").read_bytes()
        header = struct.unpack("<BBBHHBHHHHBB", data[:18])
        assert header[2] == 2 and header[8:12] == (*dimensions, 32, 0x28)
        assert len(data) == 18 + dimensions[0] * dimensions[1] * 4
    print(f"validated {len(required)} RML ids, project wiring, and 7 exact TGA assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
