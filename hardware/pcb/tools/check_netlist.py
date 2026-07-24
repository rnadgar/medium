"""Verify the generated schematic's netlist against design.py.

Runs kicad-cli to export the netlist, then compares every net's set of
(ref, pad) members. Net names may differ only for KiCad-autonamed nets;
here everything is globally labeled so names must match exactly.
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS, NC, all_nets  # noqa: E402
from kicad_sexp import find_all, parse  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.normpath(os.path.join(HERE, "..", "smart-register", "smart-register.kicad_sch"))


def exported_nets():
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "out.net")
        subprocess.run(
            ["kicad-cli", "sch", "export", "netlist", "-o", out, SCH],
            check=True, capture_output=True,
        )
        tree = parse(open(out).read())
    nets = {}
    nets_node = None
    for c in find_all(tree, "nets"):
        nets_node = c
    for net in find_all(nets_node, "net"):
        name = None
        members = set()
        for item in net:
            if isinstance(item, list):
                if str(item[0]) == "name":
                    name = item[1]
                elif str(item[0]) == "node":
                    ref = pad = None
                    for f in item:
                        if isinstance(f, list):
                            if str(f[0]) == "ref":
                                ref = f[1]
                            elif str(f[0]) == "pin":
                                pad = f[1]
                    members.add((ref, pad))
        # strip sheet-path prefix on autonamed nets, PWR_FLAG refs
        members = {m for m in members if not m[0].startswith("#")}
        if members:
            nets[name] = members
    return nets


def main():
    want = {n: set(p) for n, p in all_nets().items()}
    got = exported_nets()
    ok = True
    for name, members in sorted(want.items()):
        g = got.pop(name, None)
        if g is None:
            print(f"MISSING net {name}")
            ok = False
        elif g != members:
            print(f"MISMATCH {name}:\n  extra in sch: {sorted(g - members)}"
                  f"\n  missing in sch: {sorted(members - g)}")
            ok = False
    nc_pins = {(ref, pad) for ref, comp in COMPONENTS.items()
               for pad, net in comp["pins"].items() if net == NC}
    for name, members in got.items():
        if name.startswith("unconnected-") and members <= nc_pins:
            nc_pins -= members
            continue
        print(f"UNEXPECTED net {name}: {sorted(members)}")
        ok = False
    print("NETLIST OK" if ok else "NETLIST FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
