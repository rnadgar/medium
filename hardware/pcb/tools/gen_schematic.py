"""Generate smart-register.kicad_sch (KiCad 7 format) from design.py.

Connectivity style: every connected pin gets a global label placed exactly
on the pin's connection point (KiCad connects coincident label/pin points).
Library symbols are embedded verbatim from the installed KiCad 7 libraries
(derived symbols are flattened); the ESP32-C6-MINI-1 symbol is generated
from the pin table extracted from Espressif's official library.
"""

from __future__ import annotations

import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS, NC, POWER_NETS, PWR_FLAG_NETS  # noqa: E402
from kicad_sexp import (  # noqa: E402
    Sym,
    dump,
    find_all,
    find_one,
    get_symbol_block,
    parse,
    symbol_pins,
)

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", "smart-register"))
SYSLIB = "/usr/share/kicad/symbols"

LIB_FILES = {
    "Regulator_Switching": "Regulator_Switching.kicad_sym",
    "Driver_Motor": "Driver_Motor.kicad_sym",
    "Power_Management": "Power_Management.kicad_sym",
    "Power_Protection": "Power_Protection.kicad_sym",
    "Transistor_FET": "Transistor_FET.kicad_sym",
    "Sensor_Pressure": "Sensor_Pressure.kicad_sym",
    "Device": "Device.kicad_sym",
    "Switch": "Switch.kicad_sym",
    "Connector": "Connector.kicad_sym",
    "Connector_Generic": "Connector_Generic.kicad_sym",
    "Mechanical": "Mechanical.kicad_sym",
    "power": "power.kicad_sym",
}

ROOT_UUID = "e63e39d7-6ac0-4ffd-8aa3-1841a4541b55"  # stable across regens


def su():  # stable-ish uuids are not required; fresh ones are fine
    return str(uuid.uuid4())


# ---------------------------------------------------------------- ESP32 symbol

# (number, name, etype) — extracted from Espressif's official symbol library.
ESP32_PINS = [
    ("1", "GND", "power_in"), ("2", "GND", "passive"), ("3", "3V3", "power_in"),
    ("4", "NC", "no_connect"), ("5", "GPIO2", "bidirectional"),
    ("6", "GPIO3", "bidirectional"), ("7", "NC", "no_connect"),
    ("8", "EN", "input"), ("9", "GPIO4", "bidirectional"),
    ("10", "GPIO5", "bidirectional"), ("11", "GND", "passive"),
    ("12", "GPIO0", "bidirectional"), ("13", "GPIO1", "bidirectional"),
    ("14", "GND", "passive"), ("15", "GPIO6", "bidirectional"),
    ("16", "GPIO7", "bidirectional"), ("17", "GPIO12/USB_D-", "bidirectional"),
    ("18", "GPIO13/USB_D+", "bidirectional"), ("19", "GPIO14", "bidirectional"),
    ("20", "GPIO15", "bidirectional"), ("21", "NC", "no_connect"),
    ("22", "GPIO8", "bidirectional"), ("23", "GPIO9", "bidirectional"),
    ("24", "GPIO18", "bidirectional"), ("25", "GPIO19", "bidirectional"),
    ("26", "GPIO20", "bidirectional"), ("27", "GPIO21", "bidirectional"),
    ("28", "GPIO22", "bidirectional"), ("29", "GPIO23", "bidirectional"),
    ("30", "U0RXD/GPIO17", "bidirectional"), ("31", "U0TXD/GPIO16", "bidirectional"),
    ("32", "NC", "no_connect"), ("33", "NC", "no_connect"),
    ("34", "NC", "no_connect"), ("35", "NC", "no_connect"),
] + [(str(n), "GND", "passive") for n in range(36, 54)]

LEFT = ["3", "8", "23", "22", "17", "18", "30", "31"]
RIGHT = ["5", "6", "9", "10", "12", "13", "15", "16", "19", "20",
         "24", "25", "26", "27", "28", "29"]


def esp32_symbol_positions():
    """Return {pad: (x, y, angle)} in symbol space (Y up)."""
    pos = {}
    top = 27.94
    for i, num in enumerate(LEFT):
        pos[num] = (-20.32, top - 2.54 * i, 0.0)
    for i, num in enumerate(RIGHT):
        pos[num] = (20.32, top - 2.54 * i, 180.0)
    # GND stack: pin 1 plus all duplicates at the same point (net-tied)
    for num, name, _ in ESP32_PINS:
        if name == "GND":
            pos[num] = (0.0, -40.64, 90.0)
    # NC pins spread along the bottom, distinct points so they don't tie
    ncs = [num for num, name, _ in ESP32_PINS if name == "NC"]
    for i, num in enumerate(ncs):
        pos[num] = (-12.7 + 2.54 * i, -40.64, 90.0)
    return pos


