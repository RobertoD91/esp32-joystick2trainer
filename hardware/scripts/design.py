"""esp32-joystick2trainer JR-bay module: the single source of truth for the hardware design.

Parts, nets, schematic grouping and PCB placement live here; sch_gen.py and pcb_gen.py
render it. Coordinates are board-local millimetres, origin at the top-left corner of the
PCB, x to the right, y downwards. Side "F" = component side facing the pilot (back of the
radio), "B" = side facing the radio body. Parts with a `region` are packed automatically
into that rectangle (rows, in list order); the others have explicit coordinates.
"""
from dataclasses import dataclass
from typing import Dict, List, Optional

PROJECT = "j2t-module"
TITLE = "esp32-joystick2trainer JR bay module"
REV = "A"
BOARD_W = 22.0
BOARD_H = 62.0
CORNER_R = 1.0

# Nets that get wide tracks on the PCB.
POWER_NETS = {"VBAT_IN", "VBAT_F", "VIN_PROT", "+5V", "VBUS_C", "VBUS_SW", "VBUS_A", "+3V3", "SW"}
# Nets driven by connectors/regulators that need a PWR_FLAG for ERC.
PWR_FLAG_NETS = ["GND", "+5V", "+3V3", "VBAT_IN", "VBAT_F", "VIN_PROT", "VBUS_C", "VBUS_SW", "VBUS_A"]
POWER_SYMBOL_NETS = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V"}

# Placement regions: name -> (side, x0, y0, x1, y1)
REGIONS = {
    # name: (side, x0, y0, x1, y1, courtyard gap)
    # The module sits on the bottom with its open (antenna) end towards the USB connectors, so
    # its pad rows face left, right and the bay header; the top-side columns leave via windows
    # (x 5.8-7.6 and 19.0-21.6) next to the pad rows.
    "T1": ("F", 1.6, 29.6, 5.8, 47.4, 1.0),    # left column: BOOT button, LEDs (wide gaps: via room)
    "T3": ("F", 7.4, 29.6, 12.1, 48.0, 0.8),    # centre column: VBUS switch, polyfuse, USB-C diode, control
    "T7": ("F", 13.2, 29.6, 19.4, 48.0, 0.7),   # right column: RESET button, EN/3V3 parts, auto-reset
    "B2c": ("B", 6.8, 18.3, 9.0, 19.4, 0.4),    # CC1 pull-down next to its via
    "B2d": ("B", 6.8, 23.6, 9.0, 24.6, 0.4),    # CC2 pull-down next to its via
    "B1b": ("B", 7.4, 8.8, 14.6, 11.6, 0.4),    # battery sense divider (between the USB-A shell pins)
    "B2e": ("B", 8.1, 16.3, 10.1, 17.4, 0.4),   # CH340 decoupling, under its VCC pin
    "T5": ("F", 8.6, 50.6, 21.0, 51.6, 0.6),    # output series resistors (right of the AUX header)
    "B4": ("B", 10.0, 55.6, 18.0, 57.8, 0.4),   # I2C pull-ups between the module and the bay header pins, fed from J4 pin 2
    "T6": ("F", 8.8, 53.6, 18.7, 56.4, 0.7),    # solder jumpers (reachable from the back face); PPM passes between them
    "B1": ("B", 0.5, 0.5, 21.5, 8.4, 0.4),      # reverse protection + buck
    "B2a": ("B", 10.4, 16.0, 20.6, 27.2, 1.9),  # ESD arrays (pins towards the USB-A), VBUS bulk; wide gaps for the traces
    "B2b": ("B", 1.3, 17.6, 4.8, 24.8, 0.4),    # LDO (between the USB-C pegs, clear of the left-edge power track)
    "B3": ("B", 0.5, 55.4, 4.6, 61.4, 0.4),     # bay polyfuse next to the bay header
}


@dataclass
class Part:
    ref: str
    lib: str          # KiCad symbol library
    sym: str          # symbol name
    fp: str           # footprint "Lib:Name"
    value: str
    pins: Dict[str, str]              # pin number (or ESP32 pin name) -> net
    group: str                        # schematic group
    x: float = 0.0                    # PCB position (footprint origin) when not packed
    y: float = 0.0
    rot: float = 0.0                  # degrees, KiCad convention
    side: str = "F"
    region: Optional[str] = None
    mpn: str = ""
    lcsc: str = ""
    dnp: bool = False
    note: str = ""
    sch_rot: int = 0                  # schematic rotation (0 or 180 supported)
    nc_unlisted: bool = False         # pins not listed in `pins` are no-connects


