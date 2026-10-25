# Hardware options: other microcontrollers and an I2C display

Notes from an evaluation made in October 2026, after the first flight on the ESP32-S3-DevKitC-1.
They answer two questions that come up regularly: *could the firmware run on another chip?* and
*how hard is a small status display?* Nothing here is implemented; it is the reasoning behind
what the project does and does not plan to support.

## What the firmware needs from a chip

The ESP32-S3 was chosen for one feature: a **native USB OTG host controller** supported by a
maintained host stack (the ESP-IDF USB Host Library, with the HID class driver vendored in
`components/usb_host_hid`). Everything else is ordinary: a UART with **hardware TX inversion**
for SBUS, a timer-like peripheral for CPPM (the RMT here), a few kilobytes of persistent
storage (NVS) for the configuration, and an RTOS with a handful of tasks. The pure C++ parts
(`lib/hid_core`, `lib/rc_core`: descriptor parser, joystick decoder, PID force feedback,
mapping, SBUS/CPPM encoders, configuration keys) have no hardware dependency and are tested
on the host; the application layer in `src/app` (about two thousand lines: USB enumeration,
outputs, NVS store, console, force-feedback transport, status LED) is what a port would
rewrite.

## Espressif family

The ESP-IDF USB Host Library only runs on three chips, so the choice is short:

| Chip | USB host | Verdict |
|------|----------|---------|
| ESP32-S3 | yes, full speed | **Current target.** Smaller boards (S3 Zero, SuperMini, …) run the same firmware with different pins; they need a USB-C OTG adapter and 5 V fed to VBUS, exactly like the DevKitC-1 (see [Powering the joystick](../README.md#powering-the-joystick)) |
| ESP32-S2 | yes, full speed, same controller | **Viable with a small change.** Single core: the SBUS/CPPM tasks are pinned to core 1 today and would have to use core 0 or no affinity. No Bluetooth, which the project does not use. S2 mini boards cost a few euros |
| ESP32-P4 | yes, full and high speed | Supported by the same library, but oversized for this job and without integrated radio (irrelevant here). No reason to prefer it |
| ESP32-C3, C5, C6, H2 | **no** | These chips only have the USB Serial/JTAG peripheral (console and flashing); it cannot act as a host. The C5's selling point, dual-band Wi-Fi 6, brings nothing to a USB-to-SBUS bridge |
| ESP32 (original) | no USB at all | — |

The only way to put the firmware on a C-series chip would be an external USB host IC on SPI
(MAX3421E, the "USB Host Shield" approach) with a driver written from scratch, which is more
work than the Raspberry Pi port below and gives a worse result.

## Raspberry Pi RP2040 and RP2350

Both have a full-speed USB controller that can work as a host through TinyUSB, and the
RP2350 adds a Cortex-M33 with FPU, more RAM and a third PIO block. For this firmware the two
are the same case, and it is a **second port rather than a build option**:

| Piece | ESP32-S3 today | RP2040 / RP2350 |
|-------|----------------|-----------------|
| USB host | ESP-IDF Host Library, already flown | TinyUSB host: native host mode is less mature; the enumeration buffer must be raised for the SideWinder's 1343-byte descriptor; hub support and GET_REPORT depend on the TinyUSB version; many projects use PIO-USB on two GPIOs instead of the native port |
| SBUS | UART with hardware inversion | the PL011 UART cannot invert: a PIO UART or a transistor inverter is needed |
| CPPM | RMT | PIO (a good fit) |
| Configuration | NVS | own flash store (LittleFS or a reserved sector) |
| Console | UART0 on the board's second USB connector | UART on GPIO0/1 with an adapter, or USB CDC on the native port when the host runs on PIO-USB |
| RTOS and tasks | FreeRTOS | FreeRTOS SMP from the Pico SDK, or a superloop |
| Toolchain | official PlatformIO platform | official PlatformIO only offers the dated mbed Arduino core; the Pico SDK or the earlephilhower core need unofficial platforms or plain CMake |

Estimate: three to five full days, plus repeating the bench work (SBUS polarity, the
centring spring, VBUS power, which has the same problems on a Pico as on the DevKit). The real
advantages are the board price and availability, and the PIO, which generates SBUS and CPPM
with any polarity and no jitter. There is no functional gain: nothing the S3 does not do
already, a less proven USB host stack for this use, and on the RP2040 no FPU and less RAM. If
the goal is a smaller or cheaper board, an ESP32-S3 mini board or an ESP32-S2 gets there with
a pin change. **Decision: no RP2040/RP2350 port.**

## I2C status display

Low effort, about half a day, and worth doing for field use because it replaces the serial
console for the information that matters at the flying site.

- ESP-IDF 6 already ships what is needed: the `i2c_master` driver and the SSD1306/SH1106 panel
  in `esp_lcd`, so a 3.3 V 128x64 OLED needs no external library. A 16x2 character LCD with a
  PCF8574 backpack is even simpler, about a hundred lines written by hand.
- In the firmware it would be a `display` task modelled on `StatusLed`, reading the state that
  `InputHub`, the outputs and `FfbController` already publish and redrawing two or three times
  per second. Configuration keys `display.sda`, `display.scl`, `display.addr`, with -1 to
  disable, like the LED.
- Pins are available: on the DevKitC-1 the conventional pair is GPIO8 (SDA) and GPIO9 (SCL).
  No impact on the outputs: SBUS and CPPM are produced by hardware peripherals and a 400 kHz
  I2C bus does not disturb them.
- Avoid LVGL for a status screen: more than 100 kB of flash and a lot of RAM to draw four lines
  of text. A 6x8 font in the code and a 1 kB framebuffer are enough, and the text rendering can
  be unit-tested on the host like the rest of the project.
- Content: joystick name and VID:PID, channel bars, SBUS/CPPM state and failsafe, spring state
  and actuator power, last error.
