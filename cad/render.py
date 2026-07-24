#!/usr/bin/env python3.11
"""Render PNG previews of every part and the full assembly.

Run:  python3 cad/render.py
Outputs: cad/out/png/<part>.png, assembly*.png and parts_grid.png

Uses build123d tessellation + matplotlib 3D (no GPU/GL needed).
"""

from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from mpl_toolkits.mplot3d.art3d import Poly3DCollection  # noqa: E402

CAD_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(CAD_DIR))

from export import PART_BUILDERS  # noqa: E402
from smart_register import REGISTER_4X10, assembly  # noqa: E402

OUT = CAD_DIR / "out" / "png"

PALETTE = {
    "faceplate": "#c8b89a",
    "boot_frame": "#8a9bb0",
    "vane": "#d97757",
    "master_vane": "#c4552f",
    "tie_bar": "#e0b13f",
    "pinion": "#7aa25c",
    "sector_gear": "#5c8a52",
    "battery_tray": "#9b7fb0",
    "battery_door": "#b8a48a",
    "motor_placeholder": "#666f77",
    "battery_placeholder": "#4a7fa5",
}


def mesh_of(shape, tol=0.4):
    """Tessellate a build123d Shape -> (Nx3 verts, Mx3 tri indices)."""
    verts, tris = shape.tessellate(tol)
    v = np.array([(p.X, p.Y, p.Z) for p in verts])
    t = np.array(tris)
    return v, t


def shade(tris_xyz, base_rgb, light=(0.35, -0.5, 0.8)):
    """Flat-shade triangle colors by face normal vs light direction."""
    lt = np.array(light, dtype=float)
    lt /= np.linalg.norm(lt)
    n = np.cross(tris_xyz[:, 1] - tris_xyz[:, 0], tris_xyz[:, 2] - tris_xyz[:, 0])
    norm = np.linalg.norm(n, axis=1, keepdims=True)
    norm[norm == 0] = 1
    n = n / norm
    lam = np.abs(n @ lt)  # double-sided
    base = np.array(matplotlib.colors.to_rgb(base_rgb))
    return np.clip(0.35 * base + 0.65 * base * lam[:, None], 0, 1)


def render(meshes, path, elev=28, azim=-55, title=None):
    """meshes: list of (verts, tris, color)."""
    fig = plt.figure(figsize=(10, 7), dpi=140)
    ax = fig.add_subplot(111, projection="3d")
    ax.set_proj_type("persp")
    allv = np.vstack([m[0] for m in meshes])
    for v, t, color in meshes:
        tri = v[t]
        pc = Poly3DCollection(tri, linewidths=0)
        pc.set_facecolor(shade(tri, color))
        ax.add_collection3d(pc)
    lo, hi = allv.min(axis=0), allv.max(axis=0)
    c, r = (lo + hi) / 2, (hi - lo).max() / 2 * 1.05
    ax.set_xlim(c[0] - r, c[0] + r)
    ax.set_ylim(c[1] - r, c[1] + r)
    ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    if title:
        ax.set_title(title, fontsize=13, pad=0)
    fig.tight_layout(pad=0.1)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"wrote {path}")


def color_for(label: str) -> str:
    for key, col in PALETTE.items():
        if key in (label or "").lower():
            return col
    return "#999999"


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    p = REGISTER_4X10

    part_meshes = {}
    for name, builder in PART_BUILDERS.items():
        part = builder(p)
        v, t = mesh_of(part)
        part_meshes[name] = (v, t, PALETTE.get(name, "#999999"))
        render([part_meshes[name]], OUT / f"{name}.png", title=name)

    # parts grid (one figure, thumbnails)
    names = list(part_meshes)
    cols, rows = 4, (len(names) + 3) // 4
    fig = plt.figure(figsize=(4 * cols, 3.2 * rows), dpi=120)
    for i, name in enumerate(names):
        ax = fig.add_subplot(rows, cols, i + 1, projection="3d")
        v, t, col = part_meshes[name]
        tri = v[t]
        pc = Poly3DCollection(tri, linewidths=0)
        pc.set_facecolor(shade(tri, col))
        ax.add_collection3d(pc)
        lo, hi = v.min(axis=0), v.max(axis=0)
        c, r = (lo + hi) / 2, (hi - lo).max() / 2 * 1.05
        ax.set_xlim(c[0] - r, c[0] + r)
        ax.set_ylim(c[1] - r, c[1] + r)
        ax.set_zlim(c[2] - r, c[2] + r)
        ax.set_box_aspect((1, 1, 1))
        ax.view_init(elev=28, azim=-55)
        ax.set_axis_off()
        ax.set_title(name, fontsize=11)
    fig.tight_layout()
    fig.savefig(OUT / "parts_grid.png", facecolor="white")
    plt.close(fig)
    print(f"wrote {OUT / 'parts_grid.png'}")

    # assembly: walk children with their placements and labels/colors
    asm = assembly(p)
    meshes = []
    for child in asm.children:
        shape = child if not hasattr(child, "moved") else child
        v, t = mesh_of(shape)
        meshes.append((v, t, color_for(getattr(child, "label", ""))))
    render(meshes, OUT / "assembly_iso.png", elev=30, azim=-60,
           title="smart register — assembly (closed)")
    render(meshes, OUT / "assembly_below.png", elev=-25, azim=-120,
           title="assembly — duct side (drivetrain / snorkel)")
    render(meshes, OUT / "assembly_top.png", elev=75, azim=-90,
           title="assembly — room side")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
