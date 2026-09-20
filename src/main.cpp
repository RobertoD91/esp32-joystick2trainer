// SPDX-License-Identifier: MIT
// esp32-joystick2trainer: USB HID joystick -> SBUS + CPPM trainer bridge.
//
// Data flow:
//   USB host (UsbJoystick) -> hid_core decoder -> InputHub (mapping) -> SbusOutput (UART)
//                                                                     -> CppmOutput (RMT)
#include "app/config_store.hpp"
#include "app/console.hpp"
#include "app/cppm_output.hpp"
#include "app/input_hub.hpp"
#include "app/sbus_output.hpp"
#include "app/status_led.hpp"
#include "app/usb_joystick.hpp"
#include "esp_log.h"

namespace {
const char* TAG = "main";
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "esp32-joystick2trainer starting");

    // Objects live for the whole program; allocated once and never destroyed.
    static app::ConfigStore config;
    static app::InputHub hub(config);
    static app::SbusOutput sbus(config, hub);
    static app::CppmOutput cppm(config, hub);
    static app::UsbJoystick usb(hub);
    static app::StatusLed led(config, hub);
    static app::ConsoleContext console_ctx{&config, &hub, &usb, &sbus, &cppm};

    config.init();

    // Outputs first: they emit nothing (failsafe "stop") or the failsafe values until a
    // joystick is connected, so the radio sees a well defined state from the start.
    sbus.start();
    cppm.start();
    led.start();

    usb.start();

    app::startConsole(&console_ctx);
    ESP_LOGI(TAG, "ready");
}
