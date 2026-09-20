// SPDX-License-Identifier: MIT
#include "app/usb_joystick.hpp"

#include <cstring>
#include <cwchar>

#include "app/input_hub.hpp"
#include "esp_log.h"
#include "usb/usb_host.h"

namespace app {

namespace {
const char* TAG = "usb";
constexpr int kLibTaskPriority = 2;
constexpr int kHidDriverTaskPriority = 5;
constexpr int kHidEventTaskPriority = 4;
constexpr uint32_t kTaskStack = 4096;

void wideToUtf8(const wchar_t* in, char* out, std::size_t out_len) {
    std::size_t i = 0;
    for (; i + 1 < out_len && in[i] != 0; ++i) {
        out[i] = (in[i] < 0x80) ? static_cast<char>(in[i]) : '?';
    }
    out[i] = '\0';
}
}  // namespace

UsbJoystick::UsbJoystick(InputHub& hub) : hub_(hub) {}

void UsbJoystick::start() {
    events_ = xQueueCreate(8, sizeof(Event));
    configASSERT(events_ != nullptr);

    // 1. USB host library daemon (core 0).
    BaseType_t ok = xTaskCreatePinnedToCore(&UsbJoystick::usbLibTask, "usb_lib", kTaskStack,
                                            xTaskGetCurrentTaskHandle(), kLibTaskPriority, &lib_task_, 0);
    configASSERT(ok == pdPASS);
    // Wait until usb_host_install() has completed.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 2. HID class driver with its own background task.
    hid_host_driver_config_t hid_cfg = {};
    hid_cfg.create_background_task = true;
    hid_cfg.task_priority = kHidDriverTaskPriority;
    hid_cfg.stack_size = kTaskStack;
    hid_cfg.core_id = 0;
    hid_cfg.callback = &UsbJoystick::driverEventCallback;
    hid_cfg.callback_arg = this;
    ESP_ERROR_CHECK(hid_host_install(&hid_cfg));

    // 3. Our own task that opens/configures devices (kept out of the driver's callbacks).
    ok = xTaskCreatePinnedToCore(&UsbJoystick::hidEventTask, "hid_evt", kTaskStack, this, kHidEventTaskPriority,
                                 &hid_task_, 0);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "USB host ready, waiting for a joystick");
}

void UsbJoystick::usbLibTask(void* arg) {
    usb_host_config_t host_cfg = {};
    host_cfg.skip_phy_setup = false;
    host_cfg.intr_flags = ESP_INTR_FLAG_LOWMED;
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    xTaskNotifyGive(static_cast<TaskHandle_t>(arg));

    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            // Only happens if the HID driver is uninstalled; free devices and keep serving.
            usb_host_device_free_all();
        }
    }
}

void UsbJoystick::driverEventCallback(hid_host_device_handle_t handle, const hid_host_driver_event_t event, void* arg) {
    auto* self = static_cast<UsbJoystick*>(arg);
    const Event ev{handle, event};
    if (xQueueSend(self->events_, &ev, 0) != pdTRUE) {
        ESP_LOGE(TAG, "event queue full, device ignored");
    }
}

void UsbJoystick::hidEventTask(void* arg) {
    auto* self = static_cast<UsbJoystick*>(arg);
    for (;;) {
        Event ev;
        if (xQueueReceive(self->events_, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
                self->onDeviceConnected(ev.handle);
            }
        }
    }
}

void UsbJoystick::onDeviceConnected(hid_host_device_handle_t handle) {
    hid_host_dev_params_t params;
    const esp_err_t perr = hid_host_device_get_params(handle, &params);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_device_get_params: %s", esp_err_to_name(perr));
        return;
    }
    ESP_LOGI(TAG, "HID interface connected: addr %u iface %u subclass %u proto %u", params.addr, params.iface_num,
             params.sub_class, params.proto);

    if (active_ != nullptr) {
        ESP_LOGW(TAG, "a joystick is already active, ignoring the new interface");
        return;
    }
    if (params.proto != HID_PROTOCOL_NONE) {
        ESP_LOGI(TAG, "boot keyboard/mouse interface, not a joystick: ignored");
        return;
    }

    hid_host_device_config_t dev_cfg = {};
    dev_cfg.callback = &UsbJoystick::interfaceEventCallback;
    dev_cfg.callback_arg = this;
    esp_err_t err = hid_host_device_open(handle, &dev_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_device_open: %s", esp_err_to_name(err));
        return;
    }
    if (!adoptDevice(handle)) {
        hid_host_device_close(handle);
        return;
    }
    // Ask for continuous reports (some devices only report on change otherwise).
    // Not all devices support it; failure is harmless.
    err = hid_class_request_set_idle(handle, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SET_IDLE not accepted (%s)", esp_err_to_name(err));
    }
    err = hid_host_device_start(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_device_start: %s", esp_err_to_name(err));
        active_ = nullptr;
        info_.valid = false;
        hid_host_device_close(handle);
        return;
    }
    active_ = handle;
    hub_.onConnected();
}

