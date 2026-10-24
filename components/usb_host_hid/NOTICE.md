# Vendored component: usb_host_hid

Origin: [espressif/esp-usb](https://github.com/espressif/esp-usb), path
`host/class/hid/usb_host_hid`, version 1.2.1 (2026-08-26), licence Apache-2.0 (see `LICENSE`).

Modifications made for esp32-joystick2trainer (all marked `[esp32-joystick2trainer]` in the
source):

- The interface's interrupt **OUT** endpoint is discovered and a transfer buffer is allocated
  for it when the device is opened.
- New API `hid_host_device_output_report()` / `hid_host_device_has_output_endpoint()` to send
  output reports over that endpoint (what Windows and Linux do for PID force-feedback
  devices), with `hid_class_request_set_report()` as the fallback when no OUT endpoint exists.

Everything else is unchanged. To update: copy the upstream files over, re-apply the marked
hunks, bump the version suffix in `idf_component.yml`.
