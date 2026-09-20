# EdgeTX and RadioMaster TX16S MKII: trainer input details

Facts below were checked against the EdgeTX sources (`main` branch, September 2026) and the
EdgeTX manual. File names refer to the [EdgeTX repository](https://github.com/EdgeTX/edgetx).
Statements that could not be confirmed from a primary source are marked as such.

## Trainer modes and the pins they use

The TX16S is built from the `horus` target with `PCBREV=TX16S` (`radio/src/targets/horus/hal.h`).

| Mode (Model → Trainer) | Hardware | `hal.h` symbols |
|------------------------|----------|-----------------|
| Master/Jack | 3.5 mm trainer jack, PPM **input** | `TRAINER_IN_GPIO` PC6, TIM3 |
| Slave/Jack | 3.5 mm trainer jack, PPM **output** | `TRAINER_OUT_GPIO` PC7 |
| Master/SBUS Module | module bay **S.PORT** pin through the on-board inverter, USART2 | `TELEMETRY_USART`, `TELEMETRY_RX_GPIO` PD6 |
| Master/CPPM Module | module bay HEARTBEAT pin, TIM4 | `TRAINER_MODULE_CPPM_GPIO` PD12 |
| Master/Serial | AUX1 (USART3, PB10/PB11) or AUX2 (USART6, PG14/PG9) set to "SBUS Trainer" | `AUX_SERIAL_*`, `AUX2_SERIAL_*` |
| Master/Bluetooth, Slave/Bluetooth, Master/Multi, Master/CRSF | not applicable to this bridge | |

`Master/SBUS Module` and `Master/CPPM Module` are only offered when the model's External RF is
**off** (`isTrainerModeAvailable()` in `radio/src/gui/gui_common.cpp`). `Master/Serial` is
offered only when a serial port is configured as `SBUS Trainer`.

**Master/CPPM Module does not work on the TX16S and the MKII**: the HEARTBEAT line has a signal
shaping diode that prevents TIM4 input capture from triggering
([EdgeTX issue #4672](https://github.com/EdgeTX/edgetx/issues/4672), closed as unfixable in
firmware; the MKII changed the diode but reports of problems persist). Use the jack for CPPM.

## CPPM on the trainer jack

Decoder: `radio/src/targets/common/arm/stm32/trainer_driver.cpp`, `capture_pulse()`:

- the timer captures **rising edges** and measures the distance between consecutive ones, so
  the polarity of the marker pulse is irrelevant as long as all pulses are the same width;
- a distance of **4000…19000 µs** is the sync gap and resets the channel counter. A gap longer
  than 19 ms is *not* a sync: the frame is ignored;
- every channel must be **800…2200 µs**, otherwise the whole frame is dropped;
- up to 16 channels; value = `(us - 1500) * (PPM_Multiplier + 10) / 10` (±500 µs ≈ ±512);
- the input is declared lost after **1 s** without a valid frame (`TRAINER_IN_VALID_TIMEOUT`,
  `radio/src/trainer.cpp`).

Consequences for the bridge: `cppm.sync_max` defaults to 18 ms and the encoder shortens the
frame when few channels are used; `cppm.min/max` default to 900/2100 µs.

Jack pinout: EdgeTX/OpenTX convention is **tip = PPM, sleeve = ground, ring unused**
(compatible with mono and stereo plugs). RadioMaster does not publish a schematic; the MKII
product page states that the socket changed from the original 4-pole to a standard 3-pole TRS.
Verify with a multimeter if unsure. The STM32F429 pins are 5 V tolerant and have no internal
pull-up in this mode; the 3.3 V output of the ESP32 is adequate.

## SBUS on the module bay S.PORT pin

`trainer_init_module_sbus()` in `radio/src/trainer.cpp` first looks for a UART on the heartbeat
pin (only present on the TX16S "Express"/V16 hardware) and then falls back to the S.PORT pin,
which on the TX16S is USART2 behind the fixed FrSky-style inverter. Parameters
(`sbusTrainerParams`): **100000 baud, 8E2, RX only**, normal polarity at the UART, which means
**standard inverted SBUS (idle low) on the pin**
([EdgeTX PR #4668](https://github.com/EdgeTX/edgetx/pull/4668)). The bay is powered in this
mode: the BATT pin carries the battery voltage.

Decoder (`radio/src/sbus.cpp`): 25 bytes, header 0x0F, footer 0x00, 16 × 11 bits; frames with the
**failsafe (bit 3) or frame-lost (bit 2) flag are discarded**, so they count as "no signal".
Channel value = `((x & 0x7FF) - 992) * 5 / 8`.

Module bay pin order, top to bottom, JR standard (from the OpenTX documentation, not a RadioMaster
document): 1 PPM out, 2 HEARTBEAT, 3 BATT, 4 GND, 5 S.PORT.

## SBUS on the AUX serial ports

Radio Settings → Hardware → Serial ports → `AUX1` (or `AUX2` on non-Bluetooth firmware) →
**SBUS Trainer**; then Model → Trainer → **Master/Serial**. Same 100000 8E2 framing, but the
STM32F4 cannot invert its UART RX (`UART_MODE_SBUS_TRAINER_INV` is hidden on F4 targets in
`gui_common.cpp`), so the signal must have **UART polarity: idle high** (`sbus.inverted 0` on the
bridge). The AUX pins are 3.3 V logic and not 5 V tolerant according to the EdgeTX manual.
The TX16S has an AUX "Port Power" option that switches 5 V on the AUX connector.

Physical location and pinout of the AUX connectors on the MKII main board are not documented in
the MKII quick-start guide; consult the RadioMaster support pages or the community wiki.

## Using the trainer input in a model

- Radio Settings → **Trainer**: per stick `Mode` (`OFF`, `Add`, `Replace`), source channel,
  weight (negative reverses), and **Cal** which stores the current input values as centre
  (`g_eeGeneral.trainer.calib`). Send centred sticks (1500 µs / SBUS 992) before pressing Cal.
- Special function **Trainer** on a switch activates the takeover for the selected sticks; the
  logical switch `Trn` reflects "trainer connected".
- Sources `TR1…TR16` are available in Inputs/Mixes independently of the special function.
- When the signal disappears, `isTrainerValid()` becomes false after 1 s and the master's own
  sticks are used again; EdgeTX plays the "trainer lost" sound.

## Recommended configuration with this bridge

1. Connect CPPM to the jack (or SBUS to S.PORT), leave `failsafe.mode = stop`.
2. Model → Trainer → `Master/Jack` (or `Master/SBUS Module` with External RF off).
3. Radio Settings → Trainer: `Replace` for the four stick channels, `CH1…CH4`, weight 100,
   centre the joystick and press Cal.
4. Special function `SH↓ → Trainer → Sticks`.
5. Buttons and hat: use `TR5…TR14` as sources in the model's Inputs or Logical Switches.
