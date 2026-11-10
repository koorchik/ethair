#!/usr/bin/env python3
"""Renders the Cardputer status screens for the README (docs/images/).

Same font (src/fonts/glcdfont.h), line layout and colours as src/display.cpp,
drawn on the computer with sample values, so no board or camera is needed and
no real network name ends up in the docs. Needs Pillow.
"""
import os
import re

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
W, H, LINE_H, SCALE = 240, 135, 19, 3

WHITE, GREY, GREEN, YELLOW, RED, CYAN = 0xFFFF, 0x8410, 0x07E0, 0xFFE0, 0xF800, 0x07FF

SCREENS = {
    "ethair-ready": [
        (CYAN, "EthAir-A1B2"),
        (WHITE, "pass ethair123"),
        (GREY, "cfg http://192.168.73.1"),
        (GREEN, "USB: device OK"),
        (GREEN, "dev 192.168.73.2"),
        (GREEN, "wifi clients 1"),
        (GREY, "Hold G0 5s: factory reset"),
    ],
    "ethair-waiting": [
        (CYAN, "EthAir-A1B2"),
        (WHITE, "pass ethair123"),
        (GREY, "cfg http://192.168.73.1"),
        (YELLOW, "USB: no device"),
        (GREY, "dev -"),
        (GREY, "wifi clients 0"),
        (GREY, "Hold G0 5s: factory reset"),
    ],
    "ethair-no-usb": [
        (CYAN, "EthAir-A1B2"),
        (WHITE, "pass ethair123"),
        (GREY, "cfg http://192.168.73.1"),
        (RED, "USB: no camera data"),
        (RED, "CARDPUTER's own switch OFF? Data cable?"),
        (GREY, "wifi clients 0"),
        (GREY, "Hold G0 5s: factory reset"),
    ],
}


def load_font():
    src = open(os.path.join(ROOT, "src", "fonts", "glcdfont.h")).read()
    body = src[src.index("{", src.index("glcdfont")) + 1:]
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]


def rgb(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def render(lines, font):
    img = Image.new("RGB", (W, H))
    px = img.load()
    for i, (color, text) in enumerate(lines):
        scale = 2 if len(text) * 12 <= W else 1
        y0 = i * LINE_H + (1 if scale == 2 else 5)
        for k, ch in enumerate(text):
            x0 = k * 6 * scale
            for col in range(5):
                bits = font[ord(ch) * 5 + col]
                for row in range(8):
                    if not bits & (1 << row):
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            x, y = x0 + col * scale + dx, y0 + row * scale + dy
                            if 0 <= x < W and 0 <= y < H:
                                px[x, y] = rgb(color)
    big = img.resize((W * SCALE, H * SCALE), Image.NEAREST).convert("RGBA")
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, big.size[0] - 1, big.size[1] - 1], radius=18, fill=255)
    big.putalpha(mask)
    return big


def main():
    font = load_font()
    out = os.path.join(ROOT, "docs", "images")
    os.makedirs(out, exist_ok=True)
    for name, lines in SCREENS.items():
        path = os.path.join(out, f"{name}.png")
        render(lines, font).save(path, optimize=True)
        print("saved", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
