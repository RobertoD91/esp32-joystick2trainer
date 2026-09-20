# Evaluation of existing projects and architecture decision

Snapshot taken in September 2026 while designing this project. "Reusable" answers the question
*could code from this project be used here with a clear licence and a matching platform?*

## The candidates

| Project | Platform | USB host | PPM / SBUS generation | Licence | Last activity | Verdict |
|---------|----------|----------|-----------------------|---------|---------------|---------|
| [Doctor-Proton/USB_to_RC](https://github.com/Doctor-Proton/USB_to_RC) | RP2040, Pico SDK + FreeRTOS | Pico-PIO-USB + TinyUSB host, generic descriptor parsing, XInput | PPM via PWM slice + DMA, SBUS via UART with pin inversion, MAVLink | GPL-3.0 | 2023 | Best *architecture* of the list (descriptor driven, VID:PID config, mixer). Nothing portable at code level and GPL. Idea reused, code not. |
| [f2knpw/Radiomaster_TX16s_buddy_box](https://github.com/f2knpw/Radiomaster_TX16s_buddy_box) | ESP32-S3 + ESP32-C3, Arduino sketches | IDF `usb_host` + Espressif `hid_host.h` | ESP-NOW between two boards; SBUS code commented out | none | 2025 | Proves that the S3 host + `usb_host_hid` works with joysticks under Arduino. Hard-coded per-device structs keyed on report length. No licence, cannot reuse. |
| [tanakamasayuki/EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) | ESP32-S2/S3/P4, Arduino core ≥ 3.2 | own wrapper around IDF `usb_host`, decodes gamepad fields from the descriptor | – | MIT | 2026 (active) | Best Arduino option, but a single 500 kB source file covering HID/CDC/MSC/UAC/UVC, an API the author marks as unstable, and Arduino only. Too heavy a dependency for a small firmware. |
| [esp32beans/ESP32_USB_Host_HID](https://github.com/esp32beans/ESP32_USB_Host_HID) | ESP32-S2/S3, Arduino | verbatim copy of Espressif `usb_host_hid` (2023) | – | Apache-2.0 | 2024 | Shows the minimal integration; the upstream component is newer (1.2.1, 2026) so it is used directly instead. The joystick example hard-codes report layouts. |
| [DzikuVx/DiyMotionController](https://github.com/DzikuVx/DiyMotionController) | ESP32 (classic), Arduino | none (IMU + analogue stick) | PPM via timer ISR + `digitalWrite`; SBUS via `HardwareSerial` 8E2 with inversion | MPL-2.0 | 2024 | Clean SBUS packer, but PPM/SBUS are mutually exclusive, only 10 channels and the legacy timer API was removed in Arduino 3.x. |
| [junekyoopark/arduino_hid_joystick](https://github.com/junekyoopark/arduino_hid_joystick) | AVR + MAX3421E shield | USB_Host_Shield_2.0 | Timer1 ISR | none | 2022 | Not applicable, no licence. |
| [lexfp/le3dpToPPM](https://github.com/lexfp/le3dpToPPM) | AVR + shield | USB_Host_Shield_2.0 | Timer1 ISR | none | 2021 | Reference for the classic constants only (300 µs pulse, 22.5 ms frame). |
| [drorgl/USBJoystickPPM](https://github.com/drorgl/USBJoystickPPM) | AVR + shield | USB_Host_Shield_2.0 | external `PPMOut` library | BSD-3 | 2017 | Nothing beyond constants. |
| [DroneMesh/USB_PPM](https://github.com/DroneMesh/USB_PPM) | AVR + shield | PS3 only | Timer1 ISR | none | 2019 | Nothing. |
| [dmpriso/joystick_to_ppm](https://github.com/dmpriso/joystick_to_ppm) | Raspberry Pi + AVR | Linux joystick API | AVR ISR fed over serial | none | 2017 | Nothing (unsafe buffer handling, alpha). |
| [bolderflight/sbus](https://github.com/bolderflight/sbus) | Teensy/STM32/ESP32, Arduino | – | SBUS TX/RX on `HardwareSerial`, ESP32 UART inversion | MIT | 2023 | Solid encoder; on ESP-IDF it would only bring a `Serial` dependency, so an equivalent 40-line encoder is used and unit tested instead. |
| [blopa1961/SBUS](https://github.com/blopa1961/SBUS) | AVR/STM32F1/ESP8266 | – | SBUS → PPM/PWM (opposite direction) | GPL-3.0 | 2025 | Opposite direction; nice double-buffered timer pattern, GPL. |
| [KeesBlokland/RadioMaster16S](https://github.com/KeesBlokland/RadioMaster16S) | ESP8266 ×2 | none (reads the TX16S trainer PPM) | ESP-NOW then SBUS on `Serial1` inverted | none | 2026 | Useful only as a worked example of feeding SBUS into a TX16S. No licence. |

Other building blocks found while searching:

- [badgeteam/esp32-component-hid-host](https://github.com/badgeteam/esp32-component-hid-host)
  (MIT): an ESP-IDF descriptor parser locating axes/hat/buttons. Young and unproven; used as a
  cross-check for the parser written here.
- [o0Zz/HIDDataInterpreter](https://github.com/o0Zz/HIDDataInterpreter) (MIT): portable C++11
  descriptor parser tested with gamepads. Same role.
- Espressif `examples/peripherals/usb/host/hid`: reference for the daemon/client task layout
  and hot-plug handling. Note that the example opens only boot keyboard/mouse interfaces
  (`proto != NONE`); a joystick has `proto == 0` and must not be filtered out.
- The `usb_host_hid` component itself (`host/class/hid/usb_host_hid` in
  [espressif/esp-usb](https://github.com/espressif/esp-usb), Apache-2.0, 1.2.1): delivers raw
  input reports and the report descriptor (capped at 2048 bytes), does not parse it.

No maintained ESP32-S3 project doing USB HID joystick → SBUS + CPPM was found; the closest one
(f2knpw) is unlicensed, hard-coded and ESP-NOW only.

## Toolchain facts that drove the decision

- **Official `platformio/espressif32` 7.1.x** bundles ESP-IDF **6.1** for `framework = espidf`,
  but still Arduino core **2.0.17** (IDF 4.4) for `framework = arduino`. Arduino 2.x on the S3
  has an old USB stack and no `usb_host_hid`.
- **pioarduino/platform-espressif32** (community, well maintained) provides Arduino 3.3.x on
  IDF 5.5.x, including hybrid Arduino-as-component builds. It works, but it is an unofficial
  platform and the project would still need to write the HID handling on top of `usb_host`.
- With ESP-IDF ≥ 6.0 the USB host library moved out of the IDF tree into the registry component
  `espressif/usb`; `usb_host_hid` depends on it. PlatformIO's ESP-IDF integration runs the IDF
  Component Manager when `src/idf_component.yml` exists, so both are fetched automatically
  (`dependencies.lock` pins the versions).

## Decision

**Pure ESP-IDF 6.x on the official PlatformIO platform, Espressif's `usb_host_hid` from the
registry, and a self-written hardware independent core (HID parser, mapper, SBUS/CPPM
encoders) that is unit tested on the host.**

Why not the alternatives:

| Option | Why not |
|--------|---------|
| Arduino (pioarduino) + EspUsbHost | Two unofficial moving dependencies; EspUsbHost is huge and its API is declared unstable; testing on the host would still require separating the core. |
| Arduino (pioarduino) + vendored `hid_host.c` | Works (f2knpw/esp32beans do it), but brings Arduino only for `Serial`/`digitalWrite`, which this firmware does not need; CPPM would be a timer ISR. |
| Reuse `USB_to_RC` | RP2040-specific (PIO USB, PWM+DMA), GPL-3.0. |
| Vendor `usb_host_hid` into `components/` | Avoids the registry at build time, but freezes a 2023 copy; the registry version is newer and the lock file already makes builds reproducible. |
| Timer ISR for CPPM | Jitter of several microseconds under USB interrupt load; the RMT peripheral plays the whole frame from memory with zero CPU involvement. |

Consequences visible in the code:

- `lib/rc_core` and `lib/hid_core` have no ESP-IDF includes and compile with `-Wall -Wextra
  -Wpedantic -Wconversion` on the host.
- `src/app` is a thin adapter layer: each class wraps exactly one peripheral or driver.
- The FFB2 report descriptor (1343 bytes, captured with a USB analyser) is a test fixture, so the
  target device's layout is verified on every CI run without hardware.
