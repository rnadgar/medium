"""Canonical electrical design for the smart-register rev A board.

Single source of truth consumed by gen_schematic.py, gen_pcb.py,
check_netlist.py and gen_fab.py. Pin numbers follow the exact symbol
definitions in the KiCad 7 libraries (and Espressif's official
ESP32-C6-MINI-1 symbol) — do not edit them from memory; re-extract.
"""

# net name constants (just to catch typos — everything below uses strings)
GND = "GND"
NC = "<NC>"  # marks a no-connect pin

# fmt: off
COMPONENTS = {
    # ref: dict(lib, sym, value, footprint, mpn, desc, pins={pad: net}, sch=(x, y))
    "U1": dict(
        lib="SmartRegister", sym="ESP32-C6-MINI-1", value="ESP32-C6-MINI-1-N4",
        footprint="SmartRegister:ESP32-C6-MINI-1",
        mpn="ESP32-C6-MINI-1-N4", desc="Zigbee module, PCB antenna",
        pins={
            "1": GND, "2": GND, "3": "3V3", "4": NC,
            "5": "MOT_AIN1",        # GPIO2
            "6": "MOT_AIN2",        # GPIO3
            "7": NC,
            "8": "CHIP_EN",         # EN/CHIP_PU
            "9": "MOT_BIN1",        # GPIO4
            "10": "MOT_BIN2",       # GPIO5
            "11": GND,
            "12": "VBAT_SENSE",     # GPIO0 / ADC1_CH0
            "13": "VBAT_SENSE_EN",  # GPIO1
            "14": GND,
            "15": "I2C_SDA",        # GPIO6
            "16": "I2C_SCL",        # GPIO7
            "17": "USB_DN",         # GPIO12 / USB_D-
            "18": "USB_DP",         # GPIO13 / USB_D+
            "19": NC,               # GPIO14 spare
            "20": NC,               # GPIO15 spare
            "21": NC,
            "22": "GPIO8_PU",       # GPIO8 boot strap
            "23": "BOOT",           # GPIO9 boot / pairing button
            "24": "LIMIT_OPEN",     # GPIO18
            "25": "LIMIT_CLOSED",   # GPIO19
            "26": "LED_CTL",        # GPIO20
            "27": "MOTOR_PWR_EN",   # GPIO21
            "28": "LIMIT_SENSE_EN", # GPIO22
            "29": "SENS_IRQ",       # GPIO23 spare on sensor header
            "30": "UART_RX",        # U0RXD / GPIO17
            "31": "UART_TX",        # U0TXD / GPIO16
            **{str(n): NC for n in (32, 33, 34, 35)},
            **{str(n): GND for n in range(36, 54)},
        },
        sch=(200, 90),
    ),
    "U2": dict(
        lib="Regulator_Switching", sym="TPS62125DSG", value="TPS62125",
        footprint="Package_SON:WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm",
        mpn="TPS62125DSGR", desc="Buck 3.3 V always-on, Iq 13 uA",
        pins={"1": GND, "2": "VBAT", "3": "BUCK_EN", "4": GND,
              "5": "BUCK_FB", "6": "3V3", "7": "SW_NODE", "8": NC, "9": GND},
        sch=(80, 60),
    ),
    "U3": dict(
        lib="Driver_Motor", sym="DRV8833PWP", value="DRV8833",
        footprint="Package_SO:HTSSOP-16-1EP_4.4x5mm_P0.65mm_EP3.4x5mm",
        mpn="DRV8833PWPR", desc="Stepper driver (bipolar-modded 28BYJ-48)",
        pins={"1": "NSLEEP", "2": "MOT_A1", "3": GND, "4": "MOT_A2",
              "5": "MOT_B2", "6": GND, "7": "MOT_B1", "8": NC,
              "9": "MOT_BIN1", "10": "MOT_BIN2", "11": "VCP", "12": "VMOT",
              "13": GND, "14": "VINT", "15": "MOT_AIN2", "16": "MOT_AIN1",
              "17": GND},
        sch=(320, 60),
    ),
    "U4": dict(
        lib="Power_Management", sym="TPS22810DRV", value="TPS22810",
        footprint="Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
        mpn="TPS22810DRVR", desc="Motor rail load switch, 18 V rated",
        pins={"1": "VMOT", "2": NC, "3": "SW_CT", "4": GND,
              "5": "MOTOR_PWR_EN", "6": "VBAT", "7": GND},
        sch=(80, 120),
    ),
    "U5": dict(
        lib="Power_Management", sym="SiP32431DR3", value="SiP32431",
        footprint="Package_TO_SOT_SMD:SOT-363_SC-70-6",
        mpn="SIP32431DR3-T1GE3", desc="Sensor 3.3 V load switch, 10 nA leak",
        pins={"1": "3V3_SENS", "2": GND, "3": "MOTOR_PWR_EN", "4": "3V3",
              "5": GND, "6": NC},
        sch=(140, 120),
    ),
    "U6": dict(
        lib="Power_Protection", sym="USBLC6-2SC6", value="USBLC6-2SC6",
        footprint="Package_TO_SOT_SMD:SOT-23-6",
        mpn="USBLC6-2SC6", desc="USB ESD protection",
        pins={"1": "USB_DN_CONN", "2": GND, "3": "USB_DP_CONN",
              "4": "USB_DP", "5": "VUSB", "6": "USB_DN"},
        sch=(120, 200),
    ),
    "U7": dict(
        lib="Sensor_Pressure", sym="XGZP6897D", value="XGZP6897D-500Pa",
        footprint="Sensor_Pressure:CFSensor_XGZP6897x",
        mpn="XGZP6897D (+-500Pa I2C)", desc="On-board differential pressure sensor",
        pins={"1": NC, "2": "3V3_SENS", "3": NC, "4": NC, "5": NC,
              "6": "I2C_SDA", "7": "I2C_SCL", "8": GND},
        sch=(320, 130),
    ),
    "Q1": dict(
        lib="Transistor_FET", sym="AO3401A", value="AO3401A",
        footprint="Package_TO_SOT_SMD:SOT-23",
        mpn="AO3401A", desc="Reverse-polarity protection P-FET",
        pins={"1": GND, "2": "VBAT", "3": "VBAT_RAW"},
        sch=(40, 40),
    ),
    "Q2": dict(
        lib="Transistor_FET", sym="AO3401A", value="AO3401A",
        footprint="Package_TO_SOT_SMD:SOT-23",
        mpn="AO3401A", desc="Battery divider high-side switch",
        pins={"1": "VBAT_SENSE_G", "2": "VBAT", "3": "VBAT_DIV_TOP"},
        sch=(150, 40),
    ),
    "Q3": dict(
        lib="Transistor_FET", sym="2N7002", value="2N7002",
        footprint="Package_TO_SOT_SMD:SOT-23",
        mpn="2N7002-7-F", desc="Divider gate driver",
        pins={"1": "VBAT_SENSE_EN", "2": GND, "3": "VBAT_SENSE_G"},
        sch=(150, 70),
    ),
    "D1": dict(
        lib="Device", sym="D_Schottky", value="SS14",
        footprint="Diode_SMD:D_SMA",
        mpn="SS14", desc="USB bench power injection (into VBAT_RAW, pre-FET)",
        pins={"1": "VBAT_RAW", "2": "VUSB"},  # 1=K 2=A
        sch=(40, 200),
    ),
    "D2": dict(
        lib="Device", sym="LED", value="green",
        footprint="LED_SMD:LED_0603_1608Metric",
        mpn="150060GS75000", desc="Status LED",
        pins={"1": GND, "2": "LED_A"},  # 1=K 2=A
        sch=(260, 200),
    ),
    # --- resistors ---
    "R1": dict(lib="Device", sym="R", value="1M", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-071ML", desc="VBAT divider top",
               pins={"1": "VBAT_DIV_TOP", "2": "VBAT_SENSE"}, sch=(180, 40)),
    "R2": dict(lib="Device", sym="R", value="330k", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-07330KL", desc="VBAT divider bottom",
               pins={"1": "VBAT_SENSE", "2": GND}, sch=(180, 60)),
    "R3": dict(lib="Device", sym="R", value="100k", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-07100KL", desc="Q2 gate pull-up",
               pins={"1": "VBAT", "2": "VBAT_SENSE_G"}, sch=(120, 40)),
    "R4": dict(lib="Device", sym="R", value="3.0M", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-073ML", desc="Buck EN UVLO top (cutoff ~4.2 V pack)",
               pins={"1": "VBAT", "2": "BUCK_EN"}, sch=(55, 60)),
    "R5": dict(lib="Device", sym="R", value="1.0M", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-071ML", desc="Buck EN UVLO bottom",
               pins={"1": "BUCK_EN", "2": GND}, sch=(55, 80)),
    "R6": dict(lib="Device", sym="R", value="1M", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-071ML", desc="Buck FB top",
               pins={"1": "3V3", "2": "BUCK_FB"}, sch=(105, 60)),
    "R7": dict(lib="Device", sym="R", value="316k", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-07316KL", desc="Buck FB bottom (Vout 3.33 V)",
               pins={"1": "BUCK_FB", "2": GND}, sch=(105, 80)),
    "R8": dict(lib="Device", sym="R", value="100k", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-07100KL", desc="DRV8833 nSLEEP divider top (from VMOT)",
               pins={"1": "VMOT", "2": "NSLEEP"}, sch=(290, 40)),
    "R9": dict(lib="Device", sym="R", value="220k", footprint="Resistor_SMD:R_0603_1608Metric",
               mpn="RC0603FR-07220KL", desc="nSLEEP divider bottom",
               pins={"1": "NSLEEP", "2": GND}, sch=(290, 60)),
    "R10": dict(lib="Device", sym="R", value="10k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-0710KL", desc="Limit OPEN pull-up (gated)",
                pins={"1": "LIMIT_SENSE_EN", "2": "LIMIT_OPEN"}, sch=(360, 40)),
    "R11": dict(lib="Device", sym="R", value="10k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-0710KL", desc="Limit CLOSED pull-up (gated)",
                pins={"1": "LIMIT_SENSE_EN", "2": "LIMIT_CLOSED"}, sch=(360, 60)),
    "R12": dict(lib="Device", sym="R", value="4.7k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-074K7L", desc="I2C SDA pull-up (switched rail)",
                pins={"1": "3V3_SENS", "2": "I2C_SDA"}, sch=(350, 130)),
    "R13": dict(lib="Device", sym="R", value="4.7k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-074K7L", desc="I2C SCL pull-up (switched rail)",
                pins={"1": "3V3_SENS", "2": "I2C_SCL"}, sch=(375, 130)),
    "R14": dict(lib="Device", sym="R", value="1k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-071KL", desc="LED series",
                pins={"1": "LED_CTL", "2": "LED_A"}, sch=(240, 200)),
    "R15": dict(lib="Device", sym="R", value="10k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-0710KL", desc="GPIO8 boot strap pull-up",
                pins={"1": "3V3", "2": "GPIO8_PU"}, sch=(200, 200)),
    "R16": dict(lib="Device", sym="R", value="10k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-0710KL", desc="CHIP_EN pull-up",
                pins={"1": "3V3", "2": "CHIP_EN"}, sch=(180, 200)),
    "R17": dict(lib="Device", sym="R", value="5.1k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-075K1L", desc="USB CC1 pull-down (UFP)",
                pins={"1": "CC1", "2": GND}, sch=(90, 230)),
    "R18": dict(lib="Device", sym="R", value="5.1k", footprint="Resistor_SMD:R_0603_1608Metric",
                mpn="RC0603FR-075K1L", desc="USB CC2 pull-down (UFP)",
                pins={"1": "CC2", "2": GND}, sch=(110, 230)),
    # --- capacitors ---
    "C1": dict(lib="Device", sym="C", value="22u/25V", footprint="Capacitor_SMD:C_1206_3216Metric",
               mpn="GRM31CR61E226KE15", desc="VBAT bulk",
               pins={"1": "VBAT", "2": GND}, sch=(40, 80)),
    "C2": dict(lib="Device", sym="C", value="10u/25V", footprint="Capacitor_SMD:C_0805_2012Metric",
               mpn="GRM21BR61E106KA73", desc="Buck VIN local",
               pins={"1": "VBAT", "2": GND}, sch=(65, 100)),
    "C3": dict(lib="Device", sym="C", value="22u/10V", footprint="Capacitor_SMD:C_0805_2012Metric",
               mpn="GRM21BR61A226ME44", desc="Buck VOUT",
               pins={"1": "3V3", "2": GND}, sch=(105, 100)),
    "C4": dict(lib="Device", sym="C", value="22u/25V", footprint="Capacitor_SMD:C_1206_3216Metric",
               mpn="GRM31CR61E226KE15", desc="VMOT bulk 1",
               pins={"1": "VMOT", "2": GND}, sch=(260, 40)),
    "C5": dict(lib="Device", sym="C", value="22u/25V", footprint="Capacitor_SMD:C_1206_3216Metric",
               mpn="GRM31CR61E226KE15", desc="VMOT bulk 2",
               pins={"1": "VMOT", "2": GND}, sch=(260, 60)),
    "C6": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
               mpn="CL10B104KB8NNNC", desc="CHIP_EN reset RC",
               pins={"1": "CHIP_EN", "2": GND}, sch=(180, 230)),
    "C7": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
               mpn="CL10B104KB8NNNC", desc="U1 decoupling",
               pins={"1": "3V3", "2": GND}, sch=(200, 230)),
    "C8": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
               mpn="CL10B104KB8NNNC", desc="U3 VM local",
               pins={"1": "VMOT", "2": GND}, sch=(260, 80)),
    "C9": dict(lib="Device", sym="C", value="2.2u", footprint="Capacitor_SMD:C_0603_1608Metric",
               mpn="CL10A225KP8NNNC", desc="DRV8833 VINT",
               pins={"1": "VINT", "2": GND}, sch=(290, 80)),
    "C10": dict(lib="Device", sym="C", value="10n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B103KB8NNNC", desc="DRV8833 charge pump (VCP-VM)",
                pins={"1": "VCP", "2": "VMOT"}, sch=(290, 100)),
    "C11": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B104KB8NNNC", desc="U5 VIN local",
                pins={"1": "3V3", "2": GND}, sch=(140, 150)),
    "C12": dict(lib="Device", sym="C", value="1u", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10A105KB8NNNC", desc="3V3_SENS rail",
                pins={"1": "3V3_SENS", "2": GND}, sch=(165, 150)),
    "C13": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B104KB8NNNC", desc="U7 decoupling",
                pins={"1": "3V3_SENS", "2": GND}, sch=(345, 155)),
    "C14": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B104KB8NNNC", desc="VUSB local",
                pins={"1": "VUSB", "2": GND}, sch=(40, 230)),
    "C15": dict(lib="Device", sym="C", value="1n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B102KB8NNNC", desc="TPS22810 soft-start (CT)",
                pins={"1": "SW_CT", "2": GND}, sch=(105, 150)),
    "C16": dict(lib="Device", sym="C", value="100n", footprint="Capacitor_SMD:C_0603_1608Metric",
                mpn="CL10B104KB8NNNC", desc="ADC settle (VBAT_SENSE)",
                pins={"1": "VBAT_SENSE", "2": GND}, sch=(210, 40)),
    "L1": dict(lib="Device", sym="L", value="4.7uH", footprint="Inductor_SMD:L_Coilcraft_LPS4018",
               mpn="LPS4018-472MRB", desc="Buck inductor",
               pins={"1": "SW_NODE", "2": "3V3"}, sch=(80, 100)),
    # --- switches / connectors ---
    "SW1": dict(lib="Switch", sym="SW_Push", value="BOOT/PAIR",
                footprint="Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2",
                mpn="KMR221GLFS", desc="Boot strap / pairing button",
                pins={"1": "BOOT", "2": GND}, sch=(230, 230)),
    "SW2": dict(lib="Switch", sym="SW_Push", value="RESET",
                footprint="Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2",
                mpn="KMR221GLFS", desc="Reset button",
                pins={"1": "CHIP_EN", "2": GND}, sch=(255, 230)),
    "J1": dict(lib="Connector", sym="USB_C_Receptacle_USB2.0_16P", value="USB-C",
               footprint="Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
               mpn="GCT USB4105-GF-A", desc="Flash/debug USB",
               pins={"A1": GND, "B1": GND, "A12": GND, "B12": GND, "S1": GND,
                     "A4": "VUSB", "A9": "VUSB", "B4": "VUSB", "B9": "VUSB",
                     "A5": "CC1", "B5": "CC2",
                     "A6": "USB_DP_CONN", "B6": "USB_DP_CONN",
                     "A7": "USB_DN_CONN", "B7": "USB_DN_CONN",
                     "A8": NC, "B8": NC},
               sch=(60, 190)),
    "J2": dict(lib="Connector_Generic", sym="Conn_01x02", value="BATT 4xAA",
               footprint="Connector_JST:JST_PH_S2B-PH-K_1x02_P2.00mm_Horizontal",
               mpn="JST S2B-PH-K-S", desc="Battery pack input",
               pins={"1": "VBAT_RAW", "2": GND}, sch=(40, 60)),
    "J3": dict(lib="Connector_Generic", sym="Conn_01x04", value="MOTOR",
               footprint="Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical",
               mpn="JST B4B-PH-K-S", desc="28BYJ-48 (bipolar mod, 4-wire)",
               pins={"1": "MOT_A1", "2": "MOT_A2", "3": "MOT_B1", "4": "MOT_B2"},
               sch=(360, 90)),
    "J4": dict(lib="Connector_Generic", sym="Conn_01x03", value="LIMIT SW",
               footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical",
               mpn="JST B3B-PH-K-S", desc="Limit switches (common GND)",
               pins={"1": GND, "2": "LIMIT_OPEN", "3": "LIMIT_CLOSED"},
               sch=(390, 90)),
    "J5": dict(lib="Connector_Generic", sym="Conn_01x05", value="SDP810",
               footprint="Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical",
               mpn="header 1x5", desc="Remote pressure sensor (SDP810 option)",
               pins={"1": "3V3_SENS", "2": GND, "3": "I2C_SDA", "4": "I2C_SCL",
                     "5": "SENS_IRQ"}, sch=(320, 190)),
    "J6": dict(lib="Connector_Generic", sym="Conn_01x04", value="DEBUG UART",
               footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
               mpn="header 1x4", desc="Debug UART",
               pins={"1": "3V3", "2": "UART_TX", "3": "UART_RX", "4": GND},
               sch=(350, 190)),
    "H1": dict(lib="Mechanical", sym="MountingHole", value="M3",
               footprint="MountingHole:MountingHole_3.2mm_M3",
               mpn="-", desc="Mounting hole", pins={}, sch=(390, 230)),
    "H2": dict(lib="Mechanical", sym="MountingHole", value="M3",
               footprint="MountingHole:MountingHole_3.2mm_M3",
               mpn="-", desc="Mounting hole", pins={}, sch=(400, 230)),
    "H3": dict(lib="Mechanical", sym="MountingHole", value="M3",
               footprint="MountingHole:MountingHole_3.2mm_M3",
               mpn="-", desc="Mounting hole", pins={}, sch=(390, 240)),
    "H4": dict(lib="Mechanical", sym="MountingHole", value="M3",
               footprint="MountingHole:MountingHole_3.2mm_M3",
               mpn="-", desc="Mounting hole", pins={}, sch=(400, 240)),
}
# fmt: on

# nets that get a PWR_FLAG (placed by the schematic generator)
PWR_FLAG_NETS = ["GND", "VBAT_RAW", "VBAT", "3V3", "3V3_SENS", "VMOT", "VUSB"]

# nets whose global label uses the "power" look; purely cosmetic
POWER_NETS = {"GND", "VBAT_RAW", "VBAT", "3V3", "3V3_SENS", "VMOT", "VUSB",
              "SW_NODE"}


def all_nets():
    nets = {}
    for ref, comp in COMPONENTS.items():
        for pad, net in comp["pins"].items():
            if net == NC:
                continue
            nets.setdefault(net, []).append((ref, pad))
    return nets


def validate():
    nets = all_nets()
    single = {n: p for n, p in nets.items() if len(p) < 2}
    if single:
        raise SystemExit(f"single-pin nets: {single}")
    return nets


if __name__ == "__main__":
    nets = validate()
    print(f"{len(COMPONENTS)} components, {len(nets)} nets — OK")
    for net, pins in sorted(nets.items()):
        print(f"  {net:<16} {' '.join(f'{r}.{p}' for r, p in sorted(pins))}")
