// SPDX-License-Identifier: MIT
// Force feedback for USB HID PID joysticks: keeps a centring spring running on the device
// (the SideWinder Force Feedback 2 is loose until the host creates one) and offers a short
// vibration test. All USB traffic happens on a dedicated task; the USB layer only posts events.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid/pid.hpp"
#include "hid/report_descriptor.hpp"
#include "usb/hid_host.h"

namespace app {

class ConfigStore;

class FfbController {
public:
    struct Status {
        bool device_connected = false;
        bool pid_device = false;
        char reason[48] = {0};
        bool out_endpoint = false;
        bool initialised = false;
        int spring_block = -1;
        bool spring_running = false;
        uint8_t spring_pct = 0;
        bool state_valid = false;
        hid::pid::State state{};
        uint32_t state_reports = 0;
        bool pool_valid = false;
        hid::pid::PoolInfo pool{};
        bool direction_form = false;  ///< spring sent in the Linux-driver form (ffb.direction)
        char last_error[48] = {0};
    };

    explicit FfbController(ConfigStore& config);

    /// Create the worker task and its queue.
    void start();

    /// USB layer: the joystick interface is open and started. The descriptor is copied.
    void onDeviceReady(hid_host_device_handle_t handle, const hid::ParsedDescriptor& desc);
    /// USB layer: the interface went away (the handle must not be used any more).
    void onDeviceGone(hid_host_device_handle_t handle);
    /// USB layer: a raw input report that is not joystick data (e.g. the PID state report).
    /// Cheap and lock-free, safe to call from the HID driver task.
    void onInputReport(const uint8_t* data, std::size_t len);

    /// Re-read ffb.* from the configuration and update the device (console `set`).
    void applyConfig();
    /// Play a sine vibration for `ms` milliseconds (console test). Returns false when queued
    /// commands are backed up or no device is connected.
    bool requestRumble(uint32_t ms);

    Status status() const;

private:
    enum class Cmd : uint8_t { Connect, Disconnect, Apply, Rumble };
    struct Message {
        Cmd cmd;
        hid_host_device_handle_t handle;
        uint32_t arg;
    };

    class HidTransport : public hid::pid::Transport {
    public:
        void attach(hid_host_device_handle_t handle, bool use_out_endpoint);
        void detach();
        /// Output reports currently go over the interrupt OUT endpoint (false: SET_REPORT).
        bool usesOutEndpoint() const { return out_endpoint_; }
        bool sendOutput(const hid::pid::Report& r) override;
        bool setFeature(const hid::pid::Report& r) override;
        bool getFeature(uint8_t report_id, std::size_t expected_len, hid::pid::Report& out) override;
        void delayMs(uint32_t ms) override;

    private:
        hid_host_device_handle_t handle_ = nullptr;
        bool out_endpoint_ = false;
    };

    static void taskEntry(void* arg);
    void run();
    void handleConnect(hid_host_device_handle_t handle);
    void handleDisconnect();
    void handleApply();
    void handleRumble(uint32_t ms);
    void setError(const char* text);
    void publish();

    ConfigStore& config_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;

    // Owned by the worker task.
    hid::ParsedDescriptor descriptor_{};
    hid::ParsedDescriptor pending_descriptor_{};
    hid::pid::Device device_{};
    HidTransport transport_{};
    hid_host_device_handle_t handle_ = nullptr;
    Status st_{};
    Status published_{};
    std::atomic<bool> pid_ready_{false};
};

}  // namespace app
