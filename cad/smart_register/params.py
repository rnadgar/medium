"""Single source of truth for every dimension of the smart register CAD model.

Global coordinate system (shared by every module):

* X -- along the register length (254 mm for a 4"x10"), 0 at register center.
* Y -- across the register width, 0 at register center.
* Z -- up.  The faceplate top surface is Z = 0; everything else hangs below
  (negative Z is down into the duct boot).  The e-bay occupies the +X end.

Everything is millimetres and degrees.  All layout math lives here as derived
properties so the part modules contain no magic numbers -- change a field on
:class:`RegisterParams` and the whole register re-derives.

Angles in the Y-Z plane (gear/vane rotation plane) are measured from +Y
towards +Z (so 0 deg = +Y, 90 deg = straight up).
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class RegisterParams:
    # ---------------- boot / faceplate envelope ----------------
    duct_length: float = 254.0      # 10" nominal boot opening (X)
    duct_width: float = 101.6       # 4" nominal boot opening (Y)
    flange_overhang: float = 19.0   # flange beyond duct opening, per side
    faceplate_thickness: float = 3.0
    skirt_depth: float = 22.0       # skirt height below faceplate underside
    skirt_clearance: float = 3.2    # per side -> skirt outer ~3.75" x 9.75"
    wall: float = 2.4               # default printed wall thickness
    ebay_length: float = 62.0       # e-bay zone length at the +X end

    # ---------------- printed-fit clearances ----------------
    fit_running: float = 0.30       # rotating / sliding fit (diametral)
    fit_press: float = 0.10         # press / insert fit (diametral)
    fit_general: float = 0.25       # general assembly clearance (per side)
    insert_hole_d: float = 4.0      # heat-set insert hole for M3
    insert_hole_depth: float = 6.0
    m3_clear_d: float = 3.4
    m2_clear_d: float = 2.4

    # ---------------- vanes ----------------
    vane_count: int = 4
    vane_shaft_d: float = 4.0
    vane_thickness: float = 1.8
    vane_overlap: float = 2.0       # shingle overlap between adjacent blades
    vane_swing_margin: float = 1.0  # total Y margin kept free for blade swing
    vane_closed_tilt_deg: float = 8.0   # closed blades shingle at this tilt
    vane_travel_deg: float = 90.0
    crank_radius: float = 9.0       # crank pin radius (parallel-crank linkage)
    crank_pin_d: float = 3.0
    crank_gap: float = 9.0          # X space between west wall and blade end
    crank_plate_t: float = 2.5
    crank_arm_w: float = 6.0
    crank_pin_len: float = 3.5
    tie_bar_t: float = 2.4
    tie_bar_w: float = 7.0
    vane_pocket_boss_len: float = 3.0  # partition-side blind bearing boss

    # ---------------- gear train (pinion -> sector, ~3:1) ----------------
    gear_module: float = 1.25
    pinion_teeth: int = 12
    sector_teeth: int = 36          # full-circle equivalent tooth count
    sector_arc_deg: float = 135.0   # toothed arc actually present
    pinion_width: float = 7.0
    gear_width: float = 6.0         # sector web thickness
    sector_hub_r: float = 6.0
    sector_hub_len: float = 10.0
    # Direction (in the Y-Z plane) from the master-vane axis to the motor
    # axis.  Mostly downward so the big sector arc stays below the faceplate.
    pinion_angle_deg: float = -72.5
    mesh_margin_deg: float = 7.5    # sector arc kept past mesh line at travel ends
    # Straight-flank trapezoidal tooth proportions (chordal width / circular
    # pitch).  Pinion root is kept slimmer so the sector tip clears the
    # small-gear root gap; widths at the pitch circle come out ~40-45%.
    tooth_tip_frac: float = 0.28
    pinion_root_frac: float = 0.47
    sector_root_frac: float = 0.58
    master_flat_cut: float = 0.8    # D-flat depth on the master vane shaft
    # limit-switch actuation tab on the sector: a radial arm that reaches
    # BEYOND the tooth tips (its root merges with the toothed wedge), so the
    # switch mounts can sit outside every swept radius.
    limit_tab_r0: float = 15.0
    limit_tab_w: float = 5.0
    limit_tab_offset_deg: float = 10.0  # tab angle inside the wedge span, from arc start
    limit_tab_over: float = 3.75        # tab reach beyond the sector tip radius

    # ---------------- 28BYJ-48 stepper ----------------
    motor_body_d: float = 28.5
    motor_body_len: float = 19.0
    motor_shaft_d: float = 5.0
    motor_shaft_flat: float = 3.0   # across the two shaft flats
    motor_shaft_len: float = 9.5
    motor_screw_spacing: float = 35.0
    motor_screw_d: float = 4.2
    motor_shaft_offset: float = 8.0  # shaft axis offset from body center
    motor_boss_d: float = 9.0
    motor_rib_t: float = 2.5

    # ---------------- limit switches (Omron D2F class) ----------------
    switch_body_l: float = 12.8
    switch_body_w: float = 5.8
    switch_body_h: float = 6.5
    switch_hole_spacing: float = 6.5
    switch_screw_d: float = 2.2     # M2
    switch_slot_len: float = 5.0    # adjustment slot length
    switch_block_tan: float = 14.0  # mount block size along travel direction
    switch_block_rad: float = 6.0
    switch_block_depth: float = 8.0

    # ---------------- PCB contract (must match hardware/pcb) ----------------
    pcb_size: tuple = (58.0, 38.0)  # (long edge, short edge)
    pcb_hole_inset: float = 3.5
    pcb_thickness: float = 1.6
    standoff_h: float = 5.0         # PCB standoff length (board to frame wall)

    # ---------------- battery (4xAA holder envelope) ----------------
    battery_envelope: tuple = (58.0, 32.0, 18.0)  # holder L x W x thickness
    battery_y_offset: float = 3.0   # holder shifted +Y to clear door pillars
    tray_base_t: float = 2.4
    tray_rail_h: float = 14.0

    # ---------------- frame ----------------
    frame_depth: float = 30.0       # airflow-zone perimeter wall height
    floor_thickness: float = 2.4    # e-bay floor (seals e-bay from duct air)

    # ---------------- pressure snorkel ----------------
    snorkel_od: float = 6.0
    snorkel_id: float = 3.0
    snorkel_drop: float = 15.0      # protrusion below the airflow-zone frame
    snorkel_y: float = -30.0

    # ---------------- faceplate grille / door / vents ----------------
    grille_bar: float = 3.0
    grille_pitch: float = 10.5
    door_opening: tuple = (40.0, 70.0)   # (X, Y)
    door_lip: float = 4.0                # recess shelf width around opening
    door_recess_depth: float = 1.6
    door_plate_t: float = 1.6
    door_plug_t: float = 1.9             # plug reaches 0.5 below faceplate
    door_pillar_r: float = 4.0

    # ================= derived: envelope =================
    @property
    def flange_l(self) -> float:
        return self.duct_length + 2 * self.flange_overhang

    @property
    def flange_w(self) -> float:
        return self.duct_width + 2 * self.flange_overhang

    @property
    def skirt_outer_l(self) -> float:
        return self.duct_length - 2 * self.skirt_clearance

    @property
    def skirt_outer_w(self) -> float:
        return self.duct_width - 2 * self.skirt_clearance

    @property
    def skirt_inner_l(self) -> float:
        return self.skirt_outer_l - 2 * self.wall

    @property
    def skirt_inner_w(self) -> float:
        return self.skirt_outer_w - 2 * self.wall

    @property
    def frame_outer_l(self) -> float:
        return self.skirt_inner_l - 2 * self.fit_general

    @property
    def frame_outer_w(self) -> float:
        return self.skirt_inner_w - 2 * self.fit_general

    @property
    def frame_inner_l(self) -> float:
        return self.frame_outer_l - 2 * self.wall

    @property
    def frame_inner_w(self) -> float:
        return self.frame_outer_w - 2 * self.wall

    @property
    def airflow_zone_length(self) -> float:
        return self.duct_length - self.ebay_length

    # ================= derived: frame zones (global X) =================
    @property
    def frame_x1(self) -> float:
        return self.frame_outer_l / 2

    @property
    def frame_x0(self) -> float:
        return -self.frame_x1

    @property
    def partition_x0(self) -> float:
        """West (duct-side) face of the airflow/e-bay partition wall."""
        return self.frame_x1 - self.ebay_length

    @property
    def partition_x1(self) -> float:
        return self.partition_x0 + self.wall

    @property
    def ebay_x0(self) -> float:
        return self.partition_x1

    @property
    def ebay_x1(self) -> float:
        return self.frame_x1 - self.wall

    # ================= derived: frame depths (global Z) =================
    @property
    def frame_top_z(self) -> float:
        return -(self.faceplate_thickness + self.fit_general)

    @property
    def airflow_bottom_z(self) -> float:
        return self.frame_top_z - self.frame_depth

    @property
    def vane_axis_z(self) -> float:
        return self.frame_top_z - self.frame_depth / 2

    @property
    def floor_top_z(self) -> float:
        # e-bay floor sits just under the lowest drivetrain sweep (the pinion)
        return self.motor_axis_z - self.pinion_tip_r - 1.5

    @property
    def ebay_bottom_z(self) -> float:
        return self.floor_top_z - self.floor_thickness

    @property
    def ebay_depth(self) -> float:
        return self.frame_top_z - self.ebay_bottom_z

    # ================= derived: vanes =================
    @property
    def vane_pitch(self) -> float:
        return (self.frame_inner_w - self.vane_overlap - self.vane_swing_margin) / self.vane_count

    @property
    def vane_chord(self) -> float:
        return self.vane_pitch + self.vane_overlap

    @property
    def vane_ys(self) -> tuple:
        n = self.vane_count
        return tuple((i - (n - 1) / 2) * self.vane_pitch for i in range(n))

    @property
    def master_index(self) -> int:
        # A middle vane is master: keeps the big sector arc away from the
        # long frame walls in the e-bay.
        return self.vane_count // 2

    @property
    def master_y(self) -> float:
        return self.vane_ys[self.master_index]

    @property
    def blade_x0(self) -> float:
        return self.frame_x0 + self.wall + self.crank_gap

    @property
    def blade_x1(self) -> float:
        return self.partition_x0 - self.vane_pocket_boss_len - 0.7

    @property
    def axle_west_x(self) -> float:
        """West end of the vane stub axle (stops inside the wall bore)."""
        return self.frame_x0 + 0.5

    @property
    def axle_east_x(self) -> float:
        """East end of a non-master stub axle (inside the partition pocket)."""
        return self.partition_x0 - self.vane_pocket_boss_len + self.vane_pocket_depth - 0.75

    @property
    def vane_pocket_depth(self) -> float:
        return self.vane_pocket_boss_len + 1.0  # blind, leaves >1mm of partition

    @property
    def crank_x1(self) -> float:
        return self.blade_x0 - 1.0

    @property
    def crank_x0(self) -> float:
        return self.crank_x1 - self.crank_plate_t

    @property
    def pin_x0(self) -> float:
        return self.crank_x0 - self.crank_pin_len

    @property
    def tie_bar_x0(self) -> float:
        return self.crank_x0 - 0.6 - self.tie_bar_t

    @property
    def crank_closed_dir_deg(self) -> float:
        """Crank pin direction (Y-Z angle) with the vanes closed.  The throw
        is symmetric about straight-down (-135 deg -> -45 deg over the 90 deg
        travel) so the tie bar never sweeps across the vane axles."""
        return -90.0 - self.vane_travel_deg / 2

    @property
    def tie_bar_dy(self) -> float:
        return self.crank_radius * math.cos(math.radians(self.crank_closed_dir_deg))

    @property
    def tie_bar_dz(self) -> float:
        return self.vane_axis_z + self.crank_radius * math.sin(math.radians(self.crank_closed_dir_deg))

    @property
    def tie_bar_len(self) -> float:
        return (self.vane_count - 1) * self.vane_pitch + 8.0

    # ================= derived: gears =================
    @property
    def pinion_pitch_r(self) -> float:
        return self.gear_module * self.pinion_teeth / 2

    @property
    def pinion_tip_r(self) -> float:
        return self.pinion_pitch_r + self.gear_module

    @property
    def pinion_root_r(self) -> float:
        return self.pinion_pitch_r - 1.25 * self.gear_module

    @property
    def sector_pitch_r(self) -> float:
        return self.gear_module * self.sector_teeth / 2

    @property
    def sector_tip_r(self) -> float:
        return self.sector_pitch_r + self.gear_module

    @property
    def sector_root_r(self) -> float:
        return self.sector_pitch_r - 1.25 * self.gear_module

    @property
    def gear_center_distance(self) -> float:
        return self.gear_module * (self.pinion_teeth + self.sector_teeth) / 2

    @property
    def gear_ratio(self) -> float:
        return self.sector_teeth / self.pinion_teeth

    # Sector arc, in the CLOSED pose.  Opening rotates the vane shaft (and
    # sector) by +vane_travel_deg, so the toothed arc must cover the mesh
    # line (pinion_angle_deg) at both extremes with mesh_margin_deg spare.
    @property
    def sector_arc_a1(self) -> float:
        return self.pinion_angle_deg + self.mesh_margin_deg

    @property
    def sector_arc_a0(self) -> float:
        return self.sector_arc_a1 - self.sector_arc_deg

    @property
    def sector_flat_dir_deg(self) -> float:
        """Bore D-flat normal direction in the CLOSED pose (master vane's
        blade plane normal: blade tilted vane_closed_tilt_deg from flat)."""
        return 90.0 + self.vane_closed_tilt_deg

    @property
    def limit_tab_r1(self) -> float:
        return self.sector_tip_r + self.limit_tab_over

    @property
    def limit_tab_angle_deg(self) -> float:
        """Limit tab direction in the CLOSED pose.  Sweeps +travel to open."""
        return self.sector_arc_a0 + self.limit_tab_offset_deg

    @property
    def sector_band(self) -> tuple:
        """Angular band (deg) swept by the toothed wedge over full travel."""
        return (self.sector_arc_a0, self.sector_arc_a1 + self.vane_travel_deg)

    @property
    def limit_tab_band(self) -> tuple:
        """Angular band (deg) swept by the limit tab (radius limit_tab_r1)."""
        return (self.limit_tab_angle_deg, self.limit_tab_angle_deg + self.vane_travel_deg)

    # ================= derived: motor / drivetrain placement =================
    @property
    def motor_axis_y(self) -> float:
        return self.master_y + self.gear_center_distance * math.cos(math.radians(self.pinion_angle_deg))

    @property
    def motor_axis_z(self) -> float:
        return self.vane_axis_z + self.gear_center_distance * math.sin(math.radians(self.pinion_angle_deg))

    @property
    def motor_body_center_z(self) -> float:
        # 28BYJ-48 shaft is offset from the can center; mount the can offset
        # UP so the motor sits as shallow as possible.
        return self.motor_axis_z + self.motor_shaft_offset

    @property
    def sector_x0(self) -> float:
        return self.ebay_x0 + 1.5

    @property
    def sector_x1(self) -> float:
        return self.sector_x0 + self.gear_width

    @property
    def sector_hub_x1(self) -> float:
        return self.sector_x0 + self.sector_hub_len

    @property
    def pinion_x0(self) -> float:
        return self.ebay_x0 + 1.0

    @property
    def pinion_x1(self) -> float:
        return self.pinion_x0 + self.pinion_width

    @property
    def rib_x0(self) -> float:
        return self.ebay_x0 + 8.5

    @property
    def rib_x1(self) -> float:
        return self.rib_x0 + self.motor_rib_t

    @property
    def rib_top_z(self) -> float:
        # Just below the sector hub sweep
        return self.vane_axis_z - self.sector_hub_r - 0.75

    @property
    def motor_body_x0(self) -> float:
        return self.rib_x1 + 1.0   # flange tabs (1mm steel) against the rib

    @property
    def motor_body_x1(self) -> float:
        return self.motor_body_x0 + self.motor_body_len

    @property
    def motor_shaft_x0(self) -> float:
        return self.motor_body_x0 - self.motor_shaft_len

    @property
    def master_shaft_x1(self) -> float:
        return self.sector_hub_x1 + 0.25

    @property
    def master_flat_x0(self) -> float:
        return self.partition_x1 + 0.5

    # ================= derived: battery =================
    @property
    def bat_x0(self) -> float:
        # Battery holder stands on its 58x32 side: X = holder thickness.
        return self.ebay_x0 + 32.0

    @property
    def bat_x1(self) -> float:
        return self.bat_x0 + self.battery_envelope[2]

    @property
    def bat_y0(self) -> float:
        return self.battery_y_offset - self.battery_envelope[0] / 2

    @property
    def bat_y1(self) -> float:
        return self.battery_y_offset + self.battery_envelope[0] / 2

    @property
    def bat_z0(self) -> float:
        return self.floor_top_z + self.tray_base_t

    @property
    def bat_z1(self) -> float:
        return self.bat_z0 + self.battery_envelope[1]

    @property
    def tray_screw_pts(self) -> tuple:
        """(x, y) of the two tray hold-down screws (north-side tabs)."""
        y = self.bat_y1 + 4.0
        return ((self.bat_x0 + 4.0, y), (self.bat_x1 - 4.0, y))

    # ================= derived: PCB =================
    # NOTE (antenna): the ESP32-C6 antenna must sit above the metal boot
    # line, so the PCB mounts VERTICALLY on the east frame wall with its top
    # edge in the top 15 mm of the assembly (pcb_top_z).  Standoff bosses
    # (with M3 heat-set inserts) stick horizontally out of the east wall;
    # standoff_h is the board-to-wall gap (bottom-side component clearance).
    @property
    def pcb_plane_x(self) -> float:
        """Mounting face (east face of the PCB) global X."""
        return self.ebay_x1 - self.standoff_h

    @property
    def pcb_top_z(self) -> float:
        return -(self.faceplate_thickness + 4.0)

    @property
    def pcb_standoff_pts(self) -> tuple:
        """(y, z) of the four PCB mounting holes."""
        hy = self.pcb_size[0] / 2 - self.pcb_hole_inset
        z0 = self.pcb_top_z - self.pcb_hole_inset
        z1 = self.pcb_top_z - self.pcb_size[1] + self.pcb_hole_inset
        return ((-hy, z0), (hy, z0), (-hy, z1), (hy, z1))

    # ================= derived: faceplate features =================
    @property
    def grille_zone_x(self) -> tuple:
        return (-self.skirt_inner_l / 2 + 4.0, self.partition_x0 - 3.0)

    @property
    def grille_slot_w(self) -> float:
        return self.grille_pitch - self.grille_bar

    @property
    def grille_slot_len(self) -> float:
        return self.skirt_inner_w - 14.0

    @property
    def grille_layout(self) -> tuple:
        """(n_slots, first_slot_center_x, pitch)"""
        x0, x1 = self.grille_zone_x
        span = x1 - x0
        n = int((span - self.grille_bar) // self.grille_pitch)
        used = n * self.grille_pitch + self.grille_bar
        first = x0 + (span - used) / 2 + self.grille_bar + self.grille_slot_w / 2
        return (n, first, self.grille_pitch)

    @property
    def grille_open_ratio(self) -> float:
        """Open fraction inside the grille envelope (>= 0.6 required)."""
        return self.grille_slot_w / self.grille_pitch

    @property
    def door_cx(self) -> float:
        return self.ebay_x0 + 6.5 + self.door_opening[0] / 2

    @property
    def door_x0(self) -> float:
        return self.door_cx - self.door_opening[0] / 2

    @property
    def door_x1(self) -> float:
        return self.door_cx + self.door_opening[0] / 2

    @property
    def door_recess_x(self) -> float:
        return self.door_opening[0] + 2 * self.door_lip

    @property
    def door_recess_y(self) -> float:
        return self.door_opening[1] + 2 * self.door_lip

    @property
    def door_screw_pts(self) -> tuple:
        """(x, y) of the two battery-door screws (south shelf; the north
        edge of the door hooks under the recess shelf with a tab)."""
        y = -(self.door_opening[1] / 2 + 1.5)
        return ((self.door_cx - 10.0, y), (self.door_cx + 10.0, y))

    @property
    def vent_cx(self) -> float:
        return self.ebay_x0 + 3.0

    @property
    def vent_ys(self) -> tuple:
        return (-18.0, 0.0, 18.0)

    # ================= derived: skirt/frame screws =================
    @property
    def skirt_screw_xs(self) -> tuple:
        """X positions of the M3 skirt->frame screws (each long side)."""
        return (
            self.frame_x0 + self.wall + 3.5,      # west of the crank zone
            self.partition_x0 + self.wall / 2,    # on the partition line
            self.ebay_x1 - 8.75,                  # e-bay end
        )

    @property
    def skirt_screw_z(self) -> float:
        return -(self.faceplate_thickness + 0.55 * self.skirt_depth)

    @property
    def ref_hole_z(self) -> float:
        """Room-side pressure reference vent through the east wall (exits
        into the frame/skirt gap above the boot line)."""
        return self.frame_top_z - 4.75


REGISTER_4X10 = RegisterParams()
REGISTER_4X12 = RegisterParams(duct_length=304.8)
