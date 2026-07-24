"""Build + parametric-sanity tests for the smart register CAD package."""

import math

import pytest

from export import PART_BUILDERS
from smart_register import (
    REGISTER_4X10,
    RegisterParams,
    assembly,
    boot_frame,
    faceplate,
    pinion,
    sector_gear,
    vane,
)

P = REGISTER_4X10


# ---------------------------------------------------------------- geometry
@pytest.mark.parametrize("name", sorted(PART_BUILDERS))
def test_part_builds_as_solid(name):
    part = PART_BUILDERS[name](P)
    assert part.volume > 100.0, f"{name} has implausible volume {part.volume}"
    bb = part.bounding_box()
    assert bb.size.X > 0 and bb.size.Y > 0 and bb.size.Z > 0


def test_assembly_builds():
    asm = assembly(P)
    assert len(asm.children) >= 11
    bb = asm.bounding_box()
    # overall envelope is the flange
    assert bb.size.X == pytest.approx(P.flange_l, abs=0.5)
    assert bb.size.Y == pytest.approx(P.flange_w, abs=0.5)


def test_assembly_no_part_interference():
    import itertools

    kids = {c.label: c for c in assembly(P).children}

    def ivol(a, b):
        inter = a.intersect(b)
        if inter is None:
            return 0.0
        try:
            return inter.volume
        except AttributeError:  # ShapeList
            return sum(s.volume for s in inter if hasattr(s, "volume"))

    overlaps = [
        (a, b, v)
        for a, b in itertools.combinations(kids, 2)
        if (v := ivol(kids[a], kids[b])) > 1e-6
    ]
    assert not overlaps, f"parts interfere: {overlaps}"


def test_gear_mesh_no_interference():
    from build123d import Pos

    sec = sector_gear(P).moved(Pos(P.sector_x0, P.master_y, P.vane_axis_z))
    pin = pinion(P).moved(Pos(P.pinion_x0, P.motor_axis_y, P.motor_axis_z))
    inter = sec.intersect(pin)
    vol = 0.0 if inter is None else inter.volume
    assert vol < 1e-6, f"gears interfere at closed pose (vol={vol})"


def test_mechanism_clear_through_travel():
    """Vanes, tie bar, sector and pinion must stay collision-free (against
    the frame, faceplate and each other) at mid-travel and full open."""
    import itertools

    from build123d import Pos, Rot

    from smart_register import tie_bar, vane
    from smart_register.boot_frame import boot_frame as build_frame

    frame = build_frame(P)
    face = faceplate(P)

    def ivol(a, b):
        inter = a.intersect(b)
        if inter is None:
            return 0.0
        try:
            return inter.volume
        except AttributeError:
            return sum(s.volume for s in inter if hasattr(s, "volume"))

    for trav in (45.0, P.vane_travel_deg):
        parts = {"frame": frame, "faceplate": face}
        a = P.vane_closed_tilt_deg + trav
        for i, y in enumerate(P.vane_ys):
            parts[f"vane{i}"] = vane(P, master=(i == P.master_index)).moved(
                Pos(0, y, P.vane_axis_z) * Rot(a, 0, 0)
            )
        parts["sector"] = sector_gear(P).moved(
            Pos(P.sector_x0, P.master_y, P.vane_axis_z) * Rot(trav, 0, 0)
        )
        parts["pinion"] = pinion(P).moved(
            Pos(P.pinion_x0, P.motor_axis_y, P.motor_axis_z)
            * Rot(-trav * P.gear_ratio, 0, 0)
        )
        d = math.radians(P.crank_closed_dir_deg + trav)
        parts["tiebar"] = tie_bar(P).moved(
            Pos(P.tie_bar_x0, P.crank_radius * math.cos(d),
                P.vane_axis_z + P.crank_radius * math.sin(d))
        )
        overlaps = [
            (x, y2, v)
            for x, y2 in itertools.combinations(parts, 2)
            if (v := ivol(parts[x], parts[y2])) > 1e-6
        ]
        assert not overlaps, f"interference at travel {trav} deg: {overlaps}"