P: List[Part] = []


def add(*a, **k):
    if "region" in k and k["region"]:
        k.setdefault("side", REGIONS[k["region"]][0])
    P.append(Part(*a, **k))
    return P[-1]


R0402 = "Resistor_SMD:R_0402_1005Metric"
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1210 = "Capacitor_SMD:C_1210_3225Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOT235 = "Package_TO_SOT_SMD:SOT-23-5"
SOT236 = "Package_TO_SOT_SMD:SOT-23-6"

# ------------------------------------------------------------------ MCU
add("SW1", "Switch", "SW_Push", "Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2", "RESET",
    {"1": "ESP_EN", "2": "GND"}, "mcu", region="T7", rot=90, mpn="KMR221GLFS")
add("R10", "Device", "R", R0402, "10k", {"1": "+3V3", "2": "ESP_EN"}, "mcu", region="T7", rot=90)
add("SW2", "Switch", "SW_Push", "Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2", "BOOT",
    {"1": "IO0", "2": "GND"}, "mcu", region="T1", rot=90, mpn="KMR221GLFS")
add("C14", "Device", "C", C0402, "1uF", {"1": "ESP_EN", "2": "GND"}, "mcu", region="T7", rot=90)
add("C12", "Device", "C", C0402, "10uF 6.3V", {"1": "+3V3", "2": "GND"}, "mcu", region="T7",
    note="3V3 decoupling next to U3 pin 2")
add("C13", "Device", "C", C0402, "100nF", {"1": "+3V3", "2": "GND"}, "mcu", region="T7")
add("U3", "RF_Module", "ESP32-S3-WROOM-1", "RF_Module:ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1-N8",
    {"GND": "GND", "3V3": "+3V3", "EN": "ESP_EN", "IO0": "IO0",
     "IO17": "SBUS_OUT", "IO18": "CPPM_OUT", "IO19": "USB_DM", "IO20": "USB_DP",
     "IO2": "USBPWR_EN", "IO10": "I2C_SDA", "IO9": "I2C_SCL", "IO38": "LED_STATUS",
     "IO1": "VBAT_SENSE", "IO16": "PPM_IN", "TXD0": "ESP_TXD0", "RXD0": "ESP_RXD0"},
    "mcu", x=11.0, y=42.0, rot=180, side="B", mpn="ESP32-S3-WROOM-1-N8", nc_unlisted=True,
    note="8 MB flash, same as the firmware's esp32s3 target. Radio unused: antenna keep-out not applied.")

# ------------------------------------------------------------------ bay connector
add("J5", "Connector_Generic", "Conn_01x05", "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Horizontal",
    "JR bay", {"1": "BAY_PPM", "2": "BAY_P2", "3": "VBAT_IN", "4": "BAY_P4", "5": "BAY_SPORT"},
    "bay", x=16.08, y=59.0, rot=270, side="F", mpn="2.54 mm right-angle header, long pins",
    note="Pin 1 PPM, 2 heartbeat, 3 VBAT, 4 GND, 5 S.PORT: verify on the radio before bridging JP1/JP2")

add("JP2", "Jumper", "SolderJumper_3_Open", "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm", "pin4: GND|HB",
    {"1": "GND", "2": "BAY_P4", "3": "BAY_HB"}, "bay", region="T6",
    note="bridge 1-2 (GND) for the standard order, 2-3 (HB) when mirrored")
add("JP1", "Jumper", "SolderJumper_3_Open", "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm", "pin2: GND|HB",
    {"1": "GND", "2": "BAY_P2", "3": "BAY_HB"}, "bay", region="T6",
    note="bridge 2-3 (HB) when pin 2 is the heartbeat, 1-2 (GND) when the bay order is mirrored")

# ------------------------------------------------------------------ power input and buck
add("F1", "Device", "Polyfuse", "Fuse:Fuse_1206_3216Metric", "1.1A hold", {"1": "VBAT_IN", "2": "VBAT_F"},
    "power_in", region="B3", rot=90, mpn="1206L110", note="next to the bay header: the unfused net stays short")
add("Q1", "Device", "Q_PMOS_GSD", SOT23, "AO3407A", {"1": "GND", "2": "VIN_PROT", "3": "VBAT_F"},
    "power_in", region="B1", mpn="AO3407A",
    note="Reverse polarity protection (Vgs max 20 V: 2S battery packs only)")
