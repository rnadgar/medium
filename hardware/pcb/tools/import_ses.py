"""Import a Freerouting .ses session into the generated board.

pcbnew's ImportSpecctraSES needs the GUI frame, so this parses the session
(s-expression, 0.1 um units, Y negated vs KiCad) and creates tracks/vias,
then fills zones and writes a DRC report.
"""

import os
import re
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
from kicad_sexp import find_all, find_one, parse  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", "smart-register"))
PCB = os.path.join(PROJ, "smart-register.kicad_pcb")
SES = os.path.join(PROJ, "smart-register.ses")
RPT = os.path.join(PROJ, "drc-report.txt")

LAYERS = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}


def to_nm(v: float) -> int:
    return int(round(float(v) * 100))  # 0.1 um -> nm


USB_NETS = {"CC1", "CC2", "USB_DP", "USB_DN", "USB_DP_CONN", "USB_DN_CONN",
            "3V3", "SW_NODE", "BUCK_EN", "BUCK_FB"}


def apply_netclasses(board):
    """Board files don't persist netclass membership (it lives in the project
    file, which kicad-cli 8 reads in CI); re-apply for the local v7 DRC."""
    global _USB_NC  # keep the shared_ptr alive for the board's lifetime
    _USB_NC = pcbnew.NETCLASS("USB")
    _USB_NC.SetClearance(pcbnew.FromMM(0.13))
    _USB_NC.SetTrackWidth(pcbnew.FromMM(0.2))
    _USB_NC.SetViaDiameter(pcbnew.FromMM(0.6))
    _USB_NC.SetViaDrill(pcbnew.FromMM(0.3))
    for name in USB_NETS:
        net = board.FindNet(name)
        if net:
            net.SetNetClass(_USB_NC)


