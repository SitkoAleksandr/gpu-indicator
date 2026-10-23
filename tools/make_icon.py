"""Генерирует res/app.ico: зелёная плашка с тремя столбиками.

Запуск: python3 tools/make_icon.py
"""

import math
import struct
from pathlib import Path

SIZES = (16, 24, 32, 48, 64)
BACKGROUND = (46, 160, 67)
BAR = (255, 255, 255)


def coverage_rounded_rect(px, py, x0, y0, x1, y1, radius):
    dx = max(x0 + radius - px, 0.0, px - (x1 - radius))
    dy = max(y0 + radius - py, 0.0, py - (y1 - radius))
    if px < x0 - 0.5 or px > x1 + 0.5 or py < y0 - 0.5 or py > y1 + 0.5:
        return 0.0
    edge = min(px - x0, x1 - px, py - y0, y1 - py) + 0.5
    corner = radius - math.hypot(dx, dy) + 0.5
    return max(0.0, min(1.0, edge, corner if dx or dy else 1.0))


def render(size):
    pixels = []
    radius = size * 0.22
    bar_width = size * 0.18
    gap = size * 0.08
    left = (size - 3 * bar_width - 2 * gap) / 2
    bottom = size * 0.80
    heights = (0.30, 0.48, 0.62)
    for y in range(size):
        row = []
        for x in range(size):
            px, py = x + 0.5, y + 0.5
            bg = coverage_rounded_rect(px, py, 0, 0, size, size, radius)
            bar = 0.0
            for i, h in enumerate(heights):
                x0 = left + i * (bar_width + gap)
                bar = max(bar, coverage_rounded_rect(px, py, x0, bottom - h * size, x0 + bar_width, bottom, 0.0))
            color = tuple(round(b + (f - b) * bar) for b, f in zip(BACKGROUND, BAR))
            row.append((*color, round(max(bg, bar) * 255)))
        pixels.append(row)
    return pixels


def to_dib(size, pixels):
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    color = bytearray()
    for row in reversed(pixels):  # DIB хранится снизу вверх
        for r, g, b, a in row:
            color += bytes((b, g, r, a))
    mask_stride = ((size + 31) // 32) * 4
    mask = bytes(mask_stride * size)
    return header + bytes(color) + mask


def main():
    images = [to_dib(size, render(size)) for size in SIZES]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in zip(SIZES, images):
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for data in images:
        out += data
    path = Path(__file__).resolve().parent.parent / "res" / "app.ico"
    path.write_bytes(out)
    print(f"{path}: {len(out)} bytes")


if __name__ == "__main__":
    main()
