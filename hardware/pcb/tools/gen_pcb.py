"""Generate smart-register.kicad_pcb from design.py (pcbnew 7 API).

Flow: place footprints per PLACEMENT, build nets, outline, GND zones and the
antenna keep-out; export a Specctra DSN for Freerouting; re-run with --import
to pull the routed session back in, fill zones and write a DRC report.

Board contract with cad/smart_register/params.py: 58 x 38 mm outline,
M3 mounting holes inset 3.5 mm from each corner, ESP32-C6 antenna at the
left board edge.
"""

from __future__ import annotations

import os
import sys

import pcbnew
from pcbnew import VECTOR2I, FromMM

sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS, NC  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", "smart-register"))
PCB = os.path.join(PROJ, "smart-register.kicad_pcb")
FPLIBS = "/usr/share/kicad/footprints"
LOCAL_PRETTY = os.path.join(PROJ, "lib", "footprints", "SmartRegister.pretty")

W, H = 58.0, 38.0  # board size, mm
OX, OY = 100.0, 100.0  # board origin in sheet coords


def mm(x, y):
    return VECTOR2I(FromMM(OX + x), FromMM(OY + y))


# ref: (x, y, rot_deg, side)  — board-local mm, F=front
PLACEMENT = {
    "U1": (8.6, 19.0, 90, "F"),     # antenna (-Y in fp space) points -X, off left edge
    "J6": (9.5, 35.0, 90, "F"),     # debug header along bottom edge (pads run +X)
    # battery-sense cluster, top-left
    "Q2": (12.0, 4.0, 0, "F"),
    "Q3": (12.0, 9.0, 0, "F"),
    "R3": (16.5, 4.0, 90, "F"),
    "R1": (20.5, 4.0, 90, "F"),
    "R2": (24.5, 4.0, 90, "F"),
    "C16": (24.5, 8.5, 90, "F"),
    # USB block, top edge (rot 180 -> mating face off the top edge)
    "J1": (33.0, 2.4, 180, "F"),
    "R17": (26.5, 8.9, 90, "F"),
    "R18": (28.8, 8.9, 90, "F"),
    "C14": (38.5, 8.9, 90, "F"),
    "U6": (33.5, 9.2, 0, "F"),
    "SW1": (42.0, 2.5, 0, "F"),
    "SW2": (47.7, 2.5, 0, "F"),
    # power entry, right side
    "J2": (52.5, 14.0, 90, "F"),    # battery JST, pads run -Y, entry off right edge
    "Q1": (48.9, 6.5, 0, "F"),
    "D1": (43.2, 6.3, 0, "F"),
    "C1": (44.5, 10.7, 90, "F"),
    # buck block, centre
    "C2": (22.5, 13.0, 90, "F"),
    "U2": (26.5, 13.0, 0, "F"),
    "L1": (31.5, 13.5, 0, "F"),
    "C3": (35.5, 13.0, 90, "F"),
    "R4": (23.0, 17.6, 90, "F"),
    "R5": (25.0, 17.6, 90, "F"),
    "R6": (32.0, 17.6, 90, "F"),
    "R7": (34.5, 17.6, 90, "F"),
    # sensor switch + rail
    "U5": (20.5, 19.0, 0, "F"),
    "C11": (18.3, 22.5, 90, "F"),
    "C12": (28.0, 19.5, 0, "F"),
    # strap/reset passives near module
    "R15": (19.0, 27.5, 0, "F"),
    "R16": (19.0, 29.5, 0, "F"),
    "C6": (19.0, 31.5, 0, "F"),
    "C7": (14.5, 27.5, 0, "F"),
    # LED
    "R14": (12.0, 29.8, 0, "F"),
    "D2": (12.0, 31.8, 0, "F"),
    # motor load switch + bulk
    "U4": (41.5, 13.5, 0, "F"),
    "C15": (41.5, 10.5, 90, "F"),
    "C4": (47.0, 13.5, 90, "F"),
    "C5": (49.4, 13.5, 90, "F"),
    # stepper driver
    "U3": (42.5, 23.0, 0, "F"),
    "C8": (40.0, 17.5, 0, "F"),
    "C9": (44.0, 17.5, 0, "F"),
    "C10": (45.5, 21.5, 90, "B"),
    "R8": (37.5, 21.0, 90, "F"),
    "R9": (37.5, 29.3, 90, "F"),
    "R10": (39.5, 31.0, 90, "F"),
    "R11": (41.8, 31.0, 90, "F"),
    # pressure sensor + I2C
    "U7": (26.5, 27.0, 0, "F"),
    "R12": (34.0, 26.0, 90, "F"),
    "R13": (36.0, 26.0, 90, "F"),
    "C13": (38.0, 26.0, 90, "F"),
    # bottom-edge connectors (1xN pads run +X after rot 90)
    "J5": (21.2, 35.0, 90, "F"),
    "J4": (36.0, 34.8, 0, "F"),
    "J3": (51.0, 27.0, 90, "F"),    # motor JST is top-entry; pads run -Y
    # mounting holes
    "H1": (3.5, 3.5, 0, "F"),
    "H2": (54.5, 3.5, 0, "F"),
    "H3": (3.5, 34.5, 0, "F"),
    "H4": (54.5, 34.5, 0, "F"),
}