bool UsbJoystick::adoptDevice(hid_host_device_handle_t handle) {
    DeviceInfo info;
    hid_host_dev_info_t dev_info;
    if (hid_host_get_device_info(handle, &dev_info) == ESP_OK) {
        info.vid = dev_info.VID;
        info.pid = dev_info.PID;
        wideToUtf8(dev_info.iProduct, info.product, sizeof(info.product));
        wideToUtf8(dev_info.iManufacturer, info.manufacturer, sizeof(info.manufacturer));
    }
    hid_host_dev_params_t params;
    hid_host_device_get_params(handle, &params);
    info.interface = params.iface_num;
    ESP_LOGI(TAG, "device %04X:%04X \"%s\" / \"%s\"", info.vid, info.pid, info.manufacturer, info.product);

    std::size_t desc_len = 0;
    const uint8_t* desc = hid_host_get_report_descriptor(handle, &desc_len);
    if (desc == nullptr || desc_len == 0) {
        ESP_LOGE(TAG, "could not read the report descriptor");
        return false;
    }
    info.descriptor_len = desc_len;
    info.parse_result = hid::parse(desc, desc_len, descriptor_);
    ESP_LOGI(TAG, "report descriptor: %u bytes, %u input fields, parse: %s", static_cast<unsigned>(desc_len),
             static_cast<unsigned>(descriptor_.field_count), hid::parseResultName(info.parse_result));
    if (info.parse_result != hid::ParseResult::Ok && info.parse_result != hid::ParseResult::TooManyFields) {
        return false;
    }
    if (!decoder_.configure(descriptor_)) {
        ESP_LOGW(TAG, "interface has no axes/buttons/hat usable as a joystick: ignored");
        return false;
    }
    for (std::size_t i = 0; i < rc::kAxisCount; ++i) {
        const auto& b = decoder_.axisBinding(static_cast<rc::Axis>(i));
        if (b.present) {
            ESP_LOGI(TAG, "  axis %-11s report %u bits %3u..%-3u range %ld..%ld",
                     rc::axisName(static_cast<rc::Axis>(i)), b.field.report_id, b.field.bit_offset,
                     b.field.bit_offset + b.field.bit_size - 1, static_cast<long>(b.field.logical_min),
                     static_cast<long>(b.field.logical_max));
        }
    }
    if (decoder_.hatBinding().present) {
        ESP_LOGI(TAG, "  hat switch present");
    }
    ESP_LOGI(TAG, "  %u buttons", static_cast<unsigned>(decoder_.buttonCount()));
    state_ = rc::InputState{};
    info.valid = true;
    info_ = info;
    return true;
}

void UsbJoystick::interfaceEventCallback(hid_host_device_handle_t handle, const hid_host_interface_event_t event,
                                         void* arg) {
    auto* self = static_cast<UsbJoystick*>(arg);
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            self->onInputReport(handle);
            break;
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            self->onDisconnected(handle);
            break;
        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            ESP_LOGW(TAG, "transfer error");
            break;
        default:
            ESP_LOGD(TAG, "interface event %d", static_cast<int>(event));
            break;
    }
}

void UsbJoystick::onInputReport(hid_host_device_handle_t handle) {
    if (handle != active_) {
        return;
    }
    std::size_t len = 0;
    if (hid_host_device_get_raw_input_report_data(handle, report_buf_, sizeof(report_buf_), &len) != ESP_OK) {
        return;
    }
    if (decoder_.decode(report_buf_, len, state_)) {
        hub_.onInput(state_);
    } else {
        ++rejected_reports_;
    }
}

void UsbJoystick::onDisconnected(hid_host_device_handle_t handle) {
    const bool was_active = (handle == active_);
    if (was_active) {
        active_ = nullptr;
        info_.valid = false;
        hub_.onDisconnected();
    }
    ESP_LOGI(TAG, "HID interface disconnected%s", was_active ? " (active joystick)" : "");
    const esp_err_t err = hid_host_device_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "hid_host_device_close: %s", esp_err_to_name(err));
    }
}

DeviceInfo UsbJoystick::deviceInfo() const {
    return info_;
}

}  // namespace app
