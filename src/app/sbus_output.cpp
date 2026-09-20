// SPDX-License-Identifier: MIT
#include "app/sbus_output.hpp"

#include "app/config_store.hpp"
#include "app/input_hub.hpp"
#include "esp_log.h"
#include "rc/sbus.hpp"

namespace app {

namespace {
const char* TAG = "sbus";
constexpr int kTaskPriority = 6;
constexpr uint32_t kTaskStack = 3072;
}  // namespace

SbusOutput::SbusOutput(ConfigStore& config, InputHub& hub) : config_(config), hub_(hub) {}

bool SbusOutput::start() {
    config_.copySbus(cfg_);
    if (!cfg_.enabled || cfg_.gpio < 0) {
        ESP_LOGI(TAG, "disabled");
        return false;
    }

    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = 100000;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_EVEN;
    uart_cfg.stop_bits = UART_STOP_BITS_2;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(port_, 256, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_param_config(port_, &uart_cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(port_, cfg_.gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        active_inverted_ = cfg_.inverted;
        err = uart_set_line_inverse(port_, cfg_.inverted ? UART_SIGNAL_TXD_INV : UART_SIGNAL_INV_DISABLE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(err));
        uart_driver_delete(port_);
        return false;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &SbusOutput::timerEntry;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "sbus";
    timer_args.skip_unhandled_events = true;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_));

    if (xTaskCreatePinnedToCore(&SbusOutput::taskEntry, "sbus", kTaskStack, this, kTaskPriority, &task_, 1) != pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
        task_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "started on GPIO%d, %s, period %lu us", cfg_.gpio,
             cfg_.inverted ? "inverted (standard SBUS)" : "non-inverted", static_cast<unsigned long>(cfg_.period_us));
    return true;
}

bool SbusOutput::applyPeriod(uint32_t period_us) {
    if (period_us == active_period_us_) {
        return true;
    }
    if (active_period_us_ != 0) {
        esp_timer_stop(timer_);
    }
    const esp_err_t err = esp_timer_start_periodic(timer_, period_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer start failed: %s", esp_err_to_name(err));
        return false;
    }
    active_period_us_ = period_us;
    return true;
}

void SbusOutput::timerEntry(void* arg) {
    auto* self = static_cast<SbusOutput*>(arg);
    if (self->task_ != nullptr) {
        xTaskNotifyGive(self->task_);
    }
}

void SbusOutput::taskEntry(void* arg) {
    static_cast<SbusOutput*>(arg)->run();
}

void SbusOutput::run() {
    rc::sbus::Buffer buffer;
    applyPeriod(cfg_.period_us);

    for (;;) {
        // Wait for the periodic tick (with a safety timeout so config changes still apply).
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // Pick up live configuration changes (period / polarity / flags).
        config_.copySbus(cfg_);
        applyPeriod(cfg_.period_us);
        if (cfg_.inverted != active_inverted_) {
            active_inverted_ = cfg_.inverted;
            uart_set_line_inverse(port_, cfg_.inverted ? UART_SIGNAL_TXD_INV : UART_SIGNAL_INV_DISABLE);
        }

        const OutputDecision d = hub_.decide();
        if (!d.emit) {
            continue;
        }
        rc::sbus::Flags flags;
        if (d.failsafe && cfg_.failsafe_flags) {
            flags.frame_lost = true;
            flags.failsafe = true;
        }
        rc::sbus::encodeFrame(d.frame, flags, buffer);
        uart_write_bytes(port_, buffer.data(), buffer.size());
        ++frames_sent_;
    }
}

}  // namespace app
