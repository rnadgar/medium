"""Damper vanes, master vane and the parallel-crank tie bar.

A vane is built in LOCAL coordinates: its pivot axis is the X axis through
the origin and the blade is FLAT (in the X-Y plane).  The assembly rotates
each vane about X by ``vane_closed_tilt_deg`` (closed, blades shingled) or
``+ vane_travel_deg`` more (open, blades vertical) and translates it to
``(0, vane_y, vane_axis_z)``.  Blade X extents already match global X.

Every vane carries a crank arm at the west end whose pin (at
``crank_radius``) engages the tie bar, linking all vanes into a
parallel-crank linkage.  The master vane's east axle extends through the
partition wall into the e-bay and carries a D-flat for the sector gear.
"""

from __future__ import annotations

from build123d import Circle, Part, Plane, Pos, RectangleRounded, Rot, extrude

from ._util import box, xcyl
from .params import RegisterParams


def vane(p: RegisterParams, master: bool = False) -> Part:
    r_ax = p.vane_shaft_d / 2

    blade = box(
        p.blade_x0, p.blade_x1,
        -p.vane_chord / 2, p.vane_chord / 2,
        -p.vane_thickness / 2, p.vane_thickness / 2,
    )

    # stub axles (embedded a few mm into the blade for strength)
    west_axle = xcyl(r_ax, p.axle_west_x, p.blade_x0 + 6.0)
    east_end = p.master_shaft_x1 if master else p.axle_east_x
    east_axle = xcyl(r_ax, p.blade_x1 - 6.0, east_end)

    # crank: hub + arm + pin, just west of the blade.  Built pointing -Z,
    # then rotated so the pin sits at crank_closed_dir_deg once the assembly
    # applies the closed tilt (arm angle is fixed on the printed part).
    hub = xcyl(p.crank_arm_w / 2 + 0.5, p.crank_x0, p.crank_x1)
    arm = box(
        p.crank_x0, p.crank_x1,
        -p.crank_arm_w / 2, p.crank_arm_w / 2,
        -(p.crank_radius + 2.0), 1.0,
    )
    pin = xcyl(p.crank_pin_d / 2, p.pin_x0, p.crank_x0 + 0.5, z=-p.crank_radius)
    arm_local_deg = p.crank_closed_dir_deg - p.vane_closed_tilt_deg
    crank = Rot(arm_local_deg + 90.0, 0, 0) * (arm + pin)

    part = blade + west_axle + east_axle + hub + crank

    if master:
        # D-flat on the e-bay extension for the sector gear (flat normal is
        # local +Z, i.e. the blade plane normal -- params.sector_flat_dir_deg)
        flat = box(
            p.master_flat_x0, p.master_shaft_x1 + 1.0,
            -r_ax - 1.0, r_ax + 1.0,
            r_ax - p.master_flat_cut, r_ax + 1.0,
        )
        part -= flat

    return Part(part.wrapped)


def master_vane(p: RegisterParams) -> Part:
    return vane(p, master=True)


def tie_bar(p: RegisterParams) -> Part:
    """Flat strip linking all crank pins.  Built on Plane.YZ (its final
    orientation) with X from 0 to tie_bar_t; the assembly translates it to
    ``(tie_bar_x0, tie_bar_dy, tie_bar_dz)``.  Pin retention: press-on nylon
    washer or a dab of glue on each pin end (see README note in export)."""
    hole_r = (p.crank_pin_d + p.fit_running) / 2
    sk = RectangleRounded(p.tie_bar_len, p.tie_bar_w, p.tie_bar_w / 2 - 0.5)
    for y in p.vane_ys:
        sk -= Pos(y, 0) * Circle(hole_r)
    return Part(extrude(Plane.YZ * sk, p.tie_bar_t).wrapped)
