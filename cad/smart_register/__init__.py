"""Parametric build123d CAD package for the open-hardware smart HVAC register."""

from .assembly import assembly
from .boot_frame import boot_frame
from .drivetrain import motor_placeholder, pinion, sector_gear
from .ebay import battery_door, battery_placeholder, battery_tray
from .faceplate import faceplate
from .params import REGISTER_4X10, REGISTER_4X12, RegisterParams
from .vanes import master_vane, tie_bar, vane

__all__ = [
    "RegisterParams",
    "REGISTER_4X10",
    "REGISTER_4X12",
    "assembly",
    "boot_frame",
    "faceplate",
    "vane",
    "master_vane",
    "tie_bar",
    "pinion",
    "sector_gear",
    "motor_placeholder",
    "battery_tray",
    "battery_door",
    "battery_placeholder",
]
