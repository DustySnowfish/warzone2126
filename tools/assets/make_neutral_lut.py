#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Writes a neutral colour-grading LUT image for Warzone 2126.

The image is a strip of N squares of N x N pixels (N*N wide, N high): red increases
across each square, green increases downwards, and blue picks the square. Grade a
screenshot in an image editor, apply the same adjustments to this image, save it as
PNG and reference it from a grade file ("lut": "colourgrade/luts/NAME.png").

Usage: make_neutral_lut.py OUT.png [N]   (N defaults to 32). Requires Pillow.
"""

import sys

from PIL import Image


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 32
    img = Image.new('RGB', (n * n, n))
    px = img.load()
    for b in range(n):
        for g in range(n):
            for r in range(n):
                px[b * n + r, g] = tuple(round(v * 255 / (n - 1)) for v in (r, g, b))
    img.save(sys.argv[1])
    print('Wrote %s (%d x %d)' % (sys.argv[1], n * n, n))


if __name__ == '__main__':
    main()
