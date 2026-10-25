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
  The submit runs under the driver's open/close mutex (no use-after-free against a concurrent
  disconnect) and a failed transfer is recovered: a timed-out URB is cancelled, a stalled
  endpoint gets `CLEAR_FEATURE(ENDPOINT_HALT)`, and the pipe is cleared.
- The OUT endpoint is halted/flushed/cleared together with the IN endpoint when the interface
  is stopped or closed, so that a queued output report cannot block the interface release.
- `hid_host_install()` releases the two new semaphores on its failure path.

Everything else is unchanged. To update: copy the upstream files over, re-apply the marked
hunks, bump the version suffix in `idf_component.yml`.
