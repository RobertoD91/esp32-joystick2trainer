# Configuration reference

All settings live in one structure (`rc::Config`, `lib/rc_core/include/rc/config.hpp`) that is
persisted as a blob in NVS (namespace `j2t`, key `cfg`). The serial console exposes it as
key/value pairs. Compile-time defaults for the GPIOs and timings are `-D` flags in
`platformio.ini` (`[j2t] build_flags`).

Changes made with `set` are in RAM only until `save`. Keys marked *reboot* are only read at
start-up; everything else takes effect immediately.

## Console commands

| Command | Description |
|---------|-------------|
| `help` | list commands |
| `status` | joystick (VID:PID, product), report statistics, outputs, current channels |
| `hid` | dump the parsed report descriptor fields and the axis/hat/button bindings |
| `monitor [seconds]` | print live input state and channels five times per second |
| `show [prefix]` | print all keys or those starting with `prefix` (`show ch1.`, `show sbus`) |
| `get <key>` | print one key |
| `set <key> <value>` | change one key |
| `save` | write the configuration to NVS |
| `load` | discard unsaved changes and reload from NVS |
| `defaults` | factory defaults in RAM and erase the stored configuration |
| `ffb` | force feedback status (PID device, transport, actuator power, spring, last error) |
| `ffb test [ms]` | play a sine vibration for `ms` milliseconds (default 600) |
| `ffb apply` | re-send the spring/gain settings to the device |
| `reboot` | restart |

Booleans accept `1/0`, `on/off`, `true/false`, `yes/no`.

## Output keys

| Key | Range | Default | Description |
|-----|-------|---------|-------------|
| `sbus.enabled` | bool | 1 | enable the SBUS output (*reboot*) |
| `sbus.gpio` | -1..48 | 17 | SBUS TX pin, -1 disables (*reboot*) |
| `sbus.period` | 3000..50000 µs | 14000 | frame period; 7000 for "high speed" receivers |
| `sbus.inverted` | bool | 1 | 1 = standard SBUS (idle low, for the module bay S.PORT pin); 0 = UART polarity (idle high, for the AUX serial ports) |
| `sbus.flags` | bool | 0 | set the frame-lost/failsafe bits while in failsafe. EdgeTX ignores flagged frames, keep 0 for a radio; set 1 for flight controllers |
| `cppm.enabled` | bool | 1 | enable the CPPM output (*reboot*) |
| `cppm.gpio` | -1..48 | 18 | CPPM pin (*reboot*) |
| `cppm.channels` | 1..16 | 8 | channels per frame |
| `cppm.frame` | 5000..60000 µs | 22500 | nominal frame period |
| `cppm.pulse` | 100..600 µs | 300 | marker pulse width |
| `cppm.inverted` | bool | 0 | 0 = idle high, low pulses (standard); 1 = idle low, high pulses |
| `cppm.min` / `cppm.max` | 800..2200 µs | 900 / 2100 | channel clamp before encoding |
| `cppm.sync_min` | 2000..30000 µs | 4000 | minimum sync gap; the frame is stretched if needed |
| `cppm.sync_max` | 3000..40000 µs | 18000 | maximum sync gap; the frame is shortened if needed (EdgeTX rejects gaps above 19 ms) |

The CPPM frame is: for each channel a marker pulse followed by a space so that the pulse-start
to pulse-start distance equals the channel value, then a sync slot (marker + gap) that fills the
frame up to `cppm.frame`. With 8 channels at 1500 µs the sync gap is 10.5 ms.

## Failsafe and input keys

| Key | Values | Default | Description |
|-----|--------|---------|-------------|
| `failsafe.mode` | `stop`, `hold`, `preset` | `stop` | behaviour while no valid joystick input exists (before the first report, after unplug, after a data timeout). `stop`: no frames at all (EdgeTX falls back to its own sticks after 1 s). `hold`: repeat the last frame. `preset`: emit every channel's `chN.failsafe` value |
| `input.timeout` | 0..60000 ms | 0 | treat the input as lost when no report arrived for this long; 0 disables. Keep 0 unless the device is known to report continuously (most joysticks only report on change) |
| `led.gpio` | -1..48 | -1 | status LED: off = disabled, fast blink = no joystick, slow blink = joystick without valid data, solid = valid input (*reboot*) |
| `log.input` | bool | 0 | log every decoded report (debug only, floods the console) |

USB disconnection is always detected by the host stack independently of `input.timeout`.

## Force feedback keys