def check_placement(board):
    """Bounding-box sanity: pads on-board, footprints not overlapping."""
    import itertools

    problems = []
    boxes = {}
    edge_ok = {"J1", "J2"}  # edge connectors may overhang graphics
    sides = {}
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        bb = fp.GetBoundingBox(False, False)
        boxes[ref] = (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
                      pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))
        has_th = any(p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD for p in fp.Pads())
        sides[ref] = "TH" if has_th else ("B" if fp.IsFlipped() else "F")
        for pad in fp.Pads():
            pb = pad.GetBoundingBox()
            l, t = pcbnew.ToMM(pb.GetLeft()) - OX, pcbnew.ToMM(pb.GetTop()) - OY
            rr, b = pcbnew.ToMM(pb.GetRight()) - OX, pcbnew.ToMM(pb.GetBottom()) - OY
            m = 0.3
            if ref == "U1" and pad.GetPadName() == "":
                continue  # antenna-side paste/mask helpers
            if l < m or t < m or rr > W - m or b > H - m:
                problems.append(f"{ref} pad {pad.GetPadName()} near edge "
                                f"[{l:.2f},{t:.2f},{rr:.2f},{b:.2f}]")
    for (r1, b1), (r2, b2) in itertools.combinations(boxes.items(), 2):
        if r1 in edge_ok and r2 in edge_ok:
            continue
        if {sides[r1], sides[r2]} == {"F", "B"}:
            continue  # opposite sides, both SMD-only
        if b1[0] < b2[2] and b2[0] < b1[2] and b1[1] < b2[3] and b2[1] < b1[3]:
            ov_x = min(b1[2], b2[2]) - max(b1[0], b2[0])
            ov_y = min(b1[3], b2[3]) - max(b1[1], b2[1])
            if min(ov_x, ov_y) > 0.05:
                problems.append(f"overlap {r1}/{r2} ({ov_x:.2f}x{ov_y:.2f})")
    return problems


def load_footprint(fpid: str):
    lib, name = fpid.split(":")
    path = LOCAL_PRETTY if lib == "SmartRegister" else os.path.join(FPLIBS, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"cannot load footprint {fpid}")
    return fp