# ---------------------------------------------------------------- parametrics
def test_skirt_fits_duct_opening():
    assert P.skirt_outer_l == pytest.approx(P.duct_length - 2 * P.skirt_clearance)
    assert P.skirt_outer_w == pytest.approx(P.duct_width - 2 * P.skirt_clearance)
    assert P.skirt_outer_l < P.duct_length
    assert P.skirt_outer_w < P.duct_width


def test_frame_fits_inside_skirt():
    assert P.frame_outer_l <= P.skirt_inner_l - 2 * P.fit_general + 1e-9
    assert P.frame_outer_w <= P.skirt_inner_w - 2 * P.fit_general + 1e-9


def test_vane_swing_stays_inside_frame():
    # worst case: outermost vane, blade edge at chord/2 from its axis
    reach = max(abs(y) for y in P.vane_ys) + P.vane_chord / 2
    assert reach < P.frame_inner_w / 2
    # and vertically: open blade stays below the faceplate, above frame bottom
    assert P.vane_axis_z + P.vane_chord / 2 < -P.faceplate_thickness
    assert P.vane_axis_z - P.vane_chord / 2 > P.airflow_bottom_z


def test_gear_center_distance():
    cd = P.gear_module * (P.pinion_teeth + P.sector_teeth) / 2
    assert abs(P.gear_center_distance - cd) < 1e-6
    # motor axis really is that far from the master vane axis
    d = math.hypot(P.motor_axis_y - P.master_y, P.motor_axis_z - P.vane_axis_z)
    assert d == pytest.approx(cd, abs=1e-6)


def test_sector_sweep_stays_inside_frame():
    def band_reach(lo, hi, r):
        zs = [r * math.sin(math.radians(a / 10)) for a in range(int(lo * 10), int(hi * 10) + 1)]
        return max(zs), min(zs)

    # toothed wedge over the full travel
    zmax, zmin = band_reach(*P.sector_band, P.sector_tip_r)
    assert P.vane_axis_z + zmax < P.frame_top_z  # never pokes the faceplate
    assert P.vane_axis_z + zmin > P.floor_top_z  # never hits the e-bay floor
    # limit tab over the full travel (smaller radius, may pass vertical)
    zmax, zmin = band_reach(*P.limit_tab_band, P.limit_tab_r1)
    assert P.vane_axis_z + zmax < P.frame_top_z
    assert P.vane_axis_z + zmin > P.floor_top_z


def test_pcb_standoffs_match_pcb_contract():
    pts = P.pcb_standoff_pts
    ys = sorted({round(y, 6) for (y, _) in pts})
    zs = sorted({round(z, 6) for (_, z) in pts})
    assert ys[1] - ys[0] == pytest.approx(P.pcb_size[0] - 2 * P.pcb_hole_inset)
    assert zs[1] - zs[0] == pytest.approx(P.pcb_size[1] - 2 * P.pcb_hole_inset)
    # antenna edge (PCB top) within the top 15 mm, above the boot line
    assert P.pcb_top_z > -15.0


def test_zone_lengths_sum_to_duct_length():
    assert P.airflow_zone_length + P.ebay_length == pytest.approx(P.duct_length)


def test_grille_open_area():
    assert P.grille_open_ratio >= 0.6


def test_wall_thicknesses_printable():
    assert P.wall >= 1.6
    assert P.vane_thickness >= 1.6
    assert P.tie_bar_t >= 1.6
    assert P.floor_thickness >= 1.6


# ---------------------------------------------------------------- 4x12 preset
def test_second_preset_is_parametric():
    p12 = RegisterParams(duct_length=304.8)
    assert p12.airflow_zone_length + p12.ebay_length == pytest.approx(304.8)
    assert p12.skirt_outer_l == pytest.approx(304.8 - 2 * p12.skirt_clearance)
    # key parts rebuild at the new size
    fp = faceplate(p12)
    assert fp.bounding_box().size.X == pytest.approx(p12.flange_l, abs=0.5)
    fr = boot_frame(p12)
    assert fr.bounding_box().size.X == pytest.approx(p12.frame_outer_l, abs=0.5)
    v = vane(p12)
    assert v.volume > 100.0
    asm = assembly(p12)
    assert len(asm.children) >= 11
