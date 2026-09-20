// SPDX-License-Identifier: MIT
// SBUS output on a UART with hardware TX inversion (no external inverter needed).
#pragma once

#include <atomic>
#include <cstdint>

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc/config.hpp"

namespace app {

class ConfigStore;
class InputHub;

class SbusOutput {
public:
    SbusOutput(ConfigStore& config, InputHub& hub);

    /// Configure the UART and start the frame task. Returns false when disabled or on error.
    bool start();

    bool running() const { return task_ != nullptr; }
    uint32_t framesSent() const { return frames_sent_; }

private:
    static void taskEntry(void* arg);
    static void timerEntry(void* arg);
    void run();
    bool applyPeriod(uint32_t period_us);

    ConfigStore& config_;
    InputHub& hub_;
    rc::SbusConfig cfg_{};
    uart_port_t port_ = UART_NUM_1;
    esp_timer_handle_t timer_ = nullptr;
    TaskHandle_t task_ = nullptr;
    uint32_t active_period_us_ = 0;
    bool active_inverted_ = true;
    std::atomic<uint32_t> frames_sent_{0};
};

}  // namespace app
