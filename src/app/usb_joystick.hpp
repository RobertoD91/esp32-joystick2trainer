// SPDX-License-Identifier: MIT
// USB host side: enumerates HID devices, parses the report descriptor of the first joystick
// found and feeds decoded reports into the InputHub. Handles hot plug/unplug.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid/joystick_decoder.hpp"
#include "hid/report_descriptor.hpp"
#include "rc/input_state.hpp"
#include "usb/hid_host.h"

namespace app {

class InputHub;

struct DeviceInfo {
    bool valid = false;
    uint16_t vid = 0;
    uint16_t pid = 0;
    char product[33] = {0};
    char manufacturer[33] = {0};
    uint8_t interface = 0;
    std::size_t descriptor_len = 0;
    hid::ParseResult parse_result = hid::ParseResult::NoInputFields;
};

class UsbJoystick {
public:
    explicit UsbJoystick(InputHub& hub);

    /// Install the USB host stack and the HID class driver and start the worker tasks.
    void start();

    /// Introspection for the console.
    DeviceInfo deviceInfo() const;
    const hid::JoystickDecoder& decoder() const { return decoder_; }
    const hid::ParsedDescriptor& descriptor() const { return descriptor_; }
    bool active() const { return active_ != nullptr; }
    uint32_t rejectedReports() const { return rejected_reports_; }

private:
    struct Event {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
    };

    static void usbLibTask(void* arg);
    static void hidEventTask(void* arg);
    static void driverEventCallback(hid_host_device_handle_t handle, const hid_host_driver_event_t event, void* arg);
    static void interfaceEventCallback(hid_host_device_handle_t handle, const hid_host_interface_event_t event,
                                       void* arg);

    void onDeviceConnected(hid_host_device_handle_t handle);
    void onInputReport(hid_host_device_handle_t handle);
    void onDisconnected(hid_host_device_handle_t handle);
    bool adoptDevice(hid_host_device_handle_t handle);

    InputHub& hub_;
    QueueHandle_t events_ = nullptr;
    TaskHandle_t lib_task_ = nullptr;
    TaskHandle_t hid_task_ = nullptr;

    // The single joystick we listen to.
    hid_host_device_handle_t active_ = nullptr;
    hid::ParsedDescriptor descriptor_{};
    hid::JoystickDecoder decoder_{};
    rc::InputState state_{};
    DeviceInfo info_{};
    uint8_t report_buf_[64] = {0};
    std::atomic<uint32_t> rejected_reports_{0};
};

}  // namespace app
