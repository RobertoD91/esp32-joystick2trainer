# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow semantic versioning.

## [Unreleased]

### Added

- Force feedback for USB HID PID joysticks: the firmware creates and runs the centring
  spring (configurable strength, gain and dead band through `ffb.*`), decodes the PID state
  report and offers a vibration test (`ffb test`). Generic, descriptor driven; verified
  against the SideWinder Force Feedback 2 descriptor in the unit tests, bench test pending.
- HID report descriptor parser now extracts OUTPUT and FEATURE fields, array usage lists,
  enclosing collections and HID units; new bit-field writer.
- Vendored copy of Espressif's `usb_host_hid` driver (`components/usb_host_hid`) with an
  interrupt OUT endpoint API for output reports (see its `NOTICE.md`).
- README: first-flight record, force feedback documentation, device support assessment.

### Changed

- Configuration blob version 4 (adds `FfbConfig`). Version-3 blobs saved by 0.9.0 are
  migrated automatically; user settings are preserved.

## [0.9.0] - 2026-09-20

First hardware-validated release.

### Added

- ESP32-S3 native USB host with the Espressif `usb_host_hid` class driver, hot plug/unplug.
- Generic HID report descriptor parser and joystick decoder (report IDs, signed axes, hat
  switch, variable and array buttons), verified against the SideWinder Force Feedback 2
  descriptor in the unit tests.
- SBUS output on UART1 with hardware inversion (standard or UART polarity, configurable period).
- CPPM output on the RMT peripheral (channels, pulse width, frame length, polarity and sync gap
  limits configurable; frames kept inside EdgeTX's 4-19 ms sync window).
- Configurable per-channel mapping: source, inversion, dead band, expo, end points, trim,
  failsafe value, button toggle.
- Failsafe modes `stop`, `hold`, `preset` on USB disconnect or optional data timeout.
- Serial console (`show`, `set`, `save`, `status`, `hid`, `monitor`, …) with NVS persistence.
- Host-side unit tests (43 cases) and GitHub Actions CI: format check, tests, firmware build,
  GitHub Pages web flasher (ESP Web Tools) and release assets.
- Documentation: wiring and EdgeTX setup for the RadioMaster TX16S MKII, configuration
  reference, evaluation of existing projects.

### Verified

- SideWinder Force Feedback 2 + ESP32-S3-DevKitC-1 + TX16S MKII, SBUS on the AUX serial port
  (`Master/Serial`, `sbus.inverted 0`), with an ExpressLRS module in the external bay.

### Not yet verified

- CPPM on the trainer jack (`Master/Jack`) and SBUS on the module bay S.PORT pin.

[Unreleased]: https://github.com/RobertoD91/esp32-joystick2trainer/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/RobertoD91/esp32-joystick2trainer/releases/tag/v0.9.0