def build_board(with_zones=True):
    board = pcbnew.BOARD()
    bds = board.GetDesignSettings()
    bds.SetBoardThickness(FromMM(1.6))
    nc = bds.m_NetSettings.m_DefaultNetClass
    nc.SetClearance(FromMM(0.2))
    nc.SetTrackWidth(FromMM(0.3))
    nc.SetViaDiameter(FromMM(0.7))
    nc.SetViaDrill(FromMM(0.35))
    bds.m_TrackMinWidth = FromMM(0.2)
    bds.m_ViasMinSize = FromMM(0.6)
    bds.m_MinThroughDrill = FromMM(0.3)
    # 0.19: the stock GCT USB4105 footprint has 0.194 mm NPTH-to-shield-pad
    bds.m_HoleClearance = FromMM(0.19)
    bds.m_CopperEdgeClearance = FromMM(0.3)

    # fine-pitch class for the USB-C pad field (0.5 mm pitch, 0.3 mm pads)
    global _USB_NC_KEEPALIVE
    usb_nc = pcbnew.NETCLASS("USB")
    _USB_NC_KEEPALIVE = usb_nc  # SWIG: nets keep a raw ptr; keep python ref alive
    usb_nc.SetClearance(FromMM(0.13))
    usb_nc.SetTrackWidth(FromMM(0.2))
    usb_nc.SetViaDiameter(FromMM(0.6))
    usb_nc.SetViaDrill(FromMM(0.3))
    bds.m_NetSettings.m_NetClasses["USB"] = usb_nc
    USB_NETS = {"CC1", "CC2", "USB_DP", "USB_DN", "USB_DP_CONN", "USB_DN_CONN",
                "3V3", "SW_NODE", "BUCK_EN", "BUCK_FB"}

    # nets
    nets = {}
    for comp in COMPONENTS.values():
        for net in comp["pins"].values():
            if net != NC and net not in nets:
                item = pcbnew.NETINFO_ITEM(board, net)
                board.Add(item)
                if net in USB_NETS:
                    item.SetNetClass(bds.m_NetSettings.m_NetClasses["USB"])
                nets[net] = item

    # outline (rounded rect, r=2mm, as 4 lines + 4 arcs)
    r = 2.0
    edges = []
    lines = [((r, 0), (W - r, 0)), ((W, r), (W, H - r)),
             ((W - r, H), (r, H)), ((0, H - r), (0, r))]
    for (x1, y1), (x2, y2) in lines:
        seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(mm(x1, y1))
        seg.SetEnd(mm(x2, y2))
        edges.append(seg)
    arcs = [((r, r), (r, 0), (0, r)), ((W - r, r), (W, r), (W - r, 0)),
            ((W - r, H - r), (W - r, H), (W, H - r)), ((r, H - r), (0, H - r), (r, H))]
    for (cx, cy), (sx, sy), (ex, ey) in arcs:
        arc = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        arc.SetCenter(mm(cx, cy))
        arc.SetStart(mm(sx, sy))
        arc.SetEnd(mm(ex, ey))
        edges.append(arc)
    for e in edges:
        e.SetLayer(pcbnew.Edge_Cuts)
        e.SetWidth(FromMM(0.1))
        board.Add(e)

    # footprints
    for ref, comp in COMPONENTS.items():
        fp = load_footprint(comp["footprint"])
        fp.SetReference(ref)
        fp.SetValue(comp["value"])
        x, y, rot, side = PLACEMENT[ref]
        board.Add(fp)  # attach before Flip: board-less Flip segfaults in v7
        fp.SetPosition(mm(x, y))
        if side == "B":
            fp.Flip(mm(x, y), False)
        fp.SetOrientationDegrees(rot)
        for pad in fp.Pads():
            net = comp["pins"].get(str(pad.GetPadName()))
            if net and net != NC:
                pad.SetNet(nets[net])

    # GND zones on both copper layers (omitted for DSN export so the
    # autorouter routes GND as a net instead of assuming a plane)
    for layer in ((pcbnew.F_Cu, pcbnew.B_Cu) if with_zones else ()):
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(nets["GND"])
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in [(0, 0), (W, 0), (W, H), (0, H)]:
            outline.Append(FromMM(OX + x), FromMM(OY + y))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        zone.SetLocalClearance(FromMM(0.2))
        zone.SetMinThickness(FromMM(0.2))
        zone.SetThermalReliefGap(FromMM(0.3))
        zone.SetThermalReliefSpokeWidth(FromMM(0.4))
        board.Add(zone)

    # antenna keep-out (no copper, no tracks) under/left of the module antenna
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowCopperPour(True)
    ko.SetDoNotAllowTracks(True)
    ko.SetDoNotAllowVias(True)
    ko.SetDoNotAllowPads(False)
    ko.SetDoNotAllowFootprints(False)
    lset = pcbnew.LSET()
    lset.AddLayer(pcbnew.F_Cu)
    lset.AddLayer(pcbnew.B_Cu)
    ko.SetLayerSet(lset)
    out = ko.Outline()
    out.NewOutline()
    # module antenna section: fp -Y side rotated 90 -> board -X side of centre
    for x, y in [(0.0, 11.0), (6.3, 11.0), (6.3, 27.0), (0.0, 27.0)]:
        out.Append(FromMM(OX + x), FromMM(OY + y))
    board.Add(ko)

    tb = board.GetTitleBlock()
    tb.SetTitle("OpenRegister smart-register rev A")
    tb.SetCompany("OpenRegister project")
    tb.SetComment(0, "Zigbee smart HVAC register controller")
    return board


def main():
    board = build_board(with_zones=False)
    problems = check_placement(board)
    for p in problems:
        print("PLACEMENT:", p)
    if problems and "--force" not in sys.argv:
        raise SystemExit(f"{len(problems)} placement problems")
    board.SetFileName(PCB)
    pcbnew.SaveBoard(PCB, board)
    print(f"wrote {PCB}")
    dsn = os.path.join(PROJ, "smart-register.dsn")
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        raise SystemExit("DSN export failed")
    # the DSN exporter drops per-net class assignments; patch the USB nets
    # out of kicad_default and into the USB class by text surgery
    usb_nets = ["CC1", "CC2", "USB_DP", "USB_DN", "USB_DP_CONN", "USB_DN_CONN",
                "3V3", "SW_NODE", "BUCK_EN", "BUCK_FB"]
    txt = open(dsn).read()
    i = txt.index("(class kicad_default")
    j = txt.index("(circuit", i)
    head = txt[i:j]
    for n in sorted(usb_nets, key=len, reverse=True):
        head = head.replace(f" {n}\n", "\n").replace(f" {n} ", " ")
    txt = txt[:i] + head + txt[j:]
    txt = txt.replace("(class USB\n", "(class USB " + " ".join(usb_nets) + "\n", 1)
    open(dsn, "w").write(txt)
    print(f"wrote {dsn} (USB nets reclassed)")


if __name__ == "__main__":
    main()
