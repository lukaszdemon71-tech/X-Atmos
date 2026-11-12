#!/usr/bin/env python3
"""Build an OBJ8 pushback tug for X-Plane 12.

X-Plane's object format is plain text: a vertex table, an index table, then
draw commands that take slices of it. Everything here is written directly —
no modelling package in the loop.

Axes, X-Plane's way round: +X right, +Y up, -Z forward. The origin sits on
the ground at the middle of the vehicle, because that is where X-Plane puts
it down and where it pivots.

    python3 build_tug.py            -> out/pushback_tug/

Output: the object, its three textures and a library.txt that puts the tug in
place of the stock one.
"""

import math
import pathlib

from PIL import Image, ImageDraw

OUT = pathlib.Path(__file__).resolve().parent / "out" / "pushback_tug"
TEX = 512  # texture is square; UVs below are in pixels and divided at write

# ── the texture atlas, in pixels ─────────────────────────────────────
# Flat colour patches. A tug is painted in two or three colours and read from
# twenty metres away, so an atlas of plain regions is honest here: the detail
# that matters is the silhouette and the lights, not the paint.
PATCH = {
    "body":    (8, 8, 248, 120),      # the yellow hull
    "dark":    (8, 128, 248, 200),    # chassis, bumper, mudguards
    "glass":   (8, 208, 248, 280),    # cab glazing
    "tyre":    (8, 288, 120, 400),    # tyre tread
    "rim":     (128, 288, 240, 400),  # wheel face
    "beacon":  (264, 8, 376, 60),     # amber lamp
    "lamp":    (264, 68, 376, 120),   # white work light
    "grille":  (264, 128, 504, 200),  # radiator grille, warning stripes
    "plate":   (264, 208, 504, 280),  # flat metal
    "pad":     (264, 288, 504, 400),  # rubber push pad
}

COLOUR = {
    "body":   (214, 158, 34),
    "dark":   (44, 47, 52),
    "glass":  (36, 46, 58),
    "tyre":   (26, 26, 28),
    "rim":    (118, 122, 128),
    "beacon": (226, 128, 24),
    "lamp":   (236, 238, 240),
    "grille": (58, 61, 66),
    "plate":  (132, 136, 142),
    "pad":    (34, 34, 36),
}

# Which patches glow at night, and how brightly.
LIT = {"beacon": (255, 150, 30), "lamp": (255, 250, 235), "glass": (40, 52, 30)}


