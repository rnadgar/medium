"""Faceplate: flange + skirt + grille + battery-door opening + vents.

* Flange: faceplate_thickness thick, overhangs the duct opening by
  flange_overhang per side; filleted corners, chamfered top rim.
* Skirt: walls dropping from the flange underside around the duct opening
  perimeter minus skirt_clearance -- the standard drop-in fit.  M3 clearance
  holes match the boot-frame insert bosses.
* Grille over the airflow zone only: slots between ~3 mm bars running
  across Y (the short direction), open area >= 60%.
* Over the e-bay: solid except the recessed battery-door opening (two M3
  screws into frame pillars, north edge hooks under the recess shelf) and
  three small vent slots (pressure reference / antenna breathing).

ANTENNA NOTE: the ESP32-C6 module must stay above the metal boot line.  The
PCB standoffs on the boot frame mount the board vertically with its top
edge at params.pcb_top_z, i.e. within the top 15 mm, right under this
faceplate's solid e-bay area (RF exits through plastic + vent slots).
"""

from __future__ import annotations

from build123d import Axis, Box, Part, Pos, chamfer, fillet

from ._util import box, xcyl, ycyl, zcyl
from .params import RegisterParams


def faceplate(p: RegisterParams) -> Part:
    t = p.faceplate_thickness

    # ---- flange with softened edges ----
    flange = Pos(0, 0, -t / 2) * Box(p.flange_l, p.flange_w, t)
    flange = fillet(flange.edges().filter_by(Axis.Z), radius=8.0)
    top_rim = flange.edges().group_by(Axis.Z)[-1]
    flange = chamfer(top_rim, length=1.0)

    # ---- skirt ----
    z0, z1 = -t - p.skirt_depth, -t
    skirt = box(
        -p.skirt_outer_l / 2, p.skirt_outer_l / 2,
        -p.skirt_outer_w / 2, p.skirt_outer_w / 2, z0, z1,
    ) - box(
        -p.skirt_inner_l / 2, p.skirt_inner_l / 2,
        -p.skirt_inner_w / 2, p.skirt_inner_w / 2, z0 - 1, z1 + 1,
    )
    part = flange + skirt

    # ---- grille slots (bars run across Y, the short direction) ----
    n, first, pitch = p.grille_layout
    sw, sl = p.grille_slot_w, p.grille_slot_len
    for i in range(n):
        x = first + i * pitch
        part -= box(x - sw / 2, x + sw / 2, -sl / 2, sl / 2, -t - 1, 1)

    # ---- battery door: recess + opening ----
    rx, ry = p.door_recess_x, p.door_recess_y
    part -= box(  # recess pocket from the top
        p.door_cx - rx / 2, p.door_cx + rx / 2, -ry / 2, ry / 2,
        -p.door_recess_depth, 1,
    )
    part -= box(  # through opening
        p.door_x0, p.door_x1, -p.door_opening[1] / 2, p.door_opening[1] / 2,
        -t - 1, 1,
    )
    for (x, y) in p.door_screw_pts:  # screws pass shelf into frame pillars
        part -= zcyl(p.m3_clear_d / 2, -t - 1, 1, x=x, y=y)

    # ---- e-bay vent slots (pressure reference / RF) ----
    for y in p.vent_ys:
        part -= box(p.vent_cx - 1.0, p.vent_cx + 1.0, y - 7.0, y + 7.0, -t - 1, 1)

    # ---- skirt -> frame M3 clearance holes ----
    for x in p.skirt_screw_xs:
        for s in (-1, 1):
            part -= ycyl(
                p.m3_clear_d / 2,
                s * (p.skirt_inner_w / 2 - 1), s * (p.skirt_outer_w / 2 + 1),
                x=x, z=p.skirt_screw_z,
            )

    # ---- room-side pressure reference hole through the skirt east wall ----
    part -= xcyl(1.5, p.skirt_inner_l / 2 - 1, p.skirt_outer_l / 2 + 1,
                 y=0.0, z=p.ref_hole_z)

    return Part(part.wrapped)
