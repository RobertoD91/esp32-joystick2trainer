// SPDX-License-Identifier: MIT
#include "app/ffb.hpp"

#include <cstdio>
#include <cstring>

#include "app/config_store.hpp"
#include "esp_log.h"

namespace app {

namespace {
const char* TAG = "ffb";
constexpr int kTaskPriority = 3;
constexpr uint32_t kTaskStack = 4096;
constexpr uint32_t kTransferTimeoutMs = 500;
constexpr uint32_t kMaxRumbleMs = 5000;
}  // namespace

// ------------------------------------------------------------------------------ transport

void FfbController::HidTransport::attach(hid_host_device_handle_t handle, bool use_out_endpoint) {
    handle_ = handle;
    out_endpoint_ = use_out_endpoint;
}

void FfbController::HidTransport::detach() {
    handle_ = nullptr;
    out_endpoint_ = false;
}

bool FfbController::HidTransport::sendOutput(const hid::pid::Report& r) {
    if (handle_ == nullptr || r.len == 0) {
        return false;
    }
    esp_err_t err;
    if (out_endpoint_) {
        err = hid_host_device_output_report(handle_, r.data, r.len, kTransferTimeoutMs);
    } else {
        err = hid_class_request_set_report(handle_, HID_REPORT_TYPE_OUTPUT, r.id, const_cast<uint8_t*>(r.data), r.len);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "output report %u failed: %s", r.id, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool FfbController::HidTransport::setFeature(const hid::pid::Report& r) {
    if (handle_ == nullptr || r.len == 0) {
        return false;
    }
    const esp_err_t err =
        hid_class_request_set_report(handle_, HID_REPORT_TYPE_FEATURE, r.id, const_cast<uint8_t*>(r.data), r.len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set feature %u failed: %s", r.id, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool FfbController::HidTransport::getFeature(uint8_t report_id, std::size_t expected_len, hid::pid::Report& out) {
    out = hid::pid::Report{};
    if (handle_ == nullptr || expected_len == 0 || expected_len > hid::pid::kMaxReportBytes) {
        return false;
    }
    std::size_t len = expected_len;
    const esp_err_t err = hid_class_request_get_report(handle_, HID_REPORT_TYPE_FEATURE, report_id, out.data, &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "get feature %u failed: %s", report_id, esp_err_to_name(err));
        return false;
    }
    out.id = report_id;
    out.len = len;
    return len > 0;
}

void FfbController::HidTransport::delayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

// ------------------------------------------------------------------------------ controller

FfbController::FfbController(ConfigStore& config) : config_(config) {}

void FfbController::start() {
    queue_ = xQueueCreate(8, sizeof(Message));
    configASSERT(queue_ != nullptr);
    const BaseType_t ok =
        xTaskCreatePinnedToCore(&FfbController::taskEntry, "ffb", kTaskStack, this, kTaskPriority, &task_, 0);
    configASSERT(ok == pdPASS);
}

void FfbController::onDeviceReady(hid_host_device_handle_t handle, const hid::ParsedDescriptor& desc) {
    // The USB layer re-parses its descriptor on every connection: keep our own copy so the
    // worker never reads a descriptor that is being rewritten.
    taskENTER_CRITICAL(&lock_);
    pending_descriptor_ = desc;
    taskEXIT_CRITICAL(&lock_);
    const Message m{Cmd::Connect, handle, 0};
    if (xQueueSend(queue_, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full (connect)");
    }
}

void FfbController::onDeviceGone(hid_host_device_handle_t handle) {
    pid_ready_ = false;
    const Message m{Cmd::Disconnect, handle, 0};
    xQueueSend(queue_, &m, 0);
}

void FfbController::onInputReport(const uint8_t* data, std::size_t len) {
    if (!pid_ready_ || data == nullptr || len == 0) {
        return;
    }
    if (!device_.hasStateReport() || data[0] != device_.stateReportId()) {
        return;
    }
    hid::pid::State s;
    if (device_.decodeState(data, len, s)) {
        taskENTER_CRITICAL(&lock_);
        published_.state = s;
        published_.state_valid = true;
        ++published_.state_reports;
        taskEXIT_CRITICAL(&lock_);
    }
}

void FfbController::applyConfig() {
    const Message m{Cmd::Apply, nullptr, 0};
    xQueueSend(queue_, &m, 0);
}

bool FfbController::requestRumble(uint32_t ms) {
    if (!pid_ready_) {
        return false;
    }
    if (ms == 0) {
        ms = 600;
    }
    if (ms > kMaxRumbleMs) {
        ms = kMaxRumbleMs;
    }
    const Message m{Cmd::Rumble, nullptr, ms};
    return xQueueSend(queue_, &m, 0) == pdTRUE;
}

FfbController::Status FfbController::status() const {
    taskENTER_CRITICAL(&lock_);
    const Status s = published_;
    taskEXIT_CRITICAL(&lock_);
    return s;
}

void FfbController::setError(const char* text) {
    std::snprintf(st_.last_error, sizeof(st_.last_error), "%s", text);
    ESP_LOGW(TAG, "%s", text);
}

void FfbController::publish() {
    taskENTER_CRITICAL(&lock_);
    const hid::pid::State state = published_.state;
    const bool state_valid = published_.state_valid;
    const uint32_t reports = published_.state_reports;
    published_ = st_;
    published_.state = state;
    published_.state_valid = state_valid;
    published_.state_reports = reports;
    taskEXIT_CRITICAL(&lock_);
}

void FfbController::taskEntry(void* arg) {
    static_cast<FfbController*>(arg)->run();
}

void FfbController::run() {
    for (;;) {
        Message m;
        if (xQueueReceive(queue_, &m, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (m.cmd) {
            case Cmd::Connect:
                handleConnect(m.handle);
                break;
            case Cmd::Disconnect:
                if (m.handle == handle_) {
                    handleDisconnect();
                }
                break;
            case Cmd::Apply:
                handleApply();
                break;
            case Cmd::Rumble:
                handleRumble(m.arg);
                break;
        }
        publish();
    }
}

void FfbController::handleConnect(hid_host_device_handle_t handle) {
    handleDisconnect();
    taskENTER_CRITICAL(&lock_);
    descriptor_ = pending_descriptor_;
    taskEXIT_CRITICAL(&lock_);

    st_ = Status{};
    st_.device_connected = true;
    handle_ = handle;

    if (!device_.configure(descriptor_, st_.reason, sizeof(st_.reason))) {
        st_.pid_device = false;
        ESP_LOGI(TAG, "no force feedback: %s", st_.reason);
        return;
    }
    st_.pid_device = true;
    st_.out_endpoint = hid_host_device_has_output_endpoint(handle);
    transport_.attach(handle, st_.out_endpoint);
    ESP_LOGI(TAG, "PID device: effect blocks %d..%d, output reports via %s", device_.blockIndexMin(),
             device_.blockIndexMax(), st_.out_endpoint ? "interrupt OUT endpoint" : "control SET_REPORT");
    pid_ready_ = true;

    hid::pid::Controller ctl(device_, transport_);
    st_.initialised = ctl.initialise();
    if (!st_.initialised) {
        setError("device initialisation failed");
    }
    handleApply();
}

void FfbController::handleDisconnect() {
    pid_ready_ = false;
    transport_.detach();
    handle_ = nullptr;
    device_ = hid::pid::Device{};
    st_ = Status{};
    taskENTER_CRITICAL(&lock_);
    published_.state_valid = false;
    published_.state_reports = 0;
    taskEXIT_CRITICAL(&lock_);
}

void FfbController::handleApply() {
    if (!st_.pid_device || handle_ == nullptr) {
        return;
    }
    const rc::Config cfg = config_.get();
    hid::pid::Controller ctl(device_, transport_);
    st_.spring_pct = cfg.ffb.enabled ? cfg.ffb.spring_pct : 0;

    if (!ctl.setDeviceGain(static_cast<float>(cfg.ffb.enabled ? cfg.ffb.gain_pct : 0) / 100.0f)) {
        setError("device gain failed");
    }

    if (st_.spring_pct == 0) {
        if (st_.spring_block >= 0 && st_.spring_running) {
            ctl.stopEffect(st_.spring_block);
            st_.spring_running = false;
        }
        ESP_LOGI(TAG, "centring spring off");
        return;
    }
    if (!device_.supportsEffect(hid::pid::kUsageEtSpring) || !device_.hasConditionReport()) {
        setError("device has no spring effect");
        return;
    }
    if (st_.spring_block < 0) {
        uint16_t status = 0;
        st_.spring_block = ctl.createEffect(hid::pid::kUsageEtSpring, &status);
        if (st_.spring_block < 0) {
            char msg[48];
            std::snprintf(msg, sizeof(msg), "spring block load: %s", hid::pid::blockLoadStatusName(status));
            setError(msg);
            return;
        }
    }
    hid::pid::EffectParams e;
    e.type = hid::pid::kUsageEtSpring;
    e.duration_ms = 0;  // infinite
    e.gain = 1.0f;
    hid::pid::Condition c;
    c.positive_coeff = static_cast<float>(st_.spring_pct) / 100.0f;
    c.negative_coeff = c.positive_coeff;
    c.dead_band = static_cast<float>(cfg.ffb.deadband_pct) / 100.0f;
    if (!ctl.uploadCondition(st_.spring_block, hid::pid::kUsageEtSpring, e, c) || !ctl.startEffect(st_.spring_block)) {
        setError("spring upload failed");
        return;
    }
    st_.spring_running = true;
    st_.last_error[0] = '\0';
    ESP_LOGI(TAG, "centring spring %u %% on effect block %d", st_.spring_pct, st_.spring_block);
}

void FfbController::handleRumble(uint32_t ms) {
    if (!st_.pid_device || handle_ == nullptr) {
        return;
    }
    hid::pid::Controller ctl(device_, transport_);
    if (!device_.supportsEffect(hid::pid::kUsageEtSine) || !device_.hasPeriodicReport()) {
        setError("device has no periodic effect");
        return;
    }
    uint16_t status = 0;
    const int block = ctl.createEffect(hid::pid::kUsageEtSine, &status);
    if (block < 0) {
        char msg[48];
        std::snprintf(msg, sizeof(msg), "rumble block load: %s", hid::pid::blockLoadStatusName(status));
        setError(msg);
        return;
    }
    hid::pid::EffectParams e;
    e.duration_ms = ms;
    e.gain = 1.0f;
    hid::pid::Periodic p;
    p.magnitude = 1.0f;
    p.period_ms = 50;
    if (!ctl.uploadPeriodic(block, hid::pid::kUsageEtSine, e, p) || !ctl.startEffect(block)) {
        setError("rumble upload failed");
    } else {
        ESP_LOGI(TAG, "rumble %lu ms on effect block %d", static_cast<unsigned long>(ms), block);
        vTaskDelay(pdMS_TO_TICKS(ms + 50));
        ctl.stopEffect(block);
    }
    ctl.freeEffect(block);
}

}  // namespace app