def make_textures():
    OUT.mkdir(parents=True, exist_ok=True)

    alb = Image.new("RGB", (TEX, TEX), (18, 18, 20))
    draw = ImageDraw.Draw(alb)
    for name, box in PATCH.items():
        draw.rectangle(box, fill=COLOUR[name])

    # A little grain so large flat areas do not read as plastic under the
    # sim's specular pass.
    import random
    random.seed(7)
    px = alb.load()
    for name, (x0, y0, x1, y1) in PATCH.items():
        for _ in range((x1 - x0) * (y1 - y0) // 7):
            x = random.randrange(x0, x1)
            y = random.randrange(y0, y1)
            r, g, b = px[x, y]
            d = random.randint(-9, 9)
            px[x, y] = (max(0, min(255, r + d)), max(0, min(255, g + d)), max(0, min(255, b + d)))

    # Hazard stripes, the one piece of real marking. Drawn on their own tile
    # and pasted in: a skewed polygon drawn straight onto the atlas runs past
    # the patch it belongs to and paints the neighbour sharing that row.
    gx0, gy0, gx1, gy1 = PATCH["grille"]
    gw, gh = gx1 - gx0, gy1 - gy0
    tile = Image.new("RGB", (gw, gh), COLOUR["grille"])
    tdraw = ImageDraw.Draw(tile)
    for i in range(-(gh // 28) - 1, gw // 28 + 2):
        x = i * 28
        tdraw.polygon([(x, gh), (x + 14, gh), (x + 14 + gh, 0), (x + gh, 0)], fill=(206, 150, 30))
    alb.paste(tile, (gx0, gy0))
    alb.save(OUT / "pushback_tug_ALB.png")

    # Night texture: only the lamps and the cab glow.
    lit = Image.new("RGB", (TEX, TEX), (0, 0, 0))
    ldraw = ImageDraw.Draw(lit)
    for name, colour in LIT.items():
        ldraw.rectangle(PATCH[name], fill=colour)
    lit.save(OUT / "pushback_tug_LIT.png")

    # Flat normal map. Real panel lines would need a sculpt; a neutral map
    # keeps the shading correct rather than faking relief that is not there.
    Image.new("RGB", (TEX, TEX), (128, 128, 255)).save(OUT / "pushback_tug_NML.png")


# ── geometry ─────────────────────────────────────────────────────────
VERTS = []   # each: x, y, z, nx, ny, nz, u, v
IDX = []


def uv(patch, s, t):
    """A point inside a patch, s and t running 0..1 across it."""
    x0, y0, x1, y1 = PATCH[patch]
    # Inset by a pixel so neighbouring patches cannot bleed in under
    # mipmapping — the usual cause of a stray colour along an edge.
    x0, y0, x1, y1 = x0 + 1, y0 + 1, x1 - 1, y1 - 1
    u = (x0 + s * (x1 - x0)) / TEX
    v = 1.0 - (y0 + t * (y1 - y0)) / TEX   # OBJ8 counts v up from the bottom
    return u, v


def quad(p0, p1, p2, p3, patch, normal=None, flip=False):
    """One flat four-sided face, wound counter-clockwise seen from outside."""
    if normal is None:
        ax = [p1[i] - p0[i] for i in range(3)]
        bx = [p3[i] - p0[i] for i in range(3)]
        normal = (ax[1] * bx[2] - ax[2] * bx[1],
                  ax[2] * bx[0] - ax[0] * bx[2],
                  ax[0] * bx[1] - ax[1] * bx[0])
        length = math.sqrt(sum(c * c for c in normal)) or 1.0
        normal = tuple(c / length for c in normal)
    if flip:
        normal = tuple(-c for c in normal)

    base = len(VERTS)
    corners = [(p0, (0, 1)), (p1, (1, 1)), (p2, (1, 0)), (p3, (0, 0))]
    for point, (s, t) in corners:
        u, v = uv(patch, s, t)
        VERTS.append((*point, *normal, u, v))
    # X-Plane draws triangles; two of them make the quad.
    order = [0, 2, 1, 0, 3, 2] if flip else [0, 1, 2, 0, 2, 3]
    IDX.extend(base + i for i in order)


def box(cx, cy, cz, sx, sy, sz, patch, top=None, front=None):
    """An axis-aligned box. Separate patches for the top and the front face
    where the paint differs, which is most of the time on a vehicle."""
    x0, x1 = cx - sx / 2, cx + sx / 2
    y0, y1 = cy - sy / 2, cy + sy / 2
    z0, z1 = cz - sz / 2, cz + sz / 2
    t, f = top or patch, front or patch

    quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), patch)          # +Z back
    quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), f)              # -Z front
    quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), patch)          # +X
    quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), patch)          # -X
    quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), t)              # +Y top
    quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), "dark")         # -Y floor


def wedge(cx, y0, y1, z0, z1, half, drop, patch, glass=False):
    """The sloped front of the cab: a box whose top face leans forward."""
    x0, x1 = cx - half, cx + half
    yf = y1 - drop
    g = "glass" if glass else patch
    # X-Plane culls back faces, so each of these has to be wound anti-clockwise
    # seen from outside the cab. Wound the other way the panel is not merely
    # dark — it is not drawn at all, and you see straight through the cab.
    quad((x1, y0, z0), (x0, y0, z0), (x0, yf, z0), (x1, yf, z0), g)        # windscreen, -Z
    quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), patch)   # back wall, +Z
    quad((x1, yf, z0), (x0, yf, z0), (x0, y1, z1), (x1, y1, z1), patch, flip=True)  # roof slope
    quad((x1, y0, z1), (x1, y0, z0), (x1, yf, z0), (x1, y1, z1), g)       # +X side
    quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, yf, z0), g)       # -X side


def cylinder(cx, cy, cz, radius, width, segments, side, face, axis="x"):
    """A wheel: a tube with a disc at each end."""
    ring = [(math.cos(2 * math.pi * i / segments), math.sin(2 * math.pi * i / segments))
            for i in range(segments)]
    half = width / 2

    for i in range(segments):
        c0, s0 = ring[i]
        c1, s1 = ring[(i + 1) % segments]
        if axis == "x":
            a = (cx - half, cy + radius * s0, cz + radius * c0)
            b = (cx - half, cy + radius * s1, cz + radius * c1)
            c = (cx + half, cy + radius * s1, cz + radius * c1)
            d = (cx + half, cy + radius * s0, cz + radius * c0)
        n0 = (0.0, s0, c0)
        quad(a, b, c, d, side, normal=n0)

    for sign in (-1, 1):
        x = cx + sign * half
        base = len(VERTS)
        u, v = uv(face, 0.5, 0.5)
        VERTS.append((x, cy, cz, sign, 0.0, 0.0, u, v))
        for i in range(segments):
            c, s = ring[i]
            uu, vv = uv(face, 0.5 + 0.45 * c, 0.5 + 0.45 * s)
            VERTS.append((x, cy + radius * s, cz + radius * c, sign, 0.0, 0.0, uu, vv))
        for i in range(segments):
            a = base + 1 + i
            b = base + 1 + (i + 1) % segments
            IDX.extend([base, b, a] if sign < 0 else [base, a, b])