add("U1", "Regulator_Switching", "TPS54202DDC", SOT236, "TPS54202",
    {"1": "GND", "2": "SW", "3": "VIN_PROT", "4": "FB", "5": "NC_BUCK_EN", "6": "BOOT"},
    "power_in", region="B1", mpn="TPS54202DDCR", note="EN left floating = enabled (internal pull-up)")
add("L1", "Device", "L", "Inductor_SMD:L_Sunlord_SWPA5040S", "10uH 2.3A", {"1": "SW", "2": "+5V"},
    "power_in", region="B1", mpn="SWPA5040S100MT",
    note="5x5x4 mm; SWPA5030S100MT (3 mm high, 1.5 A sat) if the case is too thin")
add("C5", "Device", "C", C0805, "22uF 10V", {"1": "+5V", "2": "GND"}, "power_in", region="B1", rot=90)
add("C1", "Device", "C", C0805, "10uF 25V", {"1": "VIN_PROT", "2": "GND"}, "power_in", region="B1")
add("C2", "Device", "C", C0805, "10uF 25V", {"1": "VIN_PROT", "2": "GND"}, "power_in", region="B1")
add("C3", "Device", "C", C0402, "100nF 50V", {"1": "VIN_PROT", "2": "GND"}, "power_in", region="B1")
add("C4", "Device", "C", C0402, "100nF", {"1": "BOOT", "2": "SW"}, "power_in", region="B1")
add("R1", "Device", "R", R0402, "100k", {"1": "+5V", "2": "FB"}, "power_in", region="B1")
add("C6", "Device", "C", C0402, "75pF", {"1": "+5V", "2": "FB"}, "power_in", region="B1", rot=180,
    note="feed-forward across R1 (datasheet table 7-2)")
add("R2", "Device", "R", R0402, "13.3k", {"1": "FB", "2": "GND"}, "power_in", region="B1")

# ------------------------------------------------------------------ 3.3 V
add("D2", "Power_Protection", "USBLC6-2SC6", SOT236, "USBLC6-2SC6",
    {"1": "USB_DM", "2": "GND", "3": "USB_DP", "4": "USB_DP", "5": "VBUS_A", "6": "USB_DM"},
    "usb_a", region="B2a", rot=270, mpn="USBLC6-2SC6",
    note="D+ and D- pads line up with the USB-A pins")
add("D4", "Power_Protection", "USBLC6-2SC6", SOT236, "USBLC6-2SC6",
    {"1": "USBC_DM", "2": "GND", "3": "USBC_DP", "4": "USBC_DP", "5": "VBUS_C", "6": "USBC_DM"},
    "usb_c", region="B2a", rot=270, mpn="USBLC6-2SC6",
    note="rows: D+/GND/D- towards the USB-A, D+/VBUS/D- towards the module")
add("D7", "Device", "D_Schottky", "Diode_SMD:D_SOD-123", "B5819W", {"1": "+5V", "2": "VBUS_C"},
    "power_mux", region="T3", rot=0, mpn="B5819W",
    note="USB-C 5 V ORed into the 5 V rail through a Schottky: reverse biased while the buck runs, "
         "about 4.5 V on the rail when only the USB-C is plugged (bench use)")
add("C8", "Device", "C", C1210, "47uF 10V", {"1": "VBUS_A", "2": "GND"}, "usb_a", region="B2a", rot=90)
add("C9", "Device", "C", C1210, "47uF 10V", {"1": "VBUS_A", "2": "GND"}, "usb_a", region="B2a", rot=90,
    note="USB host port bulk capacitance (ceramic compromise for the 120 uF requirement)")
add("F2", "Device", "Polyfuse", "Fuse:Fuse_1206_3216Metric", "0.75A hold", {"1": "VBUS_SW", "2": "VBUS_A"},
    "usb_a", region="T3", rot=0, mpn="1206L075")
add("U2", "Regulator_Linear", "AP2112K-3.3", SOT235, "AP2112K-3.3",
    {"1": "+5V", "2": "GND", "3": "+5V", "4": "NC_U2", "5": "+3V3"}, "ldo", region="B2b", rot=90, mpn="AP2112K-3.3TRG1")
add("C10", "Device", "C", C0402, "1uF", {"1": "+5V", "2": "GND"}, "ldo", region="B2b")
add("C11", "Device", "C", C0402, "1uF", {"1": "+3V3", "2": "GND"}, "ldo", region="B2b")

# ------------------------------------------------------------------ USB-C ESD (bottom, under the connector pads)
# ------------------------------------------------------------------ USB-A ESD and power mux

