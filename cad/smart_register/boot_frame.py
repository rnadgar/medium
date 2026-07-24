"""Boot frame: the chassis that slides up inside the faceplate skirt.

One printed part (built in GLOBAL coordinates) providing:

* perimeter walls over the airflow + e-bay zones (M3 heat-set bosses that
  the skirt screws into),
* vane bores in the west wall, blind bearing pockets on the partition,
* the airflow/e-bay partition (master shaft bore through it),
* a sealed e-bay: full-depth walls + floor (duct air stays out; the only
  duct-side opening is the snorkel bore),
* motor mount rib (28BYJ-48), slotted limit-switch mounts on the partition,
* PCB standoff bosses on the east wall (vertical PCB -- the ESP32-C6
  antenna edge lands in the top 15 mm, above the metal boot line),
* battery-door screw pillars, battery-tray screw bosses,
* the pressure snorkel: 6 OD / 3 ID barb dropping ~15 mm below the airflow
  frame on the duct side of the partition (opening faces down, away from
  direct flow), L-bored through the partition to a barb inside the e-bay
  (2-3 mm silicone tube from there to the sensor).  The room-side reference
  is a small hole through the east wall above the boot line plus the
  faceplate vent slots.

The long vanes span the airflow zone bearing only on the end walls; the
blades are stiff enough at this width that no intermediate support comb is
needed (kept out for airflow and print simplicity).
"""

from __future__ import annotations


from build123d import Part, Plane, Pos, Rectangle, Rot, extrude

from ._util import box, xcyl, ycyl, zcyl
from .drivetrain import motor_mount_plate
from .params import RegisterParams


def _switch_mount(p: RegisterParams, closed_end: bool):
    """Slotted mount block for one Omron D2F-class switch on the e-bay face
    of the partition.  The sector's radial limit tab (tip at limit_tab_r1)
    sweeps from ``limit_tab_angle_deg`` (closed) to ``+ vane_travel_deg``
    (open).  Both blocks sit radially OUTSIDE every swept radius (teeth,
    tab) with
    the switch button facing inward, cam-follower style: the tab tip only
    passes under a switch when it is at that block's angle, so each switch
    trips exactly at its travel end.  The open-end block is nudged a few
    degrees short of the stop to keep clear of the pinion mesh zone (the
    protruding button + adjustment slots absorb the difference).  Mounting
    slots run along the travel (tangential) direction."""
    tab = p.limit_tab_angle_deg
    theta = tab if closed_end else tab + p.vane_travel_deg - 3.5
    r_c = p.limit_tab_r1 + 1.5 + p.switch_block_rad / 2
    place2d = Rot(0, 0, theta) * Pos(r_c, 0)
    blk2d = place2d * Rectangle(p.switch_block_rad, p.switch_block_tan)
    solid = Pos(p.ebay_x0, p.master_y, p.vane_axis_z) * extrude(
        Plane.YZ * blk2d, p.switch_block_depth
    )
    cuts = []
    for s in (-1, 1):
        slot2d = place2d * Pos(0, s * p.switch_hole_spacing / 2) * Rectangle(
            p.switch_screw_d + 0.2, p.switch_slot_len
        )
        cuts.append(
            Pos(p.ebay_x0 - 1, p.master_y, p.vane_axis_z)
            * extrude(Plane.YZ * slot2d, p.switch_block_depth + 2)
        )
    return solid, cuts


