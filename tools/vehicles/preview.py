#!/usr/bin/env python3
"""Render an OBJ8 file to a PNG, so the model can be checked without X-Plane.

A small software rasteriser: parse the vertex and index tables, read the
TRIS draw commands, project, z-buffer, sample the albedo texture, shade with
one key light and a little sky fill. Enough to see the silhouette, spot a
face wound inside out, and check the proportions.

    python3 preview.py out/pushback_tug/pushback_tug.obj preview.png
"""

import math
import pathlib
import sys

import numpy as np
from PIL import Image

W, H = 520, 390
SUN = np.array([0.45, 0.78, 0.44])
SUN /= np.linalg.norm(SUN)


def load(path):
    verts, idx, tris = [], [], []
    for line in pathlib.Path(path).read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "VT":
            verts.append([float(x) for x in parts[1:9]])
        elif parts[0] in ("IDX", "IDX10"):
            idx.extend(int(x) for x in parts[1:])
        elif parts[0] == "TRIS":
            tris.append((int(parts[1]), int(parts[2])))
    return np.array(verts, dtype=np.float64), np.array(idx, dtype=np.int64), tris


def look_at(eye, target, up=(0, 1, 0)):
    f = np.array(target) - np.array(eye)
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    s = s / np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[:3, 3] = -m[:3, :3] @ np.array(eye)
    return m


def render(path, out, eye, target=(0.0, 0.9, 0.0), fov=32.0, bg=(26, 29, 34)):
    verts, idx, tris = load(path)
    texture = np.asarray(Image.open(pathlib.Path(path).parent /
                                    (pathlib.Path(path).stem + "_ALB.png")).convert("RGB"),
                         dtype=np.float64) / 255.0
    th, tw = texture.shape[:2]

    view = look_at(eye, target)
    f = 1.0 / math.tan(math.radians(fov) / 2)
    aspect = W / H

    xyz = np.hstack([verts[:, :3], np.ones((len(verts), 1))])
    cam = (view @ xyz.T).T                       # into camera space
    z = -cam[:, 2]
    sx = (cam[:, 0] * f / aspect) / np.where(z > 1e-6, z, 1e-6)
    sy = (cam[:, 1] * f) / np.where(z > 1e-6, z, 1e-6)
    px = (sx * 0.5 + 0.5) * W
    py = (0.5 - sy * 0.5) * H

    frame = np.zeros((H, W, 3))
    frame[:] = np.array(bg) / 255.0
    # A ground plane, so the vehicle does not float in the void.
    for y in range(H):
        t = y / H
        frame[y] = frame[y] * (1 - 0.35 * t) + np.array([0.09, 0.10, 0.12]) * (0.35 * t)
    depth = np.full((H, W), np.inf)

    faces = []
    for offset, count in tris:
        for i in range(offset, offset + count, 3):
            faces.append(idx[i:i + 3])

    for tri in faces:
        a, b, c = tri
        if min(z[a], z[b], z[c]) <= 0.05:
            continue
        xs = px[[a, b, c]]
        ys = py[[a, b, c]]
        area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if area >= 0:            # back face: wound clockwise on screen
            continue

        x0 = max(int(np.floor(xs.min())), 0)
        x1 = min(int(np.ceil(xs.max())) + 1, W)
        y0 = max(int(np.floor(ys.min())), 0)
        y1 = min(int(np.ceil(ys.max())) + 1, H)
        if x0 >= x1 or y0 >= y1:
            continue

        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        w0 = ((xs[1] - xs[0]) * (gy - ys[0]) - (gx - xs[0]) * (ys[1] - ys[0])) / area
        w1 = ((gx - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (gy - ys[0])) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue

        # Barycentric weights, reordered to match a/b/c.
        la, lb, lc = w2, w1, w0
        iz = la / z[a] + lb / z[b] + lc / z[c]
        zz = 1.0 / np.where(iz == 0, 1e-9, iz)
        closer = inside & (zz < depth[y0:y1, x0:x1])
        if not closer.any():
            continue

        normal = verts[a, 3:6]
        lam = max(0.0, float(np.dot(normal, SUN)))
        sky = 0.30 + 0.22 * (normal[1] * 0.5 + 0.5)
        shade = 0.26 + 0.74 * lam * 0.9 + sky * 0.5

        u = (la * verts[a, 6] / z[a] + lb * verts[b, 6] / z[b] + lc * verts[c, 6] / z[c]) * zz
        v = (la * verts[a, 7] / z[a] + lb * verts[b, 7] / z[b] + lc * verts[c, 7] / z[c]) * zz
        tx = np.clip((u * tw).astype(int), 0, tw - 1)
        ty = np.clip(((1.0 - v) * th).astype(int), 0, th - 1)
        colour = texture[ty, tx] * shade

        region = frame[y0:y1, x0:x1]
        region[closer] = np.clip(colour[closer], 0, 1)
        depth[y0:y1, x0:x1][closer] = zz[closer]

    Image.fromarray((frame * 255).astype(np.uint8)).save(out)


if __name__ == "__main__":
    obj = sys.argv[1]
    base = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "preview")
    shots = {
        "_3q":    (6.4, 3.1, -6.6),
        "_side":  (9.0, 1.7, 0.2),
        "_front": (0.4, 2.0, -8.4),
        "_rear3q": (-5.6, 3.4, 6.8),
    }
    for suffix, eye in shots.items():
        render(obj, f"{base}{suffix}.png", eye)
        print(f"{base}{suffix}.png")
