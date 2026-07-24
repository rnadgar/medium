"""Generate fabrication outputs: gerbers + drill (kicad-cli), BOM CSV and
JLCPCB-style CPL (pick-and-place) into hardware/pcb/fab/."""

import csv
import os
import subprocess
import sys
import zipfile

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", "smart-register"))
PCB = os.path.join(PROJ, "smart-register.kicad_pcb")
FAB = os.path.normpath(os.path.join(HERE, "..", "fab"))


def gerbers():
    gdir = os.path.join(FAB, "gerbers")
    os.makedirs(gdir, exist_ok=True)
    for f in os.listdir(gdir):
        os.remove(os.path.join(gdir, f))
    subprocess.run(
        ["kicad-cli", "pcb", "export", "gerbers",
         "--layers", "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,"
                     "F.Mask,B.Mask,Edge.Cuts",
         "--subtract-soldermask", "-o", gdir + "/", PCB],
        check=True,
    )
    subprocess.run(
        ["kicad-cli", "pcb", "export", "drill", "--format", "excellon",
         "--excellon-separate-th", "-o", gdir + "/", PCB],
        check=True,
    )
    zpath = os.path.join(FAB, "smart-register-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    print(f"wrote {zpath}")


def bom():
    groups = {}
    for ref, c in COMPONENTS.items():
        if ref.startswith("H"):
            continue
        key = (c["value"], c["footprint"], c["mpn"])
        groups.setdefault(key, []).append(ref)
    path = os.path.join(FAB, "bom.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "MPN", "Qty"])
        for (value, fp, mpn), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([value, ",".join(sorted(refs)), fp.split(":")[1], mpn,
                        len(refs)])
    print(f"wrote {path}")


def cpl():
    board = pcbnew.LoadBoard(PCB)
    path = os.path.join(FAB, "cpl.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Val", "Package", "Mid X", "Mid Y",
                    "Rotation", "Layer"])
        for fp in sorted(board.GetFootprints(), key=lambda m: m.GetReference()):
            ref = fp.GetReference()
            if ref.startswith("H"):
                continue
            pos = fp.GetPosition()
            w.writerow([
                ref, fp.GetValue(),
                str(fp.GetFPID().GetLibItemName()),
                f"{pcbnew.ToMM(pos.x) - 100.0:.3f}mm",
                f"{-(pcbnew.ToMM(pos.y) - 100.0):.3f}mm",  # JLC Y-up origin
                f"{fp.GetOrientationDegrees():.0f}",
                "Bottom" if fp.IsFlipped() else "Top",
            ])
    print(f"wrote {path}")


if __name__ == "__main__":
    os.makedirs(FAB, exist_ok=True)
    gerbers()
    bom()
    cpl()