def boot_frame(p: RegisterParams) -> Part:
    adds = []
    cuts = []
    w = p.wall
    fx0, fx1 = p.frame_x0, p.frame_x1
    hyo = p.frame_outer_w / 2
    hyi = p.frame_inner_w / 2

    # ---- airflow-zone perimeter ring (full length, frame_depth tall) ----
    ring = box(fx0, fx1, -hyo, hyo, p.airflow_bottom_z, p.frame_top_z) - box(
        fx0 + w, fx1 - w, -hyi, hyi, p.airflow_bottom_z - 1, p.frame_top_z + 1
    )
    adds.append(ring)

    # ---- e-bay shell: full-depth walls + integral floor ----
    shell = box(p.partition_x0, fx1, -hyo, hyo, p.ebay_bottom_z, p.frame_top_z)
    shell -= box(
        p.ebay_x0, p.ebay_x1, -hyi, hyi, p.floor_top_z, p.frame_top_z + 1
    )
    adds.append(shell)

    # ---- vane bearings ----
    bore_r = (p.vane_shaft_d + p.fit_running) / 2
    for i, y in enumerate(p.vane_ys):
        # west wall through-bores
        cuts.append(xcyl(bore_r, fx0 - 1, fx0 + w + 1, y=y, z=p.vane_axis_z))
        if i == p.master_index:
            # master shaft passes through the partition into the e-bay
            cuts.append(
                xcyl(bore_r, p.partition_x0 - 1, p.partition_x1 + 1, y=y, z=p.vane_axis_z)
            )
        else:
            # blind bearing pocket in a boss on the partition's duct side
            adds.append(
                xcyl(p.vane_shaft_d / 2 + 2.0,
                     p.partition_x0 - p.vane_pocket_boss_len, p.partition_x0 + 0.5,
                     y=y, z=p.vane_axis_z)
            )
            x_boss = p.partition_x0 - p.vane_pocket_boss_len
            cuts.append(
                xcyl(bore_r, x_boss - 1, x_boss + p.vane_pocket_depth, y=y, z=p.vane_axis_z)
            )

    # ---- motor mount rib ----
    rib, rib_cuts = motor_mount_plate(p)
    adds.append(rib)
    cuts.extend(rib_cuts)

    # ---- limit switch mounts (slotted, on the partition e-bay face) ----
    for closed_end in (True, False):
        blk, blk_cuts = _switch_mount(p, closed_end)
        adds.append(blk)
        cuts.extend(blk_cuts)

    # ---- skirt screw bosses (M3 heat-set inserts, screws come through the
    #      faceplate skirt) ----
    for x in p.skirt_screw_xs:
        for s in (-1, 1):
            adds.append(
                ycyl(4.0, s * (hyi - 5.0), s * hyo, x=x, z=p.skirt_screw_z)
            )
            cuts.append(
                ycyl(p.insert_hole_d / 2, s * (hyo + 1), s * (hyo - 7.0),
                     x=x, z=p.skirt_screw_z)
            )

    # ---- PCB standoff bosses on the east wall (vertical PCB; see module
    #      docstring re: antenna above the boot line) ----
    for (y, z) in p.pcb_standoff_pts:
        adds.append(xcyl(4.0, p.pcb_plane_x, p.ebay_x1 + 0.5, y=y, z=z))
        cuts.append(
            xcyl(p.insert_hole_d / 2, p.pcb_plane_x - 0.1,
                 p.pcb_plane_x + p.insert_hole_depth, y=y, z=z)
        )

    # ---- battery-door screw pillars (from the e-bay floor up to just under
    #      the faceplate recess shelf) ----
    for (x, y) in p.door_screw_pts:
        adds.append(zcyl(p.door_pillar_r, p.floor_top_z - 0.5, -p.faceplate_thickness - 0.2, x=x, y=y))
        cuts.append(
            zcyl(p.insert_hole_d / 2,
                 -p.faceplate_thickness - 0.2 - 8.0, -p.faceplate_thickness - 0.1,
                 x=x, y=y)
        )

    # ---- battery tray screw bosses (feet below the floor; inserts pressed
    #      from inside through the floor) ----
    for (x, y) in p.tray_screw_pts:
        adds.append(zcyl(4.0, p.ebay_bottom_z - 4.0, p.ebay_bottom_z + 0.5, x=x, y=y))
        cuts.append(
            zcyl(p.insert_hole_d / 2, p.floor_top_z - 6.0, p.floor_top_z + 1.0, x=x, y=y)
        )

    # ---- pressure snorkel ----
    sy = p.snorkel_y
    tube_x = p.partition_x0 - p.snorkel_od / 2 + 1.0  # overlaps the partition face
    tip_z = p.airflow_bottom_z - p.snorkel_drop - 1.75
    barb_z = tip_z + 8.5
    barb_x1 = p.ebay_x0 + 8.0
    adds.append(zcyl(p.snorkel_od / 2, tip_z, p.airflow_bottom_z - 2.0, x=tube_x, y=sy))
    adds.append(xcyl(p.snorkel_od / 2, tube_x - 2.0, barb_x1, y=sy, z=barb_z))
    cuts.append(zcyl(p.snorkel_id / 2, tip_z - 1.0, barb_z, x=tube_x, y=sy))
    cuts.append(xcyl(p.snorkel_id / 2, tube_x, barb_x1 + 1.0, y=sy, z=barb_z))

    # ---- room-side pressure reference vent (east wall, above boot line) ----
    cuts.append(xcyl(1.25, p.ebay_x1 - 1, fx1 + 1, y=0.0, z=p.ref_hole_z))

    part = adds[0]
    for a in adds[1:]:
        part += a
    for c in cuts:
        part -= c
    return Part(part.wrapped)
