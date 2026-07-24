"""Positioned Compound of the whole register (closed pose) for visual checks."""

from __future__ import annotations

from build123d import Color, Compound, Pos, Rot

from .boot_frame import boot_frame
from .drivetrain import motor_placeholder, pinion, sector_gear
from .ebay import battery_door, battery_placeholder, battery_tray
from .faceplate import faceplate
from .params import REGISTER_4X10, RegisterParams
from .vanes import tie_bar, vane


def assembly(p: RegisterParams = REGISTER_4X10) -> Compound:
    children = []

    def add(part, label, color, loc=None):
        if loc is not None:
            part = part.moved(loc)
        part.label = label
        part.color = Color(*color)
        children.append(part)

    add(faceplate(p), "faceplate", (0.88, 0.88, 0.90))
    add(boot_frame(p), "boot_frame", (0.35, 0.38, 0.42))

    # vanes at the closed (shingled) tilt; master carries the gear shaft
    normal = vane(p)
    master = vane(p, master=True)
    tilt = p.vane_closed_tilt_deg
    for i, y in enumerate(p.vane_ys):
        v = master if i == p.master_index else normal
        add(v, f"vane_{i}" + ("_master" if i == p.master_index else ""),
            (0.80, 0.55, 0.20), Pos(0, y, p.vane_axis_z) * Rot(tilt, 0, 0))

    add(tie_bar(p), "tie_bar", (0.75, 0.30, 0.25),
        Pos(p.tie_bar_x0, p.tie_bar_dy, p.tie_bar_dz))

    # drivetrain (gears are built in the closed pose, axis along +X from x=0)
    add(sector_gear(p), "sector_gear", (0.20, 0.55, 0.80),
        Pos(p.sector_x0, p.master_y, p.vane_axis_z))
    add(pinion(p), "pinion", (0.90, 0.75, 0.10),
        Pos(p.pinion_x0, p.motor_axis_y, p.motor_axis_z))
    add(motor_placeholder(p), "motor_28byj48", (0.55, 0.60, 0.65))

    add(battery_tray(p), "battery_tray", (0.30, 0.60, 0.35))
    add(battery_placeholder(p), "battery_4xAA", (0.15, 0.15, 0.15))
    add(battery_door(p), "battery_door", (0.88, 0.88, 0.90))

    return Compound(children=children)
