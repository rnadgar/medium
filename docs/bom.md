# Bill of Materials

Everything needed to build **one** smart register, plus a one-time dev-bench
section for firmware bring-up. Prices are mid-2026 estimates in USD at qty 1;
order chips by **manufacturer part number (MPN)** at Digi-Key/Mouser (search
the MPN directly), or by LCSC number if you have JLCPCB assemble the board.

> **Order of operations:** buy the *dev bench* section first, validate the
> firmware end-to-end on breakouts, **then** order the custom PCB and its
> components. The PCB is validation, not the critical path.

## A. Custom PCB — semiconductors & modules

| # | Part | MPN | Package | Qty | Est. $ | Notes |
|---|---|---|---|---|---|---|
| A1 | Zigbee module | ESP32-C6-MINI-1-N4 | SMD module | 1 | 3.20 | PCB antenna; keep-out zone per Espressif AN |
| A2 | Buck converter 3.3 V | TPS62125DSGR | WSON-8 2×2 | 1 | 1.60 | 3–17 V in, Iq ≈ 13 µA, 300 mA |
| A3 | Stepper driver | DRV8833PWPR | HTSSOP-16 | 1 | 1.80 | Dual H-bridge; drives bipolar-modified 28BYJ-48 |
| A4 | Load switch (motor rail) | TPS22810DRVR | WSON-6 2×2 | 1 | 0.70 | 18 V rated — a 5.5 V-max part (TPS22919/SiP32431) cannot sit on the 7.2 V fresh pack |
| A4b | Load switch (sensor 3.3 V rail) | SIP32431DR3-T1GE3 | SC70-6 | 1 | 0.50 | 10 nA leakage; same MOTOR_PWR_EN control line |
| A5 | Reverse-polarity P-FET | AO3401A | SOT-23 | 1 | 0.30 | ±12 V Vgs handles the 7.2 V fresh pack; also battery-sense high-side (qty 2 total) |
| A6 | Battery-sense P-FET | AO3401A | SOT-23 | 1 | 0.30 | (counted with A5 in the schematic as Q1/Q2) |
| A7 | N-FET (divider gate drive) | 2N7002-7-F | SOT-23 | 1 | 0.10 | |
| A8 | Pressure sensor (on-board default) | **XGZP6897D (±500 Pa, I2C)** | DIP-8 w/ ports | 1 | 4.00 | Fitted on the board (`U7`); needs firmware auto-zero (implemented) |
| A8b | *Premium alternate* | SDP810-500PA | barbed module | (1) | 28.00 | Connects to the 5-pin remote-sensor header `J5` instead of fitting U7; best zero-point stability |
| A9 | USB ESD protection | USBLC6-2SC6 | SOT-23-6 | 1 | 0.40 | |
| A10 | Status LED | 0603 green (e.g. 150060GS75000) | 0603 | 1 | 0.10 | |
| A11 | USB bench-power Schottky | SS14 | SMA | 1 | 0.15 | Feeds VBAT_RAW from USB 5 V (pre-FET, cannot charge the cells) |

## B. Custom PCB — passives & electromechanical

| # | Part | Value / MPN | Package | Qty | Est. $ | Notes |
|---|---|---|---|---|---|---|
| B1 | Buck inductor | 4.7 µH, LPS4018-472MRB | 4×4 mm | 1 | 0.80 | Per TPS62125 datasheet app |
| B2 | Input caps | 22 µF 25 V X5R | 1206 | 2 | 0.40 | One at pack input, one at buck VIN |
| B3 | Output cap | 22 µF 10 V X5R | 0805 | 1 | 0.20 | Buck VOUT |
| B4 | Motor rail bulk cap | 100 µF 16 V (poly or MLCC ×2) | — | 1 | 0.50 | Absorbs stepper current steps |
| B5 | Decoupling | 100 nF X7R | 0603 | 8 | 0.20 | Every IC |
| B6 | Misc caps | 1 µF, 10 µF | 0603 | 4 | 0.20 | Sensor + module decoupling |
| B7 | Divider resistors | 1 MΩ + 330 kΩ 1 % | 0603 | 2 | 0.10 | VBAT sense |
| B8 | I2C pull-ups | 4.7 kΩ | 0603 | 2 | 0.05 | To switched rail (no standby drain) |
| B9 | Misc resistors | 100 Ω, 1 kΩ, 10 kΩ, 100 kΩ | 0603 | ~12 | 0.20 | Gates, LED, straps, button pull |
| B10 | USB-C receptacle | GCT USB4105-GF-A | 16-pin SMD | 1 | 0.90 | Flash/debug via USB-Serial-JTAG |
| B11 | Tactile switches (BOOT, RST) | C&K KMR221GLFS | SMD | 2 | 0.60 | BOOT doubles as pair/factory-reset |
| B12 | Battery connector | JST S2B-PH-K-S | PH 2-pin RA | 1 | 0.20 | Mates battery holder pigtail |
| B13 | Motor connector | JST B4B-PH-K-S | PH 4-pin | 1 | 0.25 | Bipolar-modified 28BYJ-48 (4 wires) |
| B14 | Limit-switch connector | JST B3B-PH-K-S | PH 3-pin | 1 | 0.20 | Common bias + 2 switch lines |
| B15 | Debug header | 2.54 mm 1×4 | TH | 1 | 0.10 | UART TX/RX/GND/3V3 |

