#!/usr/bin/env python3
"""Fail-closed patch adding native CPU RGBA blit to pinned VideoOut Canvas."""
from pathlib import Path
import sys

HEADER_ANCHOR="    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;"
HEADER_NEW=HEADER_ANCHOR+"""
    // Bounded image copy from a pre-decoded thumbnail, never a filesystem path.
    void blit(unsigned x, unsigned y, unsigned width, unsigned height,
              const std::uint32_t *pixels, unsigned source_w, unsigned source_h) noexcept;"""
CPP_ANCHOR="""void read_asset_text(const char *path, std::span<char> destination,"""
CPP_NEW="""void Canvas::blit(unsigned x, unsigned y, unsigned width, unsigned height,
                  const std::uint32_t *source, unsigned source_w, unsigned source_h) noexcept
{
    if (!source || width == 0 || height == 0 || source_w == 0 || source_h == 0 ||
        x >= frame_width || y >= frame_height)
        return;
    const unsigned visible_w = width < frame_width - x ? width : frame_width - x;
    const unsigned visible_h = height < frame_height - y ? height : frame_height - y;
    for (unsigned row = 0; row < visible_h; ++row) {
        const auto source_y = static_cast<unsigned>(
            (static_cast<std::uint64_t>(row) * source_h) / height);
        for (unsigned col = 0; col < visible_w; ++col) {
            const auto source_x = static_cast<unsigned>(
                (static_cast<std::uint64_t>(col) * source_w) / width);
            const auto color = source[static_cast<std::size_t>(source_y) * source_w + source_x];
            put_pixel_unchecked(pixels_, x + col, y + row, static_cast<Color>(color));
        }
    }
}

"""+CPP_ANCHOR

def patch(header: Path, implementation: Path) -> None:
    h=header.read_text(encoding="utf-8")
    c=implementation.read_text(encoding="utf-8")
    # Verify source shape and prevent double-patching.
    if h.count(HEADER_ANCHOR)!=1 or c.count(CPP_ANCHOR)!=1 or (
        "Canvas::blit(" in c or "void blit(" in h
    ):
        raise ValueError("unexpected pinned native Canvas ABI")
    header.write_text(h.replace(HEADER_ANCHOR,HEADER_NEW),encoding="utf-8")
    implementation.write_text(c.replace(CPP_ANCHOR,CPP_NEW),encoding="utf-8")

if __name__=="__main__":
    if len(sys.argv)!=3:raise SystemExit("usage: enable_native_preview_blit.py HEADER CPP")
    try:
        patch(Path(sys.argv[1]),Path(sys.argv[2]))
        print("Native RGBA image blit: enabled")
    except (OSError,ValueError) as exc:raise SystemExit(f"Native preview patch error: {exc}") from exc