def mark():
    """Where the next part starts, in triangles."""
    return len(IDX)


# ── the vehicle ──────────────────────────────────────────────────────
# Roughly the size of a conventional towbarless tug: 4.4 m long, 2.3 m wide,
# 2.1 m to the top of the beacon.
WHEEL_R = 0.44
AXLE = WHEEL_R                     # axle height above the ground
WHEEL_X = 0.94                     # track centre; the hull is narrower, so
WHEEL_W = 0.34                     # the wheels stand proud of the bodywork
WHEEL_Z = (-1.28, 1.26)
HALF = 0.78                        # half the body width

parts = {}

start = mark()
# Chassis rail between the axles, and a bumper at each end.
box(0.0, 0.30, 0.0, 2 * HALF + 0.08, 0.26, 4.00, "dark")
box(0.0, 0.42, -2.08, 2.12, 0.30, 0.20, "grille")     # front bumper, striped
box(0.0, 0.42, 2.08, 2.12, 0.30, 0.20, "grille")      # rear bumper
# The push pad that meets the nose gear.
box(0.0, 0.60, -2.22, 1.24, 0.46, 0.14, "pad")

# Bonnet: deliberately low and short, because the driver has to see the nose
# wheel over it. Two steps rather than one slab, so it is not a crate.
box(0.0, 0.62, -1.42, 2 * HALF, 0.38, 1.26, "body", top="body")
box(0.0, 0.80, -0.78, 2 * HALF, 0.30, 0.68, "body", top="plate")
# Side skirts down to the chassis, to close the gap under the bonnet.
for sx in (-1, 1):
    box(sx * (HALF - 0.03), 0.46, -1.10, 0.06, 0.30, 2.00, "dark")

# Engine deck behind the cab, lower than the cab roof.
box(0.0, 0.82, 1.52, 2 * HALF, 0.74, 1.08, "body", top="plate")
box(0.0, 1.21, 1.52, 2 * HALF - 0.10, 0.04, 0.96, "plate")     # deck hatch
parts["hull"] = (start, mark() - start)

start = mark()
# Cab: glazed on three sides, raked windscreen, sat between the axles.
wedge(0.0, 0.92, 1.96, -0.44, 0.96, HALF - 0.02, 0.30, "body", glass=True)
# Pillars, so the glazing is not one unbroken sheet.
for sx in (-1, 1):
    box(sx * (HALF - 0.04), 1.44, -0.42, 0.10, 1.00, 0.10, "body")
    box(sx * (HALF - 0.04), 1.44, 0.94, 0.10, 1.04, 0.10, "body")
box(0.0, 1.99, 0.26, 2 * HALF + 0.10, 0.09, 1.56, "plate")     # roof, overhanging
box(0.0, 1.22, -0.47, 0.08, 0.40, 0.05, "dark")                # wiper arm
box(-0.58, 0.88, -0.50, 0.30, 0.05, 0.26, "dark")              # step into the cab
parts["cab"] = (start, mark() - start)

start = mark()
# Mudguards: an arch over each wheel rather than a flat slab.
for zc in WHEEL_Z:
    for sx in (-1, 1):
        x = sx * WHEEL_X
        box(x, 0.94, zc, WHEEL_W + 0.10, 0.09, 1.16, "dark")            # crown
        box(x, 0.74, zc - 0.56, WHEEL_W + 0.10, 0.32, 0.07, "dark")     # front lip
        box(x, 0.74, zc + 0.56, WHEEL_W + 0.10, 0.32, 0.07, "dark")     # rear lip
parts["guards"] = (start, mark() - start)

start = mark()
# Work lights, beacon mast, head and tail lights.
for sx in (-1, 1):
    box(sx * 0.52, 2.09, -0.42, 0.24, 0.13, 0.11, "lamp")      # roof work lights
    box(sx * 0.78, 0.66, -2.14, 0.26, 0.18, 0.07, "lamp")      # headlights
    box(sx * 0.70, 0.70, 2.15, 0.22, 0.14, 0.06, "beacon")     # tail lights
box(0.0, 2.11, 0.62, 0.07, 0.14, 0.07, "dark")                 # beacon mast
box(0.0, 2.27, 0.62, 0.21, 0.18, 0.21, "beacon")               # rotating lamp
parts["lights"] = (start, mark() - start)