def esp32_symbol_sexp() -> str:
    pos = esp32_symbol_positions()
    body = ['(symbol "ESP32-C6-MINI-1_0_1"',
            '  (rectangle (start -15.24 35.56) (end 15.24 -35.56)'
            ' (stroke (width 0.254) (type default))'
            ' (fill (type background)))', ')']
    pins = ['(symbol "ESP32-C6-MINI-1_1_1"']
    for num, name, etype in ESP32_PINS:
        x, y, ang = pos[num]
        hide = " hide" if name in ("GND", "NC") and num not in ("1",) else ""
        pins.append(
            f'  (pin {etype} line (at {x:g} {y:g} {ang:g}) (length 5.08){hide}\n'
            f'    (name "{name}" (effects (font (size 1.27 1.27))))\n'
            f'    (number "{num}" (effects (font (size 1.27 1.27)))))'
        )
    pins.append(")")
    props = [
        '(property "Reference" "U" (at 0 36.83 0)'
        ' (effects (font (size 1.27 1.27))))',
        '(property "Value" "ESP32-C6-MINI-1" (at 0 -38.1 0)'
        ' (effects (font (size 1.27 1.27))))',
        '(property "Footprint" "SmartRegister:ESP32-C6-MINI-1" (at 0 0 0)'
        ' (effects (font (size 1.27 1.27)) hide))',
        '(property "Datasheet" "https://www.espressif.com/sites/default/files/'
        'documentation/esp32-c6-mini-1_mini-1u_datasheet_en.pdf" (at 0 0 0)'
        ' (effects (font (size 1.27 1.27)) hide))',
    ]
    return (
        '(symbol "ESP32-C6-MINI-1" (in_bom yes) (on_board yes)\n'
        '  (pin_names (offset 1.016)) (pin_numbers hide)\n  '
        + "\n  ".join(props) + "\n  "
        + "\n  ".join(body) + "\n  "
        + "\n  ".join(pins) + "\n)"
    )


# --------------------------------------------------------------- lib handling

_lib_cache: dict[str, str] = {}


def lib_text(libname: str) -> str:
    if libname not in _lib_cache:
        _lib_cache[libname] = open(os.path.join(SYSLIB, LIB_FILES[libname])).read()
    return _lib_cache[libname]


def flatten_symbol(libname: str, symname: str):
    """Return (sexp_text, {pad:(x,y,ang,etype)}) with `extends` resolved."""
    if libname == "SmartRegister":
        txt = esp32_symbol_sexp()
        node = parse(txt)
        pins = {n: (x, y, a, t) for n, _, t, x, y, a in
                [(p[0], p[1], p[2], p[3], p[4], p[5]) for p in symbol_pins(node)]}
        return txt, pins

    txt = lib_text(libname)
    node = parse(get_symbol_block(txt, symname))
    ext = find_one(node, "extends")
    if ext:
        parent_name = str(ext[1])
        parent = parse(get_symbol_block(txt, parent_name))
        # graft parent sub-symbols (renamed) + pin metadata onto the child
        node = [c for c in node if not (isinstance(c, list) and c and str(c[0]) == "extends")]
        child_props = {str(p[1]) for p in find_all(node, "property")}
        for c in parent:
            if not isinstance(c, list) or not c:
                continue
            head = str(c[0])
            if head == "symbol":
                sub = [x for x in c]
                sub[1] = symname + sub[1][len(parent_name):]
                node.append(sub)
            elif head in ("pin_names", "pin_numbers"):
                node.append(c)
            elif head == "property" and c[1] not in child_props:
                node.append(c)
    pins = {}
    for num, _name, etype, x, y, ang in symbol_pins(node):
        pins.setdefault(num, (x, y, ang, etype))
    return dump(parse(dump(node))), pins


# ------------------------------------------------------------------ generator

LABEL_JUSTIFY = {0: "left", 90: "left", 180: "right", 270: "right"}


def global_label(net: str, x: float, y: float, ang: float) -> str:
    shape = "passive"
    just = LABEL_JUSTIFY[int(ang) % 360]
    return (
        f'(global_label "{net}" (shape {shape}) (at {x:.2f} {y:.2f} {ang:g})'
        f' (fields_autoplaced)\n'
        f'  (effects (font (size 1.27 1.27)) (justify {just}))\n'
        f'  (uuid {su()})\n'
        f'  (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x:.2f} {y:.2f} 0)\n'
        f'    (effects (font (size 1.27 1.27)) hide)))'
    )


