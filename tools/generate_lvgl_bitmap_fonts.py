#!/usr/bin/env python3
"""Convert LVGL's bundled 4-bpp Montserrat C fonts to RmlUi BMFont assets."""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


SIZES = (20, 24, 28, 32, 36, 40, 48)
CODEPOINTS = tuple(range(0x20, 0x7F)) + (0x00B0, 0x2022)
ATLAS_WIDTH = 512
PADDING = 1
INTEGER_RE = re.compile(r"(?<![\w.])(-?(?:0[xX][0-9a-fA-F]+|\d+))(?![\w.])")


@dataclass(frozen=True)
class Glyph:
    bitmap_index: int
    advance_16: int
    width: int
    height: int
    offset_x: int
    offset_y: int


@dataclass(frozen=True)
class FontData:
    size: int
    line_height: int
    baseline_from_bottom: int
    bitmap: tuple[int, ...]
    glyphs: tuple[Glyph, ...]
    codepoint_to_gid: dict[int, int]
    kern_scale: int
    kern_left: tuple[int, ...]
    kern_right: tuple[int, ...]
    kern_values: tuple[int, ...]
    kern_right_count: int


def without_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.DOTALL)


def array_body(text: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\[\s*\]\s*=\s*\{{", text)
    if not match:
        raise ValueError(f"array not found: {name}")
    end = text.find("};", match.end())
    if end < 0:
        raise ValueError(f"unterminated array: {name}")
    return text[match.end() : end]


def integers(text: str) -> tuple[int, ...]:
    return tuple(int(token, 0) for token in INTEGER_RE.findall(without_comments(text)))


def scalar(text: str, name: str) -> int:
    match = re.search(rf"\.{re.escape(name)}\s*=\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))", without_comments(text))
    if not match:
        raise ValueError(f"field not found: {name}")
    return int(match.group(1), 0)


def parse_glyphs(text: str) -> tuple[Glyph, ...]:
    body = without_comments(array_body(text, "glyph_dsc"))
    result = []
    fields = ("bitmap_index", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y")
    for entry in re.findall(r"\{([^{}]+)\}", body):
        values = {}
        for field in fields:
            match = re.search(rf"\.{field}\s*=\s*(-?\d+)", entry)
            if not match:
                raise ValueError(f"glyph field not found: {field}")
            values[field] = int(match.group(1))
        result.append(
            Glyph(
                values["bitmap_index"],
                values["adv_w"],
                values["box_w"],
                values["box_h"],
                values["ofs_x"],
                values["ofs_y"],
            )
        )
    if not result:
        raise ValueError("no glyph descriptors found")
    return tuple(result)


def parse_cmaps(text: str) -> dict[int, int]:
    clean = without_comments(text)
    result: dict[int, int] = {}
    for entry in re.findall(r"\{([^{}]+)\}", array_body(clean, "cmaps")):
        values = {}
        for field in ("range_start", "range_length", "glyph_id_start", "list_length"):
            match = re.search(rf"\.{field}\s*=\s*(\d+)", entry)
            if not match:
                raise ValueError(f"cmap field not found: {field}")
            values[field] = int(match.group(1))
        type_match = re.search(r"\.type\s*=\s*(\w+)", entry)
        list_match = re.search(r"\.unicode_list\s*=\s*(\w+)", entry)
        if not type_match or not list_match:
            raise ValueError("invalid cmap")
        cmap_type = type_match.group(1)
        if cmap_type == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            if list_match.group(1) != "NULL":
                raise ValueError("unexpected FORMAT0 unicode list")
            for offset in range(values["range_length"]):
                result[values["range_start"] + offset] = values["glyph_id_start"] + offset
        elif cmap_type == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            offsets = integers(array_body(clean, list_match.group(1)))
            if len(offsets) != values["list_length"]:
                raise ValueError("sparse cmap length mismatch")
            for index, offset in enumerate(offsets):
                result[values["range_start"] + offset] = values["glyph_id_start"] + index
        else:
            raise ValueError(f"unsupported cmap type: {cmap_type}")
    return result


def parse_font(path: Path, size: int) -> FontData:
    text = path.read_text(encoding="utf-8")
    if f"Size: {size} px" not in text:
        raise ValueError(f"{path.name}: size marker mismatch")
    if scalar(text, "bpp") != 4 or scalar(text, "bitmap_format") != 0:
        raise ValueError(f"{path.name}: expected plain 4-bpp data")
    if re.search(r"\.stride\s*=\s*[1-9]", without_comments(text)):
        raise ValueError(f"{path.name}: row-strided bitmaps are unsupported")

    glyphs = parse_glyphs(text)
    cmap = parse_cmaps(text)
    missing = [f"U+{codepoint:04X}" for codepoint in CODEPOINTS if codepoint not in cmap]
    if missing:
        raise ValueError(f"{path.name}: missing {', '.join(missing)}")

    left = integers(array_body(text, "kern_left_class_mapping"))
    right = integers(array_body(text, "kern_right_class_mapping"))
    values = integers(array_body(text, "kern_class_values"))
    left_count = scalar(text, "left_class_cnt")
    right_count = scalar(text, "right_class_cnt")
    if len(values) != left_count * right_count:
        raise ValueError(f"{path.name}: kerning matrix size mismatch")
    if len(left) < len(glyphs) or len(right) < len(glyphs):
        raise ValueError(f"{path.name}: kerning class mapping is too short")

    return FontData(
        size=size,
        line_height=scalar(text, "line_height"),
        baseline_from_bottom=scalar(text, "base_line"),
        bitmap=integers(array_body(text, "glyph_bitmap")),
        glyphs=glyphs,
        codepoint_to_gid=cmap,
        kern_scale=scalar(text, "kern_scale"),
        kern_left=left,
        kern_right=right,
        kern_values=values,
        kern_right_count=right_count,
    )


def glyph_alpha(font: FontData, gid: int) -> bytes:
    glyph = font.glyphs[gid]
    pixel_count = glyph.width * glyph.height
    byte_count = (pixel_count + 1) // 2
    next_index = font.glyphs[gid + 1].bitmap_index if gid + 1 < len(font.glyphs) else len(font.bitmap)
    if next_index - glyph.bitmap_index != byte_count:
        raise ValueError(f"size {font.size}, glyph {gid}: packed bitmap length mismatch")
    packed = font.bitmap[glyph.bitmap_index : next_index]
    return bytes(
        ((packed[index // 2] >> 4) if index % 2 == 0 else (packed[index // 2] & 0x0F)) * 17
        for index in range(pixel_count)
    )


def pack(font: FontData) -> tuple[dict[int, tuple[int, int]], int]:
    positions: dict[int, tuple[int, int]] = {}
    x = y = PADDING
    row_height = 0
    for codepoint in CODEPOINTS:
        glyph = font.glyphs[font.codepoint_to_gid[codepoint]]
        if glyph.width == 0 or glyph.height == 0:
            positions[codepoint] = (0, 0)
            continue
        if x + glyph.width + PADDING > ATLAS_WIDTH:
            x = PADDING
            y += row_height + PADDING * 2
            row_height = 0
        positions[codepoint] = (x, y)
        x += glyph.width + PADDING * 2
        row_height = max(row_height, glyph.height)
    required_height = y + row_height + PADDING
    atlas_height = 1 << max(6, required_height - 1).bit_length()
    return positions, atlas_height


def kerning_amount(font: FontData, left_gid: int, right_gid: int) -> int:
    left_class = font.kern_left[left_gid]
    right_class = font.kern_right[right_gid]
    if left_class == 0 or right_class == 0:
        return 0
    raw = font.kern_values[(left_class - 1) * font.kern_right_count + right_class - 1]
    scaled = (raw * font.kern_scale) >> 4
    advance_16 = font.glyphs[left_gid].advance_16
    return ((advance_16 + scaled + 8) >> 4) - ((advance_16 + 8) >> 4)


def make_tga(font: FontData, positions: dict[int, tuple[int, int]], height: int) -> bytes:
    pixels = bytearray(b"\xff\xff\xff\x00") * (ATLAS_WIDTH * height)
    for codepoint in CODEPOINTS:
        gid = font.codepoint_to_gid[codepoint]
        glyph = font.glyphs[gid]
        x, y = positions[codepoint]
        alpha = glyph_alpha(font, gid)
        for row in range(glyph.height):
            for column in range(glyph.width):
                pixel = ((y + row) * ATLAS_WIDTH + x + column) * 4
                pixels[pixel + 3] = alpha[row * glyph.width + column]
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, ATLAS_WIDTH, height, 32, 0x28)
    return header + pixels


def make_fnt(font: FontData, positions: dict[int, tuple[int, int]], height: int, texture_name: str) -> bytes:
    baseline = font.line_height - font.baseline_from_bottom
    kernings = []
    for left in CODEPOINTS:
        left_gid = font.codepoint_to_gid[left]
        for right in CODEPOINTS:
            amount = kerning_amount(font, left_gid, font.codepoint_to_gid[right])
            if amount:
                kernings.append((left, right, amount))

    lines = [
        '<?xml version="1.0"?>',
        "<font>",
        f'  <info face="Montserrat" size="{font.size}" bold="0" italic="0" charset="" unicode="1" stretchH="100" smooth="1" aa="1" padding="0,0,0,0" spacing="0,0" outline="0"/>',
        f'  <common lineHeight="{font.line_height}" base="{baseline}" scaleW="{ATLAS_WIDTH}" scaleH="{height}" pages="1" packed="0" alphaChnl="0" redChnl="4" greenChnl="4" blueChnl="4"/>',
        "  <pages>",
        f'    <page id="0" file="{texture_name}"/>',
        "  </pages>",
        f'  <chars count="{len(CODEPOINTS)}">',
    ]
    for codepoint in CODEPOINTS:
        glyph = font.glyphs[font.codepoint_to_gid[codepoint]]
        x, y = positions[codepoint]
        yoffset = baseline - glyph.offset_y - glyph.height
        xadvance = (glyph.advance_16 + 8) >> 4
        lines.append(
            f'    <char id="{codepoint}" x="{x}" y="{y}" width="{glyph.width}" height="{glyph.height}" '
            f'xoffset="{glyph.offset_x}" yoffset="{yoffset}" xadvance="{xadvance}" page="0" chnl="15"/>'
        )
    lines.extend(("  </chars>", f'  <kernings count="{len(kernings)}">'))
    lines.extend(f'    <kerning first="{left}" second="{right}" amount="{amount}"/>' for left, right, amount in kernings)
    lines.extend(("  </kernings>", "</font>", ""))
    return "\n".join(lines).encode("utf-8")


def validate(font: FontData, tga: bytes, fnt: bytes, positions: dict[int, tuple[int, int]], height: int) -> None:
    if len(tga) != 18 + ATLAS_WIDTH * height * 4:
        raise ValueError(f"size {font.size}: invalid TGA length")
    header = struct.unpack("<BBBHHBHHHHBB", tga[:18])
    if header[2] != 2 or header[8:12] != (ATLAS_WIDTH, height, 32, 0x28):
        raise ValueError(f"size {font.size}: invalid uncompressed top-origin 8-bit-alpha TGA header")
    root = ET.fromstring(fnt)
    common = root.find("common")
    page = root.find("./pages/page")
    chars = root.findall("./chars/char")
    if common is None or page is None or len(chars) != len(CODEPOINTS):
        raise ValueError(f"size {font.size}: invalid BMFont XML")
    if int(common.attrib["base"]) != font.line_height - font.baseline_from_bottom:
        raise ValueError(f"size {font.size}: baseline mismatch")

    records = {int(record.attrib["id"]): record for record in chars}
    for codepoint in CODEPOINTS:
        gid = font.codepoint_to_gid[codepoint]
        glyph = font.glyphs[gid]
        record = records[codepoint]
        if int(record.attrib["xadvance"]) != (glyph.advance_16 + 8) >> 4:
            raise ValueError(f"size {font.size}, U+{codepoint:04X}: advance mismatch")
        x, y = positions[codepoint]
        expected = glyph_alpha(font, gid)
        actual = bytearray()
        for row in range(glyph.height):
            start = 18 + ((y + row) * ATLAS_WIDTH + x) * 4 + 3
            actual.extend(tga[start + column * 4] for column in range(glyph.width))
        if bytes(actual) != expected:
            raise ValueError(f"size {font.size}, U+{codepoint:04X}: alpha mask mismatch")
    if any(tga[index] % 17 for index in range(21, len(tga), 4)):
        raise ValueError(f"size {font.size}: atlas contains non-4-bpp alpha")


def readme() -> bytes:
    return (
        "# LVGL Montserrat bitmap fonts\n\n"
        "These files are deterministically generated from LVGL's built-in, uncompressed 4-bpp "
        "Montserrat Medium C fonts. Each single-page, 32-bit, top-origin TGA declares 8 alpha bits "
        "and preserves the original "
        "16 alpha levels; the BMFont XML preserves LVGL's line metrics, offsets, integer advances, "
        "and expanded class kerning for ASCII U+0020-U+007E, degree U+00B0, and bullet U+2022.\n\n"
        "Regenerate from the repository root with `python tools/generate_lvgl_bitmap_fonts.py`; verify "
        "without writing with `python tools/generate_lvgl_bitmap_fonts.py --check`. Font Awesome PUA "
        "glyphs present in the LVGL source are intentionally excluded.\n\n"
        "Montserrat is Copyright 2011 The Montserrat Project Authors and is distributed under the "
        "SIL Open Font License 1.1; see `OFL.txt`. Generated `.fnt` files use the RmlUi "
        "`Samples/basic/bitmap_font` XML schema.\n"
    ).encode("utf-8")


def generated_files(source_dir: Path, license_path: Path) -> tuple[dict[str, bytes], list[str]]:
    outputs: dict[str, bytes] = {}
    summaries = []
    for size in SIZES:
        font = parse_font(source_dir / f"lv_font_montserrat_{size}.c", size)
        positions, height = pack(font)
        stem = f"Montserrat-{size}"
        tga = make_tga(font, positions, height)
        fnt = make_fnt(font, positions, height, f"{stem}.tga")
        validate(font, tga, fnt, positions, height)
        outputs[f"{stem}.tga"] = tga
        outputs[f"{stem}.fnt"] = fnt
        kerning_count = len(ET.fromstring(fnt).findall("./kernings/kerning"))
        summaries.append(f"{size}px: {len(CODEPOINTS)} glyphs, {kerning_count} kernings, {ATLAS_WIDTH}x{height}")
    outputs["README.md"] = readme()
    outputs["OFL.txt"] = (license_path.read_text(encoding="utf-8").rstrip() + "\n").encode("utf-8")
    return outputs, summaries


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=repo.parent / "ps5-radio-lvgl" / "vendor" / "lvgl" / "src" / "font",
        help="directory containing lv_font_montserrat_SIZE.c",
    )
    parser.add_argument("--output-dir", type=Path, default=repo / "ui" / "fonts" / "lvgl-bitmap")
    parser.add_argument("--check", action="store_true", help="validate committed outputs without writing")
    args = parser.parse_args()

    try:
        outputs, summaries = generated_files(args.source_dir.resolve(), repo / "ui" / "fonts" / "lvgl-bitmap" / "OFL.txt")
        if args.check:
            mismatches = [name for name, data in outputs.items() if not (args.output_dir / name).is_file() or (args.output_dir / name).read_bytes() != data]
            if mismatches:
                print("out of date or missing: " + ", ".join(mismatches), file=sys.stderr)
                return 1
        else:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            for name, data in outputs.items():
                path = args.output_dir / name
                if not path.is_file() or path.read_bytes() != data:
                    path.write_bytes(data)
        for summary in summaries:
            print(summary)
        digest = hashlib.sha256(b"".join(outputs[name] for name in sorted(outputs))).hexdigest()
        print(f"validated {len(outputs)} files; combined sha256={digest}")
        return 0
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