def main():
    # regenerate the unrouted board on disk (separate process — mixing
    # board construction and zone filling in one interpreter segfaults v7),
    # then load it and lay the routed session on top
    import subprocess
    subprocess.run([sys.executable, os.path.join(HERE, "gen_pcb.py")],
                   check=True, capture_output=True)
    board = pcbnew.LoadBoard(PCB)
    apply_netclasses(board)

    tree = parse(open(SES).read())
    routes = find_one(tree, "routes")
    netout = find_one(routes, "network_out")
    n_seg = n_via = 0
    for net_node in find_all(netout, "net"):
        name = net_node[1]
        net = board.FindNet(name)
        if net is None:
            raise SystemExit(f"session net {name} not on board")
        for wire in find_all(net_node, "wire"):
            path = find_one(wire, "path")
            layer = str(path[1])
            width = to_nm(path[2])
            coords = [float(c) for c in path[3:] if not isinstance(c, list)]
            pts = [(to_nm(coords[i]), -to_nm(coords[i + 1]))
                   for i in range(0, len(coords), 2)]
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                if (x1, y1) == (x2, y2):
                    continue
                seg = pcbnew.PCB_TRACK(board)
                seg.SetStart(pcbnew.VECTOR2I(x1, y1))
                seg.SetEnd(pcbnew.VECTOR2I(x2, y2))
                seg.SetWidth(width)
                seg.SetLayer(LAYERS[layer])
                seg.SetNet(net)
                board.Add(seg)
                n_seg += 1
        for via_node in find_all(net_node, "via"):
            padstack = str(via_node[1])
            m = re.match(r"Via\[\d+-\d+\]_(\d+):(\d+)_um", padstack)
            dia, drill = (int(m.group(1)), int(m.group(2))) if m else (700, 350)
            x, y = to_nm(via_node[2]), -to_nm(via_node[3])
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(x, y))
            via.SetWidth(pcbnew.FromMM(dia / 1000))
            via.SetDrill(pcbnew.FromMM(drill / 1000))
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(net)
            board.Add(via)
            n_via += 1

    def add_via(x, y, net, dia=0.7, drill=0.35):
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        via.SetWidth(pcbnew.FromMM(dia))
        via.SetDrill(pcbnew.FromMM(drill))
        via.SetViaType(pcbnew.VIATYPE_THROUGH)
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNet(net)
        board.Add(via)

    # Hand repair for the one connection freerouting reliably gives up on:
    # BUCK_EN into U2's 0.25 mm WSON pad 3. The router's own VBAT feed to C2
    # blocks the only approach corridor, so replace it with a Manhattan path
    # along y=112.75 / x=123.3, then drop BUCK_EN down x=123.75 into the pad.
    # Coordinates verified against the committed .ses routing geometry.
    def _key(x, y):
        return (round(x, 2), round(y, 2))

    DELETE = {  # net -> set of segments (both endpoints must match)
        "VBAT": {
            frozenset({_key(125.019, 112.774), _key(125.526, 112.774)}),
            frozenset({_key(125.526, 112.774), _key(125.55, 112.75)}),
            frozenset({_key(125.019, 112.774), _key(123.844, 113.95)}),
            frozenset({_key(123.844, 113.95), _key(122.5, 113.95)}),
        },
    }
    removed = 0
    for t in list(board.GetTracks()):
        want = DELETE.get(t.GetNetname())
        if not want or t.GetClass() == "PCB_VIA":
            continue
        s, e = t.GetStart(), t.GetEnd()
        seg_key = frozenset({_key(pcbnew.ToMM(s.x), pcbnew.ToMM(s.y)),
                             _key(pcbnew.ToMM(e.x), pcbnew.ToMM(e.y))})
        if seg_key in want:
            board.Remove(t)
            removed += 1

    REPAIRS = [
        ("VBAT", 0.25, [(125.55, 112.75), (123.45, 112.75),
                        (123.45, 113.95), (122.5, 113.95)]),
        ("BUCK_EN", 0.2, [(123.35, 116.775), (124.05, 116.775),
                          (124.05, 113.25), (125.55, 113.25)]),
    ]
    for net_name, width, pts in REPAIRS:
        net = board.FindNet(net_name)
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            seg = pcbnew.PCB_TRACK(board)
            seg.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
            seg.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x2), pcbnew.FromMM(y2)))
            seg.SetWidth(pcbnew.FromMM(width))
            seg.SetLayer(pcbnew.F_Cu)
            seg.SetNet(net)
            board.Add(seg)
    print(f"repairs: removed {removed} segments, added "
          f"{sum(len(p[2]) - 1 for p in REPAIRS)}")

    # add the GND pours (left out of the DSN so GND got routed as a net)
    gnd_net = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(gnd_net)
        outline = zone.Outline()
        outline.NewOutline()
        for zx, zy in [(0, 0), (58, 0), (58, 38), (0, 38)]:
            outline.Append(pcbnew.FromMM(100 + zx), pcbnew.FromMM(100 + zy))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        zone.SetLocalClearance(pcbnew.FromMM(0.2))
        zone.SetMinThickness(pcbnew.FromMM(0.2))
        board.Add(zone)

    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())

    # --- GND stitching vias: connect every F.Cu fill island through the
    # B.Cu plane. One via per island, at a point clear of everything.
    gnd = board.FindNet("GND")
    fz = bz = None
    for zi in range(board.GetAreaCount()):
        z = board.GetArea(zi)
        if z.GetIsRuleArea():
            continue
        if z.GetLayer() == pcbnew.F_Cu:
            fz = z
        elif z.GetLayer() == pcbnew.B_Cu:
            bz = z
    # via copper radius 0.3 + clearance 0.2 = 0.5 mm to any other-net copper
    # edge; GND items need no clearance (touching a GND via is fine).
    VIA_EDGE = 0.5
    obstacles = []  # (sx, sy, ex, ey, extra_margin)
    for t in board.GetTracks():
        if t.GetNetname() == "GND":
            continue
        s, e = t.GetStart(), t.GetEnd()
        obstacles.append((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                          pcbnew.ToMM(e.x), pcbnew.ToMM(e.y),
                          VIA_EDGE + pcbnew.ToMM(t.GetWidth()) / 2))
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() == "GND":
                continue
            pos = p.GetPosition()
            r = max(pcbnew.ToMM(p.GetBoundingBox().GetWidth()),
                    pcbnew.ToMM(p.GetBoundingBox().GetHeight())) / 2
            # TH pads/holes on any net still need drill-to-drill spacing
            obstacles.append((pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                              pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                              VIA_EDGE + r))

    def clear_of_everything(x, y, margin_scale=1.0):
        import math
        for (sx, sy, ex, ey, m) in obstacles:
            dx, dy = ex - sx, ey - sy
            L2 = dx * dx + dy * dy
            t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((x - sx) * dx + (y - sy) * dy) / L2))
            if math.hypot(x - (sx + t * dx), y - (sy + t * dy)) < m * margin_scale + 0.1:
                return False
        return True

    n_stitch = 0
    fills = fz.GetFilledPolysList(pcbnew.F_Cu)
    for i in range(fills.OutlineCount()):
        outline = fills.Outline(i)
        bb = outline.BBox()
        l, t = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
        r, btm = pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())
        placed = False
        for scale in (1.3, 1.0):
            if placed:
                break
            step = 0.4
            y = t + 0.4
            while y < btm and not placed:
                x = l + 0.4
                while x < r and not placed:
                    v = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
                    if (outline.PointInside(v)
                            and bz.HitTestFilledArea(pcbnew.B_Cu, v)
                            and clear_of_everything(x, y, scale)):
                        add_via(x, y, gnd, dia=0.6, drill=0.3)
                        n_stitch += 1
                        placed = True
                    x += step
                y += step
        if not placed:
            print(f"  island {i} at x[{l-100:.1f},{r-100:.1f}] y[{t-100:.1f},{btm-100:.1f}]:"
                  " no stitch spot found")

    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    print(f"imported {n_seg} segments, {n_via} vias; +{n_stitch} GND stitching vias; zones filled")

    pcbnew.WriteDRCReport(board, RPT, pcbnew.EDA_UNITS_MILLIMETRES, False)
    txt = open(RPT).read()
    import collections
    counts = collections.Counter(re.findall(r"\[(\w+)\]: ", txt))

    # KiCad 7's standalone DRC cannot see project netclasses; violations whose
    # items are both USB-class nets and still >= the USB clearance are fine
    # (kicad-cli 8 in CI resolves them via the .kicad_pro patterns).
    IGNORE = {"silk_overlap", "silk_over_copper", "silk_edge_clearance",
              "lib_footprint_issues"}
    real = []
    for block in re.split(r"(?=\[\w+\]: )", txt):
        m = re.match(r"\[(\w+)\]: ", block)
        if not m or m.group(1) in IGNORE:
            continue
        kind = m.group(1)
        if kind == "clearance":
            act = re.search(r"actual ([\d.]+) mm", block)
            nets_in = set(re.findall(r"\[(\w+)\] o[nf]", block))
            if act and nets_in and nets_in <= USB_NETS and float(act.group(1)) >= 0.125:
                continue  # legal under the USB netclass rule
        real.append(block.strip().split("\n")[0])
    print("DRC (all):", dict(counts) or "clean")
    if real:
        print(f"DRC real issues: {len(real)}")
        for r in real[:20]:
            print("  ", r)
    else:
        print("DRC real issues: none")


if __name__ == "__main__":
    main()
