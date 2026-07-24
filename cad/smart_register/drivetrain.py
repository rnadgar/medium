"""Drivetrain: printed pinion, sector gear, motor mount plate and a 28BYJ-48
placeholder for the assembly.

Gear teeth are a simple straight-flank (trapezoidal) profile -- adequate for
a slow printed gear train.  Both gears share the module, the center distance
is exactly ``module * (z1 + z2) / 2`` and tooth widths are sized so the
mating tip always clears the opposing root gap with backlash.

The gears are built on ``Plane.YZ`` (axis along +X, X from 0) with the
angles of the CLOSED pose baked in, so the assembly only translates them:

* sector -> ``(sector_x0, master_y, vane_axis_z)``
* pinion -> ``(pinion_x0, motor_axis_y, motor_axis_z)``

Angle convention matches params: measured from +Y towards +Z.
"""

from __future__ import annotations

import math

from build123d import (
    Circle,
    Part,
    Plane,
    Polyline,
    Pos,
    Rectangle,
    Rot,
    extrude,
    make_face,
)

from ._util import box, xcyl
from .params import RegisterParams


# --------------------------------------------------------------------------
# tooth outline helpers
# --------------------------------------------------------------------------
def _pt(r: float, a_deg: float) -> tuple:
    a = math.radians(a_deg)
    return (r * math.cos(a), r * math.sin(a))


def _tooth_pts(c_deg, r_root, r_tip, w_root, w_tip):
    """Four outline points of one trapezoidal tooth centered at c_deg."""
    phi_r = math.degrees(2 * math.asin(w_root / (2 * r_root)))
    phi_t = math.degrees(2 * math.asin(w_tip / (2 * r_tip)))
    return [
        _pt(r_root, c_deg - phi_r / 2),
        _pt(r_tip, c_deg - phi_t / 2),
        _pt(r_tip, c_deg + phi_t / 2),
        _pt(r_root, c_deg + phi_r / 2),
    ]


def _full_gear_face(n_teeth, module, phase_deg, root_frac, tip_frac):
    """Closed outline of a complete gear (all teeth), as a planar face."""
    r_pitch = module * n_teeth / 2
    r_root = r_pitch - 1.25 * module
    r_tip = r_pitch + module
    cp = math.pi * module
    pitch_a = 360.0 / n_teeth
    pts = []
    for k in range(n_teeth):
        c = phase_deg + k * pitch_a
        pts.append(_pt(r_root, c - pitch_a / 2))  # mid-gap root point
        pts.extend(_tooth_pts(c, r_root, r_tip, root_frac * cp, tip_frac * cp))
    return make_face(Polyline(*pts, close=True))


