#!/usr/bin/env python3
"""Export the LVGL app's multilingual glyphs into paged RmlUi bitmap faces."""

from __future__ import annotations

import argparse
import hashlib
import shlex
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


PAGE_SIZE = 2048
PADDING = 1
BASE_GLYPHS = set(range(0x20, 0x7F)) | {0x00B0, 0x2022}
SIZES = (20, 24, 28, 32)


@dataclass(frozen=True)
class Glyph:
    codepoint: int
    advance: int
    width: int
    height: int
    offset_x: int
    offset_y: int
    alpha: bytes


@dataclass(frozen=True)
class ExportedFont:
    size: int
    line_height: int
    baseline_from_bottom: int
    glyphs: tuple[Glyph, ...]


def wsl_path(path: Path) -> str:
    completed = subprocess.run(
        ["wsl", "--exec", "wslpath", "-a", str(path)], check=True,
        stdout=subprocess.PIPE, text=True,
    )
    return completed.stdout.strip()


def export_from_lvgl(repo: Path, lvgl_repo: Path, output: Path) -> None:
    exporter = wsl_path(repo / "tools" / "export_lvgl_radio_fonts.c")
    destination = wsl_path(output)
    lvgl = wsl_path(lvgl_repo)
    quoted_exporter = shlex.quote(exporter)
    quoted_destination = shlex.quote(destination)
    subprocess.run([
        "wsl", "bash", "-lc",
        f"set -eu; cd {shlex.quote(lvgl)}; make -f tools/preview.mk preview >/dev/null",
    ], check=True)
    object_paths = []
    for path in sorted((lvgl_repo / "build" / "preview").rglob("*.o")):
        if "tools" in path.parts or path.name in {"radio_ui.o", "generated_icons.o"}:
            continue
        relative = path.relative_to(lvgl_repo).as_posix()
        object_paths.append(shlex.quote(f"{lvgl}/{relative}"))
    if not object_paths:
        raise ValueError("build the sibling LVGL preview before exporting fonts")
    objects = " ".join(object_paths)
    command = f"""
set -eu
cd {shlex.quote(lvgl)}
cc -DLV_CONF_INCLUDE_SIMPLE -Iconfig -Ivendor/lvgl -Iinclude -std=gnu11 -O2 \
  -c {quoted_exporter} -o /tmp/psradio-font-exporter.o
cc -o /tmp/psradio-font-exporter /tmp/psradio-font-exporter.o {objects} -lm
/tmp/psradio-font-exporter {quoted_destination}
"""
    subprocess.run(["wsl", "bash", "-lc", command], check=True)


def read_export(path: Path) -> ExportedFont:
    data = memoryview(path.read_bytes())
    if len(data) < 20 or data[:4].tobytes() != b"RBF1":
        raise ValueError(f"{path}: invalid exporter header")
    size, line_height, baseline_from_bottom, count = struct.unpack_from("<4I", data, 4)
    cursor = 20
    glyphs: list[Glyph] = []
    for _ in range(count):
        if cursor + 28 > len(data):
            raise ValueError(f"{path}: truncated glyph record")
        codepoint, advance, width, height, offset_x, offset_y, length = \
            struct.unpack_from("<IiiiiiI", data, cursor)
        cursor += 28
        if length != width * height or cursor + length > len(data):
            raise ValueError(f"{path}: invalid U+{codepoint:04X} bitmap")
        alpha = data[cursor : cursor + length].tobytes()
        cursor += length
        if codepoint not in BASE_GLYPHS:
            glyphs.append(Glyph(codepoint, advance, width, height,
                                offset_x, offset_y, alpha))
    if cursor != len(data):
        raise ValueError(f"{path}: trailing exporter data")
    return ExportedFont(size, line_height, baseline_from_bottom, tuple(glyphs))


