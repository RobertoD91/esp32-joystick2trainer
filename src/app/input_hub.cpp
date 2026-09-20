// SPDX-License-Identifier: MIT
#include "app/input_hub.hpp"

#include "app/config_store.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace app {

namespace {
const char* TAG = "hub";
}

InputHub::InputHub(ConfigStore& config) : config_(config) {}

void InputHub::onInput(const rc::InputState& state) {
    // Mapping runs here, on the USB task, so the mapper's toggle state has a single owner.
    rc::MappingConfig mapping;
    bool log_input = false;
    config_.copyMapping(mapping, log_input);
    const rc::Frame frame = mapper_.map(state, mapping);
    const int64_t now = esp_timer_get_time();

    taskENTER_CRITICAL(&lock_);
    last_input_ = state;
    last_frame_ = frame;
    have_input_ = true;
    last_report_us_ = now;
    ++report_count_;
    taskEXIT_CRITICAL(&lock_);

    if (log_input) {
        ESP_LOGI(TAG, "x=%6.3f y=%6.3f rz=%6.3f sl=%6.3f hat=%2d btn=%08lx | ch1-4: %u %u %u %u",
                 static_cast<double>(state.axis[0]), static_cast<double>(state.axis[1]),
                 static_cast<double>(state.axis[5]), static_cast<double>(state.axis[6]), state.hat,
                 static_cast<unsigned long>(state.buttons), frame.us[0], frame.us[1], frame.us[2], frame.us[3]);
    }
}

void InputHub::onConnected() {
    taskENTER_CRITICAL(&lock_);
    connected_ = true;
    have_input_ = false;
    report_count_ = 0;
    taskEXIT_CRITICAL(&lock_);
    ESP_LOGI(TAG, "joystick connected, waiting for the first report");
}

void InputHub::onDisconnected() {
    taskENTER_CRITICAL(&lock_);
    connected_ = false;
    have_input_ = false;
    last_input_ = rc::InputState{};
    taskEXIT_CRITICAL(&lock_);
    mapper_.reset();
    ESP_LOGW(TAG, "joystick disconnected, outputs in failsafe");
}

bool InputHub::inputValidLocked(int64_t now_us, uint32_t timeout_ms) const {
    if (!connected_ || !have_input_) {
        return false;
    }
    if (timeout_ms == 0) {
        return true;
    }
    return (now_us - last_report_us_) < static_cast<int64_t>(timeout_ms) * 1000;
}

OutputDecision InputHub::decide() {
    rc::FailsafeMode mode;
    uint32_t timeout_ms;
    rc::MappingConfig mapping;
    config_.copyFailsafe(mode, timeout_ms, mapping);
    const int64_t now = esp_timer_get_time();

    OutputDecision d;
    taskENTER_CRITICAL(&lock_);
    const bool valid = inputValidLocked(now, timeout_ms);
    const rc::Frame last = last_frame_;
    const bool have_input = have_input_;
    taskEXIT_CRITICAL(&lock_);

    if (valid) {
        d.emit = true;
        d.failsafe = false;
        d.frame = last;
        return d;
    }
    d.failsafe = true;
    switch (mode) {
        case rc::FailsafeMode::Stop:
            d.emit = false;
            d.frame = rc::Mapper::failsafeFrame(mapping);
            break;
        case rc::FailsafeMode::Hold:
            d.emit = true;
            d.frame = have_input ? last : rc::Mapper::failsafeFrame(mapping);
            break;
        case rc::FailsafeMode::Preset:
        default:
            d.emit = true;
            d.frame = rc::Mapper::failsafeFrame(mapping);
            break;
    }
    return d;
}

bool InputHub::connected() const {
    taskENTER_CRITICAL(&lock_);
    const bool c = connected_;
    taskEXIT_CRITICAL(&lock_);
    return c;
}

bool InputHub::inputValid() const {
    rc::FailsafeMode mode;
    uint32_t timeout_ms;
    rc::MappingConfig mapping;
    config_.copyFailsafe(mode, timeout_ms, mapping);
    taskENTER_CRITICAL(&lock_);
    const bool v = inputValidLocked(esp_timer_get_time(), timeout_ms);
    taskEXIT_CRITICAL(&lock_);
    return v;
}

rc::InputState InputHub::lastInput() const {
    taskENTER_CRITICAL(&lock_);
    const rc::InputState s = last_input_;
    taskEXIT_CRITICAL(&lock_);
    return s;
}

rc::Frame InputHub::lastFrame() const {
    taskENTER_CRITICAL(&lock_);
    const rc::Frame f = last_frame_;
    taskEXIT_CRITICAL(&lock_);
    return f;
}

uint32_t InputHub::reportCount() const {
    taskENTER_CRITICAL(&lock_);
    const uint32_t n = report_count_;
    taskEXIT_CRITICAL(&lock_);
    return n;
}

int64_t InputHub::lastReportTimeUs() const {
    taskENTER_CRITICAL(&lock_);
    const int64_t t = last_report_us_;
    taskEXIT_CRITICAL(&lock_);
    return t;
}

}  // namespace app