def instance_sexp(ref: str, comp: dict, pins_meta: dict) -> str:
    x, y = comp["sch"]
    lines = [
        f'(symbol (lib_id "{comp["lib"]}:{comp["sym"]}") (at {x:g} {y:g} 0) (unit 1)',
        "  (in_bom yes) (on_board yes) (dnp no)",
        f"  (uuid {su()})",
        f'  (property "Reference" "{ref}" (at {x:g} {y - 2:g} 0)'
        f' (effects (font (size 1.27 1.27))))',
        f'  (property "Value" "{comp["value"]}" (at {x:g} {y + 2:g} 0)'
        f' (effects (font (size 1.27 1.27))))',
        f'  (property "Footprint" "{comp["footprint"]}" (at {x:g} {y:g} 0)'
        f' (effects (font (size 1.27 1.27)) hide))',
        f'  (property "Datasheet" "" (at {x:g} {y:g} 0)'
        f' (effects (font (size 1.27 1.27)) hide))',
        f'  (property "MPN" "{comp["mpn"]}" (at {x:g} {y:g} 0)'
        f' (effects (font (size 1.27 1.27)) hide))',
    ]
    for pad in pins_meta:
        lines.append(f"  (pin \"{pad}\" (uuid {su()}))")
    lines.append(
        f'  (instances (project "smart-register"'
        f' (path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))'
    )
    lines.append(")")
    return "\n".join(lines)


def main():
    os.makedirs(PROJ, exist_ok=True)
    lib_symbols: dict[str, str] = {}
    placed: list[str] = []
    labels: list[str] = []
    no_connects: list[str] = []

    for ref, comp in COMPONENTS.items():
        key = f'{comp["lib"]}:{comp["sym"]}'
        sym_txt, pins_meta = flatten_symbol(comp["lib"], comp["sym"])
        if key not in lib_symbols:
            # rename top-level symbol to its full lib id
            node = parse(sym_txt)
            node[1] = key
            lib_symbols[key] = dump(node, 1)
        placed.append(instance_sexp(ref, comp, pins_meta))
        x0, y0 = comp["sch"]
        for pad, (px, py, pang, etype) in pins_meta.items():
            sx, sy = x0 + px, y0 - py
            net = comp["pins"].get(pad)
            if net is None:
                raise SystemExit(f"{ref}: pad {pad} missing from design pins")
            if net == NC:
                if etype != "no_connect":
                    no_connects.append(f"(no_connect (at {sx:.2f} {sy:.2f}) (uuid {su()}))")
                continue
            away = (pang + 180.0) % 360.0
            labels.append(global_label(net, sx, sy, away))
        extra = set(comp["pins"]) - set(pins_meta)
        if extra:
            raise SystemExit(f"{ref}: design pins not in symbol: {extra}")

    # PWR_FLAG row
    key = "power:PWR_FLAG"
    flag_txt, flag_pins = flatten_symbol("power", "PWR_FLAG")
    node = parse(flag_txt)
    node[1] = key
    lib_symbols[key] = dump(node, 1)
    fx, fy = 40, 270
    for i, net in enumerate(PWR_FLAG_NETS):
        x = fx + 20 * i
        ref = f"#FLG0{i + 1}"
        comp = dict(lib="power", sym="PWR_FLAG", value="PWR_FLAG",
                    footprint="", mpn="-", sch=(x, fy), pins={"1": net})
        placed.append(instance_sexp(ref, comp, flag_pins))
        px, py, pang, _ = flag_pins["1"]
        labels.append(global_label(net, x + px, fy - py, (pang + 180) % 360))

    sch = [
        "(kicad_sch (version 20230121) (generator gen_schematic)",
        f"  (uuid {ROOT_UUID})",
        '  (paper "A2")',
        "  (lib_symbols",
    ]
    for txt in lib_symbols.values():
        sch.append("    " + txt.replace("\n", "\n    "))
    sch.append("  )")
    for nc_marker in no_connects:
        sch.append("  " + nc_marker)
    for lbl in labels:
        sch.append("  " + lbl.replace("\n", "\n  "))
    for inst in placed:
        sch.append("  " + inst.replace("\n", "\n  "))
    sch.append('  (sheet_instances (path "/" (page "1")))')
    sch.append(")")

    out = os.path.join(PROJ, "smart-register.kicad_sch")
    with open(out, "w") as f:
        f.write("\n".join(sch) + "\n")
    print(f"wrote {out}: {len(COMPONENTS)} components, {len(labels)} labels, "
          f"{len(no_connects)} NC markers")


if __name__ == "__main__":
    main()