def pack(font: ExportedFont) -> tuple[dict[int, tuple[int, int, int]], int]:
    positions: dict[int, tuple[int, int, int]] = {}
    page = x = y = row_height = 0
    for glyph in font.glyphs:
        if glyph.width == 0 or glyph.height == 0:
            positions[glyph.codepoint] = (0, 0, 0)
            continue
        if glyph.width + PADDING * 2 > PAGE_SIZE or glyph.height + PADDING * 2 > PAGE_SIZE:
            raise ValueError(f"U+{glyph.codepoint:04X} exceeds atlas page")
        if x + glyph.width + PADDING > PAGE_SIZE:
            x = 0
            y += row_height + PADDING * 2
            row_height = 0
        if y + glyph.height + PADDING > PAGE_SIZE:
            page += 1
            x = y = row_height = 0
        positions[glyph.codepoint] = (page, x + PADDING, y + PADDING)
        x += glyph.width + PADDING * 2
        row_height = max(row_height, glyph.height)
    return positions, page + 1


def encode_alpha(alpha: bytes) -> bytes:
    output = bytearray()
    cursor = 0
    while cursor < len(alpha):
        if alpha[cursor] == 0:
            length = 1
            while cursor + length < len(alpha) and alpha[cursor + length] == 0 and length < 128:
                length += 1
            output.append(length - 1)
        else:
            length = 1
            while cursor + length < len(alpha) and alpha[cursor + length] != 0 and length < 128:
                length += 1
            output.append(0x80 | (length - 1))
            for index in range(0, length, 2):
                first = alpha[cursor + index]
                second = alpha[cursor + index + 1] if index + 1 < length else 0
                if first % 17 or second % 17:
                    raise ValueError("glyph alpha is not LVGL 4-bit coverage")
                output.append((first // 17) << 4 | (second // 17))
        cursor += length
    return bytes(output)


def decode_alpha(atlas: bytes) -> bytes:
    if len(atlas) < 16 or atlas[:4] != b"RTA1":
        raise ValueError("invalid radio atlas header")
    width, height, pixel_count, payload_length = struct.unpack_from("<HHII", atlas, 4)
    payload = memoryview(atlas)[16:]
    if width * height != pixel_count or len(payload) != payload_length:
        raise ValueError("invalid radio atlas dimensions")
    output = bytearray()
    cursor = 0
    while cursor < len(payload) and len(output) < pixel_count:
        token = payload[cursor]
        cursor += 1
        length = (token & 0x7F) + 1
        if token & 0x80:
            byte_count = (length + 1) // 2
            if cursor + byte_count > len(payload):
                raise ValueError("truncated radio atlas literal")
            for index in range(length):
                packed = payload[cursor + index // 2]
                output.append(((packed >> 4) if index % 2 == 0 else (packed & 0x0F)) * 17)
            cursor += byte_count
        else:
            output.extend(bytes(length))
        if len(output) > pixel_count:
            raise ValueError("radio atlas output overflow")
    if cursor != len(payload) or len(output) != pixel_count:
        raise ValueError("radio atlas output mismatch")
    return bytes(output)


def make_pages(font: ExportedFont, positions: dict[int, tuple[int, int, int]],
               page_count: int) -> dict[str, bytes]:
    pages = [bytearray(PAGE_SIZE * PAGE_SIZE) for _ in range(page_count)]
    for glyph in font.glyphs:
        page, x, y = positions[glyph.codepoint]
        for row in range(glyph.height):
            source = row * glyph.width
            destination = (y + row) * PAGE_SIZE + x
            pages[page][destination : destination + glyph.width] = \
                glyph.alpha[source : source + glyph.width]
    result = {}
    for index, alpha in enumerate(pages):
        payload = encode_alpha(alpha)
        result[f"Radio-{font.size}-{index}.rta"] = \
            b"RTA1" + struct.pack("<HHII", PAGE_SIZE, PAGE_SIZE, len(alpha), len(payload)) + payload
    return result


def make_fnt(font: ExportedFont, positions: dict[int, tuple[int, int, int]],
             page_count: int) -> bytes:
    baseline = font.line_height - font.baseline_from_bottom
    lines = [
        '<?xml version="1.0"?>', '<font>',
        f'  <info face="Montserrat" size="{font.size}" bold="0" italic="0" charset="" unicode="1" stretchH="100" smooth="1" aa="1" padding="0,0,0,0" spacing="0,0" outline="0"/>',
        f'  <common lineHeight="{font.line_height}" base="{baseline}" scaleW="{PAGE_SIZE}" scaleH="{PAGE_SIZE}" pages="{page_count}" packed="0" alphaChnl="0" redChnl="4" greenChnl="4" blueChnl="4"/>',
        '  <pages>',
    ]
    lines.extend(f'    <page id="{page}" file="Radio-{font.size}-{page}.rta"/>'
                 for page in range(page_count))
    lines.extend(('  </pages>', f'  <chars count="{len(font.glyphs)}">'))
    for glyph in font.glyphs:
        page, x, y = positions[glyph.codepoint]
        yoffset = baseline - glyph.offset_y - glyph.height
        lines.append(
            f'    <char id="{glyph.codepoint}" x="{x}" y="{y}" width="{glyph.width}" height="{glyph.height}" '
            f'xoffset="{glyph.offset_x}" yoffset="{yoffset}" xadvance="{glyph.advance}" page="{page}" chnl="15"/>'
        )
    lines.extend(('  </chars>', '  <kernings count="0">', '  </kernings>', '</font>', ''))
    return "\n".join(lines).encode("utf-8")


def generate(repo: Path, lvgl_repo: Path) -> dict[str, bytes]:
    with tempfile.TemporaryDirectory(prefix="psradio-fonts-") as temporary:
        exported = Path(temporary)
        export_from_lvgl(repo, lvgl_repo, exported)
        outputs: dict[str, bytes] = {}
        for size in SIZES:
            font = read_export(exported / f"radio-font-{size}.rbf")
            if font.size != size or len(font.glyphs) < 17000:
                raise ValueError(f"{size}px: incomplete multilingual export")
            positions, page_count = pack(font)
            outputs[f"Radio-{size}.fnt"] = make_fnt(font, positions, page_count)
            outputs.update(make_pages(font, positions, page_count))
            print(f"{size}px: {len(font.glyphs)} extended glyphs, {page_count} pages")
        return outputs


def validate(outputs: dict[str, bytes]) -> None:
    for size in SIZES:
        root = ET.fromstring(outputs[f"Radio-{size}.fnt"])
        pages = root.findall("./pages/page")
        glyphs = root.findall("./chars/char")
        if len(glyphs) < 17000 or not pages:
            raise ValueError(f"{size}px: incomplete BMFont metadata")
        for page in pages:
            payload = outputs[page.attrib["file"]]
            decoded = decode_alpha(payload)
            if len(decoded) != PAGE_SIZE * PAGE_SIZE:
                raise ValueError(f"{page.attrib['file']}: invalid atlas")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--lvgl-repo", type=Path)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    lvgl_repo = (args.lvgl_repo or repo.parent / "ps5-radio-lvgl").resolve()
    destination = repo / "ui" / "fonts" / "lvgl-bitmap" / "multilingual"
    outputs = generate(repo, lvgl_repo)
    validate(outputs)

    if args.check:
        for name, content in outputs.items():
            path = destination / name
            if not path.is_file() or path.read_bytes() != content:
                raise SystemExit(f"generated font mismatch: {path}")
    else:
        destination.mkdir(parents=True, exist_ok=True)
        for old in destination.glob("Radio-*"):
            old.unlink()
        for name, content in outputs.items():
            (destination / name).write_bytes(content)

    digest = hashlib.sha256()
    for name in sorted(outputs):
        digest.update(name.encode("utf-8") + b"\0" + outputs[name])
    print(f"validated {len(outputs)} files; combined sha256={digest.hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
