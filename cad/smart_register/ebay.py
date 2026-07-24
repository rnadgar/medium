"""E-bay parts: battery tray and battery door (plus a holder placeholder).

The 4xAA holder (envelope 58 x 32 x 18 mm) stands on its long side: 58 mm
across Y, 32 mm tall, 18 mm along X -- the only footprint that coexists
with the motor below the vertical PCB.  The tray is a base plate with end
rails (Y ends) and strap notches; a hook-and-loop strap over the holder
drops through the notches.  Two M3 screws (north tabs) hold the tray to
heat-set inserts in the e-bay floor.  Cells are swapped by lifting the
holder straight out through the faceplate door.

The door sits in the faceplate recess: flush top plate + plug, a north tab
that hooks under the recess shelf, and two countersunk M3 screws (south)
into the frame pillars.
"""

from __future__ import annotations

from build123d import Cone, Part, Pos

from ._util import box, zcyl
from .params import RegisterParams


def battery_tray(p: RegisterParams) -> Part:
    bx0, bx1 = p.bat_x0, p.bat_x1
    by0, by1 = p.bat_y0, p.bat_y1
    zb = p.floor_top_z          # tray sits on the e-bay floor
    base_top = zb + p.tray_base_t
    m = 0.55                    # base margin beyond the holder envelope

    base = box(bx0 - m, bx1 + m, by0 - 2.6, by1 + 2.6, zb, base_top)
    # end rails (Y ends) with running clearance around the holder
    rail_z1 = base_top + p.tray_rail_h
    c = p.fit_general
    south = box(bx0 - m, bx1 + m, by0 - 2.6, by0 - c, base_top - 0.5, rail_z1)
    north = box(bx0 - m, bx1 + m, by1 + c, by1 + 2.6, base_top - 0.5, rail_z1)
    part = base + south + north

    # strap notches through both rails (hook-and-loop strap over the holder)
    notch_x0 = (bx0 + bx1) / 2 - 8.0
    notch_x1 = (bx0 + bx1) / 2 + 8.0
    part -= box(notch_x0, notch_x1, by0 - 3.6, by0 + 1, base_top + 2.0, rail_z1 + 1)
    part -= box(notch_x0, notch_x1, by1 - 1, by1 + 3.6, base_top + 2.0, rail_z1 + 1)

    # north hold-down tabs + M3 countersunk holes into the floor inserts
    for (x, y) in p.tray_screw_pts:
        part += box(x - 5.0, x + 5.0, by1 + 2.0, y + 4.0, zb, base_top)
        part -= zcyl(p.m3_clear_d / 2, zb - 1, base_top + 1, x=x, y=y)
        part -= Pos(x, y, base_top - 0.85) * Cone(
            bottom_radius=p.m3_clear_d / 2, top_radius=3.4, height=1.7
        )
    return Part(part.wrapped)


def battery_door(p: RegisterParams) -> Part:
    t = p.faceplate_thickness
    cx = p.door_cx
    ox, oy = p.door_opening
    fit = p.fit_general

    # flush top plate (fills the faceplate recess)
    rx = p.door_recess_x - 2 * fit
    ry = p.door_recess_y - 2 * fit
    plate = box(cx - rx / 2, cx + rx / 2, -ry / 2, ry / 2,
                -p.door_plate_t, 0.0)
    # plug (fills the through opening, reaches slightly below the plate);
    # its south edge is trimmed back so it clears the frame screw pillars
    px, py = ox - 2 * fit, oy - 2 * fit
    plug = box(cx - px / 2, cx + px / 2, -py / 2 + 3.5, py / 2,
               -t - 0.5, -p.door_plate_t)
    # north hook tab: slides under the faceplate south of the recess shelf
    tab = box(cx - 10.0, cx + 10.0, py / 2 - 1.0, oy / 2 + 4.0,
              -t - 1.9, -t - 0.5)
    part = plate + plug + tab

    for (x, y) in p.door_screw_pts:  # countersunk M3 through the top plate
        part -= zcyl(p.m3_clear_d / 2, -t - 2, 1, x=x, y=y)
        part -= Pos(x, y, -1.0) * Cone(bottom_radius=p.m3_clear_d / 2,
                                       top_radius=3.5, height=2.0)
    return Part(part.wrapped)


def battery_placeholder(p: RegisterParams) -> Part:
    """4xAA holder envelope for the assembly visual check."""
    return Part(
        box(p.bat_x0, p.bat_x1, p.bat_y0, p.bat_y1, p.bat_z0, p.bat_z1).wrapped
    )
