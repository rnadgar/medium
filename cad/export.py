#!/usr/bin/env python3.11
"""Export every part of the smart register to STL + STEP.

Run:  python3 cad/export.py
Outputs: cad/out/stl/*.stl, cad/out/step/*.step and cad/out/step/assembly.step
"""

from __future__ import annotations

import sys
from pathlib import Path

CAD_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(CAD_DIR))

from build123d import export_step, export_stl  # noqa: E402

from smart_register import (  # noqa: E402
    REGISTER_4X10,
    assembly,
    battery_door,
    battery_placeholder,
    battery_tray,
    boot_frame,
    faceplate,
    master_vane,
    motor_placeholder,
    pinion,
    sector_gear,
    tie_bar,
    vane,
)

PART_BUILDERS = {
    "faceplate": faceplate,
    "boot_frame": boot_frame,
    "vane": vane,
    "master_vane": master_vane,
    "tie_bar": tie_bar,
    "pinion": pinion,
    "sector_gear": sector_gear,
    "battery_tray": battery_tray,
    "battery_door": battery_door,
    "motor_placeholder": motor_placeholder,
    "battery_placeholder": battery_placeholder,
}


def main() -> int:
    p = REGISTER_4X10
    stl_dir = CAD_DIR / "out" / "stl"
    step_dir = CAD_DIR / "out" / "step"
    stl_dir.mkdir(parents=True, exist_ok=True)
    step_dir.mkdir(parents=True, exist_ok=True)

    for name, builder in PART_BUILDERS.items():
        part = builder(p)
        export_stl(part, str(stl_dir / f"{name}.stl"))
        export_step(part, str(step_dir / f"{name}.step"))
        bb = part.bounding_box()
        print(
            f"{name:20s} vol={part.volume:10.0f} mm^3   "
            f"bbox {bb.size.X:7.2f} x {bb.size.Y:7.2f} x {bb.size.Z:7.2f} mm"
        )

    asm = assembly(p)
    export_step(asm, str(step_dir / "assembly.step"))
    bb = asm.bounding_box()
    print(
        f"{'assembly':20s} parts={len(asm.children):2d}          "
        f"bbox {bb.size.X:7.2f} x {bb.size.Y:7.2f} x {bb.size.Z:7.2f} mm"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