wheels = {}
for name, (sx, zc) in {
    "fl": (-1, WHEEL_Z[0]), "fr": (1, WHEEL_Z[0]),
    "rl": (-1, WHEEL_Z[1]), "rr": (1, WHEEL_Z[1]),
}.items():
    start = mark()
    cylinder(sx * WHEEL_X, AXLE, zc, WHEEL_R, WHEEL_W, 16, "tyre", "rim")
    wheels[name] = (start, mark() - start, sx, zc)


# ── write the object ─────────────────────────────────────────────────
def write_obj():
    L = [
        "I",
        "800",
        "OBJ",
        "",
        "# Pushback tug — generated by build_tug.py, not hand-edited.",
        "# X-Plane 12 ground vehicle. Origin on the ground at the centre.",
        "",
        "TEXTURE pushback_tug_ALB.png",
        "TEXTURE_LIT pushback_tug_LIT.png",
        "TEXTURE_NORMAL pushback_tug_NML.png",
        "GLOBAL_specular 0.22",
        "",
        f"POINT_COUNTS {len(VERTS)} 0 0 {len(IDX)}",
        "",
    ]
    for v in VERTS:
        L.append("VT " + " ".join(f"{c:.5f}" for c in v))
    L.append("")
    for i in range(0, len(IDX), 10):
        L.append("IDX10 " + " ".join(str(n) for n in IDX[i:i + 10])
                 if len(IDX[i:i + 10]) == 10
                 else "\n".join(f"IDX {n}" for n in IDX[i:i + 10]))
    L.append("")

    L += ["ATTR_no_blend 0.5", "ATTR_shadow"]
    for name in ("hull", "cab", "guards"):
        offset, count = parts[name]
        L.append(f"TRIS {offset} {count}")

    # The lamps are emissive at night; light_level drives the LIT texture.
    L += ["", "ATTR_light_level 0.0 1.0 sim/graphics/scenery/percent_lights_on"]
    offset, count = parts["lights"]
    L.append(f"TRIS {offset} {count}")
    L.append("ATTR_light_level_reset")

    # Wheels turn with the vehicle's ground speed. X-Plane drives scenery
    # vehicles along their route and exposes the roll in this dataref, so the
    # wheels follow the actual movement rather than spinning on a timer.
    L.append("")
    for name, (offset, count, sx, zc) in wheels.items():
        L += [
            "ANIM_begin",
            f"ANIM_trans {sx * WHEEL_X:.4f} {AXLE:.4f} {zc:.4f} "
            f"{sx * WHEEL_X:.4f} {AXLE:.4f} {zc:.4f} 0 0 none",
            "ANIM_rotate -1.0 0.0 0.0 0.0 360.0 0.0 1.0 "
            "sim/graphics/animation/ground_traffic/tire_steer_deg",
            f"ANIM_trans {-sx * WHEEL_X:.4f} {-AXLE:.4f} {-zc:.4f} "
            f"{-sx * WHEEL_X:.4f} {-AXLE:.4f} {-zc:.4f} 0 0 none",
            f"TRIS {offset} {count}",
            "ANIM_end",
        ]

    # Named lights X-Plane draws itself: they flare and fall off with
    # distance, which painted pixels cannot do.
    L += [
        "",
        "LIGHT_NAMED airplane_beacon 0.00 2.37 0.30",
        "LIGHT_NAMED airplane_landing_size -0.82 0.70 -2.24",
        "LIGHT_NAMED airplane_landing_size 0.82 0.70 -2.24",
        "LIGHT_NAMED airplane_nav_tail_size 0.00 1.10 2.22",
        "",
    ]
    (OUT / "pushback_tug.obj").write_text("\n".join(L))
    return len(VERTS), len(IDX) // 3


def write_library():
    (OUT / "library.txt").write_text(
        "A\n"
        "800\n"
        "LIBRARY\n"
        "\n"
        "# Drop this folder into X-Plane 12/Custom Scenery/.\n"
        "# EXPORT_BACKUP replaces the stock tug only where no other pack has\n"
        "# claimed it, so nothing is overwritten and removing the folder puts\n"
        "# the default back.\n"
        "\n"
        "EXPORT_BACKUP lib/airport/vehicles/pushback/tug.obj  pushback_tug.obj\n"
        "EXPORT        AtmoForge/vehicles/pushback_tug.obj    pushback_tug.obj\n"
    )


if __name__ == "__main__":
    make_textures()
    verts, tris = write_obj()
    write_library()
    print(f"{tris} triangles, {verts} vertices -> {OUT}")
