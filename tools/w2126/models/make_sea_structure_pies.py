#!/usr/bin/env python3
"""Warzone 2126: placeholder models for the sea structures (until the art arrives).

blsonarbuoy.pie  - sonar buoy: a float with a mast and a light, 1 tile
blrigbase.pie    - offshore oil rig platform: a deck on four legs; the oil derrick stands on it
bltorpedo.pie    - torpedo launcher: a raft with a launcher housing; the weapon mounts on top
blcoastbattery.pie - coastal battery: a 2 x 2 casemate with two gun pits (two connectors)
Built from boxes, textured from page-34-buildings.png.
Run:  python3 tools/w2126/models/make_sea_structure_pies.py
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "..", "data", "base", "structs")

# UV regions on page-34-buildings.png (u0, v0, u1, v1)
DECK = (0.66, 0.01, 0.85, 0.20)
WALL = (0.01, 0.01, 0.26, 0.20)
ROOF = (0.34, 0.66, 0.46, 0.76)
STRIPE = (0.62, 0.43, 0.69, 0.46)
PILE = (0.01, 0.43, 0.17, 0.56)


class Model:
    def __init__(self):
        self.points = []
        self.polys = []

    def box(self, x0, y0, z0, x1, y1, z1, side_uv, top_uv=None):
        top_uv = top_uv or side_uv
        base = len(self.points)
        for (x, y, z) in [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
                          (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]:
            self.points.append((x, y, z))

        def quad(a, b, c, d, uv):
            u0, v0, u1, v1 = uv
            for tri, tuv in (((a, b, c), ((u0, v1), (u1, v1), (u1, v0))), ((a, c, d), ((u0, v1), (u1, v0), (u0, v0)))):
                self.polys.append((tuple(base + t for t in tri), tuv))
                self.polys.append((tuple(base + t for t in reversed(tri)), tuple(reversed(tuv))))
        quad(4, 5, 6, 7, top_uv)
        quad(0, 1, 5, 4, side_uv)
        quad(1, 2, 6, 5, side_uv)
        quad(2, 3, 7, 6, side_uv)
        quad(3, 0, 4, 7, side_uv)
        quad(3, 2, 1, 0, side_uv)

    def write(self, name, connectors=()):
        path = os.path.normpath(os.path.join(OUT, name))
        with open(path, "w") as f:
            f.write("PIE 4\nTYPE 200\nTEXTURE 0 page-34-buildings.png\nLEVELS 1\nLEVEL 1\n")
            f.write("POINTS %d\n" % len(self.points))
            for p in self.points:
                f.write("\t%g %g %g\n" % p)
            f.write("POLYGONS %d\n" % len(self.polys))
            for (a, b, c), uv in self.polys:
                f.write("\t200 3 %d %d %d %s\n" % (a, b, c, " ".join("%g %g" % t for t in uv)))
            if connectors:
                f.write("CONNECTORS %d\n" % len(connectors))
                for c in connectors:
                    f.write("\t%d %d %d\n" % c)
        print("wrote", path, len(self.points), "points", len(self.polys), "polygons")


# Sonar buoy: a squat float (partly under the water line), a mast, a light box and a sonar dome below
buoy = Model()
buoy.box(-28, -14, -28, 28, 14, 28, STRIPE, STRIPE)
buoy.box(-20, 14, -20, 20, 20, 20, PILE, DECK)
buoy.box(-4, 20, -4, 4, 78, 4, PILE)
buoy.box(-9, 78, -9, 9, 90, 9, ROOF)
buoy.box(-16, -40, -16, 16, -14, 16, PILE)
buoy.write("blsonarbuoy.pie")

# Torpedo launcher: a raft on floats, a housing, and the launcher on top (connector)
torp = Model()
torp.box(-58, -10, -58, 58, 14, 58, PILE, DECK)
for x in (-48, 48):
    torp.box(x - 10, -24, -56, x + 10, -10, 56, STRIPE)
torp.box(-36, 14, -36, 36, 34, 36, WALL, ROOF)
torp.write("bltorpedo.pie", connectors=[(0, 0, 34)])

# Offshore rig platform: a deck just under the derrick's base, on four legs down into the water
rig = Model()
rig.box(-62, -8, -62, 62, -1, 62, PILE, DECK)
for x in (-54, 54):
    for z in (-54, 54):
        rig.box(x - 7, -70, z - 7, x + 7, -8, z + 7, PILE)
rig.box(-62, -20, -62, 62, -8, -52, STRIPE)
rig.box(-62, -20, 52, 62, -8, 62, STRIPE)
rig.write("blrigbase.pie")

# Coastal battery (2 x 2 tiles): a concrete casemate on a sloped earth berm, two gun pits on the roof
# for its twin heavy guns (two connectors), and an ammunition bunker at the back
bat = Model()
bat.box(-124, -6, -124, 124, 10, 124, PILE, DECK)
bat.box(-108, 10, -100, 108, 46, 92, WALL, ROOF)
bat.box(-100, 46, -60, 100, 54, 60, STRIPE, DECK)
for x in (-52, 52):
    bat.box(x - 34, 54, -34, x + 34, 62, 34, PILE, DECK)
bat.box(-60, 10, 92, 60, 30, 120, WALL, ROOF)
bat.write("blcoastbattery.pie", connectors=[(-52, 0, 62), (52, 0, 62)])
