// SPDX-License-Identifier: MIT
#include "app/cppm_output.hpp"

#include "app/config_store.hpp"
#include "app/input_hub.hpp"
#include "esp_log.h"
#include "rc/cppm.hpp"

namespace app {

namespace {
const char* TAG = "cppm";
constexpr int kTaskPriority = 7;
constexpr uint32_t kTaskStack = 3072;
constexpr uint32_t kResolutionHz = 1000000;  // 1 tick = 1 us
constexpr uint32_t kTicksPerUs = kResolutionHz / 1000000;
}  // namespace

CppmOutput::CppmOutput(ConfigStore& config, InputHub& hub) : config_(config), hub_(hub) {}

bool CppmOutput::start() {
    config_.copyCppm(cfg_);
    if (!cfg_.enabled || cfg_.gpio < 0) {
        ESP_LOGI(TAG, "disabled");
        return false;
    }
    const rc::cppm::Config wave = rc::cppm::sanitize(cfg_.wave);
    const bool idle_level = !wave.inverted;

    rmt_tx_channel_config_t chan_cfg = {};
    chan_cfg.gpio_num = static_cast<gpio_num_t>(cfg_.gpio);
    chan_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    chan_cfg.resolution_hz = kResolutionHz;
    chan_cfg.mem_block_symbols = 48;  // one hardware block on the ESP32-S3
    chan_cfg.trans_queue_depth = kBuffers;
    chan_cfg.intr_priority = 0;
    chan_cfg.flags.init_level = idle_level ? 1 : 0;

    esp_err_t err = rmt_new_tx_channel(&chan_cfg, &channel_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
        return false;
    }
    rmt_copy_encoder_config_t enc_cfg = {};
    err = rmt_new_copy_encoder(&enc_cfg, &encoder_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder: %s", esp_err_to_name(err));
        return false;
    }
    rmt_tx_event_callbacks_t cbs = {};
    cbs.on_trans_done = &CppmOutput::onTransDone;
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(channel_, &cbs, this));
    ESP_ERROR_CHECK(rmt_enable(channel_));

    if (xTaskCreatePinnedToCore(&CppmOutput::taskEntry, "cppm", kTaskStack, this, kTaskPriority, &task_, 1) != pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
        task_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "started on GPIO%d: %u channels, %lu us frame, %u us pulses, idle %s", cfg_.gpio, wave.channels,
             static_cast<unsigned long>(wave.frame_us), wave.pulse_us, idle_level ? "high" : "low");
    return true;
}

bool IRAM_ATTR CppmOutput::onTransDone(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void* ctx) {
    auto* self = static_cast<CppmOutput*>(ctx);
    BaseType_t woken = pdFALSE;
    if (self->task_ != nullptr) {
        vTaskNotifyGiveFromISR(self->task_, &woken);
    }
    return woken == pdTRUE;
}

bool CppmOutput::queueFrame(const rc::Frame& frame, const rc::cppm::Config& wave) {
    const rc::cppm::Waveform wf = rc::cppm::encode(frame, wave);
    rc::pulse::Packed packed;
    if (!rc::pulse::pack(wf, kTicksPerUs, packed) || packed.count == 0) {
        ESP_LOGE(TAG, "frame does not fit the symbol buffer");
        return false;
    }
    rmt_symbol_word_t* buf = buffers_[next_buffer_];
    for (std::size_t i = 0; i < packed.count; ++i) {
        buf[i].duration0 = packed.symbols[i].first.duration;
        buf[i].level0 = packed.symbols[i].first.level ? 1 : 0;
        buf[i].duration1 = packed.symbols[i].second.duration;
        buf[i].level1 = packed.symbols[i].second.level ? 1 : 0;
    }
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = wf.idle_level ? 1 : 0;
    const esp_err_t err = rmt_transmit(channel_, encoder_, buf, packed.count * sizeof(rmt_symbol_word_t), &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit: %s", esp_err_to_name(err));
        return false;
    }
    next_buffer_ = (next_buffer_ + 1) % kBuffers;
    ++in_flight_;
    ++frames_sent_;
    return true;
}

void CppmOutput::taskEntry(void* arg) {
    static_cast<CppmOutput*>(arg)->run();
}

void CppmOutput::run() {
    for (;;) {
        config_.copyCppm(cfg_);
        const rc::cppm::Config wave = rc::cppm::sanitize(cfg_.wave);

        // Keep the hardware queue full: one frame transmitting, one waiting. The pending
        // frame starts the moment the previous one ends, so there is no gap or jitter
        // from task scheduling. Latency is at most one frame period.
        const OutputDecision d = hub_.decide();
        while (d.emit && in_flight_ < kBuffers) {
            if (!queueFrame(d.frame, wave)) {
                break;
            }
        }

        if (in_flight_ > 0) {
            // Block until a frame finished (its buffer becomes free again).
            if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(200)) > 0) {
                --in_flight_;
            } else {
                ESP_LOGW(TAG, "no transmission-done event within 200 ms");
            }
        } else {
            // Nothing to send (failsafe "stop"): idle and poll for new input.
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

}  // namespace app
