#!/usr/bin/env python3
"""Warzone 2126: placeholder Shipyard model (data/base/structs/blshipyard.pie).

A 4 x 3 tile dock: a deck on piles, a hall on the land side, a slipway marked with hazard stripes and
a crane. Built from boxes, textured from page-34-buildings.png. Replace with the art-spec model.
Run:  python3 tools/w2126/models/make_shipyard_pie.py
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "..", "data", "base", "structs", "blshipyard.pie")

# UV regions on page-34-buildings.png (u0, v0, u1, v1)
DECK = (0.66, 0.01, 0.85, 0.20)
WALL = (0.01, 0.01, 0.26, 0.20)
ROOF = (0.34, 0.66, 0.46, 0.76)
STRIPE = (0.62, 0.43, 0.69, 0.46)
CRANE = (0.26, 0.32, 0.33, 0.58)
PILE = (0.01, 0.43, 0.17, 0.56)

points = []
polys = []


def box(x0, y0, z0, x1, y1, z1, side_uv, top_uv=None, bottom=False):
    top_uv = top_uv or side_uv
    base = len(points)
    for (x, y, z) in [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
                      (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]:
        points.append((x, y, z))
    def quad(a, b, c, d, uv):
        u0, v0, u1, v1 = uv
        # two triangles, both windings so the placeholder is visible from every side
        for tri, tuv in (((a, b, c), ((u0, v1), (u1, v1), (u1, v0))), ((a, c, d), ((u0, v1), (u1, v0), (u0, v0)))):
            polys.append((tuple(base + t for t in tri), tuv))
            polys.append((tuple(base + t for t in reversed(tri)), tuple(reversed(tuv))))
    quad(4, 5, 6, 7, top_uv)          # top
    quad(0, 1, 5, 4, side_uv)         # sides
    quad(1, 2, 6, 5, side_uv)
    quad(2, 3, 7, 6, side_uv)
    quad(3, 0, 4, 7, side_uv)
    if bottom:
        quad(3, 2, 1, 0, side_uv)


# Deck over the whole 4 x 3 footprint (x across, z along; the land side is -z)
box(-250, 0, -186, 250, 22, 186, PILE, DECK, bottom=True)
# Piles under the water side
for x in (-220, -80, 80, 220):
    for z in (40, 170):
        box(x - 10, -70, z - 10, x + 10, 0, z + 10, PILE)
# Hall on the land row
box(-240, 22, -184, 120, 140, -70, WALL, ROOF)
# Slipway: hazard stripes along both edges of the launch lane
box(-150, 22, -60, -130, 25, 184, STRIPE)
box(130, 22, -60, 150, 25, 184, STRIPE)
# Crane: tower beside the hall and an arm out over the water
box(160, 22, -170, 200, 230, -130, CRANE)
box(170, 210, -170, 190, 228, 170, CRANE)
box(165, 140, 160, 195, 210, 175, CRANE)

with open(OUT, "w") as f:
    f.write("PIE 4\nTYPE 200\nTEXTURE 0 page-34-buildings.png\nLEVELS 1\nLEVEL 1\n")
    f.write("POINTS %d\n" % len(points))
    for p in points:
        f.write("\t%g %g %g\n" % p)
    f.write("POLYGONS %d\n" % len(polys))
    for (a, b, c), uv in polys:
        f.write("\t200 3 %d %d %d %s\n" % (a, b, c, " ".join("%g %g" % t for t in uv)))
print("wrote", os.path.normpath(OUT), len(points), "points", len(polys), "polygons")
