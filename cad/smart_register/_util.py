"""Small geometry helpers shared by the part modules (build123d algebra mode)."""

from __future__ import annotations

from build123d import Box, Cylinder, Pos, Rot


def box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box by its two corners."""
    return Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * Box(
        abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)
    )


def xcyl(r, x0, x1, y=0.0, z=0.0):
    """Cylinder with its axis along X, spanning [x0, x1]."""
    return Pos((x0 + x1) / 2, y, z) * Rot(0, 90, 0) * Cylinder(r, abs(x1 - x0))


def ycyl(r, y0, y1, x=0.0, z=0.0):
    """Cylinder with its axis along Y, spanning [y0, y1]."""
    return Pos(x, (y0 + y1) / 2, z) * Rot(90, 0, 0) * Cylinder(r, abs(y1 - y0))


def zcyl(r, z0, z1, x=0.0, y=0.0):
    """Cylinder with its axis along Z, spanning [z0, z1]."""
    return Pos(x, y, (z0 + z1) / 2) * Cylinder(r, abs(z1 - z0))
