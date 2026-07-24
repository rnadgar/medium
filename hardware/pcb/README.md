# smart-register rev A — PCB

58 × 38 mm 2-layer board for the e-bay of the smart register. ESP32-C6-MINI-1
(Zigbee), TPS62125 always-on 3.3 V buck, DRV8833 stepper driver on a
TPS22810-switched battery rail, SiP32431-switched 3.3 V sensor rail,
XGZP6897D on-board differential pressure sensor (or SDP810 on the J5 remote
header), gated battery divider, limit-switch inputs, USB-C flash/debug.

## Layout facts

- Outline/mounting contract with `cad/smart_register/params.py`:
  58 × 38 mm, M3 holes inset 3.5 mm from each corner.
- ESP32-C6 antenna at the **left board edge** with a copper/track keep-out
  under it; the CAD e-bay mounts the board vertically so this edge sits in
  the top 15 mm of the register (above the steel boot line).
- Net classes: `Default` 0.3 mm / 0.2 mm clearance; `USB` (fine-pitch USB-C
  and WSON buck nets) 0.2 mm / 0.13 mm.
- DRC: clean under KiCad 8 with the project file's net classes and severity
  settings (silk cosmetics and lib checks off — all footprints are embedded).

## Regenerating everything

The whole board is **generated from code** — do not hand-edit the outputs;
edit `tools/design.py` (netlist), `tools/gen_pcb.py` (placement), or the
generators, then:

```bash
cd hardware/pcb/tools
python3 design.py            # validate the canonical netlist
python3 gen_project.py       # .kicad_pro + lib tables
python3 gen_schematic.py     # smart-register.kicad_sch
python3 check_netlist.py     # schematic netlist == design.py (kicad-cli)
python3 gen_pcb.py           # placed board + Specctra DSN
java -jar freerouting.jar -de ../smart-register/smart-register.dsn \
     -do ../smart-register/smart-register.ses -mp 30
python3 import_ses.py        # tracks/vias + repairs + pours + stitching + DRC
python3 gen_fab.py           # gerbers/drill/BOM/CPL into ../fab/
```

Requires KiCad 7+ (pcbnew python + kicad-cli) and Java 17+ for
[Freerouting](https://github.com/freerouting/freerouting). The committed
`smart-register.ses` reproduces the committed routing exactly; only re-run
Freerouting if you change the design, and re-check DRC afterwards
(`import_ses.py` prints "DRC real issues: none" on success — its hand-repair
coordinates in `REPAIRS` are tied to the committed session and must be
revisited after any re-route).

## Ordering (JLCPCB)

1. Upload `fab/smart-register-gerbers.zip` — 2-layer, 1.6 mm FR4, any color,
   HASL or ENIG, no special options.
2. Optional assembly: `fab/bom.csv` + `fab/cpl.csv` (verify rotations in
   their preview — generated rotations follow KiCad, JLC sometimes differs
   per package).
3. **Order after validating firmware on the dev bench** (see
   `docs/bom.md` section E) — the board is validation, not the critical path.

## Bring-up checklist

1. Visual + shorts check (VBAT–GND, 3V3–GND).
2. Batteries in (or USB — SS14 feeds the pack rail at ~4.7 V for bench work):
   3.3 V rail present, ~40 µA sleep draw after flashing.
3. Flash via USB-C (`idf.py -p /dev/ttyACM0 flash` — USB-Serial-JTAG).
4. `MOTOR_PWR_EN` high → VMOT = pack voltage, sensor rail 3.3 V.
5. Motor moves, limit switches read, pressure sample returns plausible Pa.
6. Join Zigbee, then the `docs/pairing-setup.md` flow.