## C. PCB fabrication

| # | Item | Qty | Est. $ | Notes |
|---|---|---|---|---|
| C1 | 2-layer PCB fab (JLCPCB/PCBWay), 5 pcs min | 1 order | 10–15 | Upload `hardware/pcb/fab/` gerbers; 1.6 mm FR4, HASL or ENIG |
| C2 | (Optional) JLCPCB assembly, 2 boards | 1 order | 30–60 | Or hand-solder; everything is 0603+ except the WSON buck (use hot air/paste) |

## D. Electromechanical (per register)

| # | Part | MPN / spec | Qty | Est. $ | Notes |
|---|---|---|---|---|---|
| D1 | Geared stepper | 28BYJ-48, **5 V** version | 1 | 2.50 | Bipolar mod: open the blue cap, cut the center-tap trace, ignore the red wire (documented in assembly.md) |
| D2 | Limit switches | Omron D2F-L (lever) or SS-5GL | 2 | 3.00 | Subminiature snap-action, lever type |
| D3 | Battery holder | 4× AA, wire leads (Keystone 2478 or equiv.) | 1 | 2.50 | Fit JST-PH pigtail to leads |
| D4 | Batteries | Energizer L91 Ultimate Lithium AA | 4 | 8.00 | LiFeS2 — do **not** substitute alkaline (sag + cold) |
| D5 | Silicone tubing | 2 mm ID × 4 mm OD, ~20 cm | 1 | 1.00 | Pressure ports → sensor barbs |
| D6 | Heat-set inserts | M3 × 5.7 (Ruthex/CNC-Kitchen style) | ~10 | 1.50 | Frame + e-bay assembly |
| D7 | Screws | M3 × 8 pan head | ~10 | 0.50 | |
| D8 | Screws (motor) | M4 × 6 or M3 w/ printed adapter | 2 | 0.20 | 28BYJ-48 flange holes are 4.2 mm |
| D9 | Hookup wire | 26–28 AWG silicone, ~1 m | — | 0.50 | Switch + motor leads |
| D10 | Print filament | **PETG minimum, ASA recommended**, ~350 g | — | 7.00 | Supply air hits 55–65 °C; PLA will creep. Faceplate can be anything |

## E. Dev bench (one-time, for firmware bring-up — buy first)

| # | Part | MPN / spec | Qty | Est. $ | Notes |
|---|---|---|---|---|---|
| E1 | Dev board | ESP32-C6-DevKitC-1-N8 | 1 | 9.00 | Same pin map as rev A via Kconfig |
| E2 | Stepper + driver kit | 28BYJ-48 + ULN2003 board | 1 | 3.00 | First motion tests (unipolar, before the bipolar mod) |
| E3 | DRV8833 breakout | Adafruit 3297 or generic | 1 | 5.00 | Validates the bipolar mod + final drive scheme |
| E4 | Pressure sensor | SDP810-500PA (same as A8) | 1 | 28.00 | Moves to the first PCB later |
| E5 | Microswitches | same as D2 | 2 | 3.00 | |
| E6 | Battery holder + 4× L91 | same as D3/D4 | 1 | 10.50 | For real power-budget measurements |
| E7 | Breadboard + jumpers | generic | 1 | 8.00 | |
| E8 | (If needed) Zigbee coordinator | Sonoff ZBDongle-E or SLZB-06 | 1 | 20–30 | Skip if your HA already has Zigbee |
| E9 | (Recommended) µA-capable meter | e.g. Nordic PPK2 | 1 | 90.00 | Optional but the only way to verify the 150 µA budget |

## Totals (approximate)

| Scope | Est. cost |
|---|---|
| One register, SDP810 build (A+B+D, excl. fab amortization) | **~$65** |
| One register, XGZP6897D cost-down build | **~$40** |
| PCB fab + assembly, amortized over 5 boards | ~$5–15/board |
| Dev bench (one-time, no coordinator/PPK2) | ~$65 |

Multi-register houses: parts A+B+D scale per register; C amortizes.

## Substitution notes

- **SDP810 → SDP31/SDP32** (SMD): same sensing core, needs a printed manifold
  instead of barbs; consider for a later rev.
- **28BYJ-48 → 35BYJ-46 (12 V run at pack voltage)**: fallback if vane torque
  is marginal (mount is parametric in `cad/smart_register/params.py`).
- Do **not** substitute the buck with a TPS62840-class part: its 6.5 V max
  input is below the 7.2 V fresh-pack voltage.