# ------------------------------------------------------------------ USB-A host port
add("C7", "Device", "C", C0805, "22uF 10V", {"1": "+5V", "2": "GND"}, "power_in", region="T3",
    note="C5 + C7 = 44 uF buck output capacitance (datasheet table 7-2); also the VBUS switch input reservoir")
add("Q5", "Device", "Q_PMOS_GSD", SOT23, "AO3401A", {"1": "G5", "2": "+5V", "3": "VBUS_SW"},
    "usb_a", region="T3", mpn="AO3401A", note="VBUS switch, on by default")
add("Q6", "Device", "Q_NMOS_GSD", "Package_TO_SOT_SMD:SOT-323_SC-70", "2N7002W", {"1": "USBPWR_EN", "2": "GND", "3": "G5"},
    "usb_a", region="T3", mpn="2N7002W")
add("R7", "Device", "R", R0402, "100k", {"1": "+5V", "2": "G5"}, "usb_a", region="T3")
add("R8", "Device", "R", R0402, "100k", {"1": "+3V3", "2": "USBPWR_EN"}, "usb_a", region="T3",
    note="pull-up: joystick powered even before the firmware runs; GPIO2 low cuts VBUS")
add("R26", "Device", "R", R0402, "4.7k", {"1": "+3V3", "2": "I2C_SCL"}, "outputs", region="B4",
    note="omit if the display module has its own pull-ups")
add("R27", "Device", "R", R0402, "4.7k", {"1": "+3V3", "2": "I2C_SDA"}, "outputs", region="B4")
add("R11", "Device", "R", R0402, "10k", {"1": "+3V3", "2": "IO0"}, "mcu", region="T1", rot=90)
add("R14", "Device", "R", R0402, "5.1k", {"1": "CC1", "2": "GND"}, "usb_c", region="B2c")
add("J1", "Connector", "USB_A", "Connector_USB:USB_A_Molex_67643_Horizontal", "USB-A joystick",
    {"1": "VBUS_A", "2": "USB_DM", "3": "USB_DP", "4": "GND", "5": "GND"}, "usb_a",
    x=14.5, y=12.8, rot=180, side="F", mpn="Molex 67643-0910 or equivalent right-angle USB-A receptacle")

# ------------------------------------------------------------------ USB-C programming port + CH340C
add("Q7", "Transistor_BJT", "BC847W", "Package_TO_SOT_SMD:SOT-323_SC-70", "BC847W", {"1": "DTR_B", "2": "RTS", "3": "ESP_EN"},
    "usb_c", region="T7", mpn="BC847W", note="auto-reset: EN")
add("R13", "Device", "R", R0402, "12k", {"1": "RTS", "2": "RTS_B"}, "usb_c", region="T7")
add("R15", "Device", "R", R0402, "5.1k", {"1": "CC2", "2": "GND"}, "usb_c", region="B2d")
add("Q8", "Transistor_BJT", "BC847W", "Package_TO_SOT_SMD:SOT-323_SC-70", "BC847W", {"1": "RTS_B", "2": "DTR", "3": "IO0"},
    "usb_c", region="T7", mpn="BC847W", note="auto-reset: IO0")
add("C15", "Device", "C", C0402, "100nF", {"1": "+3V3", "2": "GND"}, "usb_c", region="B2e")
add("R12", "Device", "R", R0402, "12k", {"1": "DTR", "2": "DTR_B"}, "usb_c", region="T7")
add("D5", "Device", "LED", "LED_SMD:LED_0603_1608Metric", "green", {"1": "LED_K", "2": "LED_STATUS"},
    "misc", region="T1", rot=90, note="status LED (led.gpio 38)")
add("R24", "Device", "R", R0402, "1k", {"1": "LED_K", "2": "GND"}, "misc", region="T1", rot=270)
add("D6", "Device", "LED", "LED_SMD:LED_0603_1608Metric", "red", {"1": "PWRLED_K", "2": "+3V3"},
    "misc", region="T1", rot=90, note="3.3 V present")
add("J2", "Connector", "USB_C_Receptacle_USB2.0_16P", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
    "USB-C console", {"A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "A4": "VBUS_C", "A9": "VBUS_C",
                      "B4": "VBUS_C", "B9": "VBUS_C", "A5": "CC1", "B5": "CC2", "A6": "USBC_DP", "B6": "USBC_DP",
                      "A7": "USBC_DM", "B7": "USBC_DM", "A8": "NC_J2A", "B8": "NC_J2B", "S1": "GND"},
    "usb_c", x=3.75, y=21.05, rot=270, side="F", mpn="HRO TYPE-C-31-M-12",
    note="position keeps the 0.3 mm pads on the router's 0.1 mm grid")