def _sector_gear_face(p: RegisterParams):
    """Toothed wedge (center -> root radius, teeth on the arc) for the
    sector, in the CLOSED pose angles ``[sector_arc_a0, sector_arc_a1]``."""
    r_root, r_tip = p.sector_root_r, p.sector_tip_r
    cp = math.pi * p.gear_module
    pitch_a = 360.0 / p.sector_teeth
    a0, a1 = p.sector_arc_a0, p.sector_arc_a1
    n = int((a1 - a0) // pitch_a)  # teeth that fit on the arc
    lead = (a1 - a0 - n * pitch_a) / 2
    centers = [a0 + lead + pitch_a / 2 + k * pitch_a for k in range(n)]
    pts = [(0.0, 0.0), _pt(r_root, a0)]
    for i, c in enumerate(centers):
        if i > 0:
            pts.append(_pt(r_root, c - pitch_a / 2))  # mid-gap root point
        pts.extend(_tooth_pts(c, r_root, r_tip, p.sector_root_frac * cp, p.tooth_tip_frac * cp))
    pts.append(_pt(r_root, a1))
    return make_face(Polyline(*pts, close=True))


def _mesh_phase_deg(p: RegisterParams) -> float:
    """Pinion tooth phase so its gaps line up with the sector teeth at the
    closed pose (verified by a zero-interference test)."""
    pitch_a_s = 360.0 / p.sector_teeth
    a0, a1 = p.sector_arc_a0, p.sector_arc_a1
    n = int((a1 - a0) // pitch_a_s)
    lead = (a1 - a0 - n * pitch_a_s) / 2
    first_center = a0 + lead + pitch_a_s / 2
    # sector tooth center angle nearest the line of centers (pinion_angle)
    k = round((p.pinion_angle_deg - first_center) / pitch_a_s)
    delta_s = first_center + k * pitch_a_s - p.pinion_angle_deg
    # external mesh: sector offset delta_s maps to pinion GAP offset
    # -ratio*delta_s about the (opposite) line of centers
    los_p = p.pinion_angle_deg + 180.0
    gap = los_p - p.gear_ratio * delta_s
    return gap + 180.0 / p.pinion_teeth  # tooth center = gap center + half pitch


# --------------------------------------------------------------------------
# parts
# --------------------------------------------------------------------------
def pinion(p: RegisterParams) -> Part:
    """12t pinion for the 28BYJ-48 5 mm double-D shaft."""
    face = _full_gear_face(
        p.pinion_teeth, p.gear_module, _mesh_phase_deg(p),
        p.pinion_root_frac, p.tooth_tip_frac,
    )
    body = extrude(Plane.YZ * face, p.pinion_width)
    # double-D bore: 5.0 dia with 3.0 across flats (+ press allowance)
    bore_r = (p.motor_shaft_d + p.fit_press) / 2
    flats = p.motor_shaft_flat + p.fit_press
    profile = Circle(bore_r) & Rectangle(2 * bore_r + 1, flats)
    bore = Pos(-1, 0, 0) * extrude(Plane.YZ * profile, p.pinion_width + 2)
    return Part((body - bore).wrapped)


def sector_gear(p: RegisterParams) -> Part:
    """36t-equivalent sector (~135 deg of teeth) with hub, D-flat bore and
    the limit-switch tab, in the CLOSED pose."""
    web2d = _sector_gear_face(p) + Circle(p.sector_hub_r)
    # limit-switch tab (radial arm)
    tab2d = (
        Rot(0, 0, p.limit_tab_angle_deg)
        * Pos((p.limit_tab_r0 + p.limit_tab_r1) / 2, 0)
        * Rectangle(p.limit_tab_r1 - p.limit_tab_r0, p.limit_tab_w)
    )
    web = extrude(Plane.YZ * (web2d + tab2d), p.gear_width)
    hub = extrude(Plane.YZ * Circle(p.sector_hub_r), p.sector_hub_len)
    # D-flat bore matching the master vane shaft (flat normal at
    # sector_flat_dir_deg in the closed pose)
    bore_r = (p.vane_shaft_d + p.fit_press) / 2
    flat_off = p.vane_shaft_d / 2 - p.master_flat_cut + p.fit_press
    keep = Pos((flat_off - bore_r) / 2, 0) * Rectangle(bore_r + flat_off, 2 * bore_r + 2)
    profile = Circle(bore_r) & (Rot(0, 0, p.sector_flat_dir_deg) * keep)
    bore = Pos(-1, 0, 0) * extrude(Plane.YZ * profile, p.sector_hub_len + 2)
    return Part((web + hub - bore).wrapped)


def motor_mount_plate(p: RegisterParams):
    """Vertical rib (part of the boot frame) the 28BYJ-48 bolts to: two
    4.2 mm holes 35 mm apart + a shallow d28.5 pocket registering the can.
    Returned as (solid, cuts) so boot_frame can fuse/cut it in one pass."""
    plate = box(
        p.rib_x0, p.rib_x1,
        -4.0, p.frame_outer_w / 2 - p.wall + 1.0,   # fused into the north wall
        p.rib_top_z, p.floor_top_z - 1.0,           # fused into the floor
    )
    cuts = [
        # shaft/boss passage
        xcyl(p.motor_boss_d / 2 + 0.5, p.rib_x0 - 1, p.rib_x1 + 1,
             y=p.motor_axis_y, z=p.motor_axis_z),
        # can-registration pocket on the motor side (east face)
        xcyl((p.motor_body_d + 0.4) / 2, p.rib_x1 - 1.5, p.rib_x1 + 0.1,
             y=p.motor_axis_y, z=p.motor_body_center_z),
    ]
    for s in (-1, 1):
        cuts.append(
            xcyl(p.motor_screw_d / 2, p.rib_x0 - 1, p.rib_x1 + 1,
                 y=p.motor_axis_y + s * p.motor_screw_spacing / 2,
                 z=p.motor_body_center_z)
        )
    return plate, cuts


def motor_placeholder(p: RegisterParams) -> Part:
    """Simple cylinder + flange 28BYJ-48 stand-in so the assembly shows fit
    (built in global position)."""
    y, zs = p.motor_axis_y, p.motor_axis_z
    zb = p.motor_body_center_z
    body = xcyl(p.motor_body_d / 2, p.motor_body_x0, p.motor_body_x1, y=y, z=zb)
    tabs = box(
        p.rib_x1, p.motor_body_x0,
        y - p.motor_screw_spacing / 2 - 3.5, y + p.motor_screw_spacing / 2 + 3.5,
        zb - 3.5, zb + 3.5,
    )
    boss = xcyl(p.motor_boss_d / 2, p.rib_x0 + 0.5, p.motor_body_x0, y=y, z=zs)
    shaft = xcyl(p.motor_shaft_d / 2, p.motor_shaft_x0, p.motor_body_x0, y=y, z=zs)
    part = body + tabs + boss + shaft
    # shaft flats (3.0 across) over the exposed length
    for s in (-1, 1):
        part -= box(
            p.motor_shaft_x0 - 1, p.rib_x0 - 0.5,
            y - 4, y + 4,
            zs + s * p.motor_shaft_flat / 2, zs + s * (p.motor_shaft_d / 2 + 1),
        )
    for s in (-1, 1):
        part -= xcyl(
            p.motor_screw_d / 2, p.rib_x1 - 1, p.motor_body_x0 + 1,
            y=y + s * p.motor_screw_spacing / 2, z=zb,
        )
    return Part(part.wrapped)