Only for joysticks that implement USB HID PID (Physical Interface Device) force feedback, such
as the SideWinder Force Feedback 2. Changes apply immediately; `save` persists them.

| Key | Range | Default | Description |
|-----|-------|---------|-------------|
| `ffb.enabled` | bool | 1 | master switch. 0 stops the centring spring and sets the device gain to 0 |
| `ffb.spring` | 0..100 % | 60 | strength of the centring spring (condition effect coefficients). 0 leaves the stick loose |
| `ffb.gain` | 0..100 % | 100 | PID device gain applied to every effect |
| `ffb.deadband` | 0..50 % | 0 | zone around the centre without spring force |
| `ffb.direction` | bool | 0 | how the spring's Set Effect report is encoded. 0 = per-axis form of the PID specification (Axes Enable X and Y, one condition block per axis). 1 = the form the Linux `hid-pidff` driver sends (Direction Enable set, direction north, axes bits clear). Try 1 if a device centres only one axis |

The spring is re-created on every connection; `ffb` on the console shows whether the device
accepted it, the device's effect memory (PID pool) and what the PID state report says (actuator
power, safety switch, effect playing). Output reports use the interrupt OUT endpoint when the
device has one; after a failed transfer the firmware retries over the control pipe
(`SET_REPORT`) and stays there for the rest of the connection (`ffb` shows which one is in use).

## Channel keys (`ch1` … `ch16`)

| Key | Values | Default | Description |
|-----|--------|---------|-------------|
| `chN.src` | `none`, `const`, `axis:<name>`, `btn:<1-32>`, `hatx`, `haty` | see table below | signal source. `none` and `const` output `chN.failsafe` |
| `chN.invert` | bool | 0 | reverse the direction (buttons: pressed = low) |
| `chN.toggle` | bool | 0 | buttons only: each press toggles the output instead of following the button |
| `chN.deadband` | 0..50 % | 2 (axes) | dead band around the centre, in percent of half travel; the remaining travel is rescaled so the end points stay reachable |
| `chN.expo` | 0..100 | 0 | exponential: 0 linear, 100 = cubic |
| `chN.min` / `chN.max` | 800..2200 µs | 1000 / 2000 | output end points (centre = their average) |
| `chN.trim` | -500..500 µs | 0 | offset added after scaling, clamped to min/max |
| `chN.failsafe` | 800..2200 µs | 1500 (1000 for buttons and throttle) | value in `preset` failsafe, and for `none`/`const` sources |

Axis names: `x`, `y`, `z`, `rx`, `ry`, `rz`, `slider`, `dial`, `wheel`, `throttle`, `rudder`,
`accelerator`, `brake`. They correspond to the HID usages Generic Desktop X…Wheel and Simulation
Controls Throttle/Rudder/Accelerator/Brake. If a device declares two `slider` usages the second
one is reachable as `dial`. Use `hid` to see which axes the connected joystick provides.

Processing order for an axis: HID value → normalised to -1…+1 (the device's logical range,
signed ranges keep 0 as the exact centre) → dead band → expo → invert → scaled to
`min`…`max` → + `trim` → clamped.

Hat switch: `hatx` is -1/0/+1 for left/centre/right, `haty` for down/centre/up, both mapped like
an axis (1000/1500/2000 µs by default).

### Factory mapping

| Channel | `src` | `invert` | Other |
|---------|-------|----------|-------|
| 1 | `axis:x` | 0 | deadband 2 |
| 2 | `axis:y` | 1 | deadband 2 |
| 3 | `axis:slider` | 1 | deadband 2, failsafe 1000 |
| 4 | `axis:rz` | 0 | deadband 2 |
| 5 | `btn:1` | 0 | failsafe 1000 |
| 6 | `btn:2` | 0 | failsafe 1000 |
| 7 | `hatx` | 0 | |
| 8 | `haty` | 0 | |
| 9…14 | `btn:3` … `btn:8` | 0 | failsafe 1000 |
| 15, 16 | `none` | | 1500 |

The Y and slider inversions match the SideWinder FFB2 (HID Y grows when pulling back, the
throttle lever reports 0 when pushed forward). Check them with `monitor` for other sticks.

## SBUS value mapping

Channel values in microseconds are converted with the FrSky/EdgeTX convention:
`sbus = (us - 880) * 8 / 5`, i.e. 988 µs → 172, 1500 µs → 992, 2012 µs → 1811. EdgeTX decodes
`(sbus - 992) * 5 / 8` back to ±512 for full travel, so 1000/1500/2000 µs give -500/0/+500.