add("U4", "Interface_USB", "CH340C", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", "CH340C",
    {"1": "GND", "2": "ESP_RXD0", "3": "ESP_TXD0", "4": "+3V3", "5": "USBC_DP", "6": "USBC_DM", "7": "NC_U4",
     "8": "NC_U4_8", "9": "NC_U4_9", "10": "NC_U4_10", "11": "NC_U4_11", "12": "NC_U4_12",
     "13": "DTR", "14": "RTS", "15": "NC_U4_15", "16": "+3V3"},
    "usb_c", x=15.2, y=19.8, rot=90, side="F", mpn="CH340C",
    note="3.3 V operation: V3 tied to VCC. Pins 1-8 (UART, USB) on the row facing the module, 9-16 (DTR/RTS) facing the USB-A")

# ------------------------------------------------------------------ outputs
add("R17", "Device", "R", R0402, "1k", {"1": "SBUS_OUT", "2": "AUX_SBUS"}, "outputs", region="T5")
add("R20", "Device", "R", R0402, "1k", {"1": "CPPM_OUT", "2": "AUX_CPPM"}, "outputs", region="T5")
add("R16", "Device", "R", R0402, "1k", {"1": "SBUS_OUT", "2": "BAY_SPORT"}, "outputs", region="T5")
add("R18", "Device", "R", R0402, "1k", {"1": "CPPM_OUT", "2": "BAY_HB"}, "outputs", region="T5")
add("R21", "Device", "R", R0402, "1k", {"1": "PPM_IN", "2": "BAY_PPM"}, "outputs", region="T5",
    note="radio PPM/CRSF output into a spare GPIO (future use)")
add("J4", "Connector_Generic", "Conn_01x04", "Connector_PinHeader_1.27mm:PinHeader_1x04_P1.27mm_Vertical",
    "I2C OLED", {"1": "GND", "2": "+3V3", "3": "I2C_SCL", "4": "I2C_SDA"}, "outputs", x=20.3, y=55.6, rot=0, side="F",
    mpn="1.27 mm pitch 1x4 pin header (or solder the display cable directly)",
    note="GND VCC SCL SDA for a 0.91in 128x32 SSD1306 module; pin 1 nearest the bay header")
add("J3", "Connector_Generic", "Conn_01x03", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical_SMD_Pin1Left",
    "AUX", {"1": "GND", "2": "AUX_SBUS", "3": "AUX_CPPM"}, "outputs", x=4.6, y=51.8, rot=0, side="F",
    note="GND / SBUS / CPPM: AUX serial port cable, or a cable to a 3.5 mm plug for the trainer jack; plugs from the back face")

# ------------------------------------------------------------------ indicators and sensing
add("R22", "Device", "R", R0402, "100k", {"1": "VIN_PROT", "2": "VBAT_SENSE"}, "misc", region="B1b")
add("R23", "Device", "R", R0402, "22k", {"1": "VBAT_SENSE", "2": "GND"}, "misc", region="B1b")
add("C16", "Device", "C", C0402, "100nF", {"1": "VBAT_SENSE", "2": "GND"}, "misc", region="B1b",
    note="8.4 V -> 1.5 V on GPIO1 (ADC)")
add("R25", "Device", "R", R0402, "2.2k", {"1": "PWRLED_K", "2": "GND"}, "misc", region="T1", rot=90)


def nets() -> Dict[str, List[tuple]]:
    """net name -> list of (ref, pin)."""
    out: Dict[str, List[tuple]] = {}
    for p in P:
        for pin, net in p.pins.items():
            out.setdefault(net, []).append((p.ref, pin))
    return out


def by_ref(ref: str) -> Part:
    for p in P:
        if p.ref == ref:
            return p
    raise KeyError(ref)


GROUP_TITLES = {
    "mcu": "ESP32-S3-WROOM-1 (USB host, SBUS/CPPM outputs)",
    "bay": "JR module bay connector",
    "power_in": "Bay supply: reverse protection, polyfuse, 5 V buck (TPS54202)",
    "power_mux": "USB-C 5 V for bench use (Schottky OR into the 5 V rail)",
    "ldo": "3.3 V regulator",
    "usb_a": "USB-A host port: VBUS switch, polyfuse, bulk capacitance, ESD",
    "usb_c": "USB-C console and flashing: CH340C, auto-reset",
    "outputs": "Outputs: bay S.PORT/heartbeat, AUX header, trainer jack, I2C display",
    "misc": "Indicators and battery sensing",
}
