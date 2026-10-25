# Device support and the "universal joystick database" question

This note answers a recurring question: could the firmware embed a database of joysticks (the
way "Linux" or "SDL" seem to) so that any USB stick works out of the box? Short answer: no such
database exists in the form people imagine, the firmware already does what Linux does for the
large majority of sticks, and the useful additions are small and specific. The findings below
come from reading the Linux and SDL sources and the SDL mapping file on 2026-10-24 (links in
[Sources](#sources)); statements about devices that were not plugged into this firmware are
marked *unverified*.

## How device support works today

The firmware is *descriptor driven*. On connection it fetches the HID report descriptor, parses
it (`lib/hid_core/report_descriptor.cpp`: report IDs, variable and array items, Push/Pop,
collections, units) and binds Generic Desktop X/Y/Z/Rx/Ry/Rz/Slider/Dial/Wheel, Simulation
Throttle/Rudder/Accelerator/Brake, the hat switch and up to 32 buttons
(`joystick_decoder.cpp`). The channel mapping (`chN.src`) then picks from those names. Force
feedback is equally generic: the PID output/feature reports are located by usage in the same
descriptor (`pid.cpp`).

This is the same model as Linux. The kernel has no per-device joystick table: `hid-generic`
binds every HID device nobody else claims, and `hid-input.c` maps the same usages to `ABS_*` and
`BTN_*` codes. What Linux adds on top is:

| Linux mechanism | What it is | Joystick-relevant content |
|-----------------|------------|---------------------------|
| `hid-quirks.c` | VID:PID → flag bits | ~45 joystick rows: `NOGET` (CH Products, Logitech Extreme 3D Pro), `BADPAD` (a few pads with wrong logical ranges), `MULTI_INPUT` (2/4-port adapters), `INCREMENT_USAGE_ON_DUPLICATE` (Saitek X52/X52 Pro/X65) |
| descriptor fix-ups (`report_fixup`) | a hook that patches or replaces the raw descriptor before parsing | Saitek PS1000 (4-byte patch), DragonRise 0079:0011, PhoenixRC and VRC-2 simulator dongles, Logitech wheels (full replacement blobs) |
| init sequences | class/vendor requests before input flows | DualShock 3 (two GET_REPORT features), Switch Pro (output-report handshake), Steam Controller, Thrustmaster/Logitech wheel mode switches |
| non-HID drivers | `xpad.c` for vendor-class interfaces | Xbox 360 (class FF/93/1) and Xbox One/Series (class FF/71/208, needs a power-on packet) |
| `hid-pidff` quirks | per-device PID force-feedback flags | Moza, VRS, FFBeast, PXN, Asetek… (`hid-universal-pidff.c`) |

The sticks RC pilots are most likely to own, Thrustmaster T.16000M, T.Flight Hotas X, Warthog,
TWCS, rudder pedals, and the Logitech Extreme 3D Pro, appear in none of the Linux tables
except as a `NOGET` flag (Extreme 3D Pro) and run on the generic parser. The SideWinder Force
Feedback 2 is plain HID + PID on Linux as well; `hid-microsoft.c` carries no SideWinder joystick
fix-up. `NOGET` means "do not GET_REPORT the input reports at start-up"; this firmware never
does that (it only sends SET_IDLE and, for PID devices, reads PID feature reports), so it is
`NOGET`-safe by construction.

## What SDL_GameControllerDB is, and is not

[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) is a zlib-licensed text
file (609,830 bytes, 2,277 mapping lines on 2026-10-24; 46 KB gzipped) whose lines read
`GUID,name,a:b1,leftx:a0,dpup:h0.1,…,platform:Linux,`. The GUID is built from the USB bus type,
vendor, product and version, so a USB host could compute the lookup key. However:

- It is a **gamepad** database. Its output vocabulary is a/b/x/y, shoulders, triggers, two
  sticks and a d-pad; there is no throttle, rudder, slider or pedal concept. Searching the file
  finds no Logitech Extreme 3D Pro, VKB, Virpil, X52, X56, Warthog or wheel entries, and no
  SideWinder FFB2; the Thrustmaster and Saitek lines are gamepads except one T.16000M entry.
- The `aN`/`bN` indices are **per-operating-system enumeration indices** (Linux evdev order,
  Windows DirectInput order), not HID usages, which is why entries exist per platform. A
  firmware would have to emulate one OS's ordering to interpret them.
- It contains no descriptor fix-ups, init sequences or protocol information. SDL's own support
  for PlayStation, Xbox, Switch and Steam controllers lives in per-device HIDAPI drivers, not
  in the database.
- SDL's fallback when no entry matches is an automatic usage-based mapping, and on Linux it
  refuses to generate one unless the device declares itself a *Gamepad* collection; plain
  joysticks get nothing.

Storage would not be the obstacle (a Linux-USB subset is ~156 KB of text, ~11 KB gzipped, or
roughly 30–60 KB packed; the 8 MB flash has room). The problem is that it adds only gamepad
button names for devices the generic parser already decodes, and nothing for flight sticks.
No embedded project was found that ships it (*unverified* negative result): Bluepad32 (ESP32)
uses SDL's much smaller VID:PID → controller-type list plus a usage-based generic parser;
BlueRetro (ESP32) parses descriptors, fingerprints them and keeps a small quirk file;
GP2040-CE (RP2040) hard-codes six controller classes.

## Compatibility table

Classes, from the Linux sources: *generic HID* = works with the descriptor parser;
*profile* = works, but a VID:PID default mapping would save console work; *quirk* = needs a
small descriptor/decoder fix; *vendor driver* = needs code outside the HID class driver.
"Expected" means derived from the Linux driver sources, not tested with this firmware.

| Device | VID:PID | Class | Notes |
|--------|---------|-------|-------|
| Microsoft SideWinder Force Feedback 2 | 045E:001B | generic HID + PID | **Flown.** Spring and vibration implemented, bench test pending |
| Thrustmaster T.16000M | 044F:B10A | generic HID (expected) | No Linux quirk; 16-bit axes, 16 buttons (*unverified* on this firmware) |
| Thrustmaster T.Flight Hotas X / 4, TWCS, Warthog, Cougar, rudder pedals | 044F:B108, B687, 0402/0404, 0400, B678 | generic HID (expected) | No Linux quirk or driver. Large throttles may exceed the parser's field limits (64 input fields, 32 local usages), *unverified* |
| Logitech Extreme 3D Pro | 046D:C215 | generic HID (expected) | Linux only adds `NOGET`, which this firmware satisfies by never polling input reports |
| Saitek / Logitech X52, X52 Pro, X65 | 06A3:075C, 0255, 0762, 0B6A | quirk | Duplicate usages: Linux assigns the next free axis (`INCREMENT_USAGE_ON_DUPLICATE`); this decoder keeps the first field per axis and only aliases a second Slider to Dial, so some axes are lost |
| CH Products sticks, yokes, pedals | 068E:… | generic HID (expected) | `NOGET` on Linux, satisfied here |
| Sony DualShock 4 (wired) | 054C:05C4, 09CC | generic HID (expected) | Report 1 carries x/y/rx/ry/z/rz and buttons in standard fields; no init sequence over USB (*unverified*) |
| Sony DualSense (wired) | 054C:0CE6 | generic HID (expected) | As above (*unverified*) |
| Sony DualShock 3 | 054C:0268 | quirk | Silent until two GET_REPORT(Feature 0xF2/0xF5) and a 1-byte output report; the vendored driver exposes those requests, the sequence is not implemented |
| Logitech F310/F510/F710 in "D" mode | 046D:C216/C218/C219 | generic HID (expected) | Flip the switch to D. In "X" mode they are XInput devices |
| Xbox 360 / One / Series controllers, pads in "X" mode | class FF/93/1, FF/71/208 | vendor driver | Not HID: the HID class driver never sees them. Needs a second USB host client (interrupt IN/OUT, 20-byte or GIP packets, power-on packet for GIP) |
| DragonRise "USB Gamepad" | 0079:0011 | quirk | Linux replaces the whole descriptor (`hid-dr.c`); 0079:0006 needs nothing |
| 2/4-port adapters (PantherLord, DragonRise, GreenAsia, Raphnet, Mayflash) | various | quirk | Linux creates one device per report (`MULTI_INPUT`); this decoder merges report IDs and keeps the first binding, so only port 1 is usable, nondeterministically |
| PhoenixRC / VRC-2 simulator dongles | e.g. 07C0:1125 | quirk | Linux substitutes clean descriptors (`hid-pxrc.c`, `hid-vrc2.c`) |
| Nintendo Switch Pro (wired), Steam Controller | 057E:2009, 28DE:1102 | quirk / vendor-HID | Output-report handshake, raw report layouts; not implemented |
| Older Logitech / Thrustmaster force-feedback wheels and pads | various | generic HID for input | Input decodes; their non-PID force feedback is out of scope |

## Recommended roadmap

1. **Built-in profile table keyed on VID:PID** (cheap, high value). One row per known stick:
   default `chN.src`, inversions, dead bands, failsafe values, plus optional quirk flags
   (`duplicate usages → next free axis`, `hat range`, `use report ID n`, `use interface n`).
   The current factory mapping is, in effect, the SideWinder row. Profiles can be contributed
   as text (the output of `show ch` on the console) and compiled into a `constexpr` array; a
   matching row only pre-fills the mapping when no saved configuration exists.
2. **Three generic-decoder improvements Linux has and this decoder lacks**: honour the HID
   *Null state* flag on axes (skip out-of-range values instead of clamping), a general
   duplicate-usage policy (second X/Y/Slider gets the next free axis, which fixes the X52
   family), and Linux's general hat formula `(v - min) * 8 / (max - min + 1) + 1` for hats that
   are not 4- or 8-position. Also add Simulation usage 0xC8 (Steering).
3. **A pre-parse descriptor hook** (the equivalent of Linux `report_fixup`): VID:PID plus
   descriptor length → byte patches or a replacement descriptor, seeded with the blobs from
   `hid-dr.c`, `hid-pxrc.c`, `hid-vrc2.c` and `hid-saitek.c`. A few kilobytes of flash.
4. **Per-device init sequences** (DualShock 3, Switch Pro) as data: a list of GET_REPORT /
   SET_REPORT / output-report steps run between open and start. All primitives already exist in
   the vendored HID driver.
5. **XInput/GIP driver** for Xbox controllers: a second USB Host Library client that claims the
   vendor-class interface, with the packet layouts documented in `xpad.c`. A few hundred lines;
   the one vendor driver worth adding.
6. **PID quirk flags** from `hid-pidff.h` (missing delay/PBO fields, fixed condition direction,
   sine-only periodic) with the `hid-universal-pidff.c` device list, so the centring spring also
   works on non-SideWinder PID devices. `ffb.direction` is the first of these knobs.
7. Optional, last: an SDL subset import for gamepad ergonomics ("left stick → ch1/ch2 whatever
   the usages are"). Only after 1–6, and only as a build-time generated table.

Before any of this, the test corpus should grow with real descriptors (T.16000M, Extreme 3D
Pro, X52 Pro, Warthog throttle, DualShock 4) so that failures can be attributed to the parser
limits rather than to a missing database entry. The vendored driver's 2048-byte descriptor cap
(Linux allows 4096) is another limit to revisit once such descriptors are collected.

## Sources

- Linux: [hid-generic.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-generic.c),
  [hid-input.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-input.c),
  [hid-quirks.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-quirks.c),
  [hid-ids.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-ids.h),
  [usbhid/hid-core.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/usbhid/hid-core.c),
  [hid-lg.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-lg.c),
  [hid-lg4ff.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-lg4ff.c),
  [hid-saitek.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-saitek.c),
  [hid-dr.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-dr.c),
  [hid-pxrc.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-pxrc.c),
  [hid-vrc2.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-vrc2.c),
  [hid-sony.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-sony.c),
  [hid-playstation.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-playstation.c),
  [hid-nintendo.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-nintendo.c),
  [hid-steam.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-steam.c),
  [hid-thrustmaster.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-thrustmaster.c),
  [hid-tmff.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-tmff.c),
  [hid-microsoft.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-microsoft.c),
  [xpad.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/joystick/xpad.c),
  [hid-pidff.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/usbhid/hid-pidff.h),
  [hid-universal-pidff.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-universal-pidff.c).
- SDL: [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) and its
  [gamecontrollerdb.txt](https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/master/gamecontrollerdb.txt),
  [SDL_AddGamepadMapping](https://wiki.libsdl.org/SDL3/SDL_AddGamepadMapping),
  [SDL_gamepad.c](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/SDL_gamepad.c),
  [SDL_joystick.c](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/SDL_joystick.c),
  [linux/SDL_sysjoystick.c](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/linux/SDL_sysjoystick.c),
  [windows/SDL_dinputjoystick.c](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/windows/SDL_dinputjoystick.c),
  [controller_list.h](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/controller_list.h).
- Embedded precedents: [Bluepad32](https://raw.githubusercontent.com/ricardoquesada/bluepad32/main/src/components/bluepad32/controller/uni_controller_type.c),
  [BlueRetro hid_parser.c](https://raw.githubusercontent.com/darthcloud/BlueRetro/master/main/adapter/hid_parser.c) and
  [its development log](https://hackaday.io/project/170365/log/178734),
  [GP2040-CE gamepad host](https://raw.githubusercontent.com/OpenStickCommunity/GP2040-CE/main/src/addons/gamepad_usb_host_listener.cpp).
- Espressif: [USB Host Library](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html).
- USB IDs: [usb.ids](http://www.linux-usb.org/usb.ids).
