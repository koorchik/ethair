#!/usr/bin/env python3
"""Cuts a product photo out of its white background (transparent PNG).

    tools/cutout.py IN OUT [--crop x0 y0 x1 y1] [--thresh 24] [--width 600]

Flood fill from the corners finds the background; the mask is shrunk past
the light fringe, smoothed and softened, and the image scaled down
premultiplied (no halo on dark pages). Needs Pillow.
"""
import argparse

from PIL import Image, ImageDraw, ImageFilter


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--crop", nargs=4, type=int)
    ap.add_argument("--thresh", type=int, default=24)
    ap.add_argument("--width", type=int, default=600)
    args = ap.parse_args()

    img = Image.open(args.src).convert("RGBA")
    if args.crop:
        img = img.crop(tuple(args.crop))
    img = img.resize((img.width * 2, img.height * 2), Image.LANCZOS)
    rgb = img.convert("RGB")
    marker = (255, 0, 255)
    for corner in ((0, 0), (rgb.width - 1, 0), (0, rgb.height - 1), (rgb.width - 1, rgb.height - 1)):
        ImageDraw.floodfill(rgb, corner, marker, thresh=args.thresh)
    r, g, b = rgb.split()
    bg = Image.merge("RGB", (r.point(lambda v: 255 if v == 255 else 0),
                             g.point(lambda v: 255 if v == 0 else 0),
                             b.point(lambda v: 255 if v == 255 else 0))).convert("L").point(lambda v: 255 if v == 255 else 0)
    mask = bg.point(lambda v: 0 if v else 255)
    mask = mask.filter(ImageFilter.MinFilter(5))
    mask = mask.filter(ImageFilter.GaussianBlur(3)).point(lambda v: 255 if v >= 128 else 0)
    mask = mask.filter(ImageFilter.GaussianBlur(1.2))
    img.putalpha(mask)
    box = mask.getbbox()
    img = img.crop(box)
    size = (args.width, int(args.width * img.height / img.width))
    img = img.convert("RGBa").resize(size, Image.LANCZOS).convert("RGBA")
    img.save(args.out, optimize=True)
    print("saved", args.out, img.size)


if __name__ == "__main__":
    main()
