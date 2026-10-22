#!/usr/bin/env python3
"""Render host-only native Canvas traces into deterministic preview PNGs.

Uses the title's own drawing calls and synthetic fixture data. This is NOT
a PS5 GPU/runtime screenshot and cannot prove firmware compatibility.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 1920, 1080
FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")

def color(raw: str) -> tuple[int, int, int]:
    value = int(raw)
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("invalid 32-bit native color")
    # Native packed ABGR (as in the PS5 CPU renderer).
    return (value & 255, (value >> 8) & 255, (value >> 16) & 255)

def preview(trace: Path, output: Path) -> None:
    frame = Image.new("RGB", (WIDTH, HEIGHT), "#0a0d19")
    draw = ImageDraw.Draw(frame)
    operations = trace.read_text(encoding="utf-8").splitlines()
    if not 1 <= len(operations) <= 1000:
        raise ValueError("unexpected number of native drawing operations")
    fonts: dict[int, ImageFont.FreeTypeFont | ImageFont.ImageFont] = {}
    for row in operations:
        fields = row.split("\t", 7)
        if len(fields) != 8:
            raise ValueError("incomplete native draw command")
        command, x, y, w, h, scale, shade, text = fields
        x, y, w, h, scale = [int(v) for v in (x, y, w, h, scale)]
        if not all(0 <= v <= 10000 for v in (x, y, w, h, scale)):
            raise ValueError("invalid native draw coordinates")
        ink = color(shade)
        if command == "C":
            draw.rectangle((0, 0, WIDTH - 1, HEIGHT - 1), fill=ink)
        elif command == "R":
            draw.rectangle((x, y, x + w, y + h), fill=ink)
        elif command == "O":
            draw.ellipse((x - w, y - w, x + w, y + w), fill=ink)
        elif command == "T":
            draw.polygon([(x, y + h), (x + w, y), (x + w * 2, y + h)], fill=ink)
        elif command == "I":
            draw.rectangle((x,y,x+w,y+h),fill=(35,70,110))
            draw.text((x+28,y+28),"PHOTO SYNTHETIQUE - PAS UNE CAPTURE PS5",fill=(240,240,240))
        elif command == "X":
            if scale not in fonts:
                if FONT.is_file():
                    fonts[scale] = ImageFont.truetype(str(FONT), max(8, scale * 7))
                else:
                    fonts[scale] = ImageFont.load_default()
            draw.text((x, y), text, fill=ink, font=fonts[scale])
        else:
            raise ValueError("unknown native draw command")
    output.parent.mkdir(parents=True, exist_ok=True)
    frame.save(output, optimize=True)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("traces", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    traces = sorted(args.traces.glob("*.trace"))
    if len(traces) != 13:
        raise SystemExit(f"expected thirteen synthetic native screens, got {len(traces)}")
    for path in traces:
        target = args.output / f"{path.stem}.png"
        preview(path, target)
        print(f"Rendered synthetic native screen: {target.name}")
    (args.output / "README.txt").write_text(
        "Host-only native UI preview generated from synthetic fixture data.\n"
        "Not a capture of real PS5 hardware or a compatibility test on 13.60.\n",
        encoding="utf-8"
    )

if __name__ == "__main__":
    main()
